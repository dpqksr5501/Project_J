// Rebuild documentation navigation only; document bodies and evidence are read-only.
import fs from 'node:fs';
import path from 'node:path';

const root = path.resolve(import.meta.dirname, '..', '..');
const docs = path.join(root, 'Docs');
const output = path.join(docs, 'Maintenance', 'Document_Catalog.md');
const slash = value => value.replaceAll('\\', '/');
const cell = value => value.replaceAll('|', '\\|').replaceAll('\n', ' ');
const topics = new Map([
  ['Overview', '프로젝트 개요'], ['Architecture', '공통 아키텍처'],
  ['Animation', '애니메이션'], ['Combat', '전투'], ['Gameplay', '게임플레이'],
  ['Networking', '네트워크'], ['Performance', '성능'], ['Benchmarks', '벤치마크'],
  ['Review', '검토·후속 결과'], ['Handoffs', '작업 인계'],
  ['Reference', '참고 원본'], ['Maintenance', '문서 관리'], ['Archive', '보관 자료']
]);
function walk(folder) {
  return fs.readdirSync(folder, { withFileTypes: true }).flatMap(entry => {
    const full = path.join(folder, entry.name);
    if (entry.isSymbolicLink()) throw new Error(`Unexpected symbolic link: ${full}`);
    return entry.isDirectory() ? walk(full) : [full];
  });
}
function kind(file, source) {
  const parts = file.split('/');
  if (parts[0] === 'Archive') return '보존 원문';
  if (source.includes('이 문서의 상세 본문은')) return '통합 문서 안내';
  if (path.basename(file) === 'README.md' || parts[0] === 'Maintenance') return '목차·문서 관리';
  if (file.endsWith('_System.md')) return '통합 상세 가이드';
  if (file.includes('Prompt')) return '당시 작업 요청';
  if (file.includes('Result')) return '시점별 결과';
  if (parts.includes('Planning')) return '계획·제안';
  if (parts.includes('Diagnostics')) return '진단·수정 기록';
  if (parts.includes('CaptureGuides')) return '자료 수집 절차';
  if (parts.includes('Reports') || parts.includes('Baselines') || parts.includes('Validation')) return '시점별 보고·검증';
  if (parts.includes('Authoring')) return '제작 가이드';
  if (parts[0] === 'Review') return '검토 기록';
  if (parts[0] === 'Handoffs') return '작업 인계';
  if (parts[0] === 'Benchmarks') return '측정·재현';
  if (parts[0] === 'Reference') return '원본 참고';
  if (parts[0] === 'Overview') return '프로젝트 문맥';
  if (parts.includes('Extensions')) return '기반 계약·당시 검증';
  return '구조·계약';
}
fs.mkdirSync(path.dirname(output), { recursive: true });
if (!fs.existsSync(output)) fs.writeFileSync(output, '# 전체 문서 목록\n');
const files = walk(docs).sort((a, b) => slash(a).localeCompare(slash(b), 'en'));
const records = files.filter(file => file.endsWith('.md')).map(file => {
  const source = fs.readFileSync(file, 'utf8');
  const rel = slash(path.relative(docs, file));
  return { file, relative: rel, topic: rel.includes('/') ? rel.split('/')[0] : '',
    title: source.match(/^#\s+(.+)$/m)?.[1]?.trim() ?? path.basename(file), type: kind(rel, source) };
});
let text = `# 전체 문서 목록\n\n[문서 시작점](../README.md) · [분류·보존 규칙](README.md)\n\n`;
text += '목록은 파일 위치와 문서 종류를 안내한다. 구현 완료·현재 성능을 판정하는 표가 아니며, 각 문서의 날짜·범위·후속 기록을 함께 읽는다. 통합 문서 안내는 이전 링크를 이어주는 진입점이고 상세 본문은 통합 가이드에 있다.\n\n';
text += `문서 폴더 파일 ${files.length}개, Markdown ${records.length}개. 목록 갱신: \`node Scripts/Documentation/Update-DocumentCatalog.mjs\`.\n\n`;
text += '## 주제별 목록\n\n';
for (const [topic, label] of topics) {
  const rows = records.filter(record => record.topic === topic);
  if (!rows.length) continue;
  text += `### ${label}\n\n| 문서 | 종류 | 경로 |\n| --- | --- | --- |\n`;
  for (const row of rows) {
    const link = slash(path.relative(path.dirname(output), row.file));
    text += `| [${cell(row.title)}](${link}) | ${row.type} | ${cell(row.relative)} |\n`;
  }
  text += '\n';
}
text += '## 측정·검증·참고 원본\n\n| 위치 | 파일 수 | 내용 |\n| --- | --- | --- |\n';
const nonMarkdown = files.filter(file => !file.endsWith('.md'));
const folders = new Map();
for (const file of nonMarkdown) {
  const folder = path.dirname(file);
  folders.set(folder, (folders.get(folder) ?? 0) + 1);
}
for (const [folder, count] of [...folders].sort((a, b) => a[0].localeCompare(b[0], 'en'))) {
  const rel = slash(path.relative(docs, folder));
  const link = slash(path.relative(path.dirname(output), folder));
  const role = rel.startsWith('Benchmarks') ? 'CSV·JSON·재계산 원본' :
    rel.startsWith('Architecture') ? '카탈로그·대응 검증 JSON' :
    rel.startsWith('Reference') ? '참고 TXT 원문' : '문서 이동·보존 기록';
  text += `| [${rel}](${link}) | ${count} | ${role} |\n`;
}
fs.writeFileSync(output, text);
console.log(`${records.length} Markdown documents indexed; ${files.length} files in Docs.`);
