// Headless check: open the local (or live) site, start a level, optionally press keys,
// print the page log and save screenshots.
//   node tools/play.mjs [--url URL] [--secs N] [--keys "G:1,W:3,Space:0.2"] [--shot docs/shots/x.png]
// --keys: comma list of KEY:holdSeconds, played in order after the game is running.
import { createRequire } from 'node:module';
import fs from 'node:fs';
import path from 'node:path';
const require = createRequire(import.meta.url);
const { chromium } = require(process.env.PLAYWRIGHT_CORE || '/Users/nakas/Downloads/DarwinWeb/node_modules/playwright-core');

const arg = (name, def) => { const i = process.argv.indexOf('--' + name); return i > 0 ? process.argv[i + 1] : def; };
const url = arg('url', 'http://localhost:8642/haloskate3wasm/?auto=1&map=a10&level=a10');
const secs = Number(arg('secs', 120));
const keys = arg('keys', '');
const shot = arg('shot', 'docs/shots/latest.png');
const grep = arg('grep', '');
const wait = Number(arg('wait', keys ? secs - 30 : secs));
const after = arg('after', ''); // wait for a log line containing this before counting --wait
let afterSeen = 0;

// A persistent profile keeps the maps in the page's storage between runs (a fresh context has a
// small quota and must re-copy 160 MB levels every time). --fresh uses a throwaway context.
const profile = arg('profile', path.join(process.env.HOME, 'Documents', 'haloskate-ref', 'chrome-profile'));
const options = {
  executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
  headless: true,
  args: ['--enable-gpu', '--use-angle=metal', '--enable-unsafe-swiftshader', '--autoplay-policy=no-user-gesture-required'],
  viewport: { width: 1280, height: 720 },
};
const fresh = process.argv.includes('--fresh');
const browser = fresh ? await chromium.launch(options) : await chromium.launchPersistentContext(profile, options);
const page = fresh ? await browser.newPage({ viewport: options.viewport }) : (browser.pages()[0] || await browser.newPage());
const t0 = Date.now();
const stamp = () => ((Date.now() - t0) / 1000).toFixed(1).padStart(6);
page.on('console', m => { const t = m.text(); if (!grep || t.includes(grep)) console.log(stamp(), m.type(), t.slice(0, 400)); });
page.on('pageerror', e => console.log(stamp(), 'PAGEERROR', e.message));
page.on('console', m => { if (after && !afterSeen && m.text().includes(after)) afterSeen = Date.now(); });
page.on('worker', w => w.on('console', m => console.log(stamp(), 'worker', m.text().slice(0, 400))));
await page.goto(url);

// wait for the game to be running (canvas shown) or for the time budget
const deadline = Date.now() + secs * 1000;
fs.mkdirSync(path.dirname(shot), { recursive: true });
let n = 0;
const snap = async (tag) => { const p = shot.replace(/\.png$/, `-${tag}.png`); await page.screenshot({ path: p }); console.log(stamp(), 'shot', p); };
if (after) { while (!afterSeen && Date.now() < deadline) await page.waitForTimeout(500); console.log(stamp(), 'seen', after); }
const waitUntil = (after ? (afterSeen || Date.now()) : t0) + wait * 1000;
while (Date.now() < waitUntil) {
  await page.waitForTimeout(Math.min(15000, Math.max(100, waitUntil - Date.now())));
  await snap(String(n++).padStart(2, '0'));
}
if (keys) {
  await page.mouse.click(640, 360);
  for (const step of keys.split(',')) {
    const [key, hold] = step.split(':');
    if (step.startsWith('type=')) { await page.keyboard.type(step.slice(5), { delay: 40 }); continue; }
    if (key === '_') { await page.waitForTimeout(Number(hold) * 1000); continue; }
    const chord = key.split('+');
    for (const k of chord) await page.keyboard.down(k);
    await page.waitForTimeout(Number(hold || 0.1) * 1000);
    for (const k of chord.reverse()) await page.keyboard.up(k);
  }
  await page.waitForTimeout(2000);
  await snap('keys');
  while (Date.now() < deadline) await page.waitForTimeout(1000);
}
await snap('end');
const probe = arg('probe', '');
if (probe) console.log(stamp(), 'probe', await page.evaluate(probe).catch(e => 'error ' + e.message));
await browser.close();
