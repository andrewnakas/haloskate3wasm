// s3_halo.h: the full Skate 3 runtime behind a C API for a host game (HaloSkate3's engine bridge).
// Patterned on skatecore-cpp web/s3/host.cpp: the pack is one buffer the VFS keeps views into, the world is
// an in-memory SkateMap built from the host's triangles, and every runtime call is wrapped so a panic
// (rs::Panic) or error turns into a 0 return with s3h_error() set.
#include "s3_halo.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <memory>
#include <string>
#include <vector>

#include "s3/data/skate_map.hpp"
#include "s3/data/vfs.hpp"
#include "s3/game/driver.hpp"

using namespace s3::data;

namespace {

Vfs vfs;
bool vfs_ready = false;
std::shared_ptr<std::vector<uint8_t>> pending;
std::string last_error;

std::vector<float> soup; // 9 floats per accepted triangle
std::unique_ptr<SkateMap> map; // the simulation refers to it
std::unique_ptr<s3::game::driver::Sim> sim;
s3::game::driver::TickRecord record;
std::string events_joined;

float view_buf[S3H_VIEW_FLOATS];
float board_buf[7 * 13];

int fail(const std::string &e)
{
	last_error = e;
	std::fprintf(stderr, "s3h: %s\n", e.c_str());
	return 0;
}

// Interleave the low 10 bits of x, y, z.
uint32_t morton3(uint32_t x, uint32_t y, uint32_t z)
{
	auto spread = [](uint32_t v) {
		v &= 0x3ff;
		v = (v | (v << 16)) & 0x030000ff;
		v = (v | (v << 8)) & 0x0300f00f;
		v = (v | (v << 4)) & 0x030c30c3;
		v = (v | (v << 2)) & 0x09249249;
		return v;
	};
	return spread(x) | (spread(y) << 1) | (spread(z) << 2);
}

// Triangles the collision builder would reject (normal from a 1 mm weld must stay well defined).
bool degenerate(const float *t)
{
	const float ax = t[3] - t[0], ay = t[4] - t[1], az = t[5] - t[2];
	const float bx = t[6] - t[0], by = t[7] - t[1], bz = t[8] - t[2];
	const float cx = ay * bz - az * by, cy = az * bx - ax * bz, cz = ax * by - ay * bx;
	const float area2 = std::sqrt(cx * cx + cy * cy + cz * cz);
	if (!(area2 > 1e-5f)) return true; // < 0.05 cm^2
	for (int e = 0; e < 3; e++) {
		const float *p = t + e * 3, *q = t + ((e + 1) % 3) * 3;
		const float dx = q[0] - p[0], dy = q[1] - p[1], dz = q[2] - p[2];
		if (dx * dx + dy * dy + dz * dz < 4e-6f) return true; // edges under 2 mm collapse when welded
	}
	for (int i = 0; i < 9; i++)
		if (!std::isfinite(t[i])) return true;
	return false;
}

void fill_view()
{
	const auto p = sim->presentation();
	float *v = view_buf;
	std::memset(v, 0, sizeof(view_buf));
	for (int c = 0; c < 4; c++)
		for (int r = 0; r < 4; r++) v[c * 4 + r] = p.root[c][r];
	v[16] = p.camera_position[0];
	v[17] = p.camera_position[1];
	v[18] = p.camera_position[2];
	for (int c = 0; c < 3; c++)
		for (int r = 0; r < 3; r++) v[19 + c * 3 + r] = p.camera_basis[c][r];
	v[28] = p.field_of_view_degrees;
	v[29] = p.has_camera ? 1.f : 0.f;
	v[30] = p.camera_discontinuity ? 1.f : 0.f;
	v[31] = p.teleport ? 1.f : 0.f;
	v[32] = (float)p.period_seconds;
	v[33] = (float)(p.pose_generation & 0xffffff);
	v[34] = p.board_speed;
	v[35] = (float)p.wheel_surface;
	v[36] = (float)p.grind_audio_surface;
	v[37] = p.powersliding ? 1.f : 0.f;
	v[38] = p.footstep_strength;
	v[39] = p.foot_push_speed;
	v[40] = record.grounded ? 1.f : 0.f;
	v[41] = record.wiping_out ? 1.f : 0.f;
	v[42] = record.landed ? 1.f : 0.f;
	v[43] = (float)record.contact_count;
	v[44] = (float)record.physical_state_id;
	v[45] = (float)record.tick;
	v[46] = p.sequence_score;
	v[47] = p.multiplier;
	v[48] = p.line_score;
	v[49] = p.total_score;
	for (int i = 0; i < 3; i++) {
		v[50 + i] = record.rider_root_position[i];
		v[53 + i] = record.rider_root_velocity[i];
	}
	for (size_t i = 0; i < record.board.size() && i < 7; i++) {
		const auto &b = record.board[i];
		float *o = board_buf + i * 13;
		std::memcpy(o, b.position.data(), 12);
		std::memcpy(o + 3, b.orientation.data(), 16);
		std::memcpy(o + 7, b.linear_velocity.data(), 12);
		std::memcpy(o + 10, b.angular_velocity.data(), 12);
	}
	events_joined.clear();
	for (const auto &e : record.events) {
		events_joined += e;
		events_joined += '\n';
	}
}

} // namespace

