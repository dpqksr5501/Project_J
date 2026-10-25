# 플레이어 캐릭터와 애니메이션 마스터

## 현재 제작 구조

```text
AProject_JMoverPawn → AProject_JBaseCharacter → AProject_JPlayerCharacter
  └─ /Game/Character_BPs/BP_Player_Master
       └─ /Game/Character_BPs/GreatSword/BP_GreatSword

UProject_JPlayerAnimInstance
  └─ /Game/Animation_Logic/ABPs/ABP_Player_Master
       ├─ 기존 전투 Linked Anim Layer
       └─ 기존 탑승 Linked Anim Layer
```

캐릭터와 ABP의 상속 트리를 일대일로 복제하지 않는다. 플레이어의 공통 컴포넌트와 기본값은 `BP_Player_Master`, 공통 포즈 흐름은 `ABP_Player_Master`가 제공한다. 직업·장비·몸체 차이는 기존 프로필과 레이어로 공급한다. 사람형 NPC는 필요한 애니메이션 계약을 공유할 수 있지만 플레이어 입력·카메라·PlayerState 소유권까지 상속하지 않는다. 동물·탈것을 이 플레이어 부모 아래로 이동하지 않는다.

## 책임과 데이터

| 위치 | 책임 |
| --- | --- |
| native Player/기존 컴포넌트 | Mover 입력, GAS 연결·권위, 장비·표현 수명, 입력 바인딩, 시각 예산 |
| `BP_Player_Master` | 공통 Manny source/마스터 ABP, 입력 자원, 공통 캐릭터 애니메이션 프로필과 컴포넌트 기본값 |
| `BP_GreatSword` | 직업 정의, 대검 시작 장비, 직업 입력 매핑, 몸체별 source와 의상 구성 |
| CharacterAnimProfile | 이동·전투·Foley·몸체 보정 프로필의 제작 진입점 |
| native AnimInstance/Proxy | GT 수집과 worker 데이터, 선택·전환·핀 계산 |
| 마스터 ABP/Linked Layer | 포즈 연결·노드별 콜백 시점·슬롯·직업/탑승 표현 |

테스트용 `BP_Player`를 `BP_Player_Master`로 승격했다. 테스트에 남아 있던 Avatar 로컬 AttributeSet, 기본 능력·속성 데이터는 native 기본값으로 정리했다. 플레이어의 지속 상태와 능력 부여는 기존 PlayerState/직업 계약이 담당한다. `CharacterAnimProfile`과 동일한 직접 `LocomotionProfile` 값도 제거하여 프로필을 통해 해석한다. native fallback 자체는 다른 제작·테스트 소비자가 있으므로 유지한다.

대검은 기존 BP 경로와 에셋 정체성을 유지한 채 부모를 변경했다. GameMode·레벨·기존 제작 데이터가 새 복사본을 잘못 가리키는 문제를 피한다. Content 안에 레거시 캐릭터 복사본을 추가하지 않았고 변경 전 네 에셋은 `Saved/Validation/PlayerMaster_20261021/Backup`에 보관했다. 원본은 Git 이력에서도 복구할 수 있다. `AProject_JGreatswordCharacter`는 동작 없는 native fixture 타입으로 기존 C++ 테스트와 파생 검증 타입이 소비하므로 보존하며, 생산 BP의 부모는 아니다.

## 의상 포즈 연결

기존 `UProject_JModularMeshComponent`의 `bFollowAttachParentPose`를 켠 authored 파츠는 등록 시 skeletal attach parent를 LeaderPose로 사용한다. `codex/greatsword-ue5-rig-test`의 대검은 `CharacterMesh0`에 `/Game/GreatSword/Meshes/SK_GreatSword_Complete`를 직접 설치한다. 공통 마스터와 몸체 Post Process가 만든 318본 최종 포즈를 Body·Head·의상 8개가 공유한다. 별도의 Manny 메시와 `GreatSwordPose` 컴포넌트는 현재 BP에서 사용하지 않는다. 등록 해제·재등록·부모 교체·분리에서도 이전 LeaderPose를 해제하고 현재 부모를 사용한다. BP Construction Script의 연결 호출을 다시 추가하지 않는다. 공유 파츠의 bone pose 평가는 끄며, 일반 파츠의 component Tick도 끈다. **독립 Chaos Cloth가 연결된 파츠는 예외적으로 component Tick이 필요하다.** 활성 cloth 메타데이터/리소스로 판단하고 전용 파츠 이름이나 두 번째 예산 시스템을 만들지 않는다. Dedicated Server에서는 cosmetic cloth Tick을 켜지 않는다.

공통 본만 따라가는 파츠에는 LeaderPose를 사용한다. 파츠 자체의 추가 뼈를 독립적으로 평가해야 하면 자동 LeaderPose를 끄고 기존 `AttachAndCopyPose`와 해당 Post Process를 사용한다. `AttachAndSetLeader`로 생성한 장비도 동일한 수명 정책을 사용한다. 같은 source에 다시 연결할 때 강제 갱신하지 않아 물리 상태 재생성을 피한다. 장비의 mesh/pose source를 설치한 **뒤에** 등록하여 cloth/Post Process가 빈 source로 시작하지 않게 한다. 슬롯 교체는 기존 visual revision·lease 취소/해제 계약을 유지하고 표시 몸체의 AnimInstance나 전체 물리를 초기화하지 않는다. LeaderPose만으로 draw call이나 머티리얼 비용까지 제거되지는 않는다.

### 대검의 실제 2차 물리 소유권

2026-10-24의 렌더링 시험에서는 망토를 제외한 의상7개만 스켈레탈 Nanite로 저장했다. 몸체는 눈 장식의 투명 재질 때문에 fallback하여 원본으로 복원했고, Manny source·몸체 LOD0–2·LeaderPose·망토 Cloth는 유지한다. 실제 Nanite 렌더 객체와 비용 비교는 [스켈레탈 Nanite 보고서](../../Performance/Reports/ProjectJ_Character_Nanite_2026-10-24.md)를 따른다. 포즈 평가 소유권과 렌더 경로를 같은 기능으로 취급하지 않는다.

두 제작 경로는 몸체 구성에서 명시적으로 선택한다. 본 개수나 클래스 이름으로 실행 중 추정 전환하지 않는다.

기존 `UProject_JRetargetAnimInstance`는 두 경로의 손 접촉·품질 snapshot 컨트롤러로 재사용한다. 이름만으로 리타깃 실행을 뜻하지 않는다. 실제 리타깃 평가는 follower 그래프의 `RetargetPoseFromMesh`가 소유하며, 직접 몸체의 InputPose 그래프에는 이 노드나 retarget processor가 없다. 이름을 바꾸기 위한 새 런타임 컨트롤러·중복 상태는 만들지 않는다.

| 경로 | 포즈 생산자 | 몸체 최종 처리 | 의상 소비 |
| --- | --- | --- | --- |
| 검증된 UE5 호환 몸체 | 실제 몸체 + 공통 `ABP_Player_Master` | 추가 처리가 필요할 때만 `InputPose` 기반 몸체 Post Process | 최종 몸체의 LeaderPose/필요한 파츠만 CopyPose |
| 호환 계약을 맞출 수 없는 몸체 | canonical source + 공통 마스터 | 명시적 Family/Body의 retarget follower | 최종 follower의 LeaderPose/필요한 파츠만 CopyPose |

새 몸체를 연결할 때 source와 follower 양쪽에 같은 손·물리 그래프를 중복 설치하지 않는다. follower 그래프가 이 처리를 소유하면 해당 컴포넌트의 중복 Post Process는 비활성화한다. 현재 비호환 Bip01 경로의 대상 메시에는 직접 재생용 Post Process를 설치하지 않는다. Complete의 Post Process는 직접 재생용이며, 보존한 UE5 retarget 실험 그래프를 같은 메시에서 재현할 때는 컴포넌트의 Disable Post Process Blueprint를 명시적으로 켜야 한다.

현재 대검은 첫 경로다. `ABP_GreatSword_Body`는 InputPose → 선택적 밑창 보정 → 필요한 입력 marker → 손목 목표 전달 → 일반 몸체 접촉 IK → 양손 GuidedHandIK → 헤어·치마 **RigidBody 두 개**를 평가하며 Retarget Pose From Mesh가 없다. Complete 메시가 이 Post Process 하나만 사용하고 CharacterAnimProfile의 RetargetBodyProfile은 None이다. 얼굴·헤어·치마·망토 등 추가 본은 유지한다. 이전 `ABP_GreatSword_Secondary`는 제작·비교 사본, `ABP_GreatSword_Retarget`와 Family/Body는 두 번째 경로의 제작·회귀 자산으로 보존한다. 공통 `Project_JRetargetAnimInstance`는 기존 이름을 유지하는 손 접촉/worker snapshot 컨트롤러이며, 실제 리타깃 유무는 그래프가 소유한다.

