// Local dev server: serves dist/ at /haloskate3wasm/ and proxies the halo-cleanroom map
// repos (same origin on Pages) from andrewnakas.github.io, caching map files on disk. /skate3-cleanroom/
// (the skate pack) is proxied too; core.pack comes from the local cleanpack copy when it is there.
//   node tools/devserver.mjs [port]
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';

const root = path.resolve(path.dirname(new URL(import.meta.url).pathname), '..', 'dist');
const cache = path.join(os.homedir(), 'Documents', 'haloskate-ref', 'maps');
const port = Number(process.argv[2] || 8642);
const types = { '.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript', '.wasm': 'application/wasm',
  '.json': 'application/json', '.css': 'text/css', '.png': 'image/png', '.svg': 'image/svg+xml', '.webmanifest': 'application/manifest+json' };
const isolate = { 'Cross-Origin-Opener-Policy': 'same-origin', 'Cross-Origin-Embedder-Policy': 'require-corp',
  'Cross-Origin-Resource-Policy': 'same-origin' };

fs.mkdirSync(cache, { recursive: true });

const localPack = path.join(os.homedir(), 'Documents', 'haloskate-ref', 'cleanpack', 'core.pack');

async function proxy(url, res) {
  const clean = url.split('?')[0];
  if (clean === '/skate3-cleanroom/core.pack' && fs.existsSync(localPack)) {
    res.writeHead(200, { ...isolate, 'Content-Type': 'application/octet-stream', 'Content-Length': fs.statSync(localPack).size });
    return fs.createReadStream(localPack).pipe(res);
  }
  const file = path.join(cache, clean.replace(/[^a-zA-Z0-9._-]/g, '_'));
  if (!fs.existsSync(file)) {
    const r = await fetch('https://andrewnakas.github.io' + clean);
    if (!r.ok) { res.writeHead(r.status, isolate); return res.end(); }
    const buf = Buffer.from(await r.arrayBuffer());
    if (!clean.endsWith('.json')) fs.writeFileSync(file, buf);
    res.writeHead(200, { ...isolate, 'Content-Type': 'application/octet-stream' });
    return res.end(buf);
  }
  res.writeHead(200, { ...isolate, 'Content-Type': 'application/octet-stream' });
  fs.createReadStream(file).pipe(res);
}

http.createServer(async (req, res) => {
  try {
    const url = decodeURIComponent(req.url);
    if (/^\/(halo-cleanroom(-maps\d)?|skate3-cleanroom)\//.test(url)) return await proxy(url, res);
    if (url === '/' || url === '/haloskate3wasm') { res.writeHead(302, { Location: '/haloskate3wasm/' }); return res.end(); }
    let p = url.split('?')[0].replace(/^\/haloskate3wasm/, '');
    if (p.endsWith('/')) p += 'index.html';
    const file = path.join(root, p);
    if (!file.startsWith(root) || !fs.existsSync(file)) { res.writeHead(404, isolate); return res.end(); }
    res.writeHead(200, { ...isolate, 'Content-Type': types[path.extname(file)] || 'application/octet-stream', 'Cache-Control': 'no-cache' });
    fs.createReadStream(file).pipe(res);
  } catch (e) {
    res.writeHead(500, isolate); res.end(String(e));
  }
}).listen(port, () => console.log(`http://localhost:${port}/haloskate3wasm/`));
