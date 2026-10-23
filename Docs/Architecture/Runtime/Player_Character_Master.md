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
| `BP_GreatSword` | 직업 정의, 대검 시작 장비, 직업 입력 매핑, 비표준 몸체와 의상 구성 |
| CharacterAnimProfile | 이동·전투·Foley·몸체 보정 프로필의 제작 진입점 |
| native AnimInstance/Proxy | GT 수집과 worker 데이터, 선택·전환·핀 계산 |
| 마스터 ABP/Linked Layer | 포즈 연결·노드별 콜백 시점·슬롯·직업/탑승 표현 |

테스트용 `BP_Player`를 `BP_Player_Master`로 승격했다. 테스트에 남아 있던 Avatar 로컬 AttributeSet, 기본 능력·속성 데이터는 native 기본값으로 정리했다. 플레이어의 지속 상태와 능력 부여는 기존 PlayerState/직업 계약이 담당한다. `CharacterAnimProfile`과 동일한 직접 `LocomotionProfile` 값도 제거하여 프로필을 통해 해석한다. native fallback 자체는 다른 제작·테스트 소비자가 있으므로 유지한다.

대검은 기존 BP 경로와 에셋 정체성을 유지한 채 부모를 변경했다. GameMode·레벨·기존 제작 데이터가 새 복사본을 잘못 가리키는 문제를 피한다. Content 안에 레거시 캐릭터 복사본을 추가하지 않았고 변경 전 네 에셋은 `Saved/Validation/PlayerMaster_20261021/Backup`에 보관했다. 원본은 Git 이력에서도 복구할 수 있다. `AProject_JGreatswordCharacter`는 동작 없는 native fixture 타입으로 기존 C++ 테스트와 파생 검증 타입이 소비하므로 보존하며, 생산 BP의 부모는 아니다.

## 의상 포즈 연결

기존 `UProject_JModularMeshComponent`의 `bFollowAttachParentPose`를 켠 authored 파츠는 등록 시 skeletal attach parent를 LeaderPose로 사용한다. 대검 의상 8개는 실제 Bip01 표시 몸체에 붙이며 숨은 Manny source에 직접 연결하지 않는다. 등록 해제·재등록·부모 교체·분리에서도 기존 source를 해제하고 현재 부모를 사용한다. BP Construction Script의 연결 호출 8개를 포함한 노드 29개를 제거했다. 공유 파츠의 bone pose 평가는 끄며, 일반 파츠의 component Tick도 끈다. **독립 Chaos Cloth가 연결된 파츠는 예외적으로 component Tick이 필요하다.** 활성 cloth 메타데이터/리소스로 판단하고 전용 파츠 이름이나 두 번째 예산 시스템을 만들지 않는다. Dedicated Server에서는 cosmetic cloth Tick을 켜지 않는다.

공통 본만 따라가는 파츠에는 LeaderPose를 사용한다. 파츠 자체의 추가 뼈를 독립적으로 평가해야 하면 자동 LeaderPose를 끄고 기존 `AttachAndCopyPose`와 해당 Post Process를 사용한다. `AttachAndSetLeader`로 생성한 장비도 동일한 수명 정책을 사용한다. 같은 source에 다시 연결할 때 강제 갱신하지 않아 물리 상태 재생성을 피한다. 장비의 mesh/pose source를 설치한 **뒤에** 등록하여 cloth/Post Process가 빈 source로 시작하지 않게 한다. 슬롯 교체는 기존 visual revision·lease 취소/해제 계약을 유지하고 표시 몸체의 AnimInstance나 전체 물리를 초기화하지 않는다. LeaderPose만으로 draw call이나 머티리얼 비용까지 제거되지는 않는다.

### 대검의 실제 2차 물리 소유권

현재 `ABP_Greatsword_Woman_RunTIme`의 retarget/손 보정 뒤에는 머리카락 4개와 치마 2개 dynamic bone을 계산하는 **RigidBody 두 개**만 남긴다. `GreatSword_Woman_Physics`, `sho_Physics`는 유지한다. 망토는 `cloak` 컴포넌트의 독립 Chaos Cloth가 표면을 계산한다. 표시 몸체의 망토 RigidBody는 포즈 입출력을 재연결하고 제거하여 본 물리와 vertex cloth가 같은 망토를 이중 계산하지 않는다. 의상은 최종 본 포즈를 공유하며 현재 몸체/의상에 Post Process ABP가 있다는 가정으로 설명하지 않는다.

2026-10-21에는 이전 실험의 embedded Chaos Cloth 세 개가 RigidBody와 함께 실행되어 별도 보관 후 제거했다. 당시 원본은 `Saved/Validation/SecondaryPhysics_20261021/Backup/cloak.uasset`와 Git 이력에 남긴다. **2026-10-24의 표면 Cloth는 이 옛 데이터를 복원한 것이 아니라, 망토 RigidBody를 제거한 뒤 엔진 factory로 새로 만든 단일 소유 경로다.** 골격·skin weights·머티리얼·LOD 수를 유지한다. 로컬 백업을 실행·테스트·패키징 의존성으로 사용하지 않는다.

망토만 기존 native Cloth 예외를 통해 component Tick을 유지하고, 다른 일곱 의상은 공유 포즈만 소비한다. 망토에도 별도 AnimInstance를 추가하지 않는다. `cloak_Physics`는 Cloth의 몸 충돌용 PA로 계속 소비하며, 교체된 망토 dynamic/anchor body 29개와 관련 관절을 제거했다. 머리카락·치마 PA의 지지대는 별도 solver의 실제 소비가 있어 유지한다. 실제 표면·포즈·화면의 확인 없이 solver 생성만으로 무관통을 판정하지 않는다.

등록 해제에서는 엔진 `Super::OnUnregister()`로 Cloth 자원과 등록 상태를 먼저 해제한 뒤 LeaderPose를 비운다. `SetLeaderPoseComponent`는 `RecreatePhysicsState()`를 호출하므로 이 순서를 거꾸로 하면 엔진이 이미 제거한 물리 상태를 다시 만들 수 있다. 실제 회귀 후 다음 PIE에서 `FChaosScene → Cloak → /Temp/Untitled` 참조가 남는 GC ensure가 발생해 수정했다. 단순 검토 경고로 취급하거나 임시 월드를 강제 reset하는 방식으로 감추지 않는다. 회귀는 등록 해제 직후 physics state가 없는지와 실제 assembly 월드가 GC로 해제되는지 확인한다.

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
