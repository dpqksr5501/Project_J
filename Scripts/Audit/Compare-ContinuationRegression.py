from pathlib import Path
import json
import sys
import re
from collections import Counter

root = Path(__file__).resolve().parents[2] / 'Saved/Validation/Continuation_20261014'
old_name, new_name = sys.argv[1:3]
old = json.loads((root / old_name / 'index.json').read_text(encoding='utf-8-sig'))
new = json.loads((root / new_name / 'index.json').read_text(encoding='utf-8-sig'))
old_tests = {r['fullTestPath']: r for r in old['tests']}
new_tests = {r['fullTestPath']: r for r in new['tests']}
focused = '--focused' in sys.argv[3:]
baseline_full_count = len(old_tests)
outside_focus = sorted(old_tests.keys() - new_tests.keys()) if focused else []
if focused:
    # An explicitly focused run does not attempt the rest of the baseline.
    # Preserve that list separately; never present it as a full regression.
    old_tests = {name: row for name, row in old_tests.items() if name in new_tests}

def warnings(tests):
    result = {}
    for name, row in tests.items():
        messages = [e['event']['message'] for e in row.get('entries', []) if e['event']['type'] == 'Warning']
        if messages:
            result[name] = messages
    return result

def normalize(messages):
    if messages is None:
        return None
    # Only generated world/actor instance suffixes; retain warning text, class and scope.
    return [re.sub(r'(/Temp/Untitled|\b[A-Za-z][A-Za-z0-9]*_C|PersistentLevel\.[A-Za-z][A-Za-z0-9]*)_\d+', r'\1_<instance>', m) for m in messages]

before, after = warnings(old_tests), warnings(new_tests)
result = {'baseline': old_name, 'current': new_name,
          'focused_comparison': focused, 'baseline_full_test_count': baseline_full_count,
          'baseline_tests_outside_focus': outside_focus,
          'missing_old_tests': sorted(old_tests.keys() - new_tests.keys()),
          'new_tests': sorted(new_tests.keys() - old_tests.keys()),
          'warning_test_names_match': before.keys() == after.keys(),
          'warning_test_counts': {'old': len(before), 'new': len(after)},
          'warning_message_counts': {'old': sum(map(len, before.values())), 'new': sum(map(len, after.values()))},
          'raw_warning_multisets_match': all(Counter(before.get(name, [])) == Counter(after.get(name, [])) for name in before.keys() | after.keys()),
          'normalized_warning_multisets_match': all(Counter(normalize(before.get(name, []))) == Counter(normalize(after.get(name, []))) for name in before.keys() | after.keys()),
          'raw_changed_warning_messages': {name: {'old': before.get(name), 'new': after.get(name)}
                                           for name in before.keys() | after.keys() if before.get(name) != after.get(name)},
          'changed_warning_messages': {name: {'old': before.get(name), 'new': after.get(name)}
                                       for name in before.keys() | after.keys() if Counter(normalize(before.get(name, []))) != Counter(normalize(after.get(name, [])))},
          'warning_order_changed_tests': [name for name in before.keys() & after.keys()
                                         if before[name] != after[name] and Counter(before[name]) == Counter(after[name])],
          'warning_extraction': 'entries.event.type==Warning; leaf state alone is not a warning classification',
          'scope_ko': '실제 roster 및 모든 warning entry를 대조. 원문 차이를 보존하고 생성된 world/actor instance suffix만 정규화한 차이를 별도 판정. 초기 state-only 도구의 빈 경고 집계는 교정하되 원자료/receipt를 덮어쓰지 않는다. 전수 수동 감사·배포/품질 완료가 아님.'}
out = root / new_name / 'WarningMultisetComparison.json'
if out.exists():
    raise RuntimeError('Preserve prior comparison')
out.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(json.dumps({k: v for k, v in result.items() if k not in ('raw_changed_warning_messages', 'changed_warning_messages', 'baseline_tests_outside_focus')}, ensure_ascii=False))
if focused:
    print(json.dumps({'baseline_tests_outside_focus_count': len(outside_focus)}))
print(json.dumps({'raw_changed_tests': len(result['raw_changed_warning_messages']), 'normalized_changed_tests': len(result['changed_warning_messages'])}))
