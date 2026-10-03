# 이동 애니메이션

## 기준과 확장

- [GASP 대응표](GASP_ProjectJ_Locomotion_Parity.md): 검토한 GASP 경로와 Project J의 적용·보류 범위.
- [원격 원샷 복제](RemoteOneShotReplication.md): Start/Stop/Land의 희소 이벤트와 로컬 연속 이동 구분.
- [Motion Matching 구조와 후속 항목](../Architecture/MotionMatchingNextSteps.md).
- [이동 콘텐츠 확장](../Authoring/DataDrivenLocomotionExtensionGuide.md).

## 구현·문제 해결 기록

| 기록 | 용도 |
| --- | --- |
| [리팩터링 상세](Reports/MotionMatching_Locomotion_Refactor.md) | 구현 계약과 당시 에디터 연결·후속 순서 |
| [2026-09-19 보완](Reports/Locomotion_Refinement_2026-09-19.md) | 후속 이동 구조 보완 |
| [2026-08-24 Pivot 정정](Reports/GASP_Pivot_Architecture_Correction_2026-08-24.md) | regular MM과 Experimental State Machine, 태그·비활성 경로 구분 |
| [2026-08-06 OTM·OffsetRootBone 문제](Reports/Locomotion_OTM_OffsetRootBone_BugReport_2026-08-06.md) | 원샷·회전·착지 문제와 당시 수정 |
| [2026-08-05 Moving Reorientation](Reports/MovingReorientation_TIP_WorkLog_2026-08-05.md) | 방향 재정렬 알고리즘과 작업 기록 |
| [2026-08-04 BranchIn TIP 인계](Reports/GASP_BranchIn_TIP_Handoff_2026-08-04.md) | 당시 연결과 인계 |
| [2026-08-01 구현 요약](Reports/MotionMatching_Locomotion_Implementation_Summary_20260801.md) · [인계](Reports/MotionMatching_StateController_Handoff_2026-08-01.md) | 초기 구현 배경과 당시 남은 작업 |

후속 TIP 진단은 [애니메이션 진단](../Diagnostics/README.md), 전투 Strafe 구현은 [전투 기록](../../Combat/README.md)에서 이어 읽는다.