직접 재생 시 독립 무기 궤적을 보정된 손에서 다시 읽지 않는다. 현재 GreatSword Post Process의 입력에서는 명시적 wrist-reference 계약으로 CopyBone 다섯 개가 FK 손발을 비변형 IK helper로 복사한다. 새 몸체에 항상 다섯 복사를 요구하는 규칙이 아니며 authored 손발/무기 목표를 보존할 몸체에는 같은 제작기를 일괄 적용하지 않는다. 공통 마스터에는 이 몸체용 writer를 넣지 않으며, 기존 BlendStack/래그돌 경로의 발 CopyBone 네 개는 보존하지만 이번 조회에서 translation/rotation/scale 복사가 꺼진 placeholder였으며 활성 writer 네 개로 해석하지 않는다. Complete의 source Drawn 소켓은 `ik_hand_gun` 아래에 원래 reference-world transform을 보존해 연결한다. 무기를 손에 장착한 상태의 소유자는 실제 손이며, 독립 무기 동작/복귀에서는 보정 전 helper가 소유한다. Presentation이 제공한 상대 grip을 GT에서 Palm→Wrist로 한 번 변환하고 GuidedHandIK가 같은 평가의 입력 본으로 해결한다. 자기 팔 아래의 목표는 거절하며 해제·재초기화·대상 소실에서 snapshot을 비운다. follower는 이 직접 재생용 상대 프레임을 사용하지 않고 기존 component-space 계약을 유지한다. Basic 무기·몽타주·노티파이·공격 권위는 공통 소비자를 사용한다.

두 경로의 기존 RigidBody는 동일한 `bEnableSecondaryMotion` snapshot을 사용한다. 서버에서는 source pose tick을 보존하면서 cosmetic solve를 끄고, 클라이언트는 기존 품질 정책을 따른다. 별도의 예산 관리자나 장비 상태를 만들지 않는다.

직접 재생의 `HandContactGoals`는 현재 입력 포즈의 손과 독립 기준 본에서 손목 목표를 계산하고 기존 손 가중치를 한 번 적용한다. 손 부착 모드의 보조손은 전달된 주손 목표를 따라간다. 목표를 비변형 `ik_hand_r/l`에 전달한 뒤 기존 `IK_GreatSword_Contact`의 일반 전신 IK가 몸통·쇄골도 참여시킨다. 발은 들어온 위치·회전에 고정한다. 이후 두 GuidedHandIK가 같은 목표로 손목을 정밀하게 맞춘다. 이 두 단계는 이미 가중된 목표를 사용하므로 가중치를 다시 곱하지 않는다. `bHasActiveHandContact`는 기존 두 손 가중치에서 유도한 실행 조건이며 별도 블렌드 시계가 아니다. 목표 소켓은 해당 비변형 본을 LOD 평가에 유지한다.

무기 표현의 애니메이션 목표는 엔진 프레임당 한 번 값으로 전달한다. 소비 뒤의 모션 노티파이는 다음 평가의 의도를 갱신하지만 늦은 Tick이 이미 소비된 무기 변환을 다시 바꾸지 않는다. 복귀도 같은 경계를 사용한다. 장착·복귀·진단은 `FindBodyContactController`로 main/follower 또는 source Post Process의 실제 컨트롤러를 찾아 같은 보정 프로필·가중치를 읽는다. 장비 제거는 전달된 값과 프레임을 함께 비운다.

`IK_GreatSword_Contact`는 몸통·쇄골을 포함한 일반 무기 접촉 리그다. 발의 위치·회전 stiffness 1은 들어온 포즈를 보존한다. 직접 재생은 입력 포즈의 손목 목표를 사용하며, 보존한 UE5 retarget 실험 그래프도 같은 리그 정의를 재사용한다. `InstallDirectBodyContactIK`는 직접 재생의 목표 전달·접촉·팔 정밀 보정을 연결하고, `InstallHandContactBodyIK`는 기존 retarget 그래프의 제작 도구다. `InstallBodyPoseMarkers`는 직접 몸체의 입력 helper, `BindBodySecondaryMotion`은 몸체 물리 노드의 품질 연결을 담당한다. 도구들은 PIE·중복/충돌 writer를 preflight하고 자동 저장하지 않는다.

새 물리 사본 `PA_GreatSword_Hair`, `PA_GreatSword_Skirt`, `PA_GreatSword_ClothCollision`은 새 메시와 함께 재배치된 legacy 가지의 bone-local 형상·관절 frame을 유지하고 이름을 매핑한다. 초기 USkeleton ref 값에서 추정했던 -90도 mesh basis는 실제 SkeletalMesh bind pose와 불일치한 근거였고 후속 비교에서 폐기했다. `RebaseCompatiblePhysics`의 bone-local 보존 모드는 identity mesh basis만 허용한다. 질량·감쇠·충돌 제외·관절 제한은 유지한다. 손 보정 프로필은 표준 `upperarm → lowerarm → hand`와 새 legacy 손 기준에서 환산한 PalmGrip을 사용한다. Bip01 보존 가지는 표준 관절 아래에 있으므로 기존 Bip01 세 본을 표준 IK 체인으로 재사용하지 않는다.

새 메시의 실제 pelvis 높이는 110.17cm 대 Manny 95.90cm, foot_l 높이는 15.95cm 대 8.24cm다. 초기 Skeleton 값 107.24cm를 실제 mesh bind로 간주하지 않는다. 발목→발볼 기울기도 약 35.5도 달라 실제 접지를 검증한다. 공통 발 Control Rig의 `bSetRefPoseFromSkeleton`(Set Initial Transforms From Mesh)을 켜 실제 owner mesh의 기준 포즈/사지 길이로 초기화한다. 체형 차이만으로 실시간 리타깃을 필수로 판정하지 않는다. source/의상 컴포넌트 높이로 오차를 가리지 않는다.

리타깃 경로의 숨은 producer는 같은 actor의 등록된 LeaderPose 소비자가 보이는 동안 평가를 유지한다. 기존 `AnimationUpdateCoordinator`에서 현재 LeaderPose/메시/가시성을 확인하며 소비자 숨김·해제·leader 교체, authored tick-disabled 및 group의 Hidden 정책을 존중한다. 직접 재생의 숨은 source도 기존 gameplay pose/시각 소비 계약을 사용한다. 별도 timer나 두 번째 예산 시스템을 추가하지 않는다.

망토는 `Cloak`의 독립 Chaos Cloth가 표면을 계산한다. 새 망토에는 엔진 factory로 기존과 같은 3개 섹션/674·633·1227개 물리 정점, 패널별 고정점과 12cm 밑단 마스크를 생성하고 기존 Chaos 설정을 복사한다. 망토 RigidBody를 추가하지 않으며 의상 자체에는 별도 AnimInstance가 없다. 원본 `GreatSword_Woman`, runtime ABP, PA·리타깃 자산은 이전 제작·검증 경로를 위해 보존하며 새 생산 BP가 실행하는 포즈 경로에 중복 연결하지 않는다.

2026-10-21에는 이전 실험의 embedded Chaos Cloth 세 개가 RigidBody와 함께 실행되어 별도 보관 후 제거했다. 당시 원본은 `Saved/Validation/SecondaryPhysics_20261021/Backup/cloak.uasset`와 Git 이력에 남긴다. **2026-10-24의 표면 Cloth는 이 옛 데이터를 복원한 것이 아니라, 망토 RigidBody를 제거한 뒤 엔진 factory로 새로 만든 단일 소유 경로다.** 골격·skin weights·머티리얼·LOD 수를 유지한다. 로컬 백업을 실행·테스트·패키징 의존성으로 사용하지 않는다.

망토만 기존 native Cloth 예외를 통해 component Tick을 유지하고, 다른 일곱 의상과 Body·Head는 공유 포즈만 소비한다. 망토에도 별도 AnimInstance를 추가하지 않는다. 새 `PA_GreatSword_ClothCollision`은 기존 `cloak_Physics`의 몸 충돌을 옮긴 사본이다. 교체된 망토 dynamic/anchor body 29개와 관련 관절은 되살리지 않는다. 머리카락·치마 PA의 지지대는 별도 solver의 실제 소비가 있어 유지한다. 실제 표면·포즈·화면의 확인 없이 solver 생성만으로 무관통을 판정하지 않는다.

