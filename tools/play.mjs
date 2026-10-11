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

const browser = await chromium.launch({
  executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
  headless: true,
  args: ['--enable-gpu', '--use-angle=metal', '--enable-unsafe-swiftshader', '--autoplay-policy=no-user-gesture-required'],
});
const page = await browser.newPage({ viewport: { width: 1280, height: 720 } });
const t0 = Date.now();
const stamp = () => ((Date.now() - t0) / 1000).toFixed(1).padStart(6);
page.on('console', m => { const t = m.text(); if (!grep || t.includes(grep)) console.log(stamp(), m.type(), t.slice(0, 400)); });
page.on('pageerror', e => console.log(stamp(), 'PAGEERROR', e.message));
page.on('worker', w => w.on('console', m => console.log(stamp(), 'worker', m.text().slice(0, 400))));
await page.goto(url);

// wait for the game to be running (canvas shown) or for the time budget
const deadline = Date.now() + secs * 1000;
fs.mkdirSync(path.dirname(shot), { recursive: true });
let n = 0;
const snap = async (tag) => { const p = shot.replace(/\.png$/, `-${tag}.png`); await page.screenshot({ path: p }); console.log(stamp(), 'shot', p); };
while (Date.now() < deadline - (keys ? 30000 : 0)) {
  await page.waitForTimeout(10000);
  await snap(String(n++).padStart(2, '0'));
  const running = await page.evaluate(() => !!(window.__haloRunning || document.body.classList.contains('playing'))).catch(() => false);
  if (running && keys) break;
}
if (keys) {
  await page.mouse.click(640, 360);
  for (const step of keys.split(',')) {
    const [key, hold] = step.split(':');
    await page.keyboard.down(key);
    await page.waitForTimeout(Number(hold || 0.1) * 1000);
    await page.keyboard.up(key);
  }
  await page.waitForTimeout(2000);
  await snap('keys');
}
await snap('end');
await browser.close();
