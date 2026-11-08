// Renders the link preview of the site (og:image, 1920x1080) for one language from the repository's own logo and screenshot.
//   node tools/og_image.mjs <assets directory> <out.png> <language> [height=720] [width=1920]
// Needs @resvg/resvg-js and DejaVu Sans (Linux: /usr/share/fonts/truetype/dejavu); the JPEGs in assets/social/ are this PNG
// converted with quality 84. The texts of the six languages are in TEXTS below; keep them in step with web/site_text.js.
import { Resvg } from '@resvg/resvg-js';
import { readFileSync, writeFileSync } from 'node:fs';
import { join } from 'node:path';

const [assets, out, lang = 'en', hArg = '720', widthOut = '1920'] = process.argv.slice(2);
const TEXTS = {
  en: { h1: 'Beach Life (2002),', h2: 'now in the browser.', s1: 'Also known as Virtual Resort: Spring Break.', s2: 'Play with your own copy of the game.', chips: ['No install', 'Windows · Mac · Linux', 'Open source'] },
  pl: { h1: 'Beach Life (2002),', h2: 'teraz w przeglądarce.', s1: 'Znana też jako Virtual Resort: Spring Break.', s2: 'Graj z własną kopią gry.', chips: ['Bez instalacji', 'Windows · Mac · Linux', 'Otwarty kod'] },
  de: { h1: 'Beach Life (2002),', h2: 'jetzt im Browser.', s1: 'Auch: Virtual Resort: Spring Break.', s2: 'Spiele mit deiner eigenen Kopie.', chips: ['Ohne Installation', 'Windows · Mac · Linux', 'Open Source'] },
  fr: { h1: 'Beach Life (2002),', h2: 'dans le navigateur.', s1: 'Alias Virtual Resort: Spring Break.', s2: 'Avec votre propre copie du jeu.', chips: ['Sans installer', 'Windows · Mac · Linux', 'Open source'] },
  it: { h1: 'Beach Life (2002),', h2: 'nel browser.', s1: 'Anche: Virtual Resort: Spring Break.', s2: 'Con la tua copia del gioco.', chips: ['Senza installare', 'Windows · Mac · Linux', 'Open source'] },
  es: { h1: 'Beach Life (2002),', h2: 'en el navegador.', s1: 'También: Virtual Resort: Spring Break.', s2: 'Con tu propia copia del juego.', chips: ['Sin instalar', 'Windows · Mac · Linux', 'Código abierto'] },
};
const T = TEXTS[lang];
const shot = readFileSync(join(assets, 'screenshots/browser.jpg')).toString('base64');
const inner = (f, color) => {
  const s = readFileSync(join(assets, 'logo', f), 'utf8');
  const vb = s.match(/viewBox="([^"]+)"/)[1];
  const body = s.replace(/^[\s\S]*?<svg[^>]*>/, '').replace(/<\/svg>\s*$/, '').replaceAll('#398ff5', color);
  return { vb, body };
};
const mark = inner('openbeach-mark.svg', '#ffffff');
const word = inner('openbeach-wordmark.svg', '#ffffff');

const W = 1280, H = Number(hArg); const dy = (H - 640) / 2;
const chip = (x, label) => {
  const w = label.length * 11.2 + 32;
  return { w, svg: `<rect x="${x}" y="0" width="${w}" height="40" rx="20" fill="#ffffff" fill-opacity="0.14" stroke="#ffffff" stroke-opacity="0.35"/>
  <text x="${x + w / 2}" y="27" text-anchor="middle" font-family="DejaVu Sans" font-weight="bold" font-size="18" fill="#ffffff">${label}</text>` };
};
let cx = 0; const chips = [];
for (const l of T.chips) { const c = chip(cx, l); chips.push(c.svg); cx += c.w + 12; }

const svg = `<svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="${W}" height="${H}" viewBox="0 0 ${W} ${H}">
  <defs>
    <linearGradient id="fade" x1="0" y1="0" x2="1" y2="0">
      <stop offset="0" stop-color="#06213f" stop-opacity="1"/>
      <stop offset="0.43" stop-color="#06213f" stop-opacity="1"/>
      <stop offset="0.62" stop-color="#06213f" stop-opacity="0.25"/>
      <stop offset="1" stop-color="#06213f" stop-opacity="0"/>
    </linearGradient>
    <linearGradient id="bottom" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0.75" stop-color="#06213f" stop-opacity="0"/>
      <stop offset="1" stop-color="#06213f" stop-opacity="0.55"/>
    </linearGradient>
  </defs>
  <rect width="${W}" height="${H}" fill="#06213f"/>
  <!-- game view only (browser chrome cropped away), shifted right -->
  <svg x="540" y="0" width="740" height="${H}" viewBox="650 100 500 ${Math.round(432*H/640)}" preserveAspectRatio="xMidYMid slice">
    <image width="1193" height="1002" xlink:href="data:image/jpeg;base64,${shot}"/>
  </svg>
  <rect width="${W}" height="${H}" fill="url(#fade)"/>
  <rect width="${W}" height="${H}" fill="url(#bottom)"/>
  <svg x="72" y="${92+dy}" width="96" height="96" viewBox="${mark.vb}">${mark.body}</svg>
  <svg x="186" y="${108+dy}" width="330" height="64" viewBox="${word.vb}" preserveAspectRatio="xMinYMid meet">${word.body}</svg>
  <text x="72" y="${270+dy}" font-family="DejaVu Sans" font-weight="bold" font-size="46" fill="#ffffff">${T.h1}</text>
  <text x="72" y="${326+dy}" font-family="DejaVu Sans" font-weight="bold" font-size="46" fill="#ffffff">${T.h2}</text>
  <text x="72" y="${384+dy}" font-family="DejaVu Sans" font-size="25" fill="#cfe4ff">${T.s1}</text>
  <text x="72" y="${418+dy}" font-family="DejaVu Sans" font-size="25" fill="#cfe4ff">${T.s2}</text>
  <g transform="translate(72 ${462+dy})">${chips.join('\n')}</g>
  <text x="72" y="${562+dy}" font-family="DejaVu Sans" font-weight="bold" font-size="26" fill="#7fc0ff">openbeach.app</text>
</svg>`;

writeFileSync(out.replace(/\.png$/, '.svg'), svg);
const png = new Resvg(svg, {
  fitTo: { mode: 'width', value: Number(widthOut) },
  font: { fontDirs: ['/usr/share/fonts/truetype/dejavu'], loadSystemFonts: false, defaultFontFamily: 'DejaVu Sans' },
}).render().asPng();
writeFileSync(out, png);
console.log(out, png.length, 'bytes');