등록 해제에서는 엔진 `Super::OnUnregister()`로 Cloth 자원과 등록 상태를 먼저 해제한 뒤 LeaderPose를 비운다. `SetLeaderPoseComponent`는 `RecreatePhysicsState()`를 호출하므로 이 순서를 거꾸로 하면 엔진이 이미 제거한 물리 상태를 다시 만들 수 있다. 실제 회귀 후 다음 PIE에서 `FChaosScene → Cloak → /Temp/Untitled` 참조가 남는 GC ensure가 발생해 수정했다. 단순 검토 경고로 취급하거나 임시 월드를 강제 reset하는 방식으로 감추지 않는다. 회귀는 등록 해제 직후 physics state가 없는지와 실제 assembly 월드가 GC로 해제되는지 확인한다.

### UE5 리그 시험 범위 (2026-10-25)

외부 원본은 `C:/Users/I/Desktop/GreatSword_UE5_Final`이며 변경하지 않는다. 제공된 UE 5.8 패키지에서 `/Game/GreatSword`만 복사했고 공통 Mannequin을 덮어쓰지 않았다. 이전 메시와 물리는 LOD 검수 맵, 리타깃/IK 제작 자산과 이전 테스트가 소비하므로 보존한다. 이전 프로필도 기존 제작 경로의 의미가 있어 새 몸체 프로필과 합치지 않는다. 새 BP의 최종 연결은 기존 경로 `/Game/Character_BPs/GreatSword/BP_GreatSword`가 소유한다.

UE의 Manny Simple·Quinn Simple 및 수정된 Complete를 공식 FBX 익스포터로 내보내 Blender의 같은 씬으로 가져와 비교했다. [제작 원본·직접 비교 씬과 측정](../../../SourceArt/Characters/GreatSword/UE5/README.md)을 보존한다. 표준 공유 89본(root 포함)의 계층은 일치하며 추가 229본과 38개 메시의 정점·웨이트를 보존했다. 수정 FBX 19개를 재임포트해 재질·Nanite·망토 Cloth를 유지했다. 실제 메시와 Skeleton의 bind 위치 차이는 0.000016cm 미만이다.

현재 직접 재생 검증은 `Saved/Validation/GreatSwordDirect_20261025`다. `FinishBodyMarkers.json`은 공통 마스터의 기존 발 helper 네 개만 유지하고, 직접 몸체 입력이 helper 다섯 개를 소유함을 확인한다. `BP_Player_Master`는 이번 작업에서 수정하지 않았다. 공통 ABP 변경은 실제 메시의 초기 transform으로 발 Rig를 초기화하는 설정이며 대검 이름별 분기를 추가하지 않는다. `LiveAudit.json`은 실제 Complete/common master/body Post Process와 공유 파츠 10개, 생성된 리타깃 프로세서 0개를 확인한다.

실제 EnhancedInput/GAS 검사 `CoherentContacts_133247.json`은 Equip·LMB1/2·RMB1·Q·R·T의 7개 montage/528표본이다. alpha > 0.99에서 주손 평균/최대 오차는 약 0.00052/0.00768cm, 보조손은 0.01672/0.78494cm다. 엔진 프레임당 전달된 무기 목표를 고정해 늦은 Tick의 재계산을 막고, 현재 입력 포즈에서 목표를 계산한다. 이 시점에 저장했던 접촉 리그는 128 iterations(후속 재점검에서 32로 축소), sub-iterations 0, pull-chain 1, pelvis/쇄골 회전 stiffness 0, 손 ChainDepth/PinRotation 0, 발 position/rotation stiffness 1이다. UE 5.8 solver-controller setter가 asset dirty를 남기지 않아 이전 저장 요청이 건너뛰어진 원인을 확인하고 소유한 리그를 강제 저장했다. 앞선 설정 문자열만으로 디스크 저장·재로드를 입증했다고 간주하지 않는다.

최종 실행 파일로 재시작한 뒤 반복한 `CoherentContacts_134657.json`도 7종/536표본으로 완료했다. 같은 full-alpha 기준 주손 평균/최대는 약 0.00055/0.00844cm, 보조손은 0.01622/0.62104cm다. 앞선 native 실패를 고친 변경은 fixture의 실제 초기화→포즈 평가 순서를 맞춘 것으로, 이 반복 검사 전 런타임 접촉 코드를 다시 바꾸지는 않았다.

지상 `Ground_20261025_133429_844090.json`은 1,546표본/18클립/OTM·Strafe, `AirSprintLanding_20261025_133531_436177.json`은 2,548표본/8조건/재선택 6회/무효 후보·재생 되감기 0회로 통과했다. 공중 fixture 종료 뒤 바닥을 복원하므로 다음 장기 검증은 새 PIE에서 시작한다. 원본 레벨을 확장하거나 저장하지 않는다.

`MontagePose_20261025_133624_196155.json`은 `DirectBodyInputAndFinalFeet` 모드로 전신 몽타주 6종/249표본을 검사했다. 몸체 접촉 IK가 골반·허벅지를 움직이는 것은 허용하며, 보정 전 입력의 발을 압축 클립의 root-relative 발과 비교하고 최종 발이 그 입력 anchor를 유지하는지도 검사한다. 최대 회전 차이는 0.07793도, 위치 차이는 0.00173cm 미만이다. 기존 `FinalLocalLegRotations` 검사와 측정 대상을 혼동하지 않는다. 두 모드 모두 GASP source clock의 진행과 montage 취소/slot 해제를 확인한다.

`Cosmetics_133430.json`은 85회 완료 포즈/표면에서 망토 2,534정점의 유효한 계산, 헤어 4본·치마 2본의 움직임, source bone revision 85회를 확인했다. 얼굴 가중 본 44개와 추가 가지는 유지한다. 복제 PA 3개의 69 body/54 constraint는 기존 bone-local 형상·관절 frame을 보존하며 최대 스칼라 roundoff는 약 0.00121이다. 기존 Basic 무기를 유지하고 패키지 무기 4종은 제작 후보로 남긴다. 별도 표정·립싱크를 제작한 결과는 아니다.

`Feet_133431.json`의 이동 검사에서는 신발 일부 최저점이 약 -4.24cm인 표본이 있다. `Final_FeetSide.png`/`Final_Weapon.png`는 현재 직접 재생의 실제 화면이지만, 정지 화면과 자동 회귀만으로 모든 자세의 무관통을 승인하지 않는다. 새 리그의 LOD0 제작 데이터에 이전 LOD0–2 감축 결과나 200명 성능 결과를 적용했다고 주장하지 않는다. 새 군중 부하·원격 네트워크 실플레이·Cook/Shipping·래그돌은 별도 검증 범위다.

### 신발 지지 표면 후속 보완

2026-10-25 후속 작업은 `Saved/Validation/GreatSwordFeet_20261025`다. Blender의 현재 foot_00(2,076정점), Armature modifier 1개, 하단 표본의 정규화된 가중치와 표준/legacy 발·발볼 기준 위치를 재검사했다. 이 범위에서 전체 재리깅을 요구하는 결함은 찾지 못했다. 원본 blend·정점·웨이트를 바꾸거나 전체 몸체 Z를 올리지 않았다. 발 Rig의 reference 높이 하한 시험은 실제 신발에서 약 -1.8cm가 남아 폐기했고 공통 Rig 파일은 원본 SHA-256 `fb549b3bce72d96cb7f7f76567cb91151f819c4bfc4aebd0ddd6777ab11fc8f7`과 일치한다.

실제 신발 SKM의 선택적 `UProject_JFootContactData`(좌우 80점)와 몸체 PP의 `FootClearance`가 지지 표면을 처리한다. 현재 표시되고 같은 포즈/변환을 공유하는 파츠만 수집하며 한 다리의 표면은 한 solve로 합친다. 본 위치만으로 밑창 높이를 대신하지 않는다. 접지·낙하 하강 중 지면 아래 표본만 최소 이동시키고 foot 회전·사지 길이·root/actor 위치를 유지한다. 몸체의 기존 contact 처리 앞에 위치하며 공통 master/부모 BP에는 신발별 분기를 추가하지 않는다. 재사용 절차·분리 발/신발·미래 캐릭터/장비 예외는 [제작 가이드](../../Animation/Authoring/Character_Rigging_And_UE5_Postprocess_Playbook.md)를 따른다.

