// Copies the web app into www/ for Capacitor: the page, app/ (including
// Engine 2's WebAssembly build) and src/ (the rules and Engine 1.6, which the
// app and its workers import). The Google Fonts links are swapped for the
// fonts bundled in fonts/, so the app looks the same offline.
import { cpSync, mkdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';

const root = new URL('../', import.meta.url);
const www = new URL('www/', import.meta.url);

rmSync(www, { recursive: true, force: true });
mkdirSync(www);
for (const dir of ['app', 'src']) cpSync(new URL(dir, root), new URL(dir, www), { recursive: true });
cpSync(new URL('fonts', import.meta.url), new URL('fonts', www), { recursive: true });

const html = readFileSync(new URL('index.html', root), 'utf8');
const fontLinks = /\s*<link[^>]*fonts\.(googleapis|gstatic)\.com[^>]*>/g;
if (!fontLinks.test(html)) throw new Error('index.html: Google Fonts links not found');
const page = html.replace(fontLinks, '').replace('</head>', '  <link rel="stylesheet" href="fonts/fonts.css">\n</head>');
writeFileSync(new URL('index.html', www), page);
console.log('www/ ready');
