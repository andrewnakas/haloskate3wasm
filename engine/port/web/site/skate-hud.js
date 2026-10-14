// HaloSkate3 HUD: trick names, combo and score over the game, fed by the game thread
// (source/skate/skate_halo.c -> web_note_skate -> app.js haloMessage kind 9).
'use strict';

(() => {
  const css = `
  #skate-hud { position: fixed; inset: 0; pointer-events: none; z-index: 30; background: none !important; border: 0 !important; border-radius: 0 !important; box-shadow: none !important; padding: 0 !important; margin: 0 !important; max-width: none !important; width: auto !important; height: auto !important; backdrop-filter: none !important; font-family: "Kenney Future", "Segoe UI", system-ui, sans-serif;
    color: #e8f6ff; text-shadow: 0 2px 6px rgba(0,0,0,.85); display: none; }
  #skate-hud.on, #skate-hud.hint { display: block; }
  #skate-hud .score { position: absolute; top: 14%; right: 4%; text-align: right; font-size: clamp(14px, 2.2vw, 26px); letter-spacing: .06em; }
  #skate-hud .score b { display: block; font-size: 1.5em; color: #7fd4ff; }
  #skate-hud .trick { position: absolute; left: 0; right: 0; top: 18%; text-align: center; font-size: clamp(18px, 3.4vw, 44px); font-weight: 700;
    letter-spacing: .04em; color: #fff; opacity: 0; transition: opacity .25s; }
  #skate-hud .trick.show { opacity: 1; }
  #skate-hud .combo { position: absolute; left: 0; right: 0; top: 25%; text-align: center; font-size: clamp(13px, 2vw, 24px); color: #ffd36a; min-height: 1.2em; }
  #skate-hud .line { position: absolute; left: 10%; right: 10%; top: 30%; text-align: center; font-size: clamp(11px, 1.4vw, 16px); color: #bfe6ff; opacity: .9; }
  #skate-hud .bail { position: absolute; left: 0; right: 0; top: 40%; text-align: center; font-size: clamp(28px, 6vw, 72px); font-weight: 800; color: #ff5a4f;
    opacity: 0; transform: scale(1.4); transition: opacity .2s, transform .2s; }
  #skate-hud .bail.show { opacity: 1; transform: scale(1); }
  #skate-hud .pack { position: absolute; bottom: 16%; right: 4%; font-size: clamp(11px, 1.3vw, 15px); color: #ffd36a; }
  #skate-hud .combat { position: absolute; bottom: 20%; left: 50%; transform: translateX(-50%); font-size: clamp(12px, 1.6vw, 20px);
    font-weight: 700; letter-spacing: .12em; color: #ff8a5c; display: none; }
  #skate-hud .combat.show { display: block; }
  #skate-hud .speed { position: absolute; bottom: 12%; right: 4%; font-size: clamp(12px, 1.6vw, 20px); color: #9fd9ff; }
  #skate-hud .help { position: absolute; bottom: 4%; left: 50%; transform: translateX(-50%); font-size: clamp(10px, 1.2vw, 14px);
    background: rgba(5, 18, 32, .72); border: 1px solid rgba(127, 212, 255, .35); border-radius: 10px; padding: .55em 1em; color: #d6efff; white-space: nowrap; }
  #skate-hud .help kbd { font: inherit; color: #7fd4ff; }
  #skate-hud.on .help.short { display: none; }
  #skate-hud:not(.on) .help.long { display: none; }
  #skate-hud.collapsed .help.long { display: none; }
  #skate-hud.collapsed.on .help.short { display: block; }
  .skate-card { margin: 14px 0 6px; padding: 12px 14px; border-radius: 12px; background: rgba(46, 140, 220, .12); border: 1px solid rgba(127, 212, 255, .3);
    font-size: 14px; line-height: 1.5; }
  .skate-card h3 { margin: 0 0 6px; font-size: 15px; color: #7fd4ff; }
  .skate-card table { border-collapse: collapse; width: 100%; }
  .skate-card td { padding: 2px 6px 2px 0; vertical-align: top; }
  .skate-card td:first-child { white-space: nowrap; color: #bfe6ff; font-weight: 600; }
  `;
  const style = document.createElement('style');
  style.textContent = css;
  document.head.appendChild(style);

  const hud = document.createElement('div');
  hud.id = 'skate-hud';
  hud.innerHTML = `
    <div class="score">SCORE<b id="skate-banked">0</b></div>
    <div class="trick" id="skate-trick"></div>
    <div class="combo" id="skate-combo"></div>
    <div class="line" id="skate-line"></div>
    <div class="bail" id="skate-bail">BAILED</div>
    <div class="speed" id="skate-speed"></div>
    <div class="pack" id="skate-pack"></div>
    <div class="combat" id="skate-combat">COMBAT · right stick aims · triggers fire</div>
    <div class="help short"><kbd>B</kbd> board · <kbd>H</kbd> controls</div>
    <div class="help long"><kbd>W</kbd>/<kbd>Space</kbd> push · <kbd>A D</kbd> carve/spin · <kbd>S</kbd> brake ·
      <kbd>↓ then ↑</kbd> ollie · <kbd>↓ then ↖/↗</kbd> kick/heelflip · <kbd>↓ then ←/→</kbd> shove-it ·
      <kbd>Q</kbd>/<kbd>C</kbd> grab · <kbd>B</kbd> off board · pad: <kbd>B</kbd> board, hold <kbd>LB</kbd> to aim and fire (LB+B melee) · <kbd>H</kbd> hide</div>`;
  document.body.appendChild(hud);

  const $ = (id) => document.getElementById(id);
  const fmt = (n) => Number(n).toLocaleString('en-US');
  let trickTimer = 0;
  let bailTimer = 0;
  let onBoard = false;
  const line = [];

  function showTrick(text) {
    const el = $('skate-trick');
    el.textContent = text;
    el.classList.add('show');
    clearTimeout(trickTimer);
    trickTimer = setTimeout(() => el.classList.remove('show'), 1400);
  }

  const pretty = (name) => name
    .replace(/^N_/, 'Nollie ')
    .replace(/^U_/, 'Underflip ')
    .replace(/FSPopShuvit/, 'FS Pop Shove-it')
    .replace(/PopShuvit/, 'Pop Shove-it')
    .replace(/([a-z])([A-Z])/g, '$1 $2');

  function message(text) {
    let m;
    try { m = JSON.parse(text); } catch { return; }
    switch (m.e) {
      case 'pack':
        $('skate-pack').textContent = m.ok ? '' : 'Skate 3 pack unavailable: simple board';
        break;
      case 'on':
        onBoard = true;
        hud.classList.add('on');
        break;
      case 'off':
        onBoard = false;
        hud.classList.remove('on');
        $('skate-combo').textContent = '';
        $('skate-line').textContent = '';
        $('skate-speed').textContent = '';
        break;
      case 'trick':
        showTrick(pretty(m.n));
        line.push(pretty(m.n));
        if (line.length > 6) line.shift();
        $('skate-line').textContent = line.join(' + ');
        break;
      case 'grind':
        showTrick(m.n);
        break;
      case 'bank':
        showTrick(`+${fmt(m.p)}`);
        line.length = 0;
        $('skate-line').textContent = '';
        break;
      case 'bail': {
        const el = $('skate-bail');
        el.classList.add('show');
        clearTimeout(bailTimer);
        bailTimer = setTimeout(() => el.classList.remove('show'), 1200);
        line.length = 0;
        $('skate-line').textContent = '';
        break;
      }
      case 's':
        $('skate-banked').textContent = fmt(m.b);
        $('skate-combo').textContent = m.c > 0 ? `${fmt(m.c)}  ×${m.m}` : '';
        $('skate-speed').textContent = `${Math.round(m.v * 3.6)} km/h`;
        $('skate-combat').classList.toggle('show', !!m.f);
        if (m.k && (m.st === 1 || m.st === 4)) {
          $('skate-trick').textContent = m.k;
          $('skate-trick').classList.add('show');
        }
        break;
    }
  }

  // H toggles the long controls strip
  addEventListener('keydown', (event) => {
    if (event.code === 'KeyH' && !event.repeat) hud.classList.toggle('collapsed');
  });

  // the controls card on the launcher, and the hint strip once the game shows
  function addCard() {
    // below the Start game button, so the button stays at the top of the launcher
    const panel = document.querySelector('#launcher #simple') || document.querySelector('#launcher .panel header');
    if (!panel || document.querySelector('.skate-card')) return;
    const card = document.createElement('div');
    card.className = 'skate-card';
    card.innerHTML = `<h3>Skate the Halo campaign</h3>
      <table>
        <tr><td>B</td><td>hop on or off the board (keyboard or gamepad; on the pad, LB + B melees while riding)</td></tr>
        <tr><td>Hold LB</td><td>gamepad combat while riding: right stick aims, triggers fire and throw grenades.
          Let go and the right stick flicks tricks again (the triggers grab)</td></tr>
        <tr><td>W / Space</td><td>push · S brake · A D carve, spin in the air</td></tr>
        <tr><td>Arrows</td><td>flick-it (right stick): pull ↓ then flick ↑ to ollie, ↓ then ↖ kickflip, ↓ then ↗ heelflip,
          ↓ then sweep ← / → shove-its, ↑ then ↓ nollie, ← → hardflip, scoop ← ↙ ↓ ↗ for a 360 flip</td></tr>
        <tr><td>Shift / C</td><td>manual / nose manual (W S to balance)</td></tr>
        <tr><td>Q / X</td><td>grab · X with a carve: powerslide · E still enters vehicles</td></tr>
        <tr><td>Mouse</td><td>aim and shoot while you ride: kills mid-trick score big</td></tr>
      </table>`;
    panel.after(card);
  }
  if (document.readyState === 'loading') addEventListener('DOMContentLoaded', addCard); else addCard();
  const watch = setInterval(() => {
    const launcher = document.getElementById('launcher');
    if (launcher && launcher.hidden || document.body.classList.contains('playing')) {
      hud.classList.add('hint');
      clearInterval(watch);
    }
  }, 1000);

  const packStatus = (text) => { $('skate-pack').textContent = text; };
  window.HaloSkateHUD = { message, packStatus, get onBoard() { return onBoard; } };
})();
