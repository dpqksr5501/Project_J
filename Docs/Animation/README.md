# 애니메이션

[R20 대각 점프·착지 복구](../Review/Audits/Project_J_Strafe_Diagonal_Warp_Repair_2026-10-15.md): authored 보정 selector가 첫 Update의 lazy Input.Player 연결을 선행 요구하던 순환 의존성을 해소했습니다. 제자리 회전·OTM 정책/에셋 보존, 호환18개·대각6시나리오·Editor/Game 빌드 결과와 한계를 기록했습니다.

[R19 소스·데이터 대조](../Review/Audits/Project_J_PC_Source_Data_Reconciliation_2026-10-15.md)는 Chooser22개 package/내부 포함28개·DT1·입력 연결을 대조하고 Stand 착지의 대각 선택 누락을 수정했다. native 방향 상태를 보존하며 authored alias 조건에서 해결했고, 논리 회귀·Cook과 실제 포즈/원격 경계를 구분한다.

현재 후속 구조·수명·실제 데이터 조사는 [심화 감사](../Review/Audits/Project_J_Deep_Structure_Audit_2026-10-13.md)와 [시스템 지도](../Review/Audits/Project_J_Deep_Structure_Map_2026-10-13.md)에 기록한다. DS01–18의 실제 변경·계약 검토·미검증을 구분한다. 최종 전체 회귀 332개·Game/Cook과 기록한 통합 검증을 수행했으며, CPU 일부 증가와 packaged RHI trace 종료 실패는 남아 있다.

[구조 개선](../Review/Audits/Project_J_Structural_Improvement_2026-10-13.md)은 전투·탑승 linked layer의 리소스 수명을 공통화하고 native/DA 발 배치 기본값의 중복 초기화를 제거했다. 레이어 선택 정책·GT/worker 경계·authored 값은 유지한다.

[2026-10-13 전체 감사](../Review/Audits/Project_J_Whole_Project_Audit_2026-10-13.md)는 사라진 수요 소유자의 예산 복원, Family 제작 검증, 현재 통합 그래프의 재생 관측과 200명 교대 CPU/렌더 검증을 다룬다. 이전 단일 실행의 증가가 해결됐다고 가정하지 않는다.

## 현재 플레이어 파이프라인

공용 소스 포즈에서 이동 Motion Matching과 전투 상체·몽타주를 합성한다. 임포트 몸체는 런타임 리타깃 뒤 몸체 비율에 맞는 Guided Hand IK를 적용하고 의상 물리를 평가한다. 무기 구동과 손 접촉의 책임은 별도로 설정한다.

2026-10-12 기준 Master는 `Project Unified Locomotion`에서 단발과 MM을 같은 재생 스택으로 실행한다. 네이티브 State Controller·Chooser는 계속 사용하며, 이전 ABP State Machine·외부 Blend Stack·Handoff 경로와 미사용 함수 그래프 9개는 제거했다. Inertialization·Lean과 플레이어별 보정·전투·접지 후단은 유지한다. 실제 이동 증거로 지속 곡선을 판정하며, 리타깃 몸체와 의상 물리는 보호 조건을 포함한 시각 예산으로 함께 관리한다.

