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

const missing = [];
const generatedRoot = path.join(root, 'Saved') + path.sep;
let checked = 0;
for (const file of files) {
  const source = fs.readFileSync(file, 'utf8');
  const relative = path.relative(root, file).replaceAll('\\', '/');
  for (const match of source.matchAll(/\]\(([^)]*)\)/g)) {
    const target = match[1];
    if (!target || target.startsWith('#') || /^(?:[a-z]+:|\/\/)/i.test(target)) continue;
    const bare = target.split('#', 1)[0];
    if (!bare || bare.includes(' ')) continue;
    checked++;
    // Markdown URLs encode spaces and non-ASCII names; resolve filesystem paths
    // after decoding, rather than reporting existing Korean-named images missing.
    let decoded;
    try { decoded = decodeURIComponent(bare); }
    catch { decoded = bare; }
    const full = path.resolve(path.dirname(file), decoded);
    if (!full.startsWith(generatedRoot) && fs.existsSync(full)) continue;
    const line = source.slice(0, match.index).split('\n').length;
    const record = `${relative}:${line} -> ${target}`;
    missing.push(record);
  }
}
console.log(`${files.length} Markdown files, ${checked} local links, ${missing.length} broken links`);
for (const record of missing) console.error(record);
if (missing.length) process.exitCode = 1;
