// Published links must also work in a clean checkout.
import fs from 'node:fs';
import path from 'node:path';

const root = path.resolve(import.meta.dirname, '..', '..');
const files = [];
function walk(folder) {
  for (const entry of fs.readdirSync(folder, {withFileTypes: true})) {
    const item = path.join(folder, entry.name);
    if (entry.isDirectory()) walk(item);
    else if (entry.name.endsWith('.md')) files.push(item);
  }
}
walk(path.join(root, 'Docs'));
files.push(path.join(root, 'README.md'));

// Inline links can use angle brackets for destinations containing spaces.
const inlineLink = /\]\([ \t]*(?:<([^<>\r\n]*)>|((?:\\[()]|[^<>()\s]|\([^()\r\n]*\))*))(?:[ \t]+(?:"[^"\r\n]*"|'[^'\r\n]*'|\([^()\r\n]*\)))?[ \t]*\)/g;

function getLocalDestination(target) {
  const bare = target.split('#', 1)[0];
  if (!bare) return null;
  // A drive letter and file: both identify machine-local paths.
  if (/^(?:[a-z]:|file:)/i.test(bare)) return bare;
  if (/^(?:[a-z][a-z0-9+.-]*:|\/\/)/i.test(bare)) return null;
  const unescaped = bare.replace(/\\([()])/g, '$1');
  try { return decodeURIComponent(unescaped); }
  catch { return unescaped; }
}

function pointsIntoCheckout(file, destination) {
  if (/^(?:[a-z]:|file:)/i.test(destination) || path.win32.isAbsolute(destination)) {
    return false;
  }
  const full = path.resolve(path.dirname(file), destination);
  const relative = path.relative(root, full);
  const firstDirectory = relative.split(path.sep, 1)[0].toLowerCase();
  return firstDirectory !== '..' && firstDirectory !== 'saved' &&
    !path.isAbsolute(relative) && fs.existsSync(full);
}

const missing = [];
let checked = 0;
for (const file of files) {
  const source = fs.readFileSync(file, 'utf8');
  const relative = path.relative(root, file).replaceAll('\\', '/');
  for (const match of source.matchAll(inlineLink)) {
    const target = match[1] ?? match[2];
    const destination = getLocalDestination(target);
    if (destination === null) continue;
    checked++;
    if (pointsIntoCheckout(file, destination)) continue;
    const line = source.slice(0, match.index).split('\n').length;
    const record = `${relative}:${line} -> ${target}`;
    missing.push(record);
  }
}
console.log(`${files.length} Markdown files, ${checked} local links, ${missing.length} broken links`);
for (const record of missing) console.error(record);
if (missing.length) process.exitCode = 1;