extern "C" {

uint8_t *s3h_pack_alloc(uint32_t size)
{
	try {
		pending = std::make_shared<std::vector<uint8_t>>(size);
		return pending->data();
	} catch (const std::exception &e) {
		fail(std::string("pack alloc: ") + e.what());
		return nullptr;
	}
}

int s3h_pack_install(void)
{
	if (!pending) return fail("no pack buffer");
	try {
		Vfs fresh;
		auto r = fresh.install_pack(std::move(pending));
		pending.reset();
		if (!r.ok) return fail("pack: " + r.error);
		vfs = std::move(fresh);
		vfs_ready = true;
		return (int)vfs.file_count();
	} catch (const std::exception &e) {
		pending.reset();
		return fail(std::string("pack panic: ") + e.what());
	}
}

int s3h_pack_ready(void) { return vfs_ready ? 1 : 0; }

void s3h_world_begin(void) { soup.clear(); }

void s3h_world_add(const float *xyz, int count)
{
	for (int i = 0; i < count; i++) {
		const float *t = xyz + i * 9;
		if (!degenerate(t)) soup.insert(soup.end(), t, t + 9);
	}
}

int s3h_world_count(void) { return (int)(soup.size() / 9); }

int s3h_world_end(const float spawn[3], const float forward[3])
{
	sim.reset();
	map.reset();
	record = {};
	if (!vfs_ready) return fail("no pack");
	const size_t n = soup.size() / 9;
	if (n == 0) return fail("empty world");
	try {
		// Spatially sort so each 64-triangle query chunk the collision builder bounds stays small.
		float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
		std::vector<std::array<float, 3>> centre(n);
		for (size_t i = 0; i < n; i++) {
			const float *t = &soup[i * 9];
			for (int a = 0; a < 3; a++) {
				centre[i][a] = (t[a] + t[3 + a] + t[6 + a]) / 3.0f;
				lo[a] = std::min(lo[a], centre[i][a]);
				hi[a] = std::max(hi[a], centre[i][a]);
			}
		}
		std::vector<std::pair<uint32_t, uint32_t>> order(n);
		for (size_t i = 0; i < n; i++) {
			uint32_t q[3];
			for (int a = 0; a < 3; a++) {
				const float span = std::max(hi[a] - lo[a], 1e-3f);
				q[a] = (uint32_t)std::clamp((centre[i][a] - lo[a]) / span * 1023.0f, 0.0f, 1023.0f);
			}
			order[i] = {morton3(q[0], q[1], q[2]), (uint32_t)i};
		}
		std::sort(order.begin(), order.end());

		auto m = std::make_unique<SkateMap>();
		m->version = 15;
		m->name = "halo";
		m->spawn = {spawn[0], spawn[1], spawn[2]};
		m->heading = std::atan2(forward[0], forward[2]);
		Material mat;
		mat.name = "halo";
		mat.friction = 0.5f;
		mat.color = {0.5f, 0.5f, 0.5f};
		mat.roughness = 1.0f;
		m->materials.push_back(mat);
		m->geometry.collision.reserve(n);
		for (const auto &o : order) {
			const float *t = &soup[o.second * 9];
			Collision c;
			c.points = {Vec3f{t[0], t[1], t[2]}, Vec3f{t[3], t[4], t[5]}, Vec3f{t[6], t[7], t[8]}};
			c.surface = 0;
			c.material = 1;
			m->geometry.collision.push_back(c);
		}
		auto r = s3::game::driver::Sim::load(vfs, "assets", m.get(), nullptr);
		if (r.is_err()) return fail("sim: " + r.unwrap_err());
		map = std::move(m);
		sim = std::move(r.unwrap());
	} catch (const std::exception &e) {
		sim.reset();
		map.reset();
		return fail(std::string("world panic: ") + e.what());
	}
	return 1;
}

int s3h_ready(void) { return sim ? 1 : 0; }

void s3h_unload(void)
{
	sim.reset();
	map.reset();
	record = {};
}

int s3h_travel(const float pos[3], const float forward[3])
{
	if (!sim) return fail("no simulation");
	try {
		auto r = sim->travel_to({pos[0], pos[1], pos[2]}, {forward[0], forward[1], forward[2]});
		if (r.is_err()) return fail("travel: " + r.unwrap_err());
	} catch (const std::exception &e) {
		return fail(std::string("travel panic: ") + e.what());
	}
	return 1;
}

int s3h_step(int buttons, int lx, int ly, int rx, int ry, int lt, int rt)
{
	if (!sim) return fail("no simulation");
	s3::game::driver::Pad pad;
	pad.buttons = (uint16_t)buttons;
	pad.lx = (int16_t)std::clamp(lx, -32768, 32767);
	pad.ly = (int16_t)std::clamp(ly, -32768, 32767);
	pad.rx = (int16_t)std::clamp(rx, -32768, 32767);
	pad.ry = (int16_t)std::clamp(ry, -32768, 32767);
	pad.left_trigger = (uint8_t)std::clamp(lt, 0, 255);
	pad.right_trigger = (uint8_t)std::clamp(rt, 0, 255);
	try {
		auto r = sim->step(pad);
		if (r.is_err()) {
			sim.reset();
			return fail("step: " + r.unwrap_err());
		}
		record = std::move(r.unwrap());
		fill_view();
	} catch (const std::exception &e) {
		sim.reset();
		return fail(std::string("step panic: ") + e.what());
	}
	return 1;
}

float s3h_time_step(void) { return sim ? sim->time_step() : 0.0f; }

void s3h_set_aspect(float aspect)
{
	if (sim) sim->set_aspect_ratio(aspect);
}

const float *s3h_view(void) { return view_buf; }
const float *s3h_board(void) { return board_buf; }
const char *s3h_events(void) { return events_joined.c_str(); }
const char *s3h_state_name(void) { return record.physical_state.c_str(); }
const char *s3h_motion_state(void) { return record.motion_state.c_str(); }
const char *s3h_trick_name(void) { return record.trick_name.c_str(); }
const char *s3h_score_name(void) { return record.score_name.c_str(); }
const char *s3h_error(void) { return last_error.c_str(); }

} // extern "C"