초기 10cm 상한은 착지 포즈의 실제 필요량보다 작았다. 호출 시점만 옮긴 검사도 이를 해결하지 못했고, 저장하지 않는 50cm A/B에서 접지 잔여가 사라짐을 확인했다. 최종 설정은 clearance 0.15cm/허용 lift 30cm이며, 완료 포즈 진단의 실제 최대 필요량은 약 26.49581cm다. 이 값을 다른 체형에 복사하지 않는다. 준비는 기존 몸체 Proxy의 GT `PreEvaluateAnimation`, worker는 값 snapshot과 현재 입력 포즈만 사용한다. fallback retarget의 기존 `PreUpdate` 캡처 순서는 유지한다. point/influence 저장 공간을 재사용하고 활성 개수는 매번 비워 이전 장비 표본을 남기지 않는다.

최종 `Landing_155717.json`은 지상·착지 691회/공중 178회 표면 관측에서 최저 약 +0.14998cm, 음수 표본 0개다. `AirSprintLanding_20261025_155718_791481.json`은 1,840표본/8조건, 재선택 6회·무효 후보/되감기 0회로 통과했다. 모니터를 fixture보다 먼저 시작해 바닥 복원 뒤의 낙하를 관통 결과로 잘못 읽지 않았다. `Ground_20261025_155856_978342.json`은 1,607표본으로 통과했고 `Feet_155857.json`의 321회 표면 최저도 약 +0.14999cm다.

`BuildReusableSupport.log`의 direct Editor UBT는 532 actions/정상 종료다. 새 프로세스 `NativeSoleFinal/index.json`은 67개 통과, 경고·실패·미실행 0개다. `VerifySoleDisk.json`은 30cm 설정·80점 두 세트·몸체 노드 1개·리타깃 노드 없음의 디스크 재로드를 확인한다. `FinalAssetsAudit.json`의 19개 SKM/재질·Nanite·Cloth와 PA 보존, 실제 Skeleton/mesh bind 최대 차이 약 0.00001576cm도 확인했다. 이 검수는 새 군중 성능이나 Cook/Shipping 승인을 포함하지 않는다.

`CoherentContacts_160108.json`은 Equip·공격 7종/560표본, full-alpha에서 주손 평균/최대 약 0.00055/0.00848cm와 보조손 약 0.01356/0.44005cm다. `MontagePose_20261025_160328_711785.json`은 6종/223표본에서 입력 발의 변화량이 완료된 sole solve의 적용량과 일치하고, 최종 발 anchor·회전과 source clock/취소가 유지되는지 검사했다. 최대 회전 차이는 약 0.07228도다. `Feet_160329.json`의 234회 공격 표면 관측은 최저 약 +0.14976cm였다. 중간 `CoherentContacts_150730.json`의 montage 없는 전환 1표본에서 관측한 보조손 17.65cm는 최종 반복에서 재현되지 않았으며, 원인 확정·전환 전 범위 해결로 바꾸지 않고 미확정 검수 후보로 보존한다.

`FootVisibility.json`은 표시 중 지지 다리 2개 → 숨김 중 0개 → 복원 후 2개를 확인했다. `SlopeIsolated.json`의 정지 8도 경사 63표본은 최저 약 +0.14999cm다. 앞선 점유 바닥 이동/복원 시험은 이동 베이스 효과가 섞여 정지 지형 검증에서 제외했고, 최종 fixture는 접촉 전에 만들고 PIE 종료로 폐기했다. 원래 평지의 새 PIE에서 정면/측면 `Final_FeetFront.png`/`Final_FeetSide.png`와 `Final_Weapon.png`를 직접 확인했다. `LiveAudit.json`도 원래 바닥 위 actor Z 약 98.15cm, 실제 Complete/common master/body PP·리타깃 processor 0개를 확인한다.

native 회귀의 경고 0개와 GUI 세션 전체를 구분한다. 상세 관측 중 및 표면 관측을 뺀 `AirSprintLanding_20261025_161849_675300.json`(2,900표본 통과)에서도 전투 전환 부근 `Independent Interpolation fell behind` 경고가 있었다. 새 신발 데이터 기여를 끈 `AirSprintLanding_NoSole_20261025_162517_417893.json`도 2,716표본으로 기능 검사를 통과했지만 같은 경고가 재현됐다. 새 sole 처리가 있어야만 생기는 현상으로 볼 근거는 없으며, 이 비교로 전체 원인이나 군중 비용을 확정하지 않는다. 기존 전투 전환/네트워크 프레임 지연의 별도 프로파일링 후보로 남긴다.


최종 `Checkpoint.json`은 미저장 콘텐츠/맵 0개와 background throttle 복원을 확인했다. 임시 지형·카메라·표시 변경은 PIE와 함께 폐기했다. `FinalProcessState.json`은 2026-10-25 16:29 KST에 Unreal Editor·Blender·빌드 관련 잔여 프로세스 0개를 확인한다. 공통 발 Rig와 `BP_Player_Master`의 실제 SHA-256은 HEAD LFS 원본과 일치한다. 시험 브랜치의 미커밋 상태로 유지하며 commit/push/merge하지 않았다.

### 신발 보완 전 두 경로 검증과 보존 자료

신발 보완 전 direct UBT 빌드는 `BuildFixtureFinal.log`의 529 actions/정상 종료다. 새 에디터의 `NativeFinal/index.json`은 **66개 통과, 경고·실패·미실행 0개**다. 직접 몸체/공통 마스터/fallback 그래프, 일반화된 본 이름의 접촉·alpha 1회 적용, 서버/숨김/거리 정책, 장비 연결·해제·복귀·lease와 실제 생산 fallback을 검사했다. 비호환 원본의 물리 회귀는 900 paired frames/LOD0–2/56 actors·6 dynamic bodies에서 위치 차이 0, 각도 차이 약 0.00000342도로 통과했다. 재시작한 프로세스의 `VerifyReload.json`도 solver·goal 연결·bone stiffness의 디스크 저장 유지를 확인했다. 앞선 `NativeBeforeFixture.json`은 65/66 통과와 초기화 뒤 평가하지 않은 alpha를 가정한 fixture의 실패 기록이며 최종 성공을 앞선 실패에 소급하지 않는다.

그 시점의 `FinalAssetsAudit.json`은 19개 메시·재질·Nanite·Cloth·PA와 bind 보존을 확인했고, `Checkpoint.json`은 미저장 콘텐츠/맵 0개와 background throttle 복원을 확인했다. 임시 테스트 월드를 저장하지 않았다. `QUIT_EDITOR` 뒤 `EditorFinal.log`의 정상 종료와 `FinalProcessState.json`의 Unreal·Blender·빌드 관련 잔여 프로세스 0개를 확인했다. Blender 자동 `.blend1` 두 개는 `Saved/Validation/GreatSwordDirect_20261025/BlenderBackups`로 옮겼으며 제작 원본의 실행 의존성으로 사용하지 않는다. 변경은 시험 브랜치의 미커밋 상태로 유지하고 push/merge하지 않았다.

`GreatSwordUE5_20261025`/`GreatSwordFix_20261025`의 03–10시 결과와 native 46/48개는 이전 직접 재생·Manny-source 실시간 리타깃 실험의 이력이다. 특히 `LiveContacts_102156.json`의 1.72/3.36cm는 이전 리타깃 경로 수치이며 현재 직접 재생의 증거가 아니다. 최초 새 몸체 시험의 blend profile 이름 누락, 숨은 producer tick, Skeleton/mesh bind 불일치와 부정확했던 basis 추정의 실패 자료도 보존한다.

`ABP_GreatSword_Secondary`는 초기 direct-source 제작·비교 사본으로, `ABP_GreatSword_Retarget`·Family/Body는 UE5 리타깃 실험/제작 자산으로 유지한다. 현재 Complete에는 직접 몸체 Post Process만 연결한다. 비호환 원본 Bip01 경로와 원본 메시·PA는 제작·회귀 소비자가 있어 보존한다. Blender 비교 장면 분리 저장의 내부 접근 오류는 crash log를 보존하고 일반 저장으로 복구·재열기를 확인했다. Desktop 원본과 수정된 제작 blend를 충돌 과정에서 덮어쓰지 않았다.


### 몸통 관통 보완 (2026-10-23)

아래는 본 물리 후보의 당시 기록이다. 최종 표면 Cloth 계약은 다음 절을 따른다.

