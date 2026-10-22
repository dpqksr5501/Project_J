# 인간형 스켈레톤과 추가 뼈 제작 기준

2026-10-21 현재 공통 포즈 기준은 `/Game/Characters/Mannequins/Meshes/SK_Mannequin`(UE5 Manny)이다. 공통 마스터는 `/Game/Animation_Logic/ABPs/ABP_Player_Master`, native 부모는 `UProject_JPlayerAnimInstance`다. 이전 `ABP_Humanoid_Master`와 `ABP_ProjectJ_GASP58`는 현행 제작 이름이 아니다.

## 몸체 선택

| 몸체 | 연결 방식 |
| --- | --- |
| 실제 `SK_Mannequin`을 공유하는 UE5 몸체 | Pawn의 source mesh에 몸체를 지정하고 공통 master를 직접 사용한다. 별도 Retarget Pose From Mesh가 필요 없다. |
| 이름·계층·기준 포즈가 검증된 별도 UE5 호환 Skeleton | Compatible Skeletons를 명시하고 master·전투 레이어·소켓·공유 뼈를 검증한다. 이름이 UE5라는 이유만으로 자동 승인하지 않는다. |
| 현재 GreatSword의 Bip01 몸체 | Manny source → `RTG_Greatsword_Manny` → 기존 몸체 접촉·물리. 비표준 골격에는 기존 runtime retarget가 필요하다. |

직접 몸체를 만드는 BP는 기존 비표준 VisualFollower를 제거하고 source mesh를 표시한다. `VisualFollower`/`RetargetFollower` 역할 태그를 장비에 붙이지 않는다. 호환 몸체가 source를 직접 대체할 경우 손·발·무기 소켓, 몸체 비율, LOD와 postprocess를 함께 검수한다.

## Manny 로코모션 라이브러리

UEFN 호환 직접 재생은 Idle에서 상완 벌어짐을 좁게 만들었고, 팔 Translation Retargeting을 AnimationScaled로 바꾸면 Manny 바인드 축에서 벗어나는 translation이 발생했다. 호환 설정만으로 원본 자세와 Manny 관절 배치를 동시에 만족한다고 간주하지 않는다.

공식 `RTG_UEFN_to_UE5_Mannequin`을 기반으로 한 프로젝트 변환기 `/Game/Animation_Logic/RTG_Player_UEFN_to_Manny`와 실제 UEFN/Manny 메시를 사용해 448개 시퀀스를 변환한다. `/Game/Characters/Mannequins/Animations/Locomotion`에는 가져온 라이브러리 427개와 BlendSpace 10개를, `Animations/ProjectJump`에는 기존 점프 21개를 둔다. 제작·비교 참조로 포함된 crouch/traversal 클립이 있어도 해당 플레이 기능을 활성화하지 않는다. 같은 이름의 원본/프로젝트 점프가 충돌하지 않도록 폴더를 나눈다.

프로젝트 변환기는 다리의 `Blend to Source` 가중치를 0으로 두어 발 목표가 Manny로 변환된 FK 체인의 공간을 따르게 한다. 루트가 `GloballyScaled`로 이동하는데 발 목표를 UEFN 절대 좌표로 복사하면 이동 거리가 누적될수록 무릎을 펴는 오차가 생긴다. Full Body IK는 유지한다. 마지막 `Rebuild Manny IK Helpers` Pin Bones 연산은 최종 대상의 `foot_l/r`를 `ik_foot_l/r`로, `hand_l/r`를 `ik_hand_l/r`로, `hand_r`를 `ik_hand_gun`으로 복사한다. IK 보조 뼈를 기준 포즈에 고정한 채 내보내지 않는다. 기존 Speed Planting의 활성 상태는 유지하며, 속도 커브가 없는 경우 해당 연산이 스스로 건너뛰는 엔진 동작을 변경하지 않는다.

원본 UEFN 애니메이션은 삭제하거나 Skeleton을 재할당하지 않는다. 변환본에서 루트 모션 설정, float curve, notify timing/payload, additive type/base/frame과 다른 애니메이션 참조를 보존·검증한다. BlendSpace는 샘플 좌표·보간 설정을 유지하고 샘플 애니메이션만 Manny 변환본으로 연결한다. additive base가 다른 폴더에 있더라도 변환본을 참조해야 한다.

