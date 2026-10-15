"""Publish bounded receipts; do not turn inventory/automation success into full audit completion."""
import hashlib
import json
import re
import shutil
import subprocess
from datetime import datetime, timezone
from pathlib import Path

root = Path(__file__).resolve().parents[2]
saved = root / 'Saved/Validation/AssetCleanup_20261015'
continuation = root / 'Saved/Validation/Continuation_20261014'
out = root / 'Docs/Review/Audits/AssetArchitectureEvidence_2026-10-15'
if out.exists():
    raise RuntimeError('Preserve prior published evidence')

def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))

def sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def git(*args):
    return subprocess.check_output(['git', '-c', 'core.fsmonitor=false', '-c', 'core.safecrlf=false', *args], cwd=root)

baseline = read(saved / 'Baseline/Files.json')
current = {}
for folder in ('Content', 'Source', 'Config', 'Plugins'):
    for path in (root / folder).rglob('*'):
        if path.is_file() and not any(p in ('Binaries', 'Intermediate', 'Saved') for p in path.parts):
            current[path.relative_to(root).as_posix()] = sha(path)
changed = [p for p in baseline if p in current and baseline[p] != current[p]]
removed = sorted(set(baseline) - set(current))
added = sorted(set(current) - set(baseline))
expected_changes = {
    'Content/Character_BPs/GreatSword/BP_GreatSword.uasset',
    'Content/Animation_Logic/ABPs/ABP_Player.uasset',
    'Source/Project_JCharacter/Public/Animation/Project_JCharacterAnimInstance.h',
    'Source/Project_JCharacter/Private/Tests/Project_JMotionMatchingCrowdTests.cpp',
}
expected_added = {'Source/Project_J/Private/Tests/Project_JAuthoredCombatInterfaceTests.cpp'}
assert set(changed) == expected_changes and set(added) == expected_added and not removed, 'Unexpected source/asset delta; preserve and inspect'
state = read(saved / 'Baseline/State.json')
assert git('rev-parse', 'HEAD').decode().strip() == state['head'], 'HEAD changed'
assert git('branch', '--show-current').decode().strip() == state['branch'], 'Branch changed'
handoff = 'Docs/Handoffs/WholeProjectAudit_2026-10-14/Project_J_Complete_Audit_Continuation_2026-10-14.md'
incoming = (saved / 'Baseline/Incoming.diff').read_bytes()
marker = ('diff --git a/' + handoff + ' b/' + handoff + '\n').encode()
start = incoming.index(marker)
end = incoming.find(b'\ndiff --git ', start + len(marker))
original_patch = incoming[start:] if end < 0 else incoming[start:end + 1]
assert git('diff', '--binary', '--no-ext-diff', '--', handoff) == original_patch, 'Incoming user handoff patch changed'

focus = read(continuation / 'R16_AssetArchitectureFinalFocused1/index.json')
assert focus['failed'] == 0 and focus['notRun'] == 0 and focus['inProcess'] == 0 and focus['succeeded'] > 0
assert read(continuation / 'R16_AssetArchitectureFinalFocused1/process.json')['exitCode'] == 0
assert read(continuation / 'R16_AssetMMCallbackTestBuild2/process.json')['exitCode'] == 0
reload = read(saved / 'Reload2/Verification.json')
assert read(saved / 'Reload2/Process.json')['exitCode'] == 0
assert read(saved / 'Cook2/Process.json')['exitCode'] == 0
cook_log = (saved / 'Cook2/Run.log').read_text(encoding='utf-8-sig', errors='replace')
counts = re.search(r'Packages Cooked: (\d+), Packages Incrementally Skipped: (\d+), Packages Skipped by Platform: (\d+), Total Packages: (\d+)', cook_log)
assert counts, 'Cook counters missing'
referenced = (saved / 'Cook2/Cooked/Windows/Project_J/Metadata/ReferencedSet.txt').read_text(encoding='utf-8-sig')
assert '/game/character_bps/greatsword/bp_greatsword' in referenced.casefold() and '/game/animation_logic/abps/abp_player' in referenced.casefold(), 'Changed asset missing from cook closure'
processes = read(saved / 'FinalProcesses.json')
assert not processes, 'Native processes still running'

out.mkdir(parents=True)
portable = [
    saved / 'Baseline/State.json', saved / 'Baseline/Files.json', saved / 'FinalFocusedSelection.json',
    saved / 'Editor1/Query2Parsed.json', saved / 'Editor1/Query3Parsed.json', saved / 'Editor1/CombatBeforeParsed.json',
    saved / 'Editor1/IdleEditorStop.json', saved / 'Editor2/RetirePlaceholders.json', saved / 'Editor2/Exit.json',
    saved / 'Editor3/Nodes1Parsed.json', saved / 'Editor3/Exit.json',
    saved / 'Editor4/RetireDuplicateMMWriter4.json', saved / 'Editor4/BindingRead.json', saved / 'Editor4/Exit.json',
    saved / 'Reload1/Verification.json', saved / 'Reload1/Process.json',
    saved / 'Reload2/Verification.json', saved / 'Reload2/Process.json',
    saved / 'Cook1/Process.json', saved / 'Cook2/Process.json', saved / 'Cook2/Arguments.json', saved / 'FinalProcesses.json',
]
for name in ('R16_AssetInterfaceTestBuild1', 'R16_AssetMMCallbackTestBuild1', 'R16_AssetMMCallbackTestBuild2'):
    portable.append(continuation / name / 'process.json')
