// Dependency-free regression fixtures; never change the caller's Git index.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {execFileSync, spawnSync} from 'node:child_process';

const validator = path.resolve(import.meta.dirname, '../Validation/Validate-DocLinks.mjs');
const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'project-j-doc-links-'));
let checks = 0;
try {
  function fixture(name) {
    const root = path.join(temporary, name);
    fs.mkdirSync(path.join(root, 'Scripts', 'Validation'), {recursive: true});
    fs.mkdirSync(path.join(root, 'Docs'));
    fs.copyFileSync(validator, path.join(root, 'Scripts', 'Validation', 'Validate-DocLinks.mjs'));
    fs.writeFileSync(path.join(root, 'README.md'), '');
    return root;
  }
  function write(root, name, content = 'fixture\n') {
    const full = path.join(root, name);
    fs.mkdirSync(path.dirname(full), {recursive: true});
    fs.writeFileSync(full, content);
  }
  function git(root, ...args) {
    return execFileSync('git', args, {cwd: root, encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe']});
  }
  function check(root, links, broken, archives = 0) {
    fs.writeFileSync(path.join(root, 'README.md'), links.map(link => `[target](${link})`).join('\n'));
    const run = spawnSync(process.execPath, ['Scripts/Validation/Validate-DocLinks.mjs'], {cwd: root, encoding: 'utf8'});
    assert.ifError(run.error);
    assert.equal(run.status, broken ? 1 : 0, run.stdout + run.stderr);
    assert.match(run.stdout, new RegExp(`, ${broken} broken links, ${archives} links to absent local archives`));
    checks++;
  }

  const repo = fixture('sparse');
  git(repo, 'init', '--quiet');
  for (const name of ['Omitted/한 글.md', 'Ordinary/deleted.md', 'Present/file.md']) write(repo, name);
  git(repo, 'add', '.');
  git(repo, 'update-index', '--skip-worktree', 'Omitted/한 글.md');
  fs.rmSync(path.join(repo, 'Omitted'), {recursive: true});
  fs.rmSync(path.join(repo, 'Ordinary'), {recursive: true});
  check(repo, ['Omitted/%ED%95%9C%20%EA%B8%80.md#anchor', 'Omitted/', 'Present/file.md'], 0);
  check(repo, ['Ordinary/deleted.md', 'Ordinary/', 'Omitted/nonexistent.md', 'OmittedExtra/'], 4);
  check(repo, ['Saved/Worktrees/old/evidence.md', 'Saved/WorktreesExtra/missing.md', 'Saved/Worktrees/../missing.md'], 2, 1);

  const archive = fixture('archive');
  write(archive, 'Present/한 글.md');
  check(archive, ['Present/%ED%95%9C%20%EA%B8%80.md', 'Present/'], 0);
  check(archive, ['Omitted/file.md', 'Omitted/', 'Saved/Worktrees/old/evidence.md'], 2, 1);
  console.log(`PASS: ${checks} doc-link regression scenarios`);
} finally {
  fs.rmSync(temporary, {recursive: true, force: true});
}
