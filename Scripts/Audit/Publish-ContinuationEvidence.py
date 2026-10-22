"""Copy bounded audit receipts; large raw traces/logs remain hash-addressed locally."""
from pathlib import Path
import hashlib
import json
import shutil

root = Path(__file__).resolve().parents[2]
saved = root / 'Saved/Validation/Continuation_20261014'
output = root / 'Docs/Review/Audits/ContinuationEvidence_2026-10-14'
index = []

def record(path, copy=True):
    if not path.is_file():
        return
    relative = path.relative_to(root)
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    target = output / 'Receipts' / relative
    if copy:
        target.parent.mkdir(parents=True, exist_ok=True)
        if target.exists() and hashlib.sha256(target.read_bytes()).hexdigest() != digest:
            raise RuntimeError(f'Prior evidence changed: {target}')
        if not target.exists():
            shutil.copyfile(path, target)
    index.append({'raw_path': relative.as_posix(), 'sha256': digest, 'bytes': path.stat().st_size,
                  'portable_copy': target.relative_to(output).as_posix() if copy else None})

# Explicit allowlist: never publish unrelated UserDir saves, caches or credentials.
for name in ['HistoricalComparisonVerification.json','CA01_05_UI_Summary.json','CA01_13_CookedUI1_Summary.json','ValidationOverlapReceipt.json','InitialUserChanges.patch']:
    record(saved / name)
for folder in saved.iterdir():
    if not folder.is_dir() or folder.name == 'RA27EditorBinaries':
        continue
    for name in ['arguments.json', 'process.json', 'index.json', 'SourceHashes.json',
                 'EditorBinaryHashes.json', 'Preservation.json', 'AssetHashes.json', 'ContentWorktreeHashes.json', 'HistoricalPreservation.json', 'Head.txt', 'TrackedSourceChanges.patch',
                 'BaselineCompiledSourceManifest.json', 'CurrentCompiledSourceManifest.json',
                 'ComparedCompiledSourceManifest.json',
                 'ComparisonManifest.json', 'RestoreEditorHashes.json', 'RestoreReceipt.json',
                 'CPUComparisonRows.csv', 'CPUComparisonSummary.json', 'StageTraceSummary.json',
                 'RenderComparisonSummary.json', 'GameVersion.json', 'LayoutReceipt.json', 'PackageIdentity.json',
                 'NotifyObservationSummary.json','NotifyLifecycle.json','NotifyStageSummary.json','AssetConnections.json',
                 'InheritedWarningComparison.json','WarningRosterComparison.json','WarningMultisetComparison.json','IntegratedPerformanceContext.json','Checkpoint.json',
                 'GitStatus.txt','Processes.json','UserPreservation.json','ExitStatus.json','ResumeIdentity.json','R15_SystemVerification.json','DemandCallerSummary.json','IntegratedVerification.json','HostSessionEvents.json','InterruptionReceipt.json','IncrementalBefore.json','IncrementalAfter.json','IncrementalSummary.json',
                 'StructureCleanupVerification.json','StructureCleanupSummary.json','ArchitectureCatalogValidation.json','ReadOnlyReviewClosure.json']:
        record(folder / name)
    for name in ['Run.log', 'UBT.log', 'Cook.log', 'Stage.log']:
        record(folder / name, False)
    if folder.name.startswith('CA_NotifyLifecycle'):
        record(folder / 'CPU.utrace', False)
        for export in folder.glob('TraceExport*'):
            for path in export.glob('*.csv'):
                record(path)
            record(export / 'Export.rsp')
            record(export / 'Insights.log', False)
    if folder.name.startswith('CA_AssetValidation'):
        for run in folder.iterdir():
            if run.is_dir():
                for name in ['arguments.json', 'process.json']:
                    record(run / name)
                record(run / 'Run.log', False)
