import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';

function makeCheckout(t) {
  const checkout = fs.mkdtempSync(path.join(os.tmpdir(), 'project-j-doc-links-'));
  t.after(() => fs.rmSync(checkout, {recursive: true, force: true}));
  fs.mkdirSync(path.join(checkout, 'Docs'));
  fs.mkdirSync(path.join(checkout, 'Saved'));
  const scripts = path.join(checkout, 'Scripts', 'Validation');
  fs.mkdirSync(scripts, {recursive: true});
  const validator = path.join(scripts, 'Validate-DocLinks.mjs');
  fs.copyFileSync(new URL('./Validate-DocLinks.mjs', import.meta.url), validator);
  fs.writeFileSync(path.join(checkout, 'README.md'), '');
  fs.writeFileSync(path.join(checkout, 'Docs', '검증 결과 (1).md'), '');
  fs.writeFileSync(path.join(checkout, 'Saved', 'report.json'), '{}');

  return markdown => {
    fs.writeFileSync(path.join(checkout, 'Docs', 'Guide.md'), markdown);
    return spawnSync(process.execPath, [validator], {encoding: 'utf8'});
  };
}

test('저장소 상대 링크와 공백이 있는 대상은 열 수 있다', t => {
  const run = makeCheckout(t);
  const result = run([
    '[꺾쇠 괄호](<검증 결과 (1).md> "제목")',
    '[URL 인코딩](검증%20결과%20%281%29.md#기록)',
    '[상위 문서](../README.md)',
    '[문서 내 이동](#기록)',
    '[외부 링크](https://example.com/path)',
    '[프로토콜 상대 링크](//example.com/path)',
    '로컬 원자료 (`Saved/report.json`)',
  ].join('\n'));

  assert.equal(result.status, 0, result.stderr);
  assert.match(result.stdout, /3 local links, 0 broken links/);
});

test('누락된 파일과 로컬 PC에 의존하는 링크는 실패한다', t => {
  const run = makeCheckout(t);
  const targets = [
    'missing.md',
    '<누락된 결과.md>',
    '../Saved/report.json',
    '../%53aved/report.json',
    'C:/Desktop/source.md',
    '<C:/Desktop/설계 기록.md>',
    '/C:/Desktop/source.md',
    'file:///C:/Desktop/source.md',
    'C%3A/Desktop/source.md',
    '../../outside.md',
  ];
  const result = run(targets.map(target => `[문서](${target})`).join('\n'));

  assert.equal(result.status, 1, result.stderr);
  assert.match(result.stdout, /10 local links, 10 broken links/);
  assert.deepEqual(result.stderr.trimEnd().split(/\r?\n/), targets.map((target, index) =>
    `Docs/Guide.md:${index + 1} -> ${target.replace(/^<|>$/g, '')}`));
});
