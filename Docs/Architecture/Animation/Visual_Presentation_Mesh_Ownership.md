# 보이는 메시 기준의 애니메이션·장비 파이프라인

현재 칼 궤적 정책은 [Weapon_Grip_Drive_Policy.md](Weapon_Grip_Drive_Policy.md), 전환은 [Weapon_Contact_Recovery.md](Weapon_Contact_Recovery.md), 몸체별 소켓·Guided 노드 설정은 [Guided_Hand_Contact.md](Guided_Hand_Contact.md)를 기준으로 한다. 아래의 이전 전환 설명보다 이 최신 문서들이 우선한다.

## 2026-10-02 현재 상태

- 오른팔은 FABRIK 대신 `Project J Guided Hand IK`로 입력 포즈 기반 팔꿈치 안정화와 손목 접촉을 수행한다. 왼팔 Two Bone IK는 미연결이며 RigidBody 3개는 유지한다.
- Palm→손목 공통 변환, 몸체 프로필, 중간 보조 뼈와 누락/LOD 정책을 구현했다. `DA_HGP_Greatsword`는 런타임 ABP 클래스 디폴트의 `Hand Grip Profile`에 할당한다.
- 자동 공격 복귀는 outgoing 몽타주의 실제 가중치와 현재 독립 소스 접촉을 따른다. 단순 KeepWorld 부착으로 전환을 끝내는 이전 설명은 대체됐다.
- Idle의 Visual 부착 오프셋과 공격의 Palm 접촉은 아직 각각 작성한다. 공통 기준의 Idle 자동 정렬은 후속 작업이다.
- 아래 Control Rig/FBIK 상체 Reach, 양손 동시 제약과 손가락 보정은 목표 설계다. 현재 구현된 그래프로 오인하지 않는다. 팔 길이 밖 목표와 원격·군중 성능 검증이 남아 있다.

## 목표와 적용 범위

`CharacterMesh0`가 숨겨진 마네킹 모션 매칭 소스이고 `Greatsword`가 `Retarget Pose From Mesh`로 구동되는 Bip01 렌더 메시라면, 화면에 보이는 무기와 의상은 `Greatsword`의 포즈와 소켓을 기준으로 배치한다. 모션 매칭, 전투 판정, 장비 상태의 기존 권위는 유지한다. 이 문서는 코드에서 준비한 연결 규칙과 에디터에서 설정할 항목을 구분한다.

## 런타임 구조

```text
서버의 장비·전투 상태 / 공격 재생 상태
                 │
                 ├─ 숨겨진 CharacterMesh0: 공용 로코모션 + 전투 레이어
                 │                  │ Retarget Pose From Mesh
                 │                  ▼
                 └─ 보이는 Greatsword: 리타깃 포즈 → 손 파지 IK → 외형 물리
                                      ├─ Bip01 의상: 호환 스켈레톤의 Leader Pose
                                      └─ 무기: 보이는 손/등 소켓에 부착
```

- 보이는 리타깃 팔로워는 소스 메시의 직계 자식 중 `UProject_JRetargetAnimInstance`를 쓰는 메시로 찾는다. 복잡한 캐릭터 BP에서는 팔로워 컴포넌트에 `VisualFollower` 태그를 붙여 명시할 수 있다. 매 프레임 전신 검색은 하지 않는다.
- 무기 프로필의 `VisualDrawnSocketName`, `VisualSheathedSocketName`은 보이는 메시의 소켓 이름이다. 비워 두면 기존 `DrawnSocketName`, `SheathedSocketName`을 같은 이름으로 찾아본다. 팔로워에 소켓이 없으면 소스 메시 소켓을 쓴다. `bPreferVisualFollowerSockets`로 캐릭터/무기 프로필별 구 방식을 유지할 수 있다.
- 독립 무기 모션의 키는 기존처럼 소스 메시의 발도 소켓 기준으로 작성한다. 새 접촉 전환에서는 시작 포즈를 한 번 캡처하고, 끝에서 현재 칼 포즈를 유지하며 보이는 손에 부착한다. 기존 키 데이터는 유지한다.
- 모듈러 의상은 장비 메시와 **동일한 `USkeleton`**을 쓰는 포즈 소스를 선택한다. Bip01 의상은 보이는 Bip01 몸체를, 마네킹 의상은 마네킹 소스를 따른다. 호환 소스가 아직 없으면 정해진 횟수만 재시도하고, 엉뚱한 리더에 결합하지 않는다.

