// Headless gamepad check: a fake standard gamepad (navigator.getGamepads) drives the game.
//   node tools/padtest.mjs [--url URL] [--shot docs/shots/pad.png]
// Starts a Blood Gulch quick match, waits for the skate world, then: B (board on), A pushes, a right-stick
// ollie flick, LB held with RT (combat: fire) plus a flick (must not trick), LB released + a flick, B (off).
import { createRequire } from 'node:module';
import path from 'node:path';
const require = createRequire(import.meta.url);
const { chromium } = require(process.env.PLAYWRIGHT_CORE || '/Users/nakas/Downloads/DarwinWeb/node_modules/playwright-core');

const arg = (name, def) => { const i = process.argv.indexOf('--' + name); return i > 0 ? process.argv[i + 1] : def; };
const url = arg('url', 'http://localhost:8642/haloskate3wasm/?auto=1&quick=host&map=bloodgulch&room=0');
const shot = arg('shot', 'docs/shots/pad.png');
const profile = path.join(process.env.HOME, 'Documents', 'haloskate-ref', 'chrome-profile');
const browser = await chromium.launchPersistentContext(profile, {
  executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome', headless: true,
  args: ['--enable-gpu', '--use-angle=metal', '--enable-unsafe-swiftshader', '--autoplay-policy=no-user-gesture-required'],
  viewport: { width: 1280, height: 720 },
});
const page = browser.pages()[0] || await browser.newPage();
await page.addInitScript(() => {
  const pad = { id: 'Xbox Wireless Controller (STANDARD GAMEPAD)', index: 0, connected: true, mapping: 'standard',
    timestamp: 0, axes: [0, 0, 0, 0], buttons: Array.from({ length: 17 }, () => ({ pressed: false, touched: false, value: 0 })) };
  window.__pad = pad;
  navigator.getGamepads = () => [pad];
  window.__press = (i, v) => { pad.buttons[i] = { pressed: v > 0.5, touched: v > 0, value: v }; pad.timestamp++; };
  window.__axis = (i, v) => { pad.axes[i] = v; pad.timestamp++; };
});
const t0 = Date.now();
const stamp = () => ((Date.now() - t0) / 1000).toFixed(1).padStart(6);
const seen = [];
page.on('console', (m) => {
  const t = m.text();
  seen.push(t);
  if (/skate|s3h|PAGEERROR/.test(t) && !/SKATE LIMITATION|Riding\/Turning\/Idle/.test(t)) console.log(stamp(), t.slice(0, 300));
});
page.on('pageerror', (e) => console.log(stamp(), 'PAGEERROR', e.message));
const wait = (s) => page.waitForTimeout(s * 1000);
const until = async (text, secs) => {
  const end = Date.now() + secs * 1000;
  while (Date.now() < end) { if (seen.some((t) => t.includes(text))) return true; await wait(0.5); }
  return false;
};
const press = async (i, secs, v = 1) => { await page.evaluate(([i, v]) => window.__press(i, v), [i, v]); await wait(secs); await page.evaluate((i) => window.__press(i, 0), i); };
const axis = (i, v) => page.evaluate(([i, v]) => window.__axis(i, v), [i, v]);
const flick = async () => { await axis(3, 1); await wait(0.25); await axis(3, -1); await wait(0.12); await axis(3, 0); };
const snap = async (tag) => { const p = shot.replace(/\.png$/, `-${tag}.png`); await page.screenshot({ path: p }); console.log(stamp(), 'shot', p); };
const count = (text) => seen.filter((t) => t.includes(text)).length;

await page.goto(url);
// a cached older build would run otherwise: drop the service worker and the game caches (not the skate pack)
await page.evaluate(async () => {
  for (const r of await navigator.serviceWorker.getRegistrations()) await r.unregister();
  for (const k of await caches.keys()) if (!k.includes('skatepack') && !k.includes('maps')) await caches.delete(k);
});
seen.length = 0;
await page.goto(url);
await until('skate: ready', 150);
await wait(3);
await page.mouse.click(640, 360);
await page.keyboard.press('Enter'); await wait(2); await page.keyboard.press('Enter');
if (!await until('skate3: world built', 120)) console.log(stamp(), 'NO WORLD');
await wait(3);
console.log(stamp(), 'controller seen by the page:', await page.evaluate(() => document.body.classList.contains('has-controller')));
const results = [];
const check = (name, ok) => { results.push(`${ok ? 'PASS' : 'FAIL'} ${name}`); console.log(stamp(), ok ? 'PASS' : 'FAIL', name); };

await press(1, 0.2); // B
check('B gets on the board', await until('on the board', 5));
for (let i = 0; i < 3; i++) { await press(0, 0.3); await wait(0.5); } // A pushes
const tricks0 = count('skate3: trick');
await flick(); await wait(2);
check('right stick flick ollies', count('skate3: trick') > tricks0);

await page.evaluate(() => window.__press(4, 1)); // hold LB
await wait(0.5);
await snap('combat');
const tricks1 = count('skate3: trick');
await flick();
await press(7, 1.0); // RT: fire
await wait(1.5);
check('LB held: flick does not trick', count('skate3: trick') === tricks1);
await page.evaluate(() => window.__press(4, 0)); // release LB
await wait(0.5);
for (let i = 0; i < 2; i++) { await press(0, 0.3); await wait(0.5); }
const tricks2 = count('skate3: trick');
await flick(); await wait(2);
check('LB released: flicks again', count('skate3: trick') > tricks2);
await snap('flick');
await press(1, 0.2); // B
check('B gets off the board', await until('off the board', 5));
await wait(2);
await snap('off');
console.log(results.join('\n'));
await browser.close();