| 목적 | 기준 문서 |
| --- | --- |
| 골격·몸체 프로필 공유, 실제 리타깃/손 접촉 연결과 호환·수명 검증 | [실시간 리타깃 프로필](Architecture/Runtime_Retarget_Profiles_2026-10-13.md) |
| 스냅샷·표시·검색·복귀·품질 예산의 경계와 동작 보존 검증 | [파이프라인 리팩토링](Architecture/Animation_Pipeline_Refactor_2026-10-13.md) |
| 현재 단발·MM의 공통 재생 스택과 ABP 노드 역할 | [통합 Locomotion 구현](Planning/Unified_Locomotion_Implementation_2026-10-12.md) |
| 느린 OTM/Strafe 회전·미세 입력·State Controller·미사용 함수 정리 | [지속 곡선 판정과 Master 정리](Planning/Sustained_Curve_And_Master_Cleanup_2026-10-12.md) |
| 좌우 곡선 반전의 후보 유지·전방 입력 묶음의 예측 안정화 | [사용자 로그 후속 구현](Planning/Sustained_Curve_And_Master_Cleanup_2026-10-12.md#사용자-로그-후속-곡선-반전과-전방-예측-안정화) |
| W 미세 회전의 Loop·곡선 공동 후보·정규화 참조 보정·실제 로그 재생 | [전방 Strafe Run 후속](Planning/Sustained_Curve_And_Master_Cleanup_2026-10-12.md#전방-strafe-run-미세-회전-후속-구현) |
| W 곡선의 전방 후보 구성과 A/D 측면 후보 보호 | [전방 이동 의도별 PSD 구성](Planning/Strafe_Forward_Intent_2026-10-13.md) |
| 착지 후 약한 회전의 후보 소유권·후속 Arc/Loop의 보간 일관성·숙인 상체의 yaw 오해 방지 | [Strafe Land 복귀 보완](Planning/Strafe_Land_Return_2026-10-13.md) |
| 200명 리타깃·의상 물리 예산·보간·병렬 경계·off/on 결과 | [시각 애니메이션 예산 구현과 검증](Planning/Crowd_Visual_Presentation_Budget_2026-10-12.md) |
| 실제 그래프 기반 GASP 비교·자연스러움 고도화·후속 프롬프트·PDF | [2026-10-06 조사와 고도화 인계](Planning/GASP_ProjectJ_Naturalness_2026-10-06/README.md) |
| 이동 상태·GASP 대응 확인 | [Locomotion](Locomotion/README.md) |
| Motion Matching 실행·원격·예산 정책 | [Motion Matching 구조와 후속 항목](Architecture/MotionMatchingNextSteps.md) |
| 이전 외부 Blend Stack 복귀·현재에도 사용하는 검색 예산의 보완 이력 | [복귀 검색과 군중 예산 보완](Architecture/MotionMatching_Return_Crowd_2026-10-05.md) |
| 통합 이전의 외부 단발→MM 크로스페이드·관성 복귀 비교 이력 | [Live Return 구조와 당시 검증](Architecture/OneShot_Live_Return_2026-10-11.md) |
| Start·Land 작은 입력 유지·지속 곡선의 Cycle 복귀·첫 MM 후보 전달 | [단발 입력 반응과 첫 검색](Architecture/OneShot_Input_Response_2026-10-11.md) |
| Strafe A/S/D 카메라 회전의 예측 흔들림·재선택 비교 | [Strafe 예측 안정화](Architecture/Strafe_Prediction_Stability_2026-10-11.md) |
| 전방 Strafe Arc의 실제 몸통 방향·원본 자세 비교 | [전방 몸통 진단](Architecture/Strafe_Forward_Facing_Diagnostics_2026-10-11.md) |
| Master 공통 상체 방향·프로필 뼈 역할·LOD·보호 포즈 검증 | [Strafe 상체 방향 보정](Architecture/Strafe_Upper_Body_Facing_2026-10-11.md) |
| 전방 Strafe Run의 골반 방향·기존 발 접지 제약·Turn/몽타주 제외 | [Strafe 하체 방향 보정](Architecture/Strafe_Lower_Body_Facing_2026-10-11.md) |
| Strafe 네 방향 Pivot·입력 취소 | [Pivot 이동 입력 기준 통일](Architecture/Strafe_Pivot_Input_Basis_2026-10-05.md) |
| 연속 Pivot·OTM/Strafe 중복 Start/Land 방지 | [일회성 동작의 요청 수명](Architecture/OneShot_Command_Lifetime_2026-10-05.md) |
| Start·착지 중 입력 변경·MM 복귀의 실행 흐름 | [기본 비활성 흐름 진단](Architecture/Start_Land_Input_Flow_Trace_2026-10-05.md) |
| 이동 착지 취소의 Idle 경유·연결 레이어 검색 완료 수정 | [착지 복귀와 연결 레이어 검색](Architecture/Landing_Return_Linked_Search_2026-10-05.md) |
| 실제 BP 군중 CPU·소켓 검증과 동시 공격 비용 | [실제 캐릭터 애니메이션 검증](Architecture/Authored_Animation_Crowd_Network_2026-10-05.md) |
| OTM·Strafe 이동 중 180도 Turn의 조건·요청 수명·PSD 연결 | [이동 중 180도 Turn](Architecture/Moving_Turn_180_2026-10-06.md) |
| 리타깃 파이프라인과 배경 | [런타임 리타깃 구조](Architecture/Runtime_Retarget_HandIK_Architecture.md) |
| 무기 부착·파지·궤적·공격 종료 복귀 | [무기 파지와 손 접촉 통합 가이드](Authoring/Weapon_Hand_Contact_System.md) |
| 외형·의상 리더 선택 | [외형 메시 소유권](Architecture/Visual_Presentation_Mesh_Ownership.md) |
| 발소리·지형 매핑·클라이언트 군중 오디오 기반 | [캐릭터 Foley 시스템](Architecture/Foley_Audio_System.md) |
| 실제 GASP 노티파이·오디오 뱅크·재생 경로 조사 | [GASP Foley 조사](Diagnostics/GASP_Foley_Investigation_2026-10-08.md) |
| GASP BP 노티파이의 native 교체·설정 보존·검증 | [Foley 노티파이 교체](Diagnostics/Foley_Notify_Migration_2026-10-08.md) |
| 이벤트 대체·발/손/소켓 정책·초기 마네킹 음원 연결 | [Foley 확장과 연결](Diagnostics/Foley_Extension_2026-10-08.md) |
| 정면 달리기의 객체 없는 노티파이 복구·실제 MM 재생 검증 | [정면 달리기 Foley 복구](Diagnostics/Foley_Forward_Run_Fix_2026-10-08.md) |

## 에디터 제작과 디버깅

1. [제작 가이드](Authoring/README.md): 몸체 DA, Palm·WeaponGrip 소켓, ABP의 오른팔·왼팔 연결.
2. [진단 기록](Diagnostics/README.md): 파지·팔꿈치·TIP의 로그와 수정 근거.
3. [계획](Planning/README.md): 파이프라인 제안과 스레딩 감사의 후속 범위.

현재 대검의 확인된 구성은 Idle 오른손 파지 → Two-Hand Grip IK 구간 양손 파지 → 왼손 해제다. 다른 무기는 보조 접촉 기능과 기본 가중치로 한손·항상 양손을 선택한다. 세부 설정은 [통합 가이드의 보조 손 절](Authoring/Weapon_Hand_Contact_System.md#secondary)에 모은다.

상체 전투 합성은 [전투](../Combat/README.md), 원격 복제 검증은 [네트워크](../Networking/README.md), 다수 플레이어 비용은 [성능](../Performance/README.md)에서 확인한다.
