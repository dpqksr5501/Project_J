# Project J 문서 시작점

프로젝트 이해 → 작업할 주제 → 문서 종류 순서로 찾는다. 현재 설정은 기준·제작 문서에서, 변경 이유와 검증 근거는 보고서·진단 기록에서 확인한다.

## 바로 시작하기

| 하려는 일 | 시작 문서 |
| --- | --- |
| 프로젝트와 코드 책임 이해 | [프로젝트 개요](Overview/ProjectOverview.md) → [작업 문맥](Overview/ProjectContext.md) |
| 이동·리타깃·손 접촉 설정 | [애니메이션](Animation/README.md) |
| 공격·콤보·전투 VFX 제작 | [전투](Combat/README.md) |
| 직업·스킬·장비·탈것 확장 | [게임플레이](Gameplay/README.md) |
| 런타임 책임과 MMO 확장 기반 확인 | [아키텍처](Architecture/README.md) |
| 최근 런타임 결함 재현과 전체 회귀 결과 확인 | [2026-10-08 수명·소유권 감사](Review/Audits/Project_J_Runtime_Ownership_Audit_2026-10-08.md) |
| 복제 구조와 원격 동작 검증 | [네트워크](Networking/README.md) |
| 성능 수집·측정·최적화 판단 | [성능](Performance/README.md) → [벤치마크](Benchmarks/README.md) |
| 감사 요청과 후속 결과 확인 | [검토 기록](Review/README.md) |
| 이전 작업 이어받기 | [작업 인계](Handoffs/README.md) |
| 원본 메모와 이전 안내 찾기 | [참고](Reference/README.md) · [보관 자료](Archive/README.md) |

## 문서 종류

| 종류 | 읽는 방법 |
| --- | --- |
| Architecture / 기준·설계 | 책임·계약·데이터 흐름. 구현과 예정 범위는 본문에서 구분 |
| Authoring / 제작 | DA·ABP·소켓·에디터 연결과 콘텐츠 추가 절차 |
| Diagnostics / 진단 | 재현 조건·로그·수정 이유·측정 한계 |
| CaptureGuides / 수집 | 로그·Insights·네트워크 자료를 남기는 절차 |
| Planning / 계획 | 제안·보류·검증 게이트. 완료 판단은 결과 문서와 코드에서 확인 |
| Reports / Baselines / Validation | 해당 시점·조건의 구현 결과와 검증 근거 |
| Review / Handoffs / Archive | 요청·검토·인계·이전 안내를 보존한 기록 |

폴더 위치나 파일 날짜만으로 구현 완료를 판단하지 않는다. 테스트 개수·성능 수치는 각 실행 조건과 함께 읽으며 서로 다른 단계의 수치를 합산하지 않는다.

[전체 문서 목록](Maintenance/Document_Catalog.md) · [문서 작성·보존 규칙](Maintenance/README.md) · [2026-10-03 경로 변경표](Maintenance/Document_Moves_2026-10-03.json)