실제 PIE에서 `CharacterMesh0(Manny) → Greatsword(리타깃/2차 물리) → 의상 8개`를 다시 확인했다. 망토용 `cloak_Physics`에는 목·어깨 주변의 기존 지지대만 있고 허리·골반·허벅지 충돌 범위가 없었다. 기존 `Bip01-Spine2` 캡슐도 반경 약 4.19cm의 작은 지지대였다. Physics Asset 편집기에서는 `Bip01-Spine2 ↔ phy_cloth_cape_41` 충돌 제외를 확인해 해제했다. 나머지 23개 dynamic 망토 본과 Spine2의 쌍은 이미 활성 상태여서 유지했다. 망토끼리의 충돌 제외와 anchor 쌍은 변경하지 않았다.

`cloak_Physics`에 다음 캡슐 6개를 추가했다. 값은 해당 본의 로컬 좌표(cm)이며 길이는 반구를 제외한 원통 길이다. Spine2의 기존 shape는 유지하고 `CapeChest`를 추가했다. 나머지 다섯 본에는 kinematic body를 추가했다. 전체는 **40 body / 41 shape / 기존 34 constraint**다.

| 본 / shape | 중심 (X,Y,Z) | 회전 (Pitch,Yaw,Roll) | 반경 / 길이 |
| --- | --- | --- | --- |
| Spine1 / CapeTorso | (-2,0,0) | (90,0,0) | 9 / 4 |
| Spine2 / CapeChest | (-6,0,0) | (90,0,0) | 9 / 8 |
| Spine / CapeWaist | (-4,0,0) | (90,0,0) | 8.5 / 8 |
| Pelvis / CapePelvis | (2,0,0) | (0,0,90) | 9 / 15 |
| L-Thigh / CapeLeftThigh | (-22,0,0) | (90,0,0) | 7 / 30 |
| R-Thigh / CapeRightThigh | (-22,0,0) | (90,0,0) | 7 / 30 |

빠른 동작의 순간 침투도 줄이기 위해 망토 24개 dynamic body의 CCD와 **망토 RigidBody 노드만** `Allow CCD`를 켰다. 해당 PA의 위치 반복은 6→12, 고정 물리 스텝은 1/120초로 설정했다. Velocity/Projection 반복 1, 원래 질량·감쇠·관절 프레임·각도 제한은 유지한다. 헤어/치마 solver, Manny 모션·리타깃·손발 보정·BP 계층·LeaderPose·망토 mesh/skin weights에는 변경이 없다. 새 solver나 vertex cloth를 추가하지 않았다.

검증은 같은 시작 위치의 새 PIE에서 정지/Run/Strafe/Sprint/180도 전환·점프/착지와 `LMB1/LMB2/Q1_or_LR2/Q2/R/T` 몽타주 직접 재생을 사용했다. 최종 일반 프레임 조건의 596표본에서 3개 solver 생성, dynamic writer 중복 0, 30개 dynamic bone의 finite transform과 의상 8개의 LeaderPose/독립 AnimInstance 없음이 유지됐다. 24개 망토 물리 캡슐 중심과 최종 몸통 캡슐 사이의 침투는 최대 약 0.036cm로 0.1cm 허용 오차 이내였다. 이 지표는 충돌 프록시의 완성 포즈 검사이며 전체 skin vertex의 무관통 판정을 뜻하지 않는다.

30fps 제한의 별도 새 PIE에서도 같은 60초 시퀀스 596표본을 통과했다. 망토 캡슐 중심의 몸통 범위 침투는 0건, 최소 여유 거리는 약 0.586cm였다. 최종 두 실행의 망토 체인 길이 최대 오차는 일반 약 7.76cm / 30fps 약 2.76cm였으며, 변경 전 일반 실행의 약 9.39cm보다 늘지 않았다. 별도 강제 위치 reset이나 관절 각도 예외를 추가하지 않았다. 이 진단은 10Hz 관측이며 모든 렌더 프레임의 vertex 품질을 보증하지 않는다.

진단 중 Projection을 끄는 시도는 체인 늘어짐이 증가해 원복했고, 충돌 쌍 수정 전/기본 시간 스텝만 사용한 후보에서 남은 순간 침투도 최종 성공으로 보고하지 않는다. 로컬 비교·수정 전 원본·실패 후보·최종 원자료는 `Saved/Validation/CapeCollision_20261023`에 둔다. 전체 모션/모든 카메라의 외형 승인이나 군중 성능·Cook 검증을 수행한 것은 아니다.

### 망토 표면 물리 전환과 감각 조정 (2026-10-24)

본에 붙인 capsule 중심이 몸 밖에 있어도 그 사이 skin triangle은 몸을 뚫을 수 있었다. 하체 proxy 확대·본 질량/감쇠 증가·관절 제한 축소 후보에서는 심한 접힘과 실제 표면 관통이 남아 최종 경로로 채택하지 않았다. 기존 후보와 실패 원자료는 `Saved/Validation/CapeLowerBody_20261024`에 보존한다.

LOD0의 세 render section에 embedded `SurfaceCloth_Section0/1/2`를 만들었다. 엔진 `UClothingAssetFactory`의 seam 용접과 `FSkeletalMeshClothingSystemUtilities::AssignClothingToSection`의 source-section/cloth binding 갱신을 사용한다. 단순 `BindToSkeletalMesh` 호출만으로 끝내면 source-section 데이터가 비어 build에서 연결이 사라질 수 있으므로 이를 제작 완료로 취급하지 않는다. 처음 전환 중 Cloth 연결이 없어 망토가 굳었던 실패도 원자료에 남긴다.

연결된 panel별 상단 8cm는 고정하고 다음 30cm에서 Max Distance를 부드럽게 90cm까지 늘렸다. 물리 mesh는 674/633/1,227 vertex이며 고정점은 89/144/656개다. Density 0.7, **Bending Stiffness는 상부 0.55 / 끝단 0.45**, Global Damping 0.025, Local Damping 0.1, Drag 0.01, Lift 0, Anim Drive Stiffness 0.05/Damping 0.4를 사용한다. 사용자 후속 요청에 따라 Bending 0.8→0.7→0.55, Damping 0.20→0.18로 완화했다. 0.55 요청 시점에는 다른 물성·몸 충돌 값을 바꾸지 않았다. 이후 사용자가 달릴 때 망토가 수평으로 떠서 가볍게 보인다고 지적하여, 전체 이동 속도를 감쇠하는 Global Damping을 0.18→0.025로 낮췄다. 질량·강성·Local Damping을 무작정 올리지 않았다. 사용자가 이 무게감을 직접 시험해 수용한 뒤, 끝만 조금 더 움직이도록 각 Cloth section의 하단 12cm에 `LowerHem_Bending` mask를 추가했다. 0.55에서 끝의 0.45까지 smoothstep으로 전환하며 674/633/1,227 vertex 중 각각 10/33/66개만 가중치가 0보다 크다. 고정점·Max Distance·몸 충돌은 유지한다. mask target ID는 하드코딩하지 않고 해당 엔진 simulation factory의 weight-map enum에서 찾는다. 자기 충돌 후보는 겹친 장식의 잔떨림 문제가 있어 사용하지 않고, 몸 충돌은 thickness 2cm와 CCD를 켠다. 공유 solver는 8 iterations / 최대 16 / 3 substeps다.

몸 충돌 PA는 13개 본의 kinematic proxy이며 망토 동역학 관절은 없다. 허벅지는 상부/무릎 capsule로 나누고 종아리를 추가했다. 상부 허벅지의 중심 X=-12, 반경 11.5/길이 14, 무릎 X=-34, 반경 8.5/길이 10, 종아리 중심 (-23,0,-1.5), 반경 8.5/길이 30이다. 골반은 중심 (-2,0,-1), 반경 13.5/길이 18, **Roll=90도**로 좌우를 덮는다. Python `Rotator`는 위치 인자 순서를 가정하지 않고 `pitch/yaw/roll` 키워드로 지정하고 실제 저장값을 확인했다. 상체의 기존 지지대는 유지한다.