## 파지 정책

파지 방식은 캐릭터 전체에 고정하지 않는다. 무기 프로필의 발도/납도 기본 IK 가중치가 대기 상태를 정의하고, 공격 몽타주의 `TwoHandIK` 노티파이와 `Weapon Motion` 노티파이가 구간별로 덮어쓴다. 리타깃 ABP의 손 IK 커브는 명시적으로 작성된 `0`도 적용한다. 따라서 오른손만 잡는 대기, 양손 대기, 공격 중 한 손 해제, 공격 중 양손 파지가 모두 데이터와 몽타주로 표현된다.

보이는 오른손 소켓에 무기를 붙인 동안에는 오른손을 그 무기 소켓으로 다시 IK하지 않는다. 자기 자신을 추적하는 순환이 되기 때문이다. 이때 오른손 IK는 기본적으로 0이며, 의도적으로 별도 보정을 작성한 프로필만 `bAllowPrimaryIKOnVisualAttachment`를 켠다. 독립 무기 모션 구간에는 노티파이에 설정한 오른손/왼손 IK 가중치를 사용한다. 왼손은 항상 무기의 `WeaponGrip_L` 소켓을 목표로 하되, 해당 구간의 IK 가중치가 0이면 자유롭게 움직인다.

## 칼의 주도권과 같은 프레임 평가

| 구간 | 칼의 주도권 | 손 목표 | 필요한 보정 |
| --- | --- | --- | --- |
| 납도 | 몸체/등 소켓 | 프로필의 선택적 납도 파지 | 기본 포즈 또는 낮은 가중치 |
| 일반 발도·오른손 주도 공격 | 보이는 오른손 소켓 | 왼손은 **현재 프레임 오른손 본 공간**의 `WeaponGrip_L`을 추적 | 왼팔·쇄골 Reach, 손목 회전 |
| 소스 주도 공격 또는 `Weapon Motion` 구간 | 몽타주의 칼 궤적 | 손이 같은 프레임의 칼 소켓을 추적 | 양팔·상체 Reach, 손 놓기 가중치 |

컴포넌트 공간의 무기 월드 좌표를 매 프레임 다시 읽는 방식만으로는 손에 붙은 칼과 손 IK 사이에 한 프레임 순환이 생길 수 있다. C++ `FProject_JWeaponGripTargets`는 `DriveMode`, `PrimaryHandBoneName`, `SecondaryGripInPrimaryHandSpace`를 제공한다. `UProject_JRetargetAnimInstance`는 캐릭터별 `SecondaryHandOffset`을 적용한 `LeftGripInPrimaryHandSpace`와 `bUsePrimaryHandSpaceGrip`을 팔로워 ABP에 노출한다. 이 좌표는 칼과 오른손 본이 함께 움직여도 일정해야 한다.

독립 칼 모션은 팔로워 애니메이션이 그립을 읽을 때 최신 몽타주 위치로 칼을 먼저 평가한다. 기존 `PostUpdateWork` 틱은 팔로워 평가가 생략된 경우의 외형 갱신 경로로 남는다. 몽타주 위치가 같은 프레임에서 변경되면 캐시를 무효화한다. 이 타이밍은 에디터 자동화 테스트로 확인했으며, 실제 공격의 소스 메시/팔로워 병렬 평가 순서는 인게임 프레임 추적으로도 확인해야 한다.

## Bip01 상체 Reach 목표 그래프