for base, pattern, names in [
    ('Saved/Validation/AuthoredAnimation_20261005', 'CA_CPU_*', ['manifest.json','summary.csv','result.json','stats.csv','analysis.json','cpu.json','arguments.json','comparison-provenance.json','ProcessCpu.json']),
    ('Saved/Validation/WholeProjectAudit_20261013', 'CA_Render_*', ['manifest.json','frames.csv','result.json','result.txt','process.json','comparison-provenance.json']),
    ('Saved/Validation/WholeProjectAudit_20261013', 'CA01_13_CookedRender*', ['manifest.json','frames.csv','result.txt','process.json','verification.json']),
    ('Saved/Validation/EF_20260912', 'CA01_05_*', ['manifest.json','frames.csv','result.txt','process.json','load.txt']),
    ('Saved/Validation/GroupD_20260910', 'CA01_05_*', ['manifest.json','server.json','client.json','verification.json','processes.json','server-arguments.json','disabled-editor-toolsets.json','network-phases.csv','animation-observations.csv','combat-observations.csv']),
    ('Saved/Validation/EF_20260912', 'CA01_13_*', ['manifest.json','frames.csv','result.txt','process.json','load.txt']),
    ('Saved/Validation/GroupD_20260910', 'CA01_13_*', ['manifest.json','server.json','client.json','verification.json','processes.json','server-arguments.json','disabled-editor-toolsets.json','network-phases.csv','animation-observations.csv','combat-observations.csv']),
    ('Saved/Validation', 'CA01_13_CookedUI*', ['Results.json']),
    ('Saved/Validation', 'CA01_05_CookedUI*', ['Results.json'])]:
    for folder in (root / base).glob(pattern):
        for path in folder.rglob('*'):
            if path.is_file() and path.name in names:
                record(path)
            elif path.is_file() and (path.suffix in ['.utrace','.png'] or path.name in ['Run.log','trace.log']):
                record(path, False)
            elif path.is_file() and folder.name.startswith('CA_CPU_Integrated_Trace1') and path.parent.name.startswith('TraceExport'):
                if path.name in ['AllTimers.csv','Threads.csv','Counters.csv','Export.rsp'] or path.name.startswith('Timers-'):
                    record(path)
                elif path.suffix == '.csv' or path.name == 'Insights.log':
                    record(path, False)
# This continuation's names have a separate allowlist. Never copy UserDir saves/caches.
for base, pattern, names in [
    ('Saved/Validation/AuthoredAnimation_20261005', 'R15_CA16_CPU_*', ['manifest.json','summary.csv','result.json','cpu.json','arguments.json','comparison-provenance.json','ProcessCpu.json']),
    ('Saved/Validation/WholeProjectAudit_20261013', 'R15_CA16_Render_*', ['manifest.json','frames.csv','result.txt','process.json','comparison-provenance.json']),
    ('Saved/Validation/EF_20260912', 'R15_CA14_15_CookedCombat*', ['manifest.json','result.txt','process.json']),
    ('Saved/Validation/EF_20260912', 'R15_CA14_16_CookedCombat*', ['manifest.json','result.txt','process.json']),
    ('Saved/Validation/EF_20260912', 'R15_CA17_18_CookedCombat*', ['manifest.json','result.txt','process.json']),
    ('Saved/Validation/GroupD_20260910', 'R15_CA14_16_EditorRemote*', ['manifest.json','server.json','client.json','verification.json','processes.json','server-arguments.json','disabled-editor-toolsets.json','network-phases.csv','animation-observations.csv','combat-observations.csv']),
    ('Saved/Validation/GroupD_20260910', 'R15_CA17_18_EditorRemote*', ['manifest.json','server.json','client.json','verification.json','processes.json','server-arguments.json','disabled-editor-toolsets.json','network-phases.csv','animation-observations.csv','combat-observations.csv']),
]:
    for folder in (root / base).glob(pattern):
        for path in folder.rglob('*'):
            if not path.is_file():
                continue
            if path.name in names:
                record(path)
            elif path.suffix in ['.utrace', '.png'] or path.name in ['Run.log', 'trace.log', 'Insights.log']:
                record(path, False)
            elif path.parent.name.startswith('TraceExport') and (path.suffix == '.csv' or path.name == 'Export.rsp'):
                record(path, path.name in ['AllTimers.csv', 'Threads.csv', 'Export.rsp'] or path.name.startswith('Timers-'))
(output / 'EvidenceIndex.json').write_text(json.dumps({'scope_ko': '새 실행의 제한된 원자료 사본과 큰 로그/trace/이미지 SHA. 파일 존재·시험 통과는 전수 수동 감사 또는 품질 완료가 아님.',
                                                       'files': index}, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(json.dumps({'evidence_files': len(index), 'portable_copies': sum(bool(r['portable_copy']) for r in index)}))
