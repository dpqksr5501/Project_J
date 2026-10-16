# 애니메이션 계획과 구현 이력

현재 동작을 이해하려면 [애니메이션 기준 목차](../README.md)를 먼저 읽는다. 이 폴더는 도입 결정·구현 결과·과거 조사·인계가 함께 있으므로 아래 분류로 찾는다. 제목의 Planning이나 날짜만으로 미구현 여부를 판단하지 않는다.

## 현재 결정

- [200명 전원 표시와 로컬 품질 보존 계획·첫 적용](../../Performance/Planning/ProjectJ_200_Player_Optimization_Plan_2026-10-15.md): 거리 관측 활성화와 로컬 중복 계산 정리, 실제 렌더·원격·판정 검증. 동시 공격 비용·CPU 증가 미해결과 후속 제안을 구분한다.
- [Mirror Data 도입 보류](Mirror_Data_Adoption_Decision_2026-10-15.md): 현재 locomotion을 유지한다. 특정 연결의 발·자세 후보 공백이 확인될 때만 해당 구간을 재검토한다.

## 이미 구현한 기능과 검증 기록

| 기능 | 구현 기록 |
| --- | --- |
| 통합 단발/MM 스택 | [Unified Locomotion](Unified_Locomotion_Implementation_2026-10-12.md). State Controller를 유지하고 player별 보정과 재생을 한 스택으로 연결한다. |
| 지속 곡선·작은 입력·Master 정리 | [지속 곡선 판정과 후속 구현](Sustained_Curve_And_Master_Cleanup_2026-10-12.md). |
| 후진 중 몸 방향 전환 | [GASP 후진 Reface 구현](GASP_Backward_Reface_Implementation_2026-10-12.md). 기존 비교·인계의 후보 제안을 다시 미구현으로 취급하지 않는다. |
| 전방 Strafe 후보와 측면 보호 | [이동 의도별 PSD 구성](Strafe_Forward_Intent_2026-10-13.md). |
| 착지 후 MM 복귀·몸 방향 | [Strafe Land 복귀](Strafe_Land_Return_2026-10-13.md). 후속 선택·보정 수정은 아래 R19/R20을 함께 읽는다. |
| 실시간 리타깃 프로필 | [Family·Body 구조](../Architecture/Runtime_Retarget_Profiles_2026-10-13.md). 골격·몸체 공유, 실제 연결과 수명 검증. |
| 다수 캐릭터의 시각 예산 | [시각 예산 구현](Crowd_Visual_Presentation_Budget_2026-10-12.md). 리타깃·의상 평가·보간과 보호·복구 조건. |
| MM 캐시 재사용 | [런타임 최적화](Animation_Runtime_Optimization_2026-10-13.md). 국소 비용 감소와 전체 성능 문제를 구분한다. |

최신 대각 착지 선택은 [R19](../../Review/Audits/Project_J_PC_Source_Data_Reconciliation_2026-10-15.md), sample 보정 초기화는 [R20](../../Review/Audits/Project_J_Strafe_Diagonal_Warp_Repair_2026-10-15.md)을 따른다. 당시 빌드·자동화·렌더·네트워크·성능 수치는 각 실행 조건 안에서 읽으며 합산하지 않는다.

## 과거 조사와 제안

| 자료 | 읽는 범위 |
| --- | --- |
| [2026-10-06 GASP 자연스러움 조사](GASP_ProjectJ_Naturalness_2026-10-06/README.md) | 실제 그래프·설정·소스 근거와 당시 제안·후속 구현 묶음. 과거 본문과 현재 통합 구조를 구분한다. |
| [2026-10-12 회전 자연스러움 비교](GASP_ProjectJ_Naturalness_Comparison_2026-10-12.md) | 후진 Reface 구현 전 비교. 채택 결과는 위 구현 보고서를 읽는다. |
| [통합 재생 재설계 조사](Locomotion_Unified_Playback_Redesign_Research_2026-10-12.md) | 대안과 도입 조건. 실제 채택 범위는 Unified Locomotion 구현 기록을 따른다. |
| [파이프라인 개선 제안](Humanoid_Animation_Pipeline_Redesign_Proposal.md) | 대안·마이그레이션·당시 요청을 보존한다. |
| [실행·스레딩 감사 계획](ProjectJ_Animation_Execution_Threading_Audit_Plan_2026-09-03.md) | 당시 조사 절차이며 현재 성능 조사 재개 지시가 아니다. |

## 당시 인계와 재현 자료

- [GASP 자연스러움 인계](GASP_Naturalness_Handoff_2026-10-12.md): 후진 Reface 조사 시작 전 요청과 보존 조건. 후속 구현 링크를 먼저 확인한다.
- [200명 최적화 인계](Animation_Optimization_Handoff_2026-10-13.md): 당시 main 기준·구조·품질 계약·CPU 과제·원자료와 재현 명령. 현재 조사 범위는 [검토 목차](../../Review/README.md)의 유지·보류 기준을 따른다.

현재 손 접촉은 [제작 가이드](../Authoring/Weapon_Hand_Contact_System.md)와 [무기 정책](../Architecture/Weapon_Grip_Drive_Policy.md), 실제 진단은 [Diagnostics](../Diagnostics/README.md)에서 읽는다. 과거 Prompt·인계 안의 권한과 작업 요청은 새 작업의 승인이 아니다.