모션을 수정할 때에는 해당 원본을 고치고 같은 Retargeter와 메시 조합으로 다시 변환한다. 변환본에 UEFN FBX를 직접 재임포트하거나 Skeleton만 재할당하면 이 보정을 잃을 수 있다. 새 UE5/Manny 애니메이션은 이 변환 과정을 거치지 않고 사용한다. 변환본 추가 후 production Chooser/PSD/BlendSpace와 notify의 Pose Search Branch In 대상까지 확인한다.

기존 변환본을 갱신할 때 에디터의 일괄 덮어쓰기로 사용 중인 UObject를 강제 삭제하지 않는다. 임시 경로로 변환한 뒤 editor 전용 `UpdateRetargetedAnimationData(Target, Converted)`로 기존 에셋의 뼈 트랙만 갱신한다. 이 API는 Skeleton·프레임 속도·프레임 수·트랙 이름·유한 키를 먼저 검사하며, 에셋 경로와 UObject, 노티파이·sync marker·커브·루트 모션·가산 설정을 유지한다. 타임라인이 다른 새 모션의 교체에는 이 API를 사용하지 않는다. 임시 변환본을 저장했다면 Branch In 노티파이가 PSD에 자동 등록될 수 있으므로, 등록 해제와 실제 참조 확인을 거쳐 정리한다.

지상 Chooser의 `BlendProfile`은 UObject 참조가 아닌 이름 출력이다. 원본 `FastFeet*`를 Manny의 `Player_FastFeet*`로 명시적으로 이관한다. 원래 Manny에 있던 `FastFeet`는 별도 튜닝이므로 덮어쓰지 않는다. editor 전용 `RemapLocomotionBlendProfileNames`는 실제 원본–대상 프로필 쌍을 확인하고 Chooser의 중첩 출력 구조만 수정한다. 재실행 시 변경 0이며 런타임 이름 fallback을 추가하지 않는다.

마스터 BlendStack의 mirror metadata는 `/Game/Animation_Logic/PoseSearch/MDT_Player_Manny`를 사용한다. 대상 Skeleton으로 엔진의 Sync를 수행해 뼈 대응을 생성하며 원본 UEFN 테이블은 보존한다. 현재 production 검색은 unmirrored이고 Schema의 mirror table도 비어 있다. 올바른 테이블 연결만으로 mirrored 후보나 재생을 활성화하지 않는다.

생산 `PSD_Player_IdleBranches`에는 canonical Manny 라이브러리 후보만 둔다. 개발자 비교본을 생산 후보에 추가하면 오래된 변환 포즈와 IK 보조 뼈가 다시 재생될 수 있다. 초기 `MannyCheck` 비교본은 검증 완료 후 백업·퇴역했다. 원본 제작용 PSD에는 원본 UEFN 후보만 유지하고, 과거 변환 중 자동 등록된 Manny 후보와 중복 BranchInId는 남기지 않는다. 새 제작 도구의 기본 Chooser도 현재 프로젝트 Chooser를 사용한다.

지상 Chooser는 `/Game/Animation_Logic/Chooser/CHT_ProjectJ_Ground`, 검색 Schema는 `/Game/Animation_Logic/PoseSearch/PSS_ProjectJ_Manny_*`이다. 검색 포즈와 현재 포즈 모두 Manny 기준으로 계산한다. 공중은 기존 `CHT_ProjectJ_Air`와 native 공중 재선택 정책을 유지한다. 발 Rig는 `/Game/Animation_Logic/Rigs/CR_ProjectJ_Manny_FootPlacement`이다.

