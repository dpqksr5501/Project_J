# 애니메이션

Project J는 **Mover 이동, C++ 상태·선택 계산, 데이터 기반 포즈 재생, 골격·몸체별 표현**을 분리한다. 플레이어 생산 마스터는 `ABP_Player_Master`, native 부모는 `UProject_JPlayerAnimInstance`, 공통 Skeleton은 Manny `SK_Mannequin`이다.

NPC는 모션매칭을 사용하지 않는다. 현재 `BP_NPC_Test`는 `ABP_NPC_Mover_Unarmed`를 사용하며 native 부모는 공통 `UProject_JCharacterAnimInstance`다. 게임 스레드가 Mover의 속도·방향·공중 상태를 수집하고, 애니메이션 갱신은 게시된 proxy 값만 읽는다. NPC는 APawn/Mover 기반이므로 ACharacter 캐스트나 CharacterMovement의 acceleration/falling 캐시를 재사용하지 않는다. 이동 pose는 실제 속도로 선택하고 simulated proxy에 로컬 입력이 있어야 한다고 가정하지 않는다.

NPC 그래프는 기존 Unarmed의 동작·BlendSpace·전투 슬롯·Control Rig를 보존한 별도 자산이다. 원본 Unarmed와 플레이어 Master는 유지한다. 기존 `AssetMigrationLibrary.CreateNPCMoverUnarmed`는 이 선택한 템플릿을 새 경로에 복제하는 편집기 제작 도구이며 기본 dry-run이다. 다른 골격·보스·탈것 그래프를 일괄 변환하는 도구가 아니다. 새로운 NPC 표현도 같은 native snapshot 계약을 사용할 수 있지만 동작·골격·슬롯·접지·예산은 해당 콘텐츠에서 검증한다. [138차 실제 실행 범위](../Architecture/Planning/Project_J_MMO_Architecture_Work_2026-10-26.md#138차-npc-비모션매칭과-mover-실행-연결)를 따른다.

공유 근접 판정의 기본 경로는 이전 실제 world socket/tip과 현재 finalized pose 사이 직선 구간을 authored hit window에 맞춰 소비한다. 별도 선택인 `AttackDefinition.MeleeTrajectory`는 편집기에서 만든 mesh 공간 gameplay 궤적을 실제 montage 시간과 두 물리 root 관측값으로 world에 매핑한다. 현재 blended/IK pose나 미관측 물리 곡선을 복원하는 기능은 아니다. 두 경로 모두 동일 mesh/anim/instance·frame/bone revision·공간 세대와 이벤트를 확인하고 순간이동·교체·취소 뒤 오래된 점을 연결하지 않는다. NPC의 모션매칭 사용 여부와 독립적인 판정 계약이며, [제작·지원 범위](Authoring/README.md#근접-gameplay-궤적-제작)와 [143차 실제 통합 근거](../Architecture/Planning/Project_J_MMO_Architecture_Work_2026-10-26.md#143차-선택-gameplay-궤적과-npc-데이터-실행)를 따른다.

## 현재 구조와 제작

| 목적 | 기준 문서 |
| --- | --- |
| 이동·스냅샷·상태 선택·ABP 노드의 책임과 현재 에셋 경로 | [플레이어 로코모션 아키텍처](Architecture/Player_Locomotion_Architecture.md) |
| 플레이어 부모/자식 BP·직업/몸체 프로필·의상 연결 | [캐릭터와 ABP 마스터](../Architecture/Runtime/Player_Character_Master.md) |
| 원본 BP/ABP와 native 실행 흐름 대조·의도한 차이·초기화/Iris 경고 해결 | [애니메이션 흐름 최종 점검](Architecture/Player_Animation_Flow_Review_2026-10-21.md) |
| 소스 수명·취소 경계 점검과 뒤대각선 Strafe Start의 입력 순서 문제 | [애니메이션 소스·Strafe 출발 점검](Diagnostics/Animation_Source_Review_2026-10-23.md) |
| 원본 보관·Manny 카탈로그·추가 수입 중복 검사 | [라이브러리 제작](Authoring/Player_Animation_Library.md) |
| 새 이동 클립·Chooser·프로필·직업/탈것 콘텐츠 연결 | [이동 콘텐츠 확장](Authoring/DataDrivenLocomotionExtensionGuide.md) |
| Manny 공용 모션·Quinn·비표준 골격·추가 망토 뼈 | [스켈레톤 제작 기준](Authoring/HumanoidSkeletonRetargetingStandards.md) |
| 몸체별 손 보정·소켓·무기 부착·양손 파지 | [무기 파지와 손 접촉](Authoring/Weapon_Hand_Contact_System.md) |
| Spellblade 이름·장비 이주, 독립 무기 본·스킨과 보존 ID | [Spellblade 제작·이주 기록](Authoring/Spellblade_Migration_2026-11-01.md) |
| 메시·의상·장비의 기존 LOD 보존과 제작 | [LOD 제작](Authoring/Mesh_LOD_Authoring.md) |
| 골격 Family와 몸체 Body 프로필의 역할·수명 | [리타깃 프로필](Architecture/Runtime_Retarget_Profiles_2026-10-13.md) |
| 실제 원인·실패·복구·빌드·회귀·PIE | [로코모션 전환·검증 기록](Architecture/Player_Locomotion_Migration_2026-10-19.md) |

OTM/Strafe, 전방 세 방향 Strafe Sprint, 현재 점프의 공중 방향 재선택·playhead 연속성·수평 관성을 유지한다. 계산·Chooser 호출·핀 입력은 C++가 소유하고 포즈 연결과 노드별 실행 시점은 ABP가 소유한다. 전신 몽타주 하체를 이후 로코모션 IK가 덮지 않도록 합성 순서를 유지한다.

UEFN 원본에서 변환한 Manny 시퀀스 575개·BlendSpace 11개를 관리하며 생산 Chooser/PSD/가산 base에 연결한다. 현재 Run/Sprint·점프/착지는 Neutral, Idle/Walk 기본 동작은 Relaxed다. 원본은 [Content 밖에 별도 보관](../../Archives/AnimationSources/README.md)하고, [수입·중복 판별 카탈로그](Authoring/Player_Animation_Library.md)와 Manny 기반 회귀를 사용한다. UE5 호환 몸체 직접 재생과 현재 Bip01 대검의 실시간 리타깃을 구분한다. 추가 망토 뼈만으로 리타깃이 필수인 것은 아니다. Quinn Simple도 같은 Skeleton에서 직접 재생하지만 체형별 접지·무기 접촉은 별도 확인한다.

## 검증을 읽을 때

2026-10-26 액션 복귀 보강은 Editor 빌드·실제 RHI 회귀59개, 실제22조건 7,895표본의 완료된 snapshot 재분석과 별도 연속 공격2조건 718표본으로 확인했다. Start/Stop/Land뿐 아니라 Idle Break와 늦은 지상 전환 요청을 같은 수명으로 관리한다. [현재 계약](Architecture/Player_Locomotion_Architecture.md#정지-모션과-발-접지) · [원인과 관측 시점·검증 범위](Authoring/Player_Animation_Library.md#액션-복귀의-idle-break와-잔여-예측-보강--2026-10-26)를 따른다.

캐릭터 마스터 승격 후에는 의상 등록·활성화 수명, 부모/자식 조립과 기존 handover·실제 GAS 소유권·몸체 검사를 포함한 76개 회귀가 경고·실패 없이 통과했다. 실제 PIE 조건·의도적으로 유지한 ABP 경계는 [마스터 제작 계약](../Architecture/Runtime/Player_Character_Master.md)을 따른다. 아래 수치는 각 이전 단계의 검증이다.

2026-10-21 후속 최종 점검에서는 Editor/Game Development direct UBT와 native 67개·엔진 문자열 검사 3개, 총 70개 회귀가 경고·실패·미실행 없이 통과했다. 현재 생산 BP/ABP 7개의 연결을 재확인하고 새 네트워크 PIE에서 루프 회전·OTM/Strafe 공중·착지를 검사했다. 초기화 검사 언어 상태는 실행 경계에서 복원하며 Voice는 Iris 연결 전에 로드한다. 아래 61/62개는 이전 단계 결과다. 범위와 호환 처리의 적용 버전·비용은 위 최종 점검 문서를 따른다.

2026-10-21 마지막 IK 수정 기준 native 회귀 61개, 변환본 2,240개 키프레임 표본과 실제 지상·급회전·공중/착지·전신 몽타주 검증이 통과했다. 팔 기준 포즈·손가락 마디 대응·Bip01 limb 시작점의 누락을 각각 원인에 맞춰 수정했다. 연결·실제 solver 작동·포즈·화면은 서로 다른 검증이며 전체 클립 외형·새 군중 성능을 인증한 것으로 확대하지 않는다.

원본 보관·퇴역 이후 별도 native 회귀 62개가 경고·실패 없이 통과했다. 생산 모션/그래프/리그 해시와 원본 없는 로드를 확인했고, 실제 입력 재실행 여부는 [보관·검증 결과](../Maintenance/Animation_Source_Archive_2026-10-21.md)에서 구분한다.

현재 실행기는 `Scripts/Validation/Run-PlayerLocomotionRegression.ps1`, 압축 포즈 검사는 `Validate-MannyRetargetedKinematics.py`다. 새 실행명과 정상 종료·프로세스 중복 방지 규칙을 지킨다. 상세 조건과 로컬 원자료 위치는 전환 기록을 따른다.

## 관련 시스템과 이전 기록

- [제작 목차](Authoring/README.md) · [이동 목차](Locomotion/README.md) · [진단](Diagnostics/README.md)
- [전투](../Combat/README.md) · [네트워크](../Networking/README.md) · [성능](../Performance/README.md)
- [계획·구현 이력](Planning/README.md) · [감사·후속 결과](../Review/README.md)

이전 `ABP_Humanoid_Master`, CMC 전용 TIP·독립 MM·단발 스택과 R19/R20 제작 안내는 당시의 구현·비교 자료다. 현재 마스터에 옛 보정 분기를 다시 추가하지 않는다. Mirror Data의 이전 독자 계층 도입 보류와 현재 Manny mirror metadata를 구분하며, 현재 production 검색은 unmirrored다.

200명 시각 예산·몸체 LOD·의상 물리의 이전 측정은 [성능 계획](../Performance/Planning/ProjectJ_200_Player_Optimization_Plan_2026-10-15.md)에서 조건과 한계를 확인한다. 새 Mover 구조의 성능으로 환산하지 않는다. 외부 기반과 프로젝트 구현의 범위는 현재 아키텍처 문서에 명시한다.