for name in ('R16_AssetInterfaceRepro1', 'R16_AssetInterfaceFocused1', 'R16_AssetMMCallbackRepro1', 'R16_AssetArchitectureFinalFocused1'):
    portable.extend([continuation / name / 'index.json', continuation / name / 'process.json'])
index = []
for path in portable:
    relative = path.relative_to(root / 'Saved')
    target = out / 'Receipts' / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(path, target)
    index.append({'raw_path':path.relative_to(root).as_posix(), 'sha256':sha(path), 'portable_copy':target.relative_to(out).as_posix()})
for path in sorted(saved.glob('*/Run.log')) + [continuation / n / f for n,f in (
    ('R16_AssetInterfaceTestBuild1','UBT.log'), ('R16_AssetMMCallbackTestBuild1','UBT.log'),
    ('R16_AssetMMCallbackTestBuild2','UBT.log'), ('R16_AssetInterfaceRepro1','Run.log'),
    ('R16_AssetMMCallbackRepro1','Run.log'), ('R16_AssetArchitectureFinalFocused1','Run.log'))]:
    index.append({'raw_path':path.relative_to(root).as_posix(), 'sha256':sha(path), 'bytes':path.stat().st_size, 'portable_copy':None})

graph_data = read(saved / 'Editor1/Query2Parsed.json')
nodes = read(saved / 'Editor3/Nodes1Parsed.json')
summary = {
    'utc':datetime.now(timezone.utc).isoformat(), 'head':state['head'], 'branch':state['branch'],
    'actual_cleanup_ko':['BP_GreatSword 상수 인터페이스 override3개→부모 native 권위',
        '테스트 ABP_Player 네 MM callback 바인딩+중복 single-DB setter→기존 native proxy',
        '제작 script 중복 기본값→기존 native CDO 상속'],
    'asset_delete_count':0, 'asset_added_count':0, 'content_changed':[p for p in changed if p.startswith('Content/')],
    'baseline_file_count':len(baseline), 'changed_existing':changed, 'added_source':added, 'removed_files':removed,
    'unchanged_baseline_files':len(baseline)-len(changed), 'incoming_user_handoff_patch_preserved':True,
    'changed_hashes':[{'path':p,'before_sha256':baseline[p],'after_sha256':current[p]} for p in changed],
    'added_hashes':[{'path':p,'sha256':current[p]} for p in added],
    'editor_binary_hashes':{str(p.relative_to(root)).replace('\\','/'):sha(p) for p in
        [root/'Binaries/Win64'/('UnrealEditor-'+m['Name']+'.dll') for m in read(root/'Project_J.uproject')['Modules']] + [root/'Binaries/Win64/UnrealEditor.modules']},
    'blueprint_metadata_count':len(graph_data['blueprints']), 'listed_graphs':sum(len(x['graphs']) for x in graph_data['blueprints']),
    'empty_dsl_graphs_read_via_nodes':len(nodes['graphs']), 'node_count':sum(len(x.get('nodes',[])) for x in nodes['graphs']),
    'game_registry_count':reload['game_package_count'], 'selected_registry_metadata_count':len(reload['selected_registry_metadata']),
    'focused':{k:focus[k] for k in ('succeeded','succeededWithWarnings','failed','notRun','inProcess','totalDuration')},
    'cook':dict(zip(('cooked','incremental_skipped','platform_skipped','total'),map(int,counts.groups()))),
    'both_changed_assets_in_cook_referenced_set':True, 'game_executable_package_remote_validated':False,
    'whole_asset_graph_property_environment_audit_complete':False,
    'remaining_ko':['모든 DA/GE·모든 노드 속성·동적 root의 전수 상세 대조',
        'mobile touch 입력모드/다중 LocalPlayer·Door 제작옵션/권위·Wyvern generated tick 실행',
        '용도 불명 (1) 제작 snapshot3개·빈 PlayerWeaponProfile·legacy Weapon1 보존',
        'CPU/GPU·대규모 부하/원격·전수 시각품질·운영 설치 보류'],
    'process_rule_ko':'빌드/시험/Cook 정상 종료·겹침 없음. Editor1은 읽기 완료 후 전용 유휴 창 없는 호스트만 종료, 이후 호스트2/3/4 QuitEditor 정상 exit0.',
    'no_additional_commit_push_merge_shutdown':True,
}
(out / 'EvidenceIndex.json').write_text(json.dumps(index, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
(out / 'Summary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
print(json.dumps({'focused':summary['focused'],'cook':summary['cook'],'content_changed':summary['content_changed'],'unrelated_delta':0},ensure_ascii=False))