발 Control Rig 노드의 `ControlRigAssetReference`와 `DefaultControlRigAssetReference`는 모두 Manny Rig를 가리켜야 한다. UE 5.8의 `SetControlRigClass`는 runtime override API여서 이전 기본값을 보존하고, `OnInitializeAnimInstance`는 기본값이 존재하면 override를 비운다. 그래프에 Manny Rig가 표시된 것만으로 실제 실행을 입증할 수 없다. 프로젝트의 `ConfigureCompatibleFoundation`은 두 직렬화 참조를 함께 이관하며, `MannyFoundation` native 회귀는 그래프와 컴파일된 CDO를, 지상 PIE 회귀는 실제 source mesh 아래 생성된 Rig 클래스를 검사한다. UEFN Rig가 실행되면 Manny 포즈를 UEFN 관절 길이로 풀어 무릎이 펴질 수 있다.

호환 설정은 IK Retargeter의 체인·포즈 보정을 대신하지 않는다. 엔진의 호환 골격 포즈 매핑과 bone retarget mode를 사용하므로 바인드 회전·비율·twist 차이는 실제 포즈에서 확인해야 한다. 팔 굽음이 반드시 해결된다고 단정하지 않는다.

호환 재생 실험에서 바뀐 팔 8개는 이관 전 Manny 정책(clavicle/upperarm/lowerarm=Skeleton, hand=AnimationScaled)으로 되돌린다. 다리나 다른 뼈의 정책은 일괄 변경하지 않는다. `Use Retarget Modes From Compatible Skeleton`은 끈 상태를 유지한다. 팔 벌어짐 보정은 원본에 대한 Skeleton 설정 예외가 아니라 변환된 Manny 모션 자체가 소유한다. UEFN과 Manny의 실험용 호환 등록은 양방향에서 해제했다. 원본 비교는 각 원본 메시/골격으로 평가하고, Manny 재생은 변환본을 사용한다. `ConfigureCompatibleFoundation`도 이전 Skeleton을 자동으로 호환 등록하지 않는다. 새 UE5 호환 몸체는 별도로 검증·명시 등록한다.

원본 뱅크의 현재 [보관·복원·중복 검사](Player_Animation_Library.md)를 먼저 확인한다. 실제 필요한 source rig/mesh는 변환 도구의 역할로 유지하며, Manny 회귀는 원본 뱅크를 다시 로드하지 않는다.

## 몸체별 IK와 같은 Skeleton의 체형 차이

공통 이름이나 호환 등록보다 실제 부모 관계·관절 위치·기준 회전을 먼저 확인한다. 현재 Bip01 대검은 팔 `Bip01-L/R-UpperArm → Forearm → Hand`, 다리 `Bip01-L/R-Thigh → Calf → Foot`의 3관절을 사용한다. Limb Solver의 시작·끝·Goal을 모두 지정하고 실제로 목표에 도달하는지 검사한다. Processor 전체 initialized 또는 체인 이름 일치만으로 솔버 작동을 판정하지 않는다. Bip01 축을 Manny와 같다고 보고 Hinge Z를 일괄 적용하지 않는다. 얼굴·망토·장식 뼈와 실제 손가락 마디도 별도로 구분한다.

