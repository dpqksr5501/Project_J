// Check local Markdown links after documentation moves. Historical evidence in
// Saved/Worktrees is optional because it is not tracked in the repository.
import fs from 'node:fs';
import path from 'node:path';
import {execFileSync} from 'node:child_process';

const root = path.resolve(import.meta.dirname, '..', '..');
// Sparse checkouts deliberately omit skip-worktree entries. Do not accept all
// indexed paths: an ordinary tracked file deleted locally is still a broken link.
const sparseTargets = new Set();
try {
  const options = {cwd: root, encoding: 'utf8', stdio: ['ignore', 'pipe', 'ignore'], maxBuffer: 64 * 1024 * 1024};
  const gitRoot = execFileSync('git', ['rev-parse', '--show-toplevel'], options).trim();
  if (path.resolve(gitRoot) === root) {
    const entries = execFileSync('git', ['ls-files', '-t', '-z', '--full-name'], options);
    for (const entry of entries.split('\0')) {
      if (!entry.startsWith('S ')) continue;
      let target = path.resolve(root, entry.slice(2));
      // Include directories only when they contain a skip-worktree entry.
      while (target !== root && target.startsWith(root + path.sep)) {
        sparseTargets.add(target);
        target = path.dirname(target);
      }
    }
  }
} catch {
  // Source archives (or systems without Git) retain filesystem-only validation.
}
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
const absentLocalArchive = [];
const archiveRoot = path.join(root, 'Saved', 'Worktrees') + path.sep;
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
    if (fs.existsSync(full) || sparseTargets.has(full)) continue;
    const line = source.slice(0, match.index).split('\n').length;
    const record = `${relative}:${line} -> ${target}`;
    (full.startsWith(archiveRoot) ? absentLocalArchive : missing).push(record);
  }
}
console.log(`${files.length} Markdown files, ${checked} local links, ${missing.length} broken links, ${absentLocalArchive.length} links to absent local archives`);
for (const record of missing) console.error(record);
if (missing.length) process.exitCode = 1;
