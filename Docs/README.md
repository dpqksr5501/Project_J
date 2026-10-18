# Project J 문서 시작점

프로젝트 이해 → 작업할 주제 → 문서 종류 순서로 찾는다. 현재 구조·제작 규칙은 담당 기준 문서에, 변경 원인·실행 조건·검증 결과는 날짜별 보고서에 둔다.

## 현재 상태를 읽는 순서

1. [프로젝트 맥락](Overview/ProjectContext.md): 시스템·권위·상태 소유권과 실제 사용 구조.
2. [애니메이션](Animation/README.md): 통합 단발/MM·리타깃·후진 Reface·착지의 현재 읽는 순서. Mirror Data는 도입을 보류한다.
3. [검토 목차](Review/README.md): main 통합·R19/R20·소스/BP/DA 정리와 이전 감사의 구분, 유지·보류 범위.
4. [성능](Performance/README.md): 과거 수치의 조건과 남은 CPU·렌더 차이. 현재 반복 성능 조사·대규모 부하는 보류한다.

과거 인계의 미완료 목록과 Prompt는 현재 작업 지시가 아니다. 이미 구현한 기능을 다시 만들지 않고, 재개 범위는 최신 사용자 요청으로 정한다. 전체 감사 요약을 여러 README에 복제하지 않으며 통합 전 안내는 [보관 자료](Archive/README.md)에 남겼다.

## 바로 시작하기

| 하려는 일 | 시작 문서 |
| --- | --- |
| 프로젝트와 코드 책임 이해 | [프로젝트 개요](Overview/ProjectOverview.md) → [작업 문맥](Overview/ProjectContext.md) |
| 버그 수정·리팩터링 후 잔여 로직·중복 에셋 점검 | [공통 작업 규칙](../AGENTS.md) → [변경 마감·중복과 레거시 관리](Maintenance/Change_Completion_Policy.md) |
| 이동·리타깃·손 접촉 설정 | [애니메이션](Animation/README.md) |
| 현재 단발→MM 구조·느린 회전·Master 정리 | [통합 재생](Animation/Planning/Unified_Locomotion_Implementation_2026-10-12.md) → [지속 곡선 판정](Animation/Planning/Sustained_Curve_And_Master_Cleanup_2026-10-12.md) |
| 200명 시각 예산·기존 병렬 구조·성능 비교 | [2026-10-12 구현과 검증](Animation/Planning/Crowd_Visual_Presentation_Budget_2026-10-12.md) |
| 공격·콤보·전투 VFX 제작 | [전투](Combat/README.md) |
| 직업·스킬·장비·탈것 확장 | [게임플레이](Gameplay/README.md) |
| 시작 권역 8×8km 재제작·MCP 에셋 조사 | [현재 월드 제작](World/README.md) → [최신 중단·인수인계](World/WORLD_HANDOFF_2026-10-18.md) |
| 자유 비행 오픈 월드·강·마을·이전 A 지역 이력 | [레벨 설계와 확장 계획](Gameplay/Planning/Open_World_Level_Design.md) → [조사 자료](Gameplay/Planning/Open_World_Research_References.md) |
| HUD·가방·장비 조작과 UI 아트 교체 | [UI](UI/README.md) |
| 런타임 책임과 MMO 확장 기반 확인 | [아키텍처](Architecture/README.md) |
| 현재 소스·BP·DA 정리와 유지 이유 | [검토 목차의 현재 통합 기준](Review/README.md#현재-통합-기준과-결정) |
| 감사 단계별 변경·재현·검증·보류 | [현재 결정과 단계별 이력](Review/README.md) |
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