`RTG_Greatsword_Manny`의 source 팔 기준 포즈와 손가락 OneToOne 마디 정렬, target rig의 사지 시작점은 현재 보정값을 유지한다. 새 몸체의 bind pose가 다르면 그 몸체의 Family/Body 프로필에서 보정하고 공용 Manny 애니메이션을 몸체별로 다시 변환하지 않는다. [실제 IK 점검과 회귀](../Architecture/Player_Locomotion_Migration_2026-10-19.md#비표준-bip01-ik-매핑과-실제-솔버-점검-2026-10-21)를 따른다.

현재 프로젝트의 `SKM_Quinn_Simple`은 Manny와 동일한 `SK_Mannequin`을 사용하므로 공용 변환 모션을 직접 재생할 수 있다. 6클립·두 시점·두 메시의 포즈 평가 24표본을 확인했다. 하지만 같은 Skeleton에서도 메시 기준 포즈의 길이·체형은 다를 수 있다. 발 Control Rig의 초기 변환/길이와 접지·Stride/LegIK, 무기·손바닥 소켓을 실제 Quinn 플레이에서 검수한다. 현재 Manny Rig의 `Set Initial Transforms From Mesh` 설정을 확인하지 않은 채 새 체형도 자동으로 같은 결과라고 판단하지 않는다. 이번 확인은 Quinn용 Rig 튜닝이나 전체 플레이 외형 승인을 수행한 것이 아니다.

## 망토·의상 추가 뼈

본체 자체에 망토 뼈가 있다면 공통 master로 기본 몸체 포즈를 평가하고, 메시의 authored Post Process ABP에서 AnimDynamics/RigidBody/ControlRig 등 필요한 추가 뼈 처리를 작성한다. 추가 뼈만 있다는 이유로 IK retarget 단계를 추가하지 않는다. 물리 자산·충돌·LOD·시뮬레이션 소유자는 해당 몸체가 가진다.

현재 비표준 Bip01 대검은 별도 예다. 표시 몸체의 runtime retarget ABP 안에서 망토/헤어/치마 RigidBody 세 개를 평가하고 의상은 그 결과를 공유한다. 현행 메시에는 Post Process ABP나 독립 vertex cloth가 없다. 새 UE5 호환 몸체의 위 제작 지침과 현행 대검의 실제 구성을 혼동하지 않는다.

분리 장비는 `UProject_JEquipmentItemDefinition.PoseMode`로 선택한다.

- `LeaderPose`(기본): 본체의 포즈 버퍼를 공유하는 일반 갑옷·장화. 독립 뼈 애니메이션 평가가 필요 없는 파츠다.
- `CopyPose`: 망토처럼 파츠에만 있는 뼈를 독립 평가해야 하는 경우. native `UProject_JCopyPoseAnimInstance`가 공유 뼈 포즈/커브/속성을 복사하고, **장비 메시의 authored Post Process ABP**가 추가 뼈를 처리한다. 기본 포즈를 복사하는 native 경로 자체가 망토 물리를 자동 생성하지는 않는다.

CopyPose는 socket 부착 장비와 함께 선택하지 않는다. 원본과 대상의 명시적 Skeleton 호환, root, 공통 뼈 부모 관계를 검사한다. 새 뼈는 말단 가지로 추가하고 기존 공유 뼈 사이에 새 부모를 끼워 넣지 않는다. 독립 파츠는 source 이후 tick하며 기존 시각 budget coordinator의 수명 주기를 따른다. 별도 Chaos cloth는 기존 독립 cloth 정책을 유지한다. 장비 해제·재등록·LeaderPose 전환 시 tick prerequisite와 포즈 소유권을 정리한다.

## 제작·검수

새 메시를 기존 Skeleton으로 임포트할 때 공통 뼈 계층·축·기준 포즈를 먼저 확인한다. 기존 Skeleton을 무조건 재임포트하거나 모든 인간형을 호환 목록에 추가하지 않는다. 추가 뼈의 새 이름은 기존 뼈를 바꾸지 않아야 한다.

OTM/Strafe의 걷기·질주·빠른 180도 회전, 공중 재선택·착지, 전신 몽타주 하체, 무기 양손 파지와 소켓을 확인한다. 새 몸체의 망토 물리와 외형 품질은 실제 메시를 넣어 별도 검수한다. 자동 테스트는 호환 연결·포즈 계산·소유권을 검사하며 주관적인 팔 모양을 승인하지 않는다.

`Scripts/Validation/Validate-MannyRetargetedKinematics.py`는 실제 압축 포즈에서 448개 변환본의 IK 보조 뼈와 최종 FK 손발을 authored frame 기준으로 비교하고, 대표 걷기·달리기·질주·출발 주기의 원본 대비 무릎 굽힘을 검사한다. 프레임 사이에는 FK 회전과 IK 뼈의 독립적인 local translation 보간이 서로 다른 궤적을 만들 수 있으므로 그 오차를 별도로 기록한다. 선택·그래프·Slot 회귀만으로는 잘못 변환된 다리 포즈를 검출할 수 없다.

실행 기록과 검증 범위는 [전환·검증 기록](../Architecture/Player_Locomotion_Migration_2026-10-19.md)의 Manny 이관 항목을 따른다.
