# 문서 분류·본문 통합 결과 — 2026-10-03

## 작업 범위

기준 코드 커밋은 `7f32060`이며 이번 작업은 문서 경로·목차·본문 통합과 목록 생성기다. 게임 코드·Config·Unreal 에셋은 수정하지 않았다. 문서 정리를 위해 새 Unreal 빌드나 에디터 실행을 수행하지 않았다.

## 정리한 구조

- 주제: Overview, Architecture, Animation, Combat, Gameplay, Networking, Performance, Benchmarks, Review, Handoffs, Reference.
- 주제 안의 문서 종류: Architecture, Authoring, Diagnostics, CaptureGuides, Planning, Reports/Baselines/Validation.
- Maintenance에는 작성·보존 규칙, 전체 목록, 경로 변경표를 둔다. Archive에는 이전 안내와 통합 전 상세 원문을 보존한다.
- 공통 Runtime·Extensions·전체 계획은 Architecture에서, 애니메이션·네트워크·성능의 상세 구조는 각 주제에서 찾는다.

## 본문 통합

| 기준 문서 | 통합한 원문 | 유지한 상세 내용 |
| --- | --- | --- |
| [무기 파지·손 접촉](../Animation/Authoring/Weapon_Hand_Contact_System.md) | 5개 | 데이터 역할, Palm 역변환 수식, 양팔 설정표, 노티파이·커브 우선순위, 현재 포즈 목표 공간, 팔꿈치 안정화, 스케일·갱신·복귀·수명·MMORPG 경계, 로그·검증 기록 |
| [전투 애니메이션](../Combat/Architecture/Combat_Animation_System.md) | 3개 | 상체/FullBody 모드, 레이어 계약, 이동 정책 경계, 비동기 로딩·취소, 슬롯·다리 IK, 콤보·커맨드 입력·첫 공격 작성과 체크리스트 |
| [콘텐츠 데이터 제작](../Gameplay/Authoring/Content_Authoring_System.md) | 3개 | DA 역할·태그, 스탯·직업·전직·스킬·장비 확장, 코드 진입점, 공유/인라인 데이터, 생성 도구·검증·저장·등록 |

원문의 설정·예외·코드·수치·단계별 내용을 상세 절로 유지했다. 복귀 문서의 동일한 몸체 프로필 계약은 통합 가이드의 몸체 상세 절로 연결하고, 비플레이어 어댑터 계약은 복귀 절에 남겼다. 전투의 초기 FullBody·소켓 예시는 현재 모드 선택과 구분했다. 서로 다른 시점의 보고서·계획·측정 결과는 개별 기록으로 유지했다.

## 보존과 검증

- 정리 전 Docs 파일 **137개 모두 보존**. 기존 경로에서 **71개 이동**, 기존 목차 원문 7개와 통합 원문 11개도 보관했다.
- 기존 Markdown 보존본은 허용된 경로·링크 수정 외의 본문 변화가 없는지 비교했다.
- 기존 비-Markdown **38개**는 원본과 바이트 단위로 일치한다. CSV·JSON·기존 스크립트·참고 TXT를 포함한다.
- 루트 README는 문서 참조 경로·통합 절 링크만 수정했다. 카탈로그 생성기·코드가 참조하는 MMO 카탈로그 경로와 벤치마크 데이터 배치는 유지했다.
- 통합 문서·이전 경로 안내의 절 링크 **226개**를 검사했고 누락이 없다.
- `node Scripts/Validation/Validate-DocLinks.mjs`: 현재 저장소의 깨진 파일 링크 **0개**.
- 저장소에 없는 과거 `Saved/Worktrees` 자료 링크 **12개**는 정리 전부터 존재한 로컬 증거 참조로 유지한다.
- `git diff --check`: 공백 오류 없음. 문서 목록 생성기를 반복 실행해 같은 결과가 나오는지도 확인한다.

이 검증은 문서 보존·탐색의 검증이다. 과거 빌드·성능·PIE 결과를 이번 작업에서 재실행하거나 현재 버전으로 승격한 것이 아니다. 원문별 검증 날짜·조건·한계는 그대로 읽는다.

[문서 관리 규칙](README.md) · [전체 목록](Document_Catalog.md) · [경로 변경과 원문 해시](Document_Moves_2026-10-03.json) · [보관 자료](../Archive/README.md)