```text
Retarget Pose From Mesh
  → 팔로워 현재 포즈 기반 칼 접촉 목표 선택
  → 상체 Reach (쇄골·가슴·양팔, 발과 골반은 안정적으로 유지)
  → 손 위치/회전 및 손가락 파지
  → Bip01 치마·망토·머리카락 RigidBody
  → Output Pose
```

현재 오른팔 Guided 노드와 세 `RigidBody`는 아직 이 상체 Reach 목표 그래프로 교체되지 않았다. `Two Bone IK`는 어깨 위치가 고정된 두 본 체인만 풀기 때문에 팔 길이 차이로 닿을 수 없는 손잡이까지 자연스럽게 도달시키지 못한다. 먼저 `IK_GreatSword_Woman`과 런타임 IK Retargeter의 팔 체인/리타깃 포즈를 맞춰 기본 자세를 개선한다. 그다음 팔로워 ABP의 리타깃 뒤에 상체 Control Rig/FBIK를 배치해 캐릭터별 어깨·쇄골·가슴 이동 범위, 팔꿈치 선호 방향, 손목 회전을 조절한다. 기본적으로 뼈 길이를 늘리지 않는다. 도달 불가능한 칼 궤적은 솔버가 아니라 해당 공격의 칼 경로나 손 해제 타이밍을 수정한다.

상체 솔버는 양손을 항상 고정하지 않는다. 프로필 기본값, `Two-Hand Grip IK`, `Weapon Motion`, 명시적 애니메이션 커브 순서로 얻은 손별 가중치를 사용한다. 오른손이 칼을 이끄는 구간에는 오른손에 대한 칼 소켓 IK를 금지하고, 칼 주도 구간에는 노티파이의 오른손 가중치를 적용한다. 왼손은 손을 놓는 구간에서 자연스럽게 애니메이션 포즈로 복귀한다.

### 팔로워 ABP 연결 절차

1. `ABP_Greatsword_Woman_RunTIme`에서 `Retarget Pose From Mesh` 뒤의 현재 포즈를 상체 솔버 입력으로 사용한다. 기존 Two Bone IK와 RigidBody를 한꺼번에 제거하지 않고, 대표 공격의 프리뷰에서 새 솔버 경로와 이전 경로를 전환해 비교한다.
2. `bUsePrimaryHandSpaceGrip`가 참이면 현재 포즈의 `PrimaryHandBoneName` 변환에 `LeftGripInPrimaryHandSpace`를 합성해 왼손 목표를 만든다. 이 구간의 칼은 오른손이 구동하므로 오른손을 같은 칼 소켓으로 다시 IK하지 않는다. 거짓이면 `LeftGripLocation/Rotation`과 필요할 때 `RightGripLocation/Rotation`을 팔로워 컴포넌트 공간 목표로 사용한다.
3. 목표값에 `LeftGripAlpha`/`RightGripAlpha`를 곱해 손을 놓는 모션에서는 제약을 해제한다. 손목 위치와 회전을 함께 전달한다. 캐릭터의 손바닥 축과 소켓 축 차이는 `CharacterAnimProfile.HandGripCalibration`의 손별 오프셋에서 맞춘다.
4. 상체 Control Rig/FBIK에는 현재 포즈, 손 목표, 팔꿈치 선호 방향, 캐릭터별 Reach 한계를 넣는다. 골반과 발의 안정성을 우선하고 쇄골·가슴·팔에 필요한 만큼만 자유도를 준다. 본 길이 늘리기는 기본값에서 끈다. 치마·망토·머리카락 본은 솔버에서 제외하고 뒤의 RigidBody 패스에서만 처리한다.
5. LOD/품질 정책의 손 IK 허용값과 그립 가중치를 솔버 알파에 반영한다. 가까운 캐릭터의 파지 중에만 Reach를 평가하고, 멀어질 때는 현재 몽타주 포즈로 부드럽게 돌아간다. 리타깃 자체를 끄는 먼 거리 정책과 구분한다.