`InspectSkinnedMeshSurface`는 LeaderPose의 완료된 bone skinning과 활성 Cloth의 완료된 simulation particle/topology를 구분해서 world-space로 내보낸다. Cloth를 검사하면서 입력 bone pose만 측정하지 않는다. 가까운 skin face의 법선 기반 부호는 열린 장식이나 오목한 곳에서 오판할 수 있어 내부 여부와 화면을 함께 확인한다. 10Hz 표본과 일부 surface snapshot은 전 렌더 프레임의 무관통을 보증하지 않는다. 이전에 에셋 검증에서 나온 `Mesh LOD: Cloth/morph/... requires its own review; data preserved`는 Cloth 실행 실패가 아니라 일반 LOD 자동 생성 제외 안내였다. 현재 망토는 공통 LOD 정책의 `ManualReviewAssets`에 native Cloth 제작 대상으로 명시하여 반복 경고 대신 정보를 남긴다. 미분류 메시의 경고와 잘못된 LOD0·플랫폼 MinLOD 검사는 유지하며, [메시 LOD 제작](../../Animation/Authoring/Mesh_LOD_Authoring.md)의 예외 계약을 따른다.

**수행 검증과 남은 경계:** Editor/Game Development direct UBT가 통과했다. 등록 해제 순서 수정 후 새 에디터에서 의상 등록/Cloth Tick/추가 본/실제 assembly/부모 계층/장비 로드·재시도 7개 회귀가 경고·실패·미실행 0건으로 통과했다. 실제 assembly 월드의 GC 해제도 포함한다. 이어서 PIE 시작→종료→재시작에서 두 RigidBody·세 Cloth section·의상 8개의 LeaderPose와 독립 AnimInstance 없음, 강성 0.55의 재로드 유지, 같은 Old World ensure 미재현을 확인했다. 수정 전의 7개 단순 성공 뒤 후속 GC에서 ensure가 발생한 결과를 최종 성공으로 재사용하지 않는다.

0.55의 60초 이동/180도 전환·점프/착지·여섯 몽타주 실행은 545개 완료 포즈 표본에서 finite/소유권 검사를 통과했다. 이 실행은 등록 해제 순서 수정과 후속 Global Damping/끝단 mask 변경 전의 기록이며 최종 물성 검증으로 재사용하지 않는다. 당시 정지·전진·Sprint 등 일부 표면 snapshot에는 5mm 이상의 내부 후보가 없었지만, 후진·정지 복귀·일부 공격에는 후보가 남았다. 열린 장식 근처의 큰 음수는 winding 검사에서 외부로 판정된 사례도 있으므로 단순 음수 최대치를 침투 깊이로 확정하지 않는다. 전체 관통 해결이나 모든 클립의 외형 승인을 보고하지 않는다. 자기 충돌/물성 후보와 3fps background throttle이 섞인 초기 실행은 최종 품질 근거에서 제외한다. 관측 도중 준비 스크립트가 사용자 미저장 머티리얼을 발견해 설정 적용을 중단한 것이 원인이었으며, 머티리얼을 보존한 채 관측 설정을 별도로 적용했다. 사용자가 저장·빌드·재시작을 승인한 `MT_Cloak1`은 그 변경 그대로 저장했다. 군중 성능·Cook/Shipping은 수행하지 않았다.

2026-10-24 후속 경고 분류/끝단 제작 도구의 Editor direct UBT 빌드와 MeshLOD Contracts·Exceptions·AssetValidation 및 실제 PlayerMasterAssembly GC 검사 4개가 경고·실패·미실행 0건으로 통과했다. 실제 망토 저장 로그도 Warning 없이 native workflow 정보만 남으며, 전체 등록 validator를 통한 망토 재검사도 1개 checked/valid, 경고·오류·미검증 0개였다. 새 회귀는 미분류 Cloth 경고, 명시한 native workflow의 정보 전환, 플랫폼 MinLOD 경고와 LOD0 오류 유지까지 구분한다. 첫 회귀의 두 실패는 fixture가 validator 범위 밖의 transient 경로여서 `NotValidated`였던 것으로, 저장되지 않는 transient fixture를 실제 검증 루트 안의 고유 패키지에 배치해 전체 validator 경로를 검사하도록 수정했다. 최초 실패 보고서를 삭제하지 않는다.

동일한 하단 vertex 422개의 달리기(500cm/s) 평균은 Global Damping 0.18 후보의 수직 낙차 약 21.1cm/수직 기준 후방 각도 76.7도에서, 최종 0.025+끝단 mask의 낙차 약 52.0cm/각도 50.3도로 달라졌다. 최종 질주(750cm/s)는 약 39.0cm/61.3도였다. 이는 제한된 22초 동작의 형상 비교이며 전체 동작이나 관통 품질 승인이 아니다. 첫 비교의 질주 구간은 벽에 막혀 속도 0이어서 질주 근거로 사용하지 않는다. 중간 비교는 사용자 직접 테스트 중 PIE가 종료되어 완료 보고서가 없으므로 사용자 피드백과 남은 surface 자료만 보존한다. 최종 87개 표면 표본은 finite 검사에 통과했고, 저장 후 재시작에서 상부 0.55/끝 0.45, Global Damping 0.025 유지와 망토 native 검증 결과를 확인했다. 원자료는 `Saved/Validation/CapeWeight_20261024`에 둔다.

마감 검사에서는 이번 변경 에셋 다섯 개(리타깃 ABP, 망토 머티리얼, 망토 메시·PA, 공통 LOD 정책)를 등록된 전체 validator로 검사해 valid 5 / warning 0 / invalid 0을 확인했다. 최종 22초 이동·질주·정지 87표본과 PIE 종료에서 새 프로젝트 경고·오류·GC ensure는 없었다. 동일 소스의 Editor/Game 빌드 성공과 수명 회귀 7개·LOD/assembly 회귀 4개의 기존 성공 근거를 재사용했다. 엔진 MCP의 EULA 안내 경고는 기능 결함으로 취급하거나 숨기지 않았다.

로컬 진단 자료 정리에서는 종료된 반복 캡처·일회성 스크립트 45개·이전 백업 등 297파일을 `Saved/Validation/Retention/CapeDiagnostics_20261024.zip`으로 보관하고, 모든 항목의 SHA-256/ZIP 무결성을 확인한 뒤 압축 전 파일만 제거했다. 이전 실패와 백업도 ZIP에서 복원 가능하며, 대표 보고서·빌드/자동화 결과·현재 에디터 로그·재사용 검증 도구는 직접 읽을 수 있게 유지했다. 원본 논리 크기 347,455,172바이트, ZIP 216,182,799바이트이며 드라이브 여유 공간 실측 증가는 131,878,912바이트였다. 파일별 지문과 로컬 정리 요약은 ZIP 내부 `manifest.json` 및 옆의 JSON에 둔다. 위 절의 과거 원자료 경로 중 압축된 항목은 이 ZIP 안의 같은 프로젝트 상대 경로에서 찾는다. 현재 빌드 캐시·프로젝트 에셋·영구 테스트·추적된 MCP 도구와 다른 작업의 자료는 삭제하지 않았다. 이 로컬 압축본은 Git 공유나 새 PC 재현의 필수 입력이 아니다.

## 애니메이션 확장과 비용

현재 마스터 ABP는 BP 자체 상태 변수 없이 native 계산을 소비한다. 직업마다 지상·공중 그래프를 복제하거나 모션 목록을 부모 BP의 분기문에 넣지 않는다. 후보·튜닝은 Chooser/PSD/프로필, 다른 포즈 동작은 기존 Animation Layer Interface와 레이어를 사용한다. 동일한 노드 함수라도 재생 시각·커브·NodeRef·레이어별 aim 한계가 다르면 결과를 전역 캐시로 합치지 않는다.

기존 `AnimationUpdateCoordinator`, source mesh budget, 장비 표시 큐와 프로필의 중요도 설정을 유지한다. 새 전역 manager·두 번째 Tick/예산 시스템·모든 레이어의 하드 참조 목록은 추가하지 않는다. 이번 부모 정리는 대규모 인원 성능 측정을 대신하지 않는다. 서버 판정과 시각 갱신 빈도는 별도 계약이다.

엔진 소스 패치 없이 프로젝트의 native 컴포넌트·프로필·에디터 도구를 사용한다. Mover·애니메이션 실험 기능의 변경은 해당 adapter와 회귀에서 확인하며, 미래 엔진 호환성을 보장하는 별도 프레임워크는 만들지 않는다.

## 제작 시 주의와 검증

새 플레이어 BP는 `BP_Player_Master`의 자식으로 만들고 기존 프로필을 지정한다. 값만 달라지는 직업마다 빈 native 클래스를 추가하지 않는다. 추가 런타임 행동이 실제로 필요한 경우에만 native 확장을 검토한다. NPC의 상태 소유권과 플레이어 부모를 혼동하지 않는다.

