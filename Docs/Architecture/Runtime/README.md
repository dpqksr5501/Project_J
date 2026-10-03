# 런타임 책임과 변경 보고서

## 읽는 순서

1. [현재 작업 문맥](../../Overview/ProjectContext.md)에서 모듈과 코드 진입점을 확인한다.
2. [캐릭터 컴포넌트 실행·수명 정리](Reports/Character_Component_Ownership_2026-09-20.md)와 대응 [검증 JSON](Reports/Character_Component_Ownership_Validation_2026-09-20.json)을 읽는다.
3. 이후 캐릭터·이동·AnimInstance 책임 변경은 [캐릭터 런타임 후속 결과](../../Review/CharacterRuntime/README.md)에서 이어 읽는다.

## 구현·검증 기록

| 보고서 | 범위 |
| --- | --- |
| [2026-09-19 내부 갱신·수명 정리](Reports/Internal_Polish_2026-09-19.md) · [검증](Reports/Internal_Polish_Validation_2026-09-19.json) | GAS 갱신, 서버 피격 기록, NPC 실행과 제작 도구 |
| [2026-09-19 서비스 점검](Reports/Service_Internal_Audit_2026-09-19.md) | Core·MMO·서비스의 요청·종료 계약 |
| [2026-09-13 내부 리팩터링](Reports/Internal_Refinement_2026-09-13.md) · [검증](Reports/Internal_Refinement_Validation_2026-09-13.json) | 전투·입력·장비·카메라·되감기 책임 |
| [2026-06-19 초기 리팩터링](Reports/ArchitectureRefactor_20260619.md) | 당시 소유권과 장비·애니메이션 경계 |

Reports는 해당 시점의 결과다. 이후 변경과 현재 소스까지 확인하며, 과거 통과 개수를 현재 전체 테스트 수로 사용하지 않는다.