이 절차는 **에셋 작업으로 남아 있다.** C++이 새 목표값을 제공하고 자동화 테스트가 수학적 불변성을 확인했어도, 기존 ABP가 새 본 공간 값을 사용하기 전에는 실제 손 동작이 개선됐다고 볼 수 없다.

### 에셋 직접 수정 순서와 완료 기준

| 순서 | 에셋 | 작업 | 확인할 결과 |
| --- | --- | --- | --- |
| 1 | `IK_GreatSword_Woman`, `RTG_Greatsword_UE5` | Bip01의 척추·쇄골·상완·하완·손 체인과 리타깃 포즈를 실제 A/T 포즈에 맞춘다. 손목 축과 팔꿈치 굽힘 방향을 함께 확인한다. | 무기를 빼고 리타깃 포즈만 재생해도 어깨·팔의 기본 자세가 자연스럽다. |
| 2 | `Greatsword` 메시, 무기 프레젠테이션 프로필 | 보이는 오른손/등 부착 소켓과 칼의 `WeaponGrip_R`, `WeaponGrip_L` 위치·회전을 맞추고 `VisualDrawnSocketName`, `VisualSheathedSocketName`을 지정한다. | 발도·납도 대기에서 칼이 보이는 손/등을 정확히 따라간다. |
| 3 | `ABP_Greatsword_Woman_RunTIme` | `Retarget Pose From Mesh` 뒤에서 기존 Two Bone IK를 우회하는 시험 경로를 만든다. `bUsePrimaryHandSpaceGrip`이면 현재 오른손 본 포즈에 `LeftGripInPrimaryHandSpace`를 합성하고, 아니면 컴포넌트 공간 그립 목표를 사용한다. | 오른손 주도 구간에 칼과 왼손 목표의 상대 위치가 빠른 스윙에서도 유지된다. |
| 4 | Bip01용 상체 Control Rig, 같은 ABP | 손 위치·회전을 목표로 하는 상체 Reach를 구성한다. 쇄골/가슴은 제한적으로 움직이고 골반·발·의상 보조 본은 고정한다. `LeftGripAlpha`/`RightGripAlpha`가 0일 때 해당 손 제약을 끈다. | 짧은 팔 체형에서도 손목 비틀림과 팔꿈치 팝핑이 줄고, 한 손을 놓는 모션이 유지된다. |
| 5 | 같은 ABP와 피직스 에셋 | Reach 뒤에 의상 `RigidBody`를 유지하고 영향 본을 의상·헤어로 제한한다. 근거리/원거리 품질 전환을 확인한다. | 손 파지 결과를 물리가 덮어쓰지 않고 거리 전환 때 손과 의상이 튀지 않는다. |

1~2단계는 **IK가 꺼진 상태**로 기본 포즈와 칼 부착을 먼저 확인한다. 3~4단계는 발도 대기와 대표 공격 하나로 시작한다. 공격 중 칼의 주도권이 바뀌면 `Weapon Motion` 노티파이의 손별 알파를 비교한다. `SK_Mannequin`의 본 Translation Retargeting 설정을 Bip01에 그대로 복사하는 작업이 아니라, 서로 다른 스켈레톤 사이의 IK Retargeter 체인·포즈와 그 뒤의 손 접촉을 조정하는 작업이다.

## 에디터에서 연결할 항목


