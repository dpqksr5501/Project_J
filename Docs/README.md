# Project J 문서 시작점

**PC 잔여 검토:** [Door·BP·DA의 구조 대조와 마감 검증](Review/Audits/Project_J_PC_Asset_Review_Closure_2026-10-15.md). 현재 DA19·GE2와 기존 근거를 연결해 유지 이유를 기록했고 Door·identity 경계를 수정했습니다. 전수 실행 품질·성능 검증과 구분합니다.

**최신 PC 후속:** [소스·BP·DA 정리 결과와 남은 범위](Review/Audits/Project_J_PC_Structure_Cleanup_2026-10-15.md). 실제 중복 스킬 부여·입력 receipt·애니메이션/편집/퇴장 소유권을 정리했습니다. 집중90개+실제 BP1개 통과. 성능·전수 환경 검증과 구분합니다.

**BP·DA 후속 정리:** [실제 에셋 연결·구조 정리·MMORPG 계약](Review/Audits/Project_J_Asset_Architecture_Cleanup_2026-10-15.md). 테스트/제작용 자산은 보존하고, 확인된 상수 상태 override·MM 중복 writer를 기존 native 경로로 통합했습니다. 전수 에셋 속성·환경/성능 검증과 구분합니다.

**현재 우선 작업:** [전체 시스템의 소스·아키텍처 정리](Review/Audits/Project_J_Source_Architecture_Cleanup_2026-10-15.md). 실제 호출/상태 소유권으로 중복·무동작 코드·책임 혼재를 정리하고 영향 기능을 집중 회귀합니다. 성능·운영·품질 문제의 전수 해결은 최신 요청에 따라 보류합니다.

남은 전체 감사·개선과 누락 점검을 다른 채팅에서 이어갈 때는 [2026-10-14 마무리 인계](Handoffs/WholeProjectAudit_2026-10-14/Project_J_Complete_Audit_Continuation_2026-10-14.md)를 먼저 읽는다. 최신 RA27 결과·커밋 기준점·반복 방지·전체 범위 장부·완료 기준을 포함하며 전체 정밀 감사 완료를 주장하지 않는다.

[계속 작업의 현재 변경·명시 범위 장부·원자료와 남은 일](Review/Audits/Project_J_Continuation_Audit_2026-10-14.md)은 인계 이후 추가 수정만 기록한다. 이전 RA27 검증을 새 production 수정본의 검증으로 승격하지 않는다.

[23시 경계의 재개 기준점](Handoffs/WholeProjectAudit_2026-10-14/Project_J_Continuation_Checkpoint_2026-10-14_2300.md)은 CA01–13 latest compiled/전체373·Game/Cook/package·성능 원자료와 미완료를 구분한다. 전체 감사 완료 인계가 아니다.

[2026-10-15 계속 작업](Review/Audits/Project_J_Continuation_Audit_2026-10-15.md)은 진행·클래스·인계·NPC 콜백·표시 수요·입력 상태 CA14–18의 지정 수정과 통합389·Game/Cook/패키지·실제 전투·socket2/NPC200 결과를 기록한다. 전체 source/asset 상세 감사·CPU 전체 증가·렌더 차이는 미완료다. 후속 변경을 이 통합 기준점과 분리한다.

2026-10-14 검증 기준 [전체 구조 심화 감사](Review/Audits/Project_J_Deep_Structure_Audit_2026-10-13.md)와 [시스템 지도](Review/Audits/Project_J_Deep_Structure_Map_2026-10-13.md)에 DS01–18의 실제 변경·검토·미검증을 기록합니다. 상태·자원 소유권, 실제 공격 데이터, 종료/재진입, MCP/native 에셋 대조와 최종 검증·성능 원자료를 연결합니다. CPU 일부 증가와 packaged RHI trace 실패는 남은 문제입니다.

프로젝트 이해 → 작업할 주제 → 문서 종류 순서로 찾는다. 현재 설정은 기준·제작 문서에서, 변경 이유와 검증 근거는 보고서·진단 기록에서 확인한다.

## 바로 시작하기

| 하려는 일 | 시작 문서 |
| --- | --- |
| 프로젝트와 코드 책임 이해 | [프로젝트 개요](Overview/ProjectOverview.md) → [작업 문맥](Overview/ProjectContext.md) |
| 이동·리타깃·손 접촉 설정 | [애니메이션](Animation/README.md) |
| 현재 단발→MM 구조·느린 회전·Master 정리 | [통합 재생](Animation/Planning/Unified_Locomotion_Implementation_2026-10-12.md) → [지속 곡선 판정](Animation/Planning/Sustained_Curve_And_Master_Cleanup_2026-10-12.md) |
| 200명 시각 예산·기존 병렬 구조·성능 비교 | [2026-10-12 구현과 검증](Animation/Planning/Crowd_Visual_Presentation_Budget_2026-10-12.md) |
| 공격·콤보·전투 VFX 제작 | [전투](Combat/README.md) |
| 직업·스킬·장비·탈것 확장 | [게임플레이](Gameplay/README.md) |
| HUD·가방·장비 조작과 UI 아트 교체 | [UI](UI/README.md) |
| 런타임 책임과 MMO 확장 기반 확인 | [아키텍처](Architecture/README.md) |
| 실제 중복 제거·책임 분리·private 미사용 정리 | [2026-10-13 구조 개선](Review/Audits/Project_J_Structural_Improvement_2026-10-13.md) |
| 최근 전체 구조·결함·소유권·성능 감사 | [2026-10-13 전체 감사](Review/Audits/Project_J_Whole_Project_Audit_2026-10-13.md) · [10월 8일 수명 감사](Review/Audits/Project_J_Runtime_Ownership_Audit_2026-10-08.md) |
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
