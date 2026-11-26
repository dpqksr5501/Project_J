# Project J 문서 시작점

프로젝트 이해 → 작업할 주제 → 문서 종류 순서로 찾는다. 현재 구조·제작 규칙은 담당 기준 문서에, 변경 원인·실행 조건·검증 결과는 날짜별 보고서에 둔다.

## 현재 상태를 읽는 순서

1. [프로젝트 맥락](Overview/ProjectContext.md): 시스템·권위·상태 소유권과 실제 사용 구조.
2. [애니메이션](Animation/README.md): Mover·native 상태/핀 계산·공용 포즈 재생·골격/몸체 제작의 현재 기준. 이전 MM/CMC 이력과 mirror 메타데이터를 구분한다.
3. [Phase A~D 독립 감사·마감](Architecture/Planning/Project_J_PhaseAD_Codex_Audit_2026-11-25.md) · [Wave 3·4 및 A-1~A-3 감사 인계](Architecture/Planning/Project_J_Wave3_Wave4_Audit_Handoff_2026-11-25.md) · [독립 기술 감사 후속 현황](Architecture/Planning/Project_J_MMO_Audit_Followup_2026-11-24.md). Phase A~D 서버·백엔드 인프라 구축, 589/589 전체 회귀 마감 및 Windows 실전 운영 검증 결과.
4. [성능](Performance/README.md). 화면에 보이는 군중, 실제 접속, 확정 타격과 서버 틱을 측정 조건별로 확인한다.
5. [오픈월드·MMORPG 통합 인계](Architecture/Planning/Project_J_OpenWorld_Project_Handoff.md). 전체 대화와 조사 자료, Astra 설계, Sol 구현, 독립 검증 결과, 출시 인수 과제를 분야별로 찾는다.

과거 인계의 미완료 목록과 Prompt는 현재 작업 지시가 아니다. 이미 구현한 기능을 다시 만들지 않고, 재개 범위는 최신 사용자 요청으로 정한다. 전체 감사 요약을 여러 README에 복제하지 않으며 통합 전 안내는 [보관 자료](Archive/README.md)에 남겼다.

## 바로 시작하기

| 하려는 일 | 시작 문서 |
| --- | --- |
| 프로젝트와 코드 책임 이해 | [프로젝트 개요](Overview/ProjectOverview.md) → [작업 문맥](Overview/ProjectContext.md) |
| 외부 에셋 출처·라이선스·공개/LFS·향후 교체 확인 | [에셋 현황](../THIRD_PARTY_ASSETS.md) → [정확한 검토 경로](Reference/Asset_Review_Paths_2026-11-01.md) |
| 코드·콘텐츠 개선 기여와 PR 검증 준비 | [기여 안내](../CONTRIBUTING.md) |
| 버그 수정·리팩터링 후 잔여 로직·중복 에셋 점검 | [공통 작업 규칙](../AGENTS.md) → [변경 마감·중복과 레거시 관리](Maintenance/Change_Completion_Policy.md) |
| 이동·리타깃·손 접촉 설정 | [애니메이션](Animation/README.md) |
| 현재 단발→MM 구조·느린 회전·Master 정리 | [통합 재생](Animation/Planning/Unified_Locomotion_Implementation_2026-10-12.md) → [지속 곡선 판정](Animation/Planning/Sustained_Curve_And_Master_Cleanup_2026-10-12.md) |
| 200명 시각 예산·기존 병렬 구조·성능 비교 | [2026-10-12 구현과 검증](Animation/Planning/Crowd_Visual_Presentation_Budget_2026-10-12.md) |
| 측정 원자료 수신·압축 검증·복원 | [Git LFS 검증 자료 보관소](../Archives/Validation/README.md) |
| 공격·콤보·전투 VFX 제작 | [전투](Combat/README.md) |
| 직업·스킬·장비·탈것 확장 | [게임플레이](Gameplay/README.md) |
| 새 지역 제작·메인 맵 교체·기존 8×8km 월드 편집 | [맵 신설·교체 절차](World/World_Map_Authoring_and_Replacement.md) · [기존 월드 제작 자료](World/README.md) |
| 자유 비행 오픈 월드·강·마을·이전 A 지역 이력 | [레벨 설계와 확장 계획](Gameplay/Planning/Open_World_Level_Design.md) → [조사 자료](Gameplay/Planning/Open_World_Research_References.md) |
| HUD·가방·장비 조작과 UI 아트 교체 | [UI](UI/README.md) |
| 런타임 책임과 MMO 확장 기반 확인 | [아키텍처](Architecture/README.md) |
| **새 콘텐츠(보스·던전·탈것·스킬·월드) 제작 & 시스템 연결** | **[콘텐츠 제작 & 시스템 연결 실전 가이드](Architecture/Blueprints/Content_Authoring_Integration_Guide.md)** → [청사진 인덱스](Architecture/Blueprints/README.md) |
| 현재 소스·BP·DA 정리와 유지 이유 | [검토 목차의 현재 통합 기준](Review/README.md#현재-통합-기준과-결정) |
| 감사 단계별 변경·재현·검증·보류 | [검토 기록](Review/README.md) |
| 복제 구조와 원격 동작 검증 | [네트워크](Networking/README.md) |
| 백엔드·관측성 스택 및 Linux DS·Agones 배포 | [로컬 K3s/Agones 및 배포 가이드](../Infra/deploy/agones/k3s-local-setup.md) |
| 성능 수집·측정·최적화 판단 | [성능](Performance/README.md) → [벤치마크](Benchmarks/README.md) |
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