1. `Greatsword` 렌더 메시의 실제 오른손 파지점과 등 위치에 소켓을 만든다. 무기 프로필의 `VisualDrawnSocketName`/`VisualSheathedSocketName`에 정확히 연결하고, 원점·회전·손잡이 방향을 검수한다. 소켓 이름이 같으면 프로필의 시각 소켓 필드는 비워도 된다.
2. 무기 메시의 `WeaponGrip_R`/`WeaponGrip_L` 소켓은 실제 손 위치와 손바닥 방향에 맞춘다. 왼손 IK의 팔꿈치 타깃도 Bip01 체형에 맞게 조정한다. 팔 길이 차이와 빠른 칼 모션을 검증할 대표 공격 하나에서 상체 Reach를 먼저 시험한다.
   프로필의 `Source Driven Montage Attacks`가 켜져 있으면 공격 몽타주는 `Weapon Motion` 노티파이 없이도 원본 `DrawnSocketName`을 따라간다. 노티파이는 구간별 칼 오프셋이나 손 가중치를 별도로 작성할 때 사용한다. 새 접촉 전환에서는 시작 포즈만 블렌딩하고, 끝에서는 현재 칼 포즈를 보존해 보이는 손으로 부착한다. 한손 공격은 `Secondary Grip IK Alpha`를 0으로 두고, 오른손을 칼에 맞춰야 하는 구간은 ABP의 오른손 IK 본과 `PalmGrip_R` 보정을 확인한 뒤 `Primary Grip IK Alpha`를 올린다.
3. 팔로워 ABP는 위의 칼 주도권에 따라 손 목표 공간을 선택한 뒤 `Retarget Pose From Mesh → 상체 Reach/손 파지 → 외형 RigidBody → Output Pose` 순서로 둔다. RigidBody가 팔·손의 IK 결과를 다시 움직이지 않도록 물리 본 범위와 알파를 제한한다. 현재 팔로워가 리타깃 자체를 담당하므로 그 단계를 무조건 Post Process ABP로 옮기지 않는다.
4. 같은 스켈레톤을 직접 쓰는 향후 캐릭터는 메인 ABP 공유와 경량 Post Process ABP를 선택할 수 있다. Bip01처럼 다른 스켈레톤인 캐릭터는 현재의 리타깃 팔로워를 유지한다. Post Process ABP, Control Rig, RigidBody 그래프와 피직스 에셋은 실제 에셋에서 별도로 구성해야 한다.
5. 의상의 보조 물리 본이 몸체와 같은 스켈레톤에 포함되면 보이는 몸체의 RigidBody 결과를 Leader Pose로 따른다. 의상에만 있는 독립 물리 본은 단순 Leader Pose만으로 시뮬레이션되지 않으므로 해당 의상에 `Copy Pose From Mesh` 이후 별도 물리 패스가 필요한지 검토한다.

## 성능·네트워크 확인

- 보이는 무기·의상과 IK 목표값은 각 클라이언트에서 계산하는 외형 데이터다. 서버는 장비와 공격 상태를 결정하고, 클라이언트는 그 상태에서 같은 몽타주와 프로필을 사용한다. 손 위치를 프레임마다 복제하지 않는다.
- 전용 서버에서는 무기/의상 시각 컴포넌트 생성 및 리타깃 IK 계산을 건너뛴다. 원격 캐릭터에서는 기존의 표시 예산, 업데이트 빈도와 LOD 정책을 유지하고, 가까운 캐릭터와 먼 캐릭터에서 IK·물리 비용을 프로파일링해 임계값을 조정한다.
- 상체 Reach는 근거리·파지 활성 구간에만 평가한다. 원거리로 내려갈 때 IK·물리를 한 프레임에 끊어서 손이 튀지 않도록 가중치를 감쇄하고, 다시 가까워질 때 물리 상태를 재설정한다. 현재 품질 정책은 원거리 손 IK 비활성화를 이미 수행하지만 이 전환의 시각 검증은 남아 있다.
- 검증 장면은 최소한 발도 대기(한 손/양손), 공격 중 손 놓기와 다시 잡기, 독립 무기 모션의 시작/종료, 납도, 원격 플레이어, 의상 교체, LOD 전환, 몽타주 중단·연계, 텔레포트와 낮은 프레임률을 포함한다. 코드 자동화 테스트는 소켓 소유권·현재 몽타주 키 소비·손 본 공간 좌표를 확인하지만 실제 소켓 위치·리타깃 결과·피직스 충돌은 인게임에서 확인해야 한다.
