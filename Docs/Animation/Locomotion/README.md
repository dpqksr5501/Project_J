# 이동 애니메이션

## 현재 기준

- [플레이어 로코모션 아키텍처](../Architecture/Player_Locomotion_Architecture.md): Mover·C++ 계산·State Controller/Pose Match/BlendStack·몸체 표현의 책임.
- [이동 콘텐츠 확장](../Authoring/DataDrivenLocomotionExtensionGuide.md): 현재 클립·Chooser·프로필 제작과 검증.
- [스켈레톤·추가 뼈](../Authoring/HumanoidSkeletonRetargetingStandards.md): Manny 공용 모션과 몸체별 연결.
- [전환·검증 기록](../Architecture/Player_Locomotion_Migration_2026-10-19.md): 공중 재선택·IK·전신 몽타주·회귀의 최신 근거.
- [원격 표현 이벤트](RemoteOneShotReplication.md): 입력/이동 상태와 희소 경계 이벤트의 역할.

## 이전 구조와 비교 자료

[상태 선택 대응 조사](GASP_ProjectJ_Locomotion_Parity.md), [이전 공통 재생 스택](../Planning/Unified_Locomotion_Implementation_2026-10-12.md), [지속 곡선 판정](../Planning/Sustained_Curve_And_Master_Cleanup_2026-10-12.md), [이전 MM 계획](../Architecture/MotionMatchingNextSteps.md)은 당시 설계·시도·측정 자료다. 현재 제작에 옛 CMC·퇴역 노드·독립 MM 경로를 복원하지 않는다.

| 기록 | 용도 |
| --- | --- |
| [리팩터링 상세](Reports/MotionMatching_Locomotion_Refactor.md) | 당시 구현·에디터 연결 |
| [2026-09-19 보완](Reports/Locomotion_Refinement_2026-09-19.md) | 이전 이동 구조 보완 |
| [Pivot 구조 조사](Reports/GASP_Pivot_Architecture_Correction_2026-08-24.md) | 샘플 분기·비활성 경로 구분 |
| [OTM·OffsetRootBone 진단](Reports/Locomotion_OTM_OffsetRootBone_BugReport_2026-08-06.md) | 당시 원샷·회전·착지 문제 |
| [이동 방향 재정렬](Reports/MovingReorientation_TIP_WorkLog_2026-08-05.md) | 알고리즘과 작업 기록 |
| [초기 인계](Reports/GASP_BranchIn_TIP_Handoff_2026-08-04.md) · [구현 요약](Reports/MotionMatching_Locomotion_Implementation_Summary_20260801.md) · [상태 선택 인계](Reports/MotionMatching_StateController_Handoff_2026-08-01.md) | 당시 연결과 후속 과제 |

증상별 진단은 [진단 목차](../Diagnostics/README.md), 전투 애니메이션은 [전투 목차](../../Combat/README.md)로 이어진다.
