// Loads the deployed site, starts a Blood Gulch match with the board on, and checks the skate
// module ran: a skate world built from the level, rider on the board, no wasm errors.
// The full runtime (skate3) logs 'skate3: ...'; the lite fallback logs 'skate: ...'. Accept either.
import { chromium } from 'playwright';

const url = process.env.URL;
const browser = await chromium.launch({ args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist',
  '--autoplay-policy=no-user-gesture-required'] });
const page = await browser.newPage({ viewport: { width: 1280, height: 720 } });
const logs = [];
const seen = (text) => logs.some((l) => l.includes(text));
const log = (m) => logs.push(`[${m.type()}] ${m.text()}`);
page.on('console', log);
page.on('worker', (w) => w.on('console', log));
page.on('response', (r) => { if (r.status() >= 400) logs.push(`[http ${r.status()}] ${r.url()}`); });
page.on('pageerror', (e) => logs.push(`[pageerror] ${e.message}`));
await page.goto(url, { timeout: 120000 });

const until = async (text, ms) => {
  const end = Date.now() + ms;
  while (!seen(text) && Date.now() < end) await page.waitForTimeout(500);
  return seen(text);
};
// the lobby appears after the UI map loads: Enter twice (lobby, start game)
if (await until('map ui: loading', 300000)) {
  await page.waitForTimeout(12000);
  await page.mouse.click(640, 360).catch(() => {});
  for (const wait of [200, 4000]) {
    await page.waitForTimeout(wait);
    await page.keyboard.press('Enter');
  }
}
const rode = (await until('on the board', 240000)) && /skate3?: on the board/.test(logs.join('\n'));
if (rode) {
  // push and ollie (pull back, flick forward)
  await page.keyboard.down('w'); await page.waitForTimeout(2000); await page.keyboard.up('w');
  await page.keyboard.down('ArrowDown'); await page.waitForTimeout(350); await page.keyboard.up('ArrowDown');
  await page.keyboard.down('ArrowUp'); await page.waitForTimeout(150); await page.keyboard.up('ArrowUp');
  await page.waitForTimeout(3000);
}
try { await page.screenshot({ path: 'smoke.png', timeout: 60000 }); } catch (e) { logs.push(`[smoke] screenshot skipped: ${e.message.split('\n')[0]}`); }
await browser.close().catch(() => {});

const text = logs.join('\n');
console.log(logs.filter((l) => /skate|map |error|http|pageerror/i.test(l)).slice(0, 80).join('\n'));
const fail = [];
if (!seen('skate: ready')) fail.push('skate module never started');
if (!/skate: \d+ grind rails derived|skate3: world built/.test(text)) fail.push('no skate world built from the level');
if (!rode) fail.push('never got on the board');
if (/RuntimeError|unreachable|Aborted\(/.test(text)) fail.push('wasm error in console');
if (fail.length) { console.error('SMOKE FAIL: ' + fail.join('; ')); process.exit(1); }
const runtime = seen('skate3: on the board') ? 'full runtime' : 'lite';
console.log(`SMOKE OK (${runtime})` + (/skate: pop Ollie|skate3: trick Ollie/.test(text) ? ' (ollie popped)' : ''));
