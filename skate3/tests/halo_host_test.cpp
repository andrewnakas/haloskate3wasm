// Native checks for s3_halo.h: the full runtime on host-built triangle worlds.
//   halo_host_test <core.pack> [world.tris]
// world.tris (optional): a dump from the engine (HALO_SKATE_DUMPBSP): uint32 count, then 9 floats per
// triangle in runtime axes (Y up, metres), then 3 floats spawn.
#include "s3_halo.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

int failures = 0;
int checks = 0;
#define CHECK(cond, ...)                                                    \
	do {                                                                    \
		checks++;                                                           \
		if (!(cond)) {                                                      \
			failures++;                                                     \
			std::printf("FAIL %s:%d %s: ", __FILE__, __LINE__, #cond);      \
			std::printf(__VA_ARGS__);                                       \
			std::printf("\n");                                              \
		}                                                                   \
	} while (0)

struct V
{
	float x, y, z;
};
V operator-(V a, V b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V cross(V a, V b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float dot(V a, V b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

struct World
{
	std::vector<float> tris;
	// Triangle facing `outward` (winding fixed up to match).
	void tri(V a, V b, V c, V outward)
	{
		if (dot(cross(b - a, c - a), outward) < 0) std::swap(b, c);
		for (V p : {a, b, c}) tris.insert(tris.end(), {p.x, p.y, p.z});
	}
	void quad(V a, V b, V c, V d, V outward)
	{
		tri(a, b, c, outward);
		tri(a, c, d, outward);
	}
	// Axis-aligned box, faces outward.
	void box(V lo, V hi)
	{
		V p[8];
		for (int i = 0; i < 8; i++) p[i] = {i & 1 ? hi.x : lo.x, i & 2 ? hi.y : lo.y, i & 4 ? hi.z : lo.z};
		quad(p[2], p[3], p[7], p[6], {0, 1, 0});
		quad(p[0], p[1], p[5], p[4], {0, -1, 0});
		quad(p[0], p[2], p[6], p[4], {-1, 0, 0});
		quad(p[1], p[3], p[7], p[5], {1, 0, 0});
		quad(p[0], p[1], p[3], p[2], {0, 0, -1});
		quad(p[4], p[5], p[7], p[6], {0, 0, 1});
	}
	// Floor tiles (5 m) over [-half, half]^2 at y = 0.
	void floor(float half)
	{
		for (float x = -half; x < half; x += 5)
			for (float z = -half; z < half; z += 5) quad({x, 0, z}, {x + 5, 0, z}, {x + 5, 0, z + 5}, {x, 0, z + 5}, {0, 1, 0});
	}
	// Quarter-pipe across x in [-w, w], starting at z0, radius r, rising toward +z; coping deck behind it.
	void quarter_pipe(float z0, float r, float w)
	{
		const int steps = 12;
		for (int i = 0; i < steps; i++) {
			const float a0 = (float)i / steps * 1.5707963f, a1 = (float)(i + 1) / steps * 1.5707963f;
			const V p0 = {-w, r - r * std::cos(a0), z0 + r * std::sin(a0)}, p1 = {-w, r - r * std::cos(a1), z0 + r * std::sin(a1)};
			const V centre = {0, r, z0};
			const V mid = {0, (p0.y + p1.y) / 2, (p0.z + p1.z) / 2};
			quad(p0, {w, p0.y, p0.z}, {w, p1.y, p1.z}, p1, centre - mid);
		}
		// Deck on top and the back wall.
		quad({-w, r, z0 + r}, {w, r, z0 + r}, {w, r, z0 + r + 3}, {-w, r, z0 + r + 3}, {0, 1, 0});
		quad({-w, 0, z0 + r + 3}, {w, 0, z0 + r + 3}, {w, r, z0 + r + 3}, {-w, r, z0 + r + 3}, {0, 0, 1});
	}
};

struct Run
{
	float max_y = -1e9f, min_y = 1e9f, max_z = -1e9f;
	int airborne = 0, wipeouts = 0, tricks = 0, steps = 0, failed = 0;
	std::string last_trick;
};

// Steps the sim `ticks` times with a fixed pad, collecting stats.
void drive(Run &run, int ticks, int buttons, float lx, float ly, float rx, float ry)
{
	for (int i = 0; i < ticks; i++) {
		if (!s3h_step(buttons, (int)(lx * 32767), (int)(ly * 32767), (int)(rx * 32767), (int)(ry * 32767), 0, 0)) {
			run.failed++;
			std::printf("  step failed: %s\n", s3h_error());
			return;
		}
		run.steps++;
		const float *v = s3h_view();
		run.max_y = std::max(run.max_y, v[51]);
		run.min_y = std::min(run.min_y, v[51]);
		run.max_z = std::max(run.max_z, v[52]);
		const char *ev = s3h_events();
		if (std::strstr(ev, "AIRBORNE")) run.airborne++;
		if (std::strstr(ev, "WIPEOUT")) run.wipeouts++;
		if (const char *t = std::strstr(ev, "TRICK:")) {
			run.tricks++;
			run.last_trick = std::string(t + 6, std::strcspn(t + 6, "\n"));
		}
	}
}

// Push forward (A taps) for `seconds`.
void push(Run &run, float seconds)
{
	const int ticks = (int)(seconds * 60);
	for (int t = 0; t < ticks; t += 30) {
		drive(run, 12, S3H_A, 0, 0, 0, 0);
		drive(run, 18, 0, 0, 0, 0, 0);
	}
}

void ollie(Run &run)
{
	drive(run, 12, 0, 0, 0, 0, -0.95f);
	drive(run, 6, 0, 0, 0, 0, 0.95f);
	drive(run, 60, 0, 0, 0, 0, 0);
}

bool load(const World &w, V spawn, V forward)
{
	s3h_world_begin();
	s3h_world_add(w.tris.data(), (int)(w.tris.size() / 9));
	const float s[3] = {spawn.x, spawn.y, spawn.z}, f[3] = {forward.x, forward.y, forward.z};
	const auto t0 = std::chrono::steady_clock::now();
	const int ok = s3h_world_end(s, f);
	const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
	std::printf("  world %d tris -> %s in %.0f ms\n", s3h_world_count(), ok ? "ok" : s3h_error(), ms);
	return ok;
}

std::vector<uint8_t> read_file(const char *path)
{
	std::vector<uint8_t> out;
	FILE *f = std::fopen(path, "rb");
	if (!f) return out;
	std::fseek(f, 0, SEEK_END);
	out.resize((size_t)std::ftell(f));
	std::fseek(f, 0, SEEK_SET);
	if (std::fread(out.data(), 1, out.size(), f) != out.size()) out.clear();
	std::fclose(f);
	return out;
}

} // namespace

int main(int argc, char **argv)
{
	if (argc < 2) {
		std::printf("usage: halo_host_test <core.pack> [world.tris]\n");
		return 2;
	}
	{
		const auto pack = read_file(argv[1]);
		CHECK(!pack.empty(), "read %s", argv[1]);
		if (pack.empty()) return 1;
		uint8_t *buf = s3h_pack_alloc((uint32_t)pack.size());
		std::memcpy(buf, pack.data(), pack.size());
		const int files = s3h_pack_install();
		CHECK(files > 0, "pack install: %s", s3h_error());
		std::printf("pack: %d files\n", files);
	}

	std::printf("flat ground: push + ollie\n");
	{
		World w;
		w.floor(60);
		CHECK(load(w, {0, 0, 0}, {0, 0, 1}), "load");
		Run run;
		drive(run, 60, 0, 0, 0, 0, 0);
		const float *v = s3h_view();
		const V start = {v[50], v[51], v[52]};
		std::printf("  settled at (%.2f %.2f %.2f) state %s\n", start.x, start.y, start.z, s3h_state_name());
		const float *deck = s3h_board() + S3H_BOARD_DECK * 13;
		std::printf("  deck body at (%.3f %.3f %.3f) q (%.2f %.2f %.2f %.2f), wheel y %.3f\n", deck[0], deck[1], deck[2], deck[3], deck[4],
		            deck[5], deck[6], s3h_board()[1]);
		CHECK(deck[1] > 0.02f && deck[1] < 0.3f, "deck rides just above the floor, y=%.3f", deck[1]);
		CHECK(std::fabs(start.y) < 1.5f, "rider sits on the floor, y=%.2f", start.y);
		push(run, 3);
		v = s3h_view();
		const V moved = {v[50], v[51], v[52]};
		const float dist = std::sqrt((moved.x - start.x) * (moved.x - start.x) + (moved.z - start.z) * (moved.z - start.z));
		std::printf("  after pushing: (%.2f %.2f %.2f) speed %.2f m/s, %.2f m\n", moved.x, moved.y, moved.z, v[34], dist);
		CHECK(dist > 3.0f, "pushing moves the board, %.2f m", dist);
		CHECK(moved.z - start.z > 2.0f, "travel follows the spawn facing (+z), dz=%.2f", moved.z - start.z);
		const float y0 = v[51];
		Run air;
		ollie(air);
		std::printf("  ollie: max rise %.2f m, airborne events %d, trick '%s'\n", air.max_y - y0, air.airborne, air.last_trick.c_str());
		CHECK(air.max_y - y0 > 0.2f, "ollie rises, %.2f", air.max_y - y0);
		CHECK(run.failed == 0 && air.failed == 0, "no step failure");

		// Teleport to a new spot, facing -x.
		const float pos[3] = {-20, 0, -20}, fwd[3] = {-1, 0, 0};
		CHECK(s3h_travel(pos, fwd), "travel: %s", s3h_error());
		Run after;
		drive(after, 30, 0, 0, 0, 0, 0);
		v = s3h_view();
		std::printf("  after travel: (%.2f %.2f %.2f)\n", v[50], v[51], v[52]);
		CHECK(std::fabs(v[50] + 20) < 1.5f && std::fabs(v[52] + 20) < 1.5f, "travel moves the rider");
		push(after, 2);
		v = s3h_view();
		CHECK(v[50] < -21.0f, "after travel, pushing goes -x: x=%.2f", v[50]);
	}

	std::printf("quarter-pipe: ride up the transition and come back down\n");
	{
		World w;
		w.floor(40);
		w.quarter_pipe(12, 3.0f, 10);
		CHECK(load(w, {0, 0, -10}, {0, 0, 1}), "load");
		Run run;
		drive(run, 30, 0, 0, 0, 0, 0);
		push(run, 5);
		drive(run, 240, 0, 0, 0, 0, 0);
		std::printf("  max height %.2f m, max z %.2f, min y %.2f, wipeouts %d, airborne %d\n", run.max_y, run.max_z, run.min_y,
		            run.wipeouts, run.airborne);
		CHECK(run.max_z > 12.5f, "reaches the transition, z=%.2f", run.max_z);
		CHECK(run.max_y > 1.0f, "rides up the quarter-pipe, y=%.2f", run.max_y);
		CHECK(run.failed == 0, "no step failure");
	}

	std::printf("obstacle course: random pad for 3000 ticks, no panic\n");
	{
		World w;
		w.floor(40);
		w.quarter_pipe(15, 2.5f, 8);
		w.box({3, 0, -5}, {3.6f, 0.45f, 10});
		w.box({-8, 0, -8}, {-4, 0.8f, -4});
		CHECK(load(w, {0, 0, -15}, {0, 0, 1}), "load");
		Run run;
		uint32_t seed = 12345;
		auto rnd = [&]() {
			seed = seed * 1664525u + 1013904223u;
			return (float)((seed >> 8) & 0xffff) / 65535.0f * 2 - 1;
		};
		for (int i = 0; i < 100 && !run.failed; i++) {
			const int b = (rnd() > 0.3f ? S3H_A : 0) | (rnd() > 0.8f ? S3H_LB : 0);
			drive(run, 30, b, rnd(), rnd(), rnd(), rnd());
		}
		std::printf("  %d steps, %d airborne, %d tricks, %d wipeouts\n", run.steps, run.airborne, run.tricks, run.wipeouts);
		CHECK(run.failed == 0, "survives random input");
	}

	if (argc > 2) {
		std::printf("dumped world %s\n", argv[2]);
		const auto bytes = read_file(argv[2]);
		CHECK(bytes.size() > 4, "read dump");
		if (bytes.size() > 4) {
			uint32_t n = 0;
			std::memcpy(&n, bytes.data(), 4);
			CHECK(bytes.size() >= 4 + (size_t)n * 36 + 12, "dump size");
			const float *t = (const float *)(bytes.data() + 4);
			const float *sp = t + (size_t)n * 9;
			s3h_world_begin();
			s3h_world_add(t, (int)n);
			const auto t0 = std::chrono::steady_clock::now();
			const float fwd[3] = {0, 0, 1};
			const int ok = s3h_world_end(sp, fwd);
			const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
			std::printf("  %u tris in, %d kept -> %s in %.0f ms\n", n, s3h_world_count(), ok ? "ok" : s3h_error(), ms);
			CHECK(ok, "real world loads");
			if (ok) {
				Run run;
				const auto s0 = std::chrono::steady_clock::now();
				drive(run, 60, 0, 0, 0, 0, 0);
				push(run, 4);
				ollie(run);
				drive(run, 300, S3H_A, 0.4f, 1, 0, 0);
				const double sms =
				    std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - s0).count() / std::max(run.steps, 1);
				const float *v = s3h_view();
				std::printf("  %d steps, %.3f ms/step, end (%.1f %.1f %.1f) from spawn (%.1f %.1f %.1f), %d wipeouts\n", run.steps, sms,
				            v[50], v[51], v[52], sp[0], sp[1], sp[2], run.wipeouts);
				CHECK(run.failed == 0, "no step failure on the real world");
			}
		}
	}

	std::printf("%d/%d checks passed\n", checks - failures, checks);
	return failures ? 1 : 0;
}
