from pathlib import Path
import json,re
out=Path(__file__).resolve().parent
results=[]
for name in ['ProjectDataAssetValidation1','ProjectDataAssetValidation2']:
    folder=out/name/'DataAsset'
    if not (folder/'process.json').exists():continue
    process=json.loads((folder/'process.json').read_text(encoding='utf-8-sig'))
    text=(folder/'Run.log').read_text(encoding='utf-8-sig')
    rows={}; errors=[]; warnings=[]
    for line in text.splitlines():
        # Native commandlet logs individual asset outcomes but omits the editor summary API.
        # Exclude duplicated LogInit exit summaries; never infer missing skipped/unable counters as zero.
        if 'LogInit:' in line:continue
        match=re.search(r"AssetCheck: (\S+)'([^']+)' (Validating asset|contains valid data\.|contains invalid data\.|.*unable.*)",line)
        if match:
            cls,path,event=match.groups()
            row=rows.setdefault(path,dict(path=path,asset_class=cls,events=[]))
            row['events'].append(event)
        if 'AssetCheck: Error:' in line:errors.append(line)
        if 'AssetCheck: Warning:' in line:warnings.append(line)
    result=dict(run=name,process=process,per_asset_rows=list(rows.values()),
        unique_assets_with_validating_log=sum('Validating asset' in r['events'] for r in rows.values()),
        unique_assets_with_valid_log=sum('contains valid data.' in r['events'] for r in rows.values()),
        unique_assets_with_invalid_log=sum('contains invalid data.' in r['events'] for r in rows.values()),
        asset_error_entries=errors,asset_warning_entries=warnings,
        summary_counters_available=process.get('validationSummaryAvailable',False),
        limitation='Counts are actual per-asset log observations; absent skipped/unable summary counters remain unavailable. Generic package/reference validators can return valid without a class-specific gameplay numeric validator. Not all assets or authored visual/index quality.')
    results.append(result)
(out/'DataAssetValidationSummary.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps([{k:v for k,v in r.items() if k not in {'per_asset_rows','asset_error_entries','asset_warning_entries'}} for r in results]))