초기 부모 승격용 `PreparePlayerMaster`는 이관 완료 후 퇴역했다. 현재 자식 제작에 초기 프로토타입을 다시 설치하거나 기본값을 덮는 함수를 사용하지 않는다. 기존 프로필·컴포넌트와 상속 템플릿 보정 도구는 계속 사용한다. 로코모션 초기 설치 함수의 퇴역과 BP/ABP 실제 소비 검사는 [완료 후 정리 기록](../../Animation/Authoring/Player_Animation_Library.md#완료된-이관-코드-정리--2026-10-22)을 따른다.

옛 `/Game/ThirdPerson/Blueprints/BP_Player_Third`는 사용자 용도 폐기 확인과 hard/soft/search/management·실제 로드 참조 검사를 거쳐 제거했다. 공유 입력·Quinn Simple·기존 Unarmed 제작 자산은 함께 지우지 않는다. `ConfigureGameplayAvatar`의 삭제된 궤적 컴포넌트 이름 기반 정리 분기도 제거했으며, 현재 Mover·source mesh·상속 템플릿 구성 기능은 유지한다.

Mover의 class-owned inline mode는 자식 BP의 archetype이므로 `RF_Public | RF_ArchetypeObject` 계약을 지켜야 한다. 이전 제작 함수에서 public 플래그 누락을 수정했고, 기존 템플릿은 editor 전용 `FinalizePlayerAvatarTemplates`로 보정한다. 이동 모드 설정은 바꾸지 않으며 저장 검사를 끄지 않는다.

마스터 이관 당시 76개 회귀가 경고·실패·미실행 없이 통과했다. 새 검사는 실제 부모·자식 상속/기본값, native 컴포넌트 단일성, 8개 의상 실제 연결, 등록·활성화·교체·해제 수명과 상속 템플릿 저장 계약을 확인한다. 기존 handover·실제 대검의 PlayerState/GAS 소유권·표시 몸체 검사를 실행기에 포함했고, 마지막 검사의 예전 CMC `ACharacter` 타입 가정도 현재 플레이어 Mover 타입으로 갱신했다. 대검의 제작 기본값·몸체·입력 매핑은 변경 전후 비교했다.

앞선 마스터 이관의 Editor/Game Development direct UBT 빌드가 통과했다. MCP로 시작한 새 네트워크 PIE에서 지상 2,134표본의 OTM/Strafe·Start/Stop, 공중/착지 8조건 4,187표본과 공중 재선택·재생 되감기 0건, 전신 몽타주 6개 280표본을 확인했다. 몽타주 하체의 클립 대비 최대 회전 차이는 약 0.0541도였다. 두 월드 1,278표본의 의상 검사는 공통 본 연결만 증명했으며 당시 남아 있던 vertex cloth 실행까지 증명한 검사는 아니었다. 후속 점검에서 이 구분을 바로잡고 cloth Tick 예외와 실험 데이터 정리를 적용했다.

로컬 원자료는 `Saved/Validation/PlayerMaster_20261021`, 최종 회귀는 `Saved/Validation/PlayerLocomotion/PlayerMasterComplete_20261021/Automation/index.json`이다. 첫 저장 실패의 private archetype 진단과 예전 CMC 타입 테스트 실패도 보존했다. 초기화/Iris 대상 경고는 마지막 GUI에서 발생하지 않았으며 MCP 라이선스 안내는 별개다. 콘텐츠·맵 미저장 항목 0개를 확인하고 테스트용 편집기 설정을 복원한 뒤 에디터를 정상 종료했다. 전체 448개 모션 외형·대규모 성능·Cook/Shipping 검증을 의미하지 않는다.

### 의상 물리 후속 검증

실험 데이터 정리 후 Editor/Game Development direct UBT 빌드가 통과했고, 최종 **80개 회귀가 경고·실패·미실행 0건**으로 통과했다. `EquipmentLoadLifecycle`은 최초 등록의 포즈 연결, 같은 완료 콜백의 중복 방지, 슬롯 교체 시 이전 mesh 파괴, 이전 item 완료의 재생성 거부와 lease 해제를 확인한다. 기존 `EquipmentRetry`, 일반 cloth의 예산 제외/등록·활성화·mesh 교체 정책도 실행기에 포함했다. Cloth 정책 fixture는 생산 망토나 로컬 백업에 의존하지 않는 transient render-section 메타데이터이며, 미래 Chaos solver의 품질 테스트를 대신하지 않는다. 첫 회귀의 이 두 fixture 실패는 editor가 cooked cache bit가 아닌 실제 section 메타데이터로 활성 cloth를 계산하는 차이를 반영해 수정했다.

MCP로 시작한 PIE에서 지상 OTM/Strafe·Start/Stop·Walk/Run/Sprint 2,115표본, 공중/착지 8조건 4,115표본(공중 재선택 증가 2, 무효 후보·되감기 0), 전신 몽타주 6개 261표본을 확인했다. 몽타주 하체 최대 차이는 약 0.05384도였다. 병행한 두 월드의 완료된 물리 포즈 1,266표본에서 망토/헤어/치마의 30개 dynamic bone이 모두 변화했고, finite transform·세 solver 생성·dynamic writer 중복 0·의상 AnimInstance/Tick 0·독립 cloth instance 0을 확인했다. 원본/복제 PA의 900프레임 물리 동등성 검사도 통과했다(91 actor/30 dynamic, 최대 회전 차이 약 0.00000342도). 실제 화면에서 파츠 누락·분리는 보이지 않았다.

후속 원자료는 `Saved/Validation/SecondaryPhysics_20261021`, 최종 자동 보고서는 `Saved/Validation/PlayerLocomotion/SecondaryPhysicsFinal_20261021/Automation/index.json`이다. 진단 중 잘못 호출한 Python getter 오류 및 첫 fixture 실패는 수정 전 근거로 보존한다. GUI 로그에는 단일 LOD 제작 검토 안내와 무거운 물리 관측 중 `Independent Interpolation fell behind` 1건이 있어 GUI 전체 경고 0건으로 보고하지 않는다. 별도 새 에디터에서 상세 물리 관측 없이 같은 공중/착지 8조건을 다시 실행한 4,213표본은 통과했고, 해당 보간 경고는 재현되지 않았다(`LightReplayEditor.log`, `AirLanding_20261021_214520_495596.json`). 관측 부하의 영향 가능성은 있으나 원인으로 확정하지 않으며, 경고를 감추는 설정 변경도 하지 않았다. 이 점검으로 모든 네트워크 부하의 안정성이나 모든 포즈의 망토 관통 방지까지 보장하지 않는다. 종료 시 미저장 콘텐츠/맵 0개와 background throttle 복원을 확인했고 GUI/자동화 에디터를 정상 종료했다.

- [로코모션 아키텍처](../../Animation/Architecture/Player_Locomotion_Architecture.md)
- [이동 콘텐츠 제작](../../Animation/Authoring/DataDrivenLocomotionExtensionGuide.md)
- [모듈러 의상 제작](../../Gameplay/Authoring/ModularCharacterEquipmentGuide.md)

### 리깅·후처리 전체 재점검 (2026-10-25)

단일 UE5 코어와 peripheral 추가 가지, 직접 호환/비호환 RTG의 배타적 경로, 기존 gameplay·예산 소유권은 유지한다. 이번 점검에서 공통 Master ABP나 부모 캐릭터 BP에 메시 이름별 분기를 추가하지 않았다. Epic/Blender 공식 자료와 실제 엔진 effective translation mode, 원본 압축 pose, Blender 38개 메시를 비교한 판단과 재사용 절차는 [제작 가이드](../../Animation/Authoring/Character_Rigging_And_UE5_Postprocess_Playbook.md)를 따른다.

- GreatSword source는 38개 메시/88,053정점·최대 influence 4다. 단일 Armature·vertex groups·LBS, 가중치 누락/합계 오류·미존재 가중 본·helper weight 0을 확인했다. 리그 1.2는 무가중 표준 IK 7본의 Deform flag와 독립 무기 제어를 개선했다. skin hash·rest·평가 형상 전후 차이 0, 네 사지 FK/IK·root translation/yaw·독립 무기 12조건과 저장 재열기를 확인했다. 기존 FBX/SKM skin/bind가 같아 authoring 변경만으로 재임포트를 반복하지 않았다.
- `InstallBodyPoseMarkers`의 새 기본값은 4개 사지 marker만 생성하고 authored `ik_hand_gun`을 보존한다. 현재 대검은 보정 전 오른손을 무기 기준으로 쓰므로 명시적 5개 구성과 기존 runtime pose를 유지한다. tool-owned 4↔5 전환·반복 호출·충돌 writer 거부는 제작 도구의 책임이며, 공통 Master를 수정하지 않는다. 실제 독립 gun 애니메이션을 새로 제작/연결한 결과는 이번 범위가 아니다.
- 접촉 `IK_GreatSword_Contact`는 128→32회로 낮춰 저장했다. 32회 입력 7종/541표본은 full-alpha 주손 평균/최대 0.00081/0.02022cm, 보조손 0.01527/0.64731cm였다. 별도 montage 242표본은 완료됐으며 sole correction 실제 delta·발 anchor/회전·source 시간/취소를 확인했다. 20회/517표본은 활성 montage 보조손 최대 2.331cm로 채택하지 않았다. montage 없는 전환의 22.987cm는 후술한 수명 문제와 분리하며 반복 수 부족의 증거로 사용하지 않는다. 서로 다른 실행 일정이며 동일 시각의 완벽한 A/B나 CPU 비용 75% 절감의 증거로 해석하지 않는다.
- 큰 발 lift는 실제 Manny도 같은 cliff-start clip 1.2167초에서 발볼이 root보다 21.50cm 낮았고 GreatSword는 21.41cm 낮았다. 원본 공중 pose의 이동 위상과 피부 형상 잔차를 분리한다. 현재 optional sole 보정은 유지하지만 큰 clamp를 리깅 정상 판정으로 쓰지 않는다. root/Actor·사지 길이·skin을 이 수치만 보고 다시 변형하지 않았다. 원본 clip은 측정만 했으며 지속적인 큰 보정은 animation/phase 제작 재검토 대상이다.

로컬 근거는 `Saved/Validation/RigPostReview_20261025`다. Manny를 PIE 중 교체했던 비교는 공중 case가 부족해 승인 근거에서 제외했다. 위 raw clip 비교와 혼동하지 않는다. 이전 신발/직접 재생의 native 67개·PIE 표면 성공은 당시 기록이며, 이번 변경의 재시작 검증 결과와 나눠 기록한다. Cook/Shipping·새 군중 성능·모든 극단 포즈/지형·장비 조합은 이번 결과로 승인하지 않는다.

최종 direct Editor UBT는 533 actions/359.85초로 통과했다. 새 프로세스 `NativeVerified`의 **68개 회귀가 경고·실패·미실행 0건**이었다. 제작 도구 기본 gun 보존, tool-owned 4↔5 전환·idempotence·충돌 writer 거부와 공통 master 보호를 포함한다. 실제 대검에 `true`로 재호출한 검사는 기존 5개 체인을 유지하고 dirty package를 만들지 않았다. 저장된 32회·stretch 없음·손/발 제약도 재로드했다.

최초 새 테스트는 임시 그래프의 ComponentToLocal 출력 핀을 잘못 지정해 충돌했다(`EditorFinal.log`). 실제 제작 자산 수정/저장 전이었고 올바른 `Pose` 출력과 schema 연결/null 검사를 적용해 단독 및 위 전체 회귀를 다시 통과했다. 이 실패를 성공 기록으로 덮지 않는다. 첫 컴파일의 테스트 TObjectPtr 추론 오류도 보존했다.

추가로 실제 Schema 6개를 읽어 Idle의 character pose, Loop의 continuing curve 등 query 입력을 확인했다(`SchemaAudit.json`; top-level channel 조회). UE5.8 `PoseSearchHistory.cpp`의 실제 pose 저장·bone remapping과 `PoseSearchFeatureChannel_Position.cpp`의 continuing/character 분기를 대조했다. 체형의 좌표 정규화나 모든 nested 채널/비용 품질까지 승인한 것은 아니다. 공통 DB/ABP를 이 확인만으로 변경하지 않았다.

### 무기 복귀 경계의 추가 원인 분리

32회 설정 재로드 뒤 실제 입력 7종/508표본의 활성 montage full-alpha 최대는 주손 0.09945cm·보조손 0.60926cm였고, 동일 PIE 신발 543표본의 최저는 +0.13385cm·관통 0이었다. 그러나 montage 없는 RMB 복귀의 보조손 27.15459cm를 성공에서 제외해 숨기지 않았다. 기존 고빈도 GuidedIK/GripTrace로 RMB 4회/323표본을 다시 측정해 종료 경계의 15.77131/14.16548cm 두 표본을 재현했다.

`TransitionBefore.log`에서 같은 프레임 31441의 두 손 solve/post-alpha contact는 0이었지만, 그 뒤 stopped montage의 늦은 NotifyBegin이 `MotionBegin`을 실행해 ContactRecovery를 취소했다. 무기 위치가 완료 포즈 뒤 바뀐 것이며 source bone/frame·부착 cache 차이나 사지 길이 부족으로 단정하지 않는다.

수정은 기존 `UProject_JAnimNotifyState_WeaponMotion::NotifyBegin`에 두었다. engine montage context의 정확한 실행 ID를 조회해 없는/비활성 인스턴스의 새 lease를 거부한다. 정상 실행·같은 asset 재실행·context-free 기존 sequence/editor 경로와 token 기반 Tick/End는 유지한다. 기존 PrimaryGripAttachment 회귀에는 outgoing pose weight가 남은 stopped 실행, 늦은 Begin, 새 실행·이전 End/없는 ID를 추가했다. 캐릭터별 ABP/리깅·profile·montage 데이터와 공통 상태/예산 소유권은 이 문제 때문에 바꾸지 않았다.

실행 ID 승인 수정 후 direct Editor UBT는 533 actions/358.31초로 통과했다. 새 프로세스 `NativeMotionFinal`의 **68개 회귀가 경고·실패·미실행 0건**이었다. 확장한 PrimaryGripAttachment는 단독과 전체에서 통과했다. 이후 RMB 네 번을 매 프레임 관측한 655표본에서는 full-alpha 최대 주손 0.00875cm·보조손 0.16185cm였고, montage 없는 보조손 38표본의 최대는 약 0.000000554cm였다. 이전 14~27cm의 복귀 표본은 이 반복에서 사라졌다. 실행 일정/관측 빈도가 달라 같은 frame lock-step A/B나 모든 clip 보장으로 부르지 않는다. 원자료는 `TraceContactsEveryFrame_180033.json`, `MotionAdmissionSummary.json`, `EditorMotionFinal.log`다.

최종 저장값 32회의 일반 7종 입력 재검사(`CoherentContacts_180125.json`)는 517표본이었다. full-alpha 주손 평균/최대 0.00126/0.09322cm, 보조손 평균/최대 0.03450/1.63430cm다. 활성 montage 보조손 최대는 0.67990cm였으며, **Q 복귀의 montage 없는 한 표본 1.63430cm도 전체 최대에 포함**했다. 해당 표본은 published goal에도 같은 도달 잔차가 있어 늦은 무기 재시작과 구분한다. 이 경계까지 모든 자세의 정확한 파지를 보장한다고 보고하지 않는다. 동시에 신발 553표본은 최저 +0.14238cm·관통 0이었다(`Landing_180126.json`, `FinalSummary.json`).

새 잔차를 근거로 저장하지 않은 64회/7종·556표본을 비교했지만, 보조손 최대 3.51861cm(T의 release/도달 자세)가 관측되어 일관된 개선을 확인하지 못했다. 64회는 저장하지 않고 기존 32회로 복원했다(`Solver64Comparison.json`). 두 실행은 frame-locked A/B가 아니므로 64가 항상 나쁘다는 결론도 내리지 않는다. 반복 수·stretch·몸체 변형으로 모든 도달 잔차를 감추지 않으며, 이후 무기/체형/원본 동작 검수에서 grip 간격·접촉 curve·회복 자세와 실제 도달 범위를 조정한다. 리깅 결함을 확인한 사례로 바꾸지 않는다.

마감 시 32회 복원과 기존 5개 marker idempotence를 다시 확인했다. 콘텐츠/맵 dirty 0개, 진단 cvar 기본값·background throttle 복원 후 UE 에디터를 정상 종료했다. Blender도 저장·재열기 확인 후 정상 종료했으며 종료 시 Unreal/Blender/빌드 관련 프로세스는 남지 않았다. 공통 부모 BP와 발 Control Rig의 저장소 원본 해시도 그대로였다. commit/push는 수행하지 않았다.
