# 무기 파지와 손 접촉 통합 가이드

갱신일: 2026-10-03. 몸체 손바닥 기준, Idle 부착, 무기 구동, 공격 구간 양손 파지와 복귀를 한 문서에서 읽는다. 원문 다섯 개의 설정·수식·예외·진단·검증 이력을 아래 상세 절에 보존했다.

## 전체 구조와 담당 데이터

| 담당 | 정의할 내용 |
| --- | --- |
| 몸체 HandGripProfile | 실제 팔 뼈 역할, Palm 소켓, 손목 접촉 오프셋과 굽힘 안정화 |
| 무기 PresentationProfile | WeaponGrip, Idle 부착 모드, 보조 접촉 기능과 상태별 기본 가중치 |
| 공격 정의·Weapon Motion | 무기를 손 또는 소스 포즈에서 구동하는 정책과 선택적 모션 키 |
| Two-Hand Grip IK | 그 공격 안에서 양손으로 잡을 구간과 가중치. 무기 궤적과 독립 |
| 런타임 ABP | 리타깃 → 오른팔 Guided → 왼팔 Guided → 의상 물리 |

### 현재 대검의 확인된 설정

1. 몸체 DA의 PalmGrip_R/L과 Primary/Secondary Arm을 실제 몸체에 맞춘다.
2. Idle 무기 부착은 Primary Grip to Body Palm을 사용한다. 기존 Socket 모드는 호환·특수 부착용이다.
3. 무기 DA의 보조 접촉은 켜고 Drawn/Attack/Sheathed Secondary Alpha는 모두 0으로 둔다.
4. 소스 공격 몽타주의 기존 Two-Hand Grip IK 구간은 Secondary Alpha 1, Override Primary 꺼짐으로 둔다.
5. 오른팔은 Primary Body Profile, 오른손 목표와 Right Grip Alpha를 사용하고 Bone Space Effector는 끈다.
6. 왼팔은 Secondary Body Profile이며 다음 다섯 입력을 모두 연결한다.

| 왼팔 입력 | 변수 |
| --- | --- |
| Alpha | Left Grip Alpha |
| Effector Transform | Left Wrist Target |
| Use Bone Space Effector | Use Primary Hand Space Grip |
| Effector Bone Space Transform | Left Grip In Primary Hand Space |
| Effector Space Bone Name | Primary Hand Bone Name |

두 팔은 Match Wrist Rotation을 켜고 Explicit Elbow Guide는 기본적으로 끈다. 프로필 모드의 뼈·안정화 값은 몸체 DA에서 읽는다. 기존 Grip Location/Rotation의 Scale 1 Make Transform도 같은 손목 목표다. 스테이트만으로 왼손을 제어할 때 Left Hand IK Curve Name은 None이며 개별 Attack/Motion의 보조 기본값도 확인한다.

현재 사용자 플레이 확인은 Idle 오른손 → 파지 구간 양손 → 왼손 해제다. 항상 양손 Idle은 Drawn Secondary 1, 한손 무기는 보조 접촉 기능 끄기로 같은 구조를 사용한다. 도달 범위, 손가락·의상, 원격·다수 플레이어 품질은 아래 한계와 검수 항목을 따른다.

## 상세 내용의 읽는 순서

1. 몸체 보정·솔버·로그를 읽고 Palm 기준을 설정한다.
2. 정상 부착의 변환·스케일·갱신과 실제 수정 근거를 확인한다.
3. 무기 궤적 선택과 노티파이 없는 공격의 정책을 확인한다.
4. 보조 손 접촉 구간·현재 포즈 목표 공간·한손 호환을 설정한다.
5. 공격 종료·연계·취소의 복귀 시계와 수명 경계를 확인한다.

각 검증 기록은 해당 단계의 결과이며 통과 개수와 성능 수치를 합산하지 않는다.

---

<a id="body"></a>
## 몸체 보정·Guided 솔버·팔꿈치 안정화·로그

[보존한 전체 원문](../../Archive/SourceDocuments/2026-10-03/Animation/Authoring/Guided_Hand_Contact.md)

<a id="body-손바닥-접촉-기준과-guided-hand-ik"></a>
원제: **손바닥 접촉 기준과 Guided Hand IK**

갱신일: 2026-10-03. 현재 구현과 에디터 설정의 기준 문서다. 초기 FABRIK 비교와 이후 개선 이력은 [측정 기록](../Diagnostics/Weapon_Grip_Trace_2026-10-02.md)을 함께 읽는다.

<a id="body-1-적용-범위와-현재-그래프"></a>
### 1. 적용 범위와 현재 그래프

공용 마네킹의 Motion Matching, 전투 상체 레이어, 공격 몽타주, 루트 모션과 서버 타격 판정의 소유권을 유지한다. 임포트된 몸체는 런타임 리타깃으로 기본 포즈를 받고, 몸체에 맞춘 손 접촉 보정을 외형 물리 전에 수행한다.

```text
공용 소스 ABP: 이동 모션 매칭 → 전투 레이어·몽타주·AO
  → 보이는 몸체: Retarget Pose From Mesh
  → Local To Component
  → Project J Guided Hand IK (오른팔)
  → Project J Guided Hand IK (왼팔, Secondary Body Profile)
  → 기존 RigidBody 3개
  → Component To Local → 출력
```

왼팔 연결은 에디터에서 추가한다. 기존 왼팔 Two Bone IK를 새 Guided 왼팔 노드와 동시에 직렬 적용하지 않는다. [보조 손 접촉 설정](Weapon_Hand_Contact_System.md#secondary)의 파지 표와 핀 연결을 따른다. 상체 Reach, 추가 손가락 파지, 관절 제한과 자동 비틀림 분배는 구현되지 않았다.

<a id="body-2-소켓의-역할과-idle-조정"></a>
### 2. 소켓의 역할과 Idle 조정

| 기준점 | 소유 데이터 | 현재 역할 |
| --- | --- | --- |
| `WeaponSocket_Visual_R` | 보이는 몸체의 손 부착 소켓 | Socket 모드의 Idle 부착과 Palm 모드의 호환 대체 경로. |
| `PalmGrip_R` / `PalmGrip_L` | 몸체의 실제 손 뼈에 붙인 접촉 소켓 | 손바닥의 접촉 위치와 방향을 정의한다. |
| `WeaponGrip_R` / `WeaponGrip_L` | 무기 메시의 접촉 소켓 | 손이 잡아야 하는 손잡이 위치와 방향을 정의한다. |
| `WeaponSocket_Greatsword_Combat` | 소스 스켈레톤의 발도 소켓 | 소스 주도 공격의 무기 궤적 기준이다. |
| `WeaponSocket_Back` 또는 별도 Visual 등 소켓 | 소스 또는 보이는 몸체 | 납도 부착 기준이다. |

Idle에서는 무기가 보이는 손을 따르므로 오른손 IK를 억제한다. 공격에서는 독립 무기 목표에 손을 맞춘다. 공격 복귀는 몽타주의 남은 가중치에 맞춰 접촉을 해제하고 선택한 정상 부착으로 돌아간다. 붙인 무기를 같은 오른손이 다시 추적하는 순환을 막는다.

**Idle에서 검이 손바닥 위에 뜬다면** 먼저 무기 DA의 Drawn Attachment Mode를 확인한다. `Primary Grip to Body Palm`에서는 Palm 또는 몸체 Hand Offset을 조정하며 Idle·공격에 같은 기준이 적용된다. 기존 Socket 모드에서는 `WeaponSocket_Visual_R`에 메시를 프리뷰로 붙여 해당 소켓을 조정한다. 프리뷰는 무기 원점을 소켓에 붙이며 무기 액터의 추가 메시 상대 변환까지 자동 재현한다고 보장하지 않는다.

공격 중에만 접촉이 어긋나면 Palm 접촉 축과 위치, 몸체 프로필의 Hand Offset, 무기의 Grip을 확인한다. Palm에 검을 프리뷰로 붙이면 검의 원점이 맞춰지므로 실제 WeaponGrip 정렬과 같다고 해석하지 않는다. 손목이 맞아도 손가락이 열려 있으면 손가락 포즈를 별도로 작성해야 한다.

<a id="body-정상-부착의-공통-접촉-기준"></a>
#### 정상 부착의 공통 접촉 기준

무기 DA의 새 선택 모드 `Primary Grip to Body Palm`은 Palm·WeaponGrip·Body Offset과 무기 자식 메시 변환으로 정상 부착도 계산한다. 공격 진입·복귀가 같은 목적지를 사용한다. 기존 에셋 기본값은 Socket이므로 설정을 바꾸기 전에는 기존 작성 방식으로 동작한다.

별도 DA 종류를 추가하지 않았다. Visual 소켓은 호환·특수 부착과 누락 대체용으로 유지한다. 설정 순서, 스케일·움직이는 무기 뼈의 제한, 런타임 갱신은 [Palm 정상 부착](Weapon_Hand_Contact_System.md#mount)을 따른다.

<a id="body-3-솔버와-접촉-변환의-분리"></a>
### 3. 솔버와 접촉 변환의 분리

```text
무기 Grip 목표 → 몸체 접촉 오프셋 → Palm의 손 뼈 상대 변환 역산
  → 보이는 메시 컴포넌트 공간의 손목 목표 → 팔 솔버
```

스케일 1인 접촉의 언리얼 변환 곱셈 순서는 다음과 같다:

```cpp
Wrist = PalmInHand.Inverse() * (BodyOffset * Goal);
PalmInHand * Wrist = BodyOffset * Goal;
```

손바닥 중심에 실제 뼈가 없어도 된다. Palm 소켓의 위치와 회전을 실제 손 뼈 기준으로 작성한다. Mesh Socket이 같은 이름의 Skeleton Socket보다 우선하므로, 같은 스켈레톤을 공유하는 다른 몸체도 각각 보정할 수 있다.

`ResolveHandContact`는 게임 스레드에서 소켓의 작성된 로컬 변환을 읽는다. 이전 프레임의 최종 IK 손 월드 포즈를 보정 기준으로 읽지 않는다. 지정한 손 뼈와 소켓 부모가 다르거나 손 뼈가 없으면 접촉을 무효화한다. 이름을 생략한 Hand는 소켓 부모에서 추론한다. 프로필에 실제 손 역할을 명시해 손가락·보조 뼈를 손목으로 오인하지 않도록 한다.

`MakeWristContactTarget`은 UObject 없는 값 변환이며 월드 공간과 오른손 뼈 공간에서 같은 수식을 사용한다. Guided IK, FABRIK, Two Bone IK, Control Rig에 같은 손목 목표를 공급할 수 있다. 오프셋은 한 번만 적용한다.

스케일이 다른 무기는 접촉 위치·회전을 맞추되 실제 몸체 손뼈의 스케일을 유지한다. 무기 마커 스케일로 손바닥 오프셋이나 손 크기를 늘리지 않는다. 정상 부착과 공격 변환의 왕복은 회전된 자식 메시·균일 스케일을 포함해 검증한다.

기존 `RightGripLocation/Rotation`, `LeftGripLocation/Rotation`도 이미 **손목** 목표다. 새 `RightWristTarget` / `LeftWristTarget`은 동일 위치·회전과 스케일 1을 묶은 컴포넌트 공간 Transform이다. 노드에서 Palm 역변환을 다시 적용하지 않는다. 유효성 핀은 목표 스냅샷 상태이며 실제 평가 여부는 Alpha와 품질 정책으로 결정한다.

보정 스케일은 0이 아니어야 한다. 회전된 비균일 조상 스케일로 생기는 전단까지 정확히 처리하는 접촉 모델은 아니다. 실제 임포트 몸체의 스케일과 변형은 별도 검수한다.

<a id="body-4-몸체-프로필과-에디터-연결"></a>
### 4. 몸체 프로필과 에디터 연결

`UProject_JHandGripProfile`은 몸체의 소켓·해부학적 팔 역할·접촉 오프셋·팔꿈치 안정화를 정의한다. 직업, 무기 클래스, 공격 몽타주를 포함하지 않는다. 같은 몸체를 쓰는 직업들이 공유할 수 있다.

읽기 우선순위:

1. 보이는 `UProject_JRetargetAnimInstance` / 런타임 ABP의 `HandGripProfile`.
2. 캐릭터 애니메이션 프로필의 `HandGripProfile`.
3. 기존 캐릭터 애니메이션 프로필의 인라인 `HandGripCalibration`.
4. 기본 Palm 이름과 항등 오프셋.

현재 만든 에셋은 `DA_HGP_Greatsword`다. 아래는 설정 절차이며, 개별 저장 에셋의 연결 상태는 에디터에서 확인한다.

1. DA의 Primary/Secondary Palm 이름에 `PalmGrip_R` / `PalmGrip_L`을 입력한다.
2. 팔 역할을 다음처럼 설정한다.

| 역할 | Primary Arm | Secondary Arm |
| --- | --- | --- |
| Shoulder | `Bip01-R-UpperArm` | `Bip01-L-UpperArm` |
| Elbow | `Bip01-R-Forearm` | `Bip01-L-Forearm` |
| Hand | `Bip01-R-Hand` | `Bip01-L-Hand` |

Shoulder는 이 솔버에서 상완 루트다. 쇄골을 입력하는 항목이 아니다. Hand를 생략하면 Palm의 부모에서 추론한다. Shoulder와 Elbow는 함께 지정하거나 함께 비운다. 둘 다 비운 방식은 Hand의 직계 부모 두 개를 사용하므로 단순 팔 계층에서만 사용한다.

3. 새 몸체는 `Missing Palm Policy = Disable Contact IK`로 둔다. 기존 에셋 기본값인 `Legacy Wrist Origin (Compatibility)`는 Palm이 없을 때 이전 손목 원점 목표를 유지한다. 명시적으로 잘못된 손 이름이나 소켓 부모 불일치는 호환 모드에서도 허용하지 않는다.
4. `ABP_Greatsword_Woman_RunTIme`의 **클래스 디폴트 → 오른쪽 위 디테일 → Project J → IK → Config → Hand Grip Profile**에 DA를 지정한다. 아래쪽 애님 프리뷰 인스턴스 설정과 구분한다. 검색창에 `Hand Grip`을 입력해도 된다.
5. 오른팔 Guided 노드의 `Arm Definition Source = Primary Body Profile`을 선택한다. Effector에 `RightWristTarget` 또는 기존 Make Transform을 연결하고 Alpha는 `RightGripAlpha`를 유지한다. `Match Wrist Rotation`은 켜고 `Use Explicit Elbow Guide`는 기본적으로 끈다.
6. 컴파일·저장 후 Idle, 평타, 연계, 복귀를 확인한다. 기존 수동 노드는 `Node Settings` 기본값으로 계속 동작한다. 자동 에셋 마이그레이션은 하지 않는다.

왼팔 노드는 `Secondary Body Profile`을 쓴다. 기본 Effector는 컴포넌트 공간이다. `Use Bone Space Effector`, `Effector Bone Space Transform`, `Effector Space Bone Name`을 핀으로 노출해 각각 `Use Primary Hand Space Grip`, `Left Grip In Primary Hand Space`, `Primary Hand Bone Name`에 연결한다. 노드가 현재 입력 포즈의 기준 뼈에서 합성하므로 게임 스레드에서 이전 프레임의 최종 오른손 포즈를 읽어 변환할 필요가 없다. 독립 무기 공격은 bool이 false가 되어 `Left Wrist Target`을 쓴다. 이전 그래프는 새 bool 기본값 false로 동작을 유지한다.

<a id="body-5-팔꿈치-안정화-수치"></a>
### 5. 팔꿈치 안정화 수치

Guided 솔버는 현재 **입력 포즈**의 팔꿈치 굽힘 방향을 목표 손목 방향으로 옮긴다. 거의 펴진 입력 팔은 방향이 불안정하므로 안정적인 방향을 유지하고, 입력이 다시 굽혀지면 현재 방향을 제한된 속도로 따라간다. 이전 최종 IK 팔꿈치 위치를 다음 목표로 쓰지 않는다.

프로필 모드에서는 DA의 해당 Primary/Secondary Bend Stability가 적용된다. Node Settings 모드에서는 노드 자체 설정이 적용된다.

| 설정 | 기본값 | 역할과 조절 영향 |
| --- | --- | --- |
| Enabled | true | 입력 팔이 거의 펴졌을 때 굽힘 방향 안정화. |
| Fallback Pole Axis | 상완 로컬 +Y | 신뢰할 입력/이력이 없는 진입에서 사용. 임포트 뼈 축의 해부학적 정답을 보장하지 않는다. |
| Hold Bend Ratio | 0.15 | 아래에서는 이전 방향을 유지한다. 높이면 안정화가 강해지지만 원래 모션 반영이 늦을 수 있다. |
| Reliable Bend Ratio | 0.25 | 입력 방향을 충분히 신뢰하는 기준. Hold보다 커야 한다. |
| Reacquire Degrees Per Second | 720 | 재획득 중 방향 회전 속도의 상한. 낮추면 부드러워질 수 있으나 반응 지연이 늘어난다. |
| Max History Seconds | 0.25 | 긴 평가 간격에서 오래된 방향을 버린다. 몽타주 블렌드 시간이 아니다. |

Ratio는 입력 팔꿈치가 어깨–손목 직선에서 떨어진 거리 / 이 몸체의 짧은 팔 구간 길이다. 프레임 고정값이나 마네킹 길이를 사용하지 않는다. 재획득 속도에는 입력 방향의 신뢰도도 반영한다. 충분히 신뢰하는 입력으로 재획득이 끝난 뒤에는 계속 저역 필터를 적용하지 않는다.

복귀 때 방향 전환이 너무 빠르면 다른 값은 유지하고 720 → 540, 필요하면 360을 시험한다. 이는 시각 비교용 제안이며 검증된 최적값이 아니다. 팔꿈치가 뒤늦게 따라오면 다시 높인다. Hand Offset은 잡는 위치·각도 보정이며 팔꿈치 부드러움용이 아니다. 프로필의 Elbow Target은 기존 Two Bone IK 그래프용 값이다. Guided 노드에서 사용하려면 Explicit Elbow Guide를 명시적으로 켜고 해당 컴포넌트 공간 값을 연결해야 한다.

<a id="body-6-보조-뼈수명mmorpg-비용"></a>
### 6. 보조 뼈·수명·MMORPG 비용

명시적인 상완·하완·손 역할은 중간 보조 뼈를 건너뛰어 정의할 수 있다. 실제 세 해부학적 관절의 현재 위치에서 두 구간 길이를 구하고 해석적으로 푼다. 중간 보조 뼈는 해당 상완/하완 구간에 대한 입력 상대 포즈를 보존한다. 보조 뼈를 별도 팔꿈치 관절로 취급하지 않으며, 추가 손목 회전을 자동 분배하는 기능은 아니다.

- 게임 스레드에서 프로필과 소켓을 스냅샷으로 읽는다. 워커는 값과 캐시된 Compact Bone 경로만 사용한다.
- 역할 변경이나 뼈/LOD 재캐시 때 체인을 재구성한다. 프레임마다 조상 경로를 다시 검색하지 않는다.
- Alpha는 엔진 skeletal control에서 한 번만 적용한다. 0이면 입력 포즈를 유지한다. 별도 복귀 타이머를 노드에 만들지 않는다.
- 무효 체인, Alpha 0, LOD 우회, 초기화, 뼈 재캐시, dynamics reset/텔레포트, relevance 손실, 긴 평가 간격은 방향 이력을 초기화한다.
- 애니메이션 업데이트 경과 시간을 한 번 누적하고 평가에서 소비한다. 새 업데이트 없는 반복 평가가 재획득을 중복 진행하지 않는다. 허용 범위의 URO 간격은 실제 경과 시간을 사용한다.
- 추가 컴포넌트 틱, 본 변환 RPC, 복제 상태와 전용 서버 외형 계산을 추가하지 않는다. 기존 IK 품질 알파와 표시 예산을 유지한다.
- 저렴한 솔버라는 이유로 군중 성능을 검증했다고 보지 않는다. 디버깅을 끈 상태에서 근거리/원격/저빈도·가시성·장비/몸체 교체를 측정한다.
- 뒤의 RigidBody가 팔·손 보정을 덮어쓰지 않도록 실제 영향 본과 피직스 에셋을 확인한다. 추가 다리·목·몸체 보정은 각 단계의 진단과 관절 규칙을 별도로 설계한다.

도달 범위 밖 목표는 뼈를 늘리지 않고 가능한 경계로 제한한다. 이때 손이 무기에서 떨어질 수 있다. 고정 어깨·고정 뼈 길이와 임의 무기 궤적을 동시에 만족할 수는 없다. 기본 리타깃/공격 궤적을 개선하거나 제한된 쇄골·가슴 Reach와 외형 궤적 적응 정책을 검토한다. 양손 동시 접촉과 관절 제한도 별도 검증 대상이다.

<a id="body-7-진단-로그"></a>
### 7. 진단 로그

PIE에서 명령을 각각 입력한다.

```text
ProjectJ.Animation.GuidedIKTraceHz 60
ProjectJ.Animation.GuidedIKTrace 1
ProjectJ.Presentation.GripTraceHz 60
ProjectJ.Presentation.GripTrace 1
```

Idle → 평타 → Idle, 연계 → Idle을 10~15초 재현한 뒤 끈다.

```text
ProjectJ.Animation.GuidedIKTrace 0
ProjectJ.Presentation.GripTrace 0
```

`Saved/Logs/Project_J.log`를 재시작 전에 보관한다. 여러 캐릭터에서는 `ProjectJ.Animation.GuidedIKTraceActor`, `ProjectJ.Presentation.GripTraceActor`에 이름 일부를 지정한다.

| 기록 | 측정 대상 |
| --- | --- |
| `[GuidedIK][State]` | 노드/액터/메시/월드, PreUpdate 시각·프레임·epoch, 실제 Alpha, LOD, 목표 이동, 실제 구간 길이, 도달 제한, fallback, 방향 안정화 상태. |
| `Pose stage=Input` | 해당 평가의 리타깃 입력 팔. |
| `stage=Solve` | Alpha 적용 전 해석적 결과. |
| `stage=PostAlphaProbe` | 입력 포즈 복사본에 엔진의 `LocalBlendCSBoneTransforms`를 적용한 결과. 뒤쪽 노드의 결과가 아니다. |
| `[GuidedIK][Correction]` | 솔버와 Alpha가 추가한 관절 변위·방향 변화. |
| `[GripTrace][FinalCS]` | 보이는 메시의 최종 팔 포즈. 타깃/평가 프레임을 함께 비교한다. |
| `[GripTrace][WeaponContact]` | 실제 화면 무기 소켓과 Palm의 접촉 오차. IK 목표로 피드백하지 않는다. |
| `Timing/Event` | 무기 소유권·전환, 소스 몽타주 위치/가중치/종료, 목표와 평가 시각. |

`rawGuideCS/guideCS`, `guideHistory`, `guideReacquire`, `guideCorrection`으로 방향 안정화 과정을 구분한다. `InvalidChain`, `AlphaBypass`, `LODBypass`, `SolveFailed`는 계산하지 않은 이유다. 없는 측정값은 -1이다.

WeaponContact의 `socketErr`는 원시 Palm–실제 무기 소켓 거리, `calibratedErr/rotationErr`는 몸체 오프셋 적용 후 실제 접촉 오차, `goalGap`은 실제 접촉과 독립 IK 목표의 차이다. 복귀 중 기존 `palmErr/wristErr`는 독립 소스 목표 기준이므로 화면 무기와 손의 실제 분리 거리라고 단정하지 않는다.

좌표는 보이는 메시 컴포넌트 공간, 거리는 cm, 각도는 도다. 평면 비교는 양쪽 굽힘이 0.5cm를 넘고 0 < 간격 <= 0.25초인 경우만 유효하다. 이는 진단 기준이며 솔버 동작을 바꾸지 않는다. PreUpdate 프레임은 epoch 식별자이며 정확히 소비된 소스 포즈의 증명이 아니다. 동일 액터·메시·월드·시각을 확인한다.

진단은 기본 off, 메타데이터/CVar/필터는 게임 스레드, 값 기록은 워커에서 수행한다. 노드 PreUpdate epoch마다 최대 한 번 기록한다. 포즈 복사와 문자열 비용이 있으므로 성능 측정에는 끈다. Guided 진단 저장과 훅은 Shipping에서 제외하며 전용 서버 캡처도 제외한다.

<a id="body-8-검증-이력과-한계"></a>
### 8. 검증 이력과 한계

2026-10-02 최신 직접 UBT 빌드와 자동화 12개, 저장된 런타임 ABP 범위 컴파일이 성공했다. [Palm 정상 부착 검증](Weapon_Hand_Contact_System.md#mount-검증-결과)에 로그와 범위를 둔다. 아래는 그 이전 몸체 프로필 단계의 검증 이력 6개이며 모두 Success, 프로세스 종료 코드 0:

- `ProjectJ.Animation.HandContactConversion`
- `ProjectJ.Animation.HandContactRig`
- `ProjectJ.Animation.GuidedArmContact`
- `ProjectJ.Animation.GuidedBendStability`
- `ProjectJ.Animation.GuidedIKTraceIsolation`
- `ProjectJ.Presentation.VisualMeshOwnership`

로그: `Saved/Logs/HandContactRig_Automation.log`. 임의 이름/비율·미러 팔, 소켓 위치/회전 역산, 메시 소켓 우선권, 누락 정책, 역할 검증, 보조 뼈 5개 체인, Alpha 1의 엔진 블렌드, 입력 불변성, 프로필/소켓 변경과 LOD endpoint 제거를 확인했다. 변환 테스트는 회전·이동·균일 스케일 1.75인 컴포넌트, 오른손 공간 접촉과 특이 스케일 거부도 포함한다.

방향 회귀 테스트는 캡처된 두 번째 사례의 최대 팔꿈치 이동을 60Hz에서 23.437 → 5.196cm, 30Hz에서 26.253 → 10.221cm로 줄였다. 접촉·구간 길이는 테스트 허용오차 0.001cm 이내다. 전체 몽타주/물리/네트워크를 재현한 결과는 아니다.

노드 그래프 래퍼는 `Project_JAnimationNodes`의 `UncookedOnly`에 둔다. 이전 Editor 모듈 위치에서 저장된 노드는 Class Redirect로 연결한다. 런타임 솔버는 `Project_JCharacter`에 남는다. 이전 Editor 모듈의 숨겨진 구조 경고 원인을 수정한 것이다.

저장된 `ABP_Greatsword_Woman_RunTIme` 하나의 읽기 전용 컴파일은 Blueprint 오류 0, 경고 0, 로드 실패 0, 종료 코드 0이다. 로그: `Saved/Logs/HandContactRig_Blueprint_Scoped.log`. 프로세스 요약의 경고 1개는 설치된 MCP 플러그인의 시작 안내이며 ABP 경고가 아니다. MCP 도구나 에셋 저장은 사용하지 않았다. 초기 commandlet은 절대 allowlist 경로를 프로젝트 경로 뒤에 붙여 거부했고 더 넓은 읽기 전용 컴파일을 수행했다. 종료 후 프로젝트 상대 경로로 범위를 수정해 재검증했다.

추가 숨은 그래프 메시지는 에디터 전용 `ProjectJ.Animation.ValidateBlueprintGraph /Game/Path/Asset.Asset`로 조회할 수 있다. 노드/그래프 이름과 메시지를 기록하며 저장하지 않는다. 연결되지 않은 노드도 포함하므로 실제 컴파일의 정리된 그래프와 동일한 검사는 아니다.

19:31 인게임 캡처에서 두 연계의 늦은 LMB2 팔꿈치 최대 이동이 20.107/23.428 → 5.213/5.216cm로 감소했다. 닿을 수 있는 목표의 접촉도 유지됐다. 이 캡처는 새 Palm 정상 부착 활성화 이전이다. 도달 불가능한 목표와 추가 몸체·무기·원격 군중 검증은 남아 있다. 자세한 수치와 프레임 비교 제한은 [측정 기록](../Diagnostics/Weapon_Grip_Trace_2026-10-02.md)에 둔다.

---

<a id="mount"></a>
## Idle 부착·스케일·보정 갱신·검증 근거

[보존한 전체 원문](../../Archive/SourceDocuments/2026-10-03/Animation/Architecture/Primary_Grip_Attachment.md)

<a id="mount-palm과-weapongrip을-공유하는-정상-부착"></a>
원제: **Palm과 WeaponGrip을 공유하는 정상 부착**

갱신일: 2026-10-02. 기존 소켓 부착과 호환하면서 Idle·손 주도 공격·소스 공격 복귀의 파지 기준을 통일한다. [Guided 손 접촉](Weapon_Hand_Contact_System.md#body), [접촉 복귀](Weapon_Hand_Contact_System.md#recovery)를 함께 읽는다.

<a id="mount-데이터-소유권"></a>
### 데이터 소유권

| 데이터 | 소유하는 값 | 공유 범위 |
| --- | --- | --- |
| Hand Grip Profile | 몸체 Palm 소켓, 실제 팔 역할, 손 접촉 Offset, 굽힘 안정화 | 같은 몸체를 쓰는 직업들이 공유 |
| Weapon Presentation Profile | 무기 Actor, 무기 Grip, 정상 부착 방식, 소스 공격 기본 정책, 납도 소켓 | 무기 외형/규격 |
| Attack Definition / Weapon Motion Notify | 공격별 손 주도·소스 주도, 접촉 Alpha, 구간별 Motion Keys | 공격/구간 |
| 소스 ABP와 전투 레이어 | 이동 Motion Matching, 상체 합성, 몽타주·AO | 공용 애니메이션 소스 |
| 몸체 런타임 ABP | 리타깃, 접촉 솔버, 몸체 추가 뼈의 물리 | 몸체 스켈레톤 |

새 DA 종류를 추가하지 않는다. 인라인 Calibration은 기존 콘텐츠의 호환 경로로 남긴다. 공유 몸체 프로필을 쓰면 인라인 값을 따로 맞출 필요가 없다. 직업별 ABP 이름이 남아 있더라도 몸체 보정을 직업 코드에 하드코딩하지 않는다. 모든 몸체 ABP/물리 에셋 통합은 이번 변경 범위가 아니다.

<a id="mount-선택-가능한-부착-방식"></a>
### 선택 가능한 부착 방식

기존 무기 DA의 **Weapon → Attachment → Drawn Attachment Mode**에서 선택한다.

- **Socket (Compatibility / Custom Mount)**: 기존 Drawn/Visual Drawn 소켓에 무기 Actor 루트를 항등 상대 변환으로 붙인다. 기존 에셋의 기본값이다. 특수 거치, 움직이는 무기 스켈레톤, 기존 작성 방식에 사용한다.
- **Primary Grip to Body Palm**: 몸체의 Primary Palm과 무기의 Primary Grip 및 몸체 Primary Hand Offset으로 루트의 손 뼈 상대 변환을 계산한다. Visual Drawn 소켓은 정상 파지 계산에 쓰지 않고 대체 경로로 남긴다.

납도는 계속 Sheathed/Visual Sheathed 소켓을 쓴다. 소스 공격의 Drawn Socket은 작성된 공격 궤적을 정의하므로 Palm 모드에서도 유지한다. 이름의 R/L 대신 Primary/Secondary 역할과 설정된 소켓·뼈를 읽는다. 공격 IK를 쓰지 않는 직업도 정상 Palm 부착만 사용할 수 있다.

<a id="mount-계산과-소유권-전환"></a>
### 계산과 소유권 전환

```text
Idle / 손 주도 공격:
  몸체 Palm 로컬 + Body Offset + Grip의 Actor 루트 상대 변환
  → 고정 Root-in-Hand 부착 → 입력 손 포즈가 무기를 움직임

소스 주도 공격:
  작성된 소스 소켓 + Motion Keys → 독립 무기 Grip 목표
  → 몸체 Palm 위치·방향에서 실제 손목 목표 역산 → Guided 팔 보정

복귀:
  독립 소스 목표로 손 IK 해제 + 같은 정상 Root-in-Hand 목적지로 무기 블렌드
```

게임 스레드에서 소켓 로컬 데이터와 프로필을 읽는다. 정상 부착을 이전 IK 손의 월드 위치로 보정하지 않는다. StaticMesh 소켓의 작성된 로컬 변환과 Grip 컴포넌트부터 Actor 루트까지의 상대 변환을 합성한다. `GetSocketTransform(RTS_Component)`도 엔진 내부에서 월드 변환을 거쳐 돌아오므로 정상 부착 보정에는 쓰지 않는다. 자식 무기 메시의 위치·회전·균일 스케일을 포함한다. 무기 루트 부착 스케일은 기존 Snap 규칙처럼 1이고, 자식 메시 크기는 유지한다. Grip 마커의 스케일을 상쇄하려고 무기를 줄이지 않는다. 공격 손목 변환도 무기 마커 크기를 손뼈 크기로 전달하지 않고 몸체의 현재 뼈 스케일을 유지한다.

발도, 정상 Refresh, 소스 모션 복귀와 Contact Handoff를 끈 기존 exit blend가 같은 목적지 계산을 사용한다. 복귀 목적지는 항상 항등이라고 가정하지 않는다. Palm 모드의 Primary IK는 정상 부착 중 항상 억제한다. 기존 `Allow Primary IK On Visual Attachment`를 켰더라도 이 모드에는 순환을 허용하지 않는다. 공격·복귀의 독립 접촉은 기존 Alpha/몽타주 가중치 정책을 따른다.

<a id="mount-수명비용대체-경로"></a>
### 수명·비용·대체 경로

- 프로필 우선순위는 보이는 ABP Hand Grip Profile → 캐릭터 애니메이션 프로필의 공유 Profile → 인라인 Calibration → 기본값이다. Idle 부착과 공격 변환이 같은 함수에서 이 우선순위를 읽는다.
- 무기 Grip 소유 컴포넌트 검색은 기존 캐시를 사용한다. 정상 부착은 매 프레임 재계산하지 않는다. Retarget 인스턴스 등록 뒤 다음 애니메이션 조회에서 새 프로필을 반영한다.
- 게임 중 몸체/프로필 또는 무기 자식 변환을 바꾸는 호출자는 Weapon Presentation Component의 **Refresh Attachment Calibration**을 호출한다. Idle은 기존 Actor를 보정하며, 활성 소스 공격은 즉시 손 부착으로 바꾸지 않는다. 종료 경계에서 현재 몸체/프로필을 다시 읽고, 복귀 중 변경은 복귀 완료 뒤 반영한다. 장비 시스템의 기존 Refresh/파괴 수명도 유지한다.
- 자동 Palm 부착에는 실제 Palm 소켓이 필요하다. `Legacy Wrist Origin`은 이전 IK 목표 호환 정책이며 Palm 없는 정상 부착을 만들어내지 않는다.
- 없는 Palm/Grip, 잘못된 Hand 역할, 무기 루트 밖 Grip, 움직이는 SkeletalMesh Grip, 비균일·반전 스케일, 부모를 무시하는 Absolute 변환은 기존 Drawn/Visual Drawn 소켓으로 대체하고 부착 시 경고를 남긴다. 대체 소켓도 없으면 부착 실패를 기록한다. 움직이는 Grip이나 전단을 고정 부착으로 정확히 처리한다고 가정하지 않는다.
- 추가 컴포넌트 틱·RPC·프레임별 변환 복제를 추가하지 않는다. 전용 서버 외형 생성과 기존 타격 판정의 경계를 유지한다. 플레이어 Motion Matching/상체 합성/런타임 리타깃을 교체하지 않는다.
- 원격·URO·LOD·표시 예산·텔레포트의 기존 정책은 유지한다. 군중 성능, 모든 몸체·무기 조합의 시각 품질을 자동화만으로 보장하지 않는다.

<a id="mount-지금-에디터에서-할-일"></a>
### 지금 에디터에서 할 일

1. C++ 빌드 반영 후 에디터를 열고 무기 Presentation DA의 **Drawn Attachment Mode = Primary Grip to Body Palm**을 선택한다.
2. `Primary Grip Socket Name = WeaponGrip_R`를 유지한다. 소스 `Drawn Socket Name = WeaponSocket_Greatsword_Combat`, 납도 소켓과 기존 Visual 소켓도 유지한다.
3. 몸체 런타임 ABP의 클래스 디폴트에 `DA_HGP_Greatsword`가 할당됐는지 확인한다. DA의 Palm `PalmGrip_R/L`, Hand/UpperArm/Forearm은 실제 몸체 이름을 쓴다. Guided 노드 연결은 그대로 유지한다.
4. Idle와 공격의 공통 파지가 뜨면 몸체 **Palm 위치·회전** 또는 **Primary Hand Offset**을 조정한다. Palm에 검 원점을 프리뷰 부착한 모습은 Grip 정렬 결과와 다르므로 PIE에서 최종 접촉을 확인한다. 기존 Visual 소켓을 맞추는 방식은 Socket 모드에서만 정상 부착에 적용된다.
5. Idle → 평타 → Idle, 연계/캔슬, 납도, AO, 몸체·장비 교체를 확인한다. Weapon Motion 스테이트를 추가할 필요는 없다. 구간별 키나 손 해제에 필요한 스테이트는 유지한다.

실제 DA를 자동 저장하거나 일괄 변환하지 않았다. 새 모드는 기존 DA에서 명시적으로 켠다.

<a id="mount-진단과-제한"></a>
### 진단과 제한

기존 `ProjectJ.Presentation.GripTrace 1`을 사용한다. `[GripTrace][Attachment]`의 `context=AttachmentResolved`, `configured=PrimaryGripContact`, `reason=PrimaryGripContact`, `appliedPalmMount=1`과 실제 부착 Hand를 확인한다. `WeaponContact.calibratedErr`는 몸체 Offset 적용 후 실제 접촉 오차다. 복귀 중 독립 목표 오차와 구분한다. 성능 측정 전 진단을 끈다.

이 변경은 Idle·공격의 작성 기준과 복귀 목적지 중복을 해결한다. 팔꿈치 급변 완화는 기존 입력 포즈 굽힘 방향 유지/재획득과 몽타주 가중치 복귀가 담당한다. 팔 길이 밖 목표, 쇄골/가슴 Reach, 양손 동시 제약, 관절 제한과 손가락 포즈는 별도 문제로 남는다.

<a id="mount-새-모드가-그대로-보일-때의-캡처"></a>
#### 새 모드가 그대로 보일 때의 캡처

PIE 콘솔에서 한 줄씩 입력한다. 별도 DA나 그래프 변경은 필요 없다.

```text
ProjectJ.Presentation.GripTraceHz 60
ProjectJ.Presentation.GripTrace 1
ProjectJ.Animation.GuidedIKTraceHz 60
ProjectJ.Animation.GuidedIKTrace 1
```

Idle 3초 → 평타 2회 → Idle 3초 → 평타 2회 → Idle 3초를 재현한다. 공격을 시작하기 전 Idle에서도 애니메이션 조회가 기존 진단 틱을 깨운다. 종료 후 아래 두 명령을 입력하고 에디터 재시작 전에 `Saved/Logs/Project_J.log`를 보관한다.

```text
ProjectJ.Presentation.GripTrace 0
ProjectJ.Animation.GuidedIKTrace 0
```

| 기록/값 | 확인할 내용 |
| --- | --- |
| Attachment: configured, weaponProfile | PIE에서 실제 사용한 무기 DA와 요청 모드. 에디터에서 수정한 DA와 경로가 같은지 확인한다. |
| bodyProfile, calibration, characterProfile | 실제 몸체 DA와 VisualOverride/CharacterSharedProfile/CharacterInline/Defaults 우선순위. |
| reason=PrimaryGripContact | Palm 자동 정렬 계산 성공. 정상 부착에서 appliedPalmMount=1, parentMatch=1이어야 한다. |
| reason=ConfiguredSocket | 기존 Socket 모드 사용. |
| MissingPalm / InvalidPalmHand / InvalidPalmTransform | Palm 누락, 손 역할·부모 불일치, 무효 로컬 변환. |
| MissingWeaponGrip / SkeletalWeaponGrip / GripOutsideWeaponRoot | 무기 Grip 누락 또는 고정 부착 조건 불충족. |
| UnsupportedBodyScale / UnsupportedHandScale / UnsupportedWeaponChildScale | 목적지 몸체·손 또는 작성된 무기 자식의 비균일·반전·특이 스케일. 이전 부착의 Root 월드 스케일은 새 목적지를 거부하는 조건이 아니다. |
| UnsupportedWeaponGripComponent | 고정 StaticMesh 소켓의 작성 데이터를 제공하지 않는 Grip 컴포넌트. 특수 Grip은 Socket 모드 또는 별도 명시적 제공자가 필요하다. |
| SocketMountedWeaponChild / AbsoluteWeaponRoot / AbsoluteWeaponChild / InvalidContactTransform | 자식 소켓 부착, 절대 변환, 접촉 보정 변환 제한. |
| expectedParent/Socket, actualParent/Socket | 계산한 목적지와 실제 부착 비교. 공격·복귀에서는 실제 소유권이 달라지는 것이 정상이다. |
| mountErrorValid, worldErrCm, rotationErrDeg, relativeErrCm | 정상 부착에서만 목적지 오차를 비교한다. 공격·복귀의 -1은 측정 비적용이며 오류 증거가 아니다. |
| AttachmentCalibration | Palm 로컬·Body Offset·Grip-in-Root·예상/실제 Root 상대 변환·몸체 스케일·복귀 목적지. |
| AttachmentScale | 몸체·손·현재 Root 월드 스케일, Grip 자식 상대 스케일 및 작성된 Grip-in-Root 스케일을 소수 9자리로 기록한다. `uniform`은 접촉용 스케일 검사 결과이며 현재 Root 값은 관측용이다. |
| WeaponContact / GuidedIK | 실제 검–손 접촉 오차와 입력/솔버/최종 팔 변화를 구분한다. |

설정 진단은 최대 2Hz와 전환 이벤트에만 기록한다. 실제 포즈/접촉은 기존 요청 빈도를 따른다. 보이는 애님 인스턴스가 없어도 설정 진단을 먼저 남긴다. 로그는 게임 스레드의 관측이며 부착·프로필·IK 목표를 변경하지 않는다. 진단 off에서는 이 목적지 재계산과 문자열 생성을 수행하지 않는다.

추가 진단의 직접 UBT 빌드와 `PrimaryGripAttachment`, `VisualMeshOwnership`, `GuidedIKTraceIsolation` 테스트 3개가 성공했다. `Saved/Logs/AttachmentDiagnostics_Automation.log`에서 자동 정렬 성공과 누락 애님/기본 설정 대체 기록을 확인했다. 테스트는 진단의 관측 불변성과 Idle 진단 틱 시작을 검사하며, 실제 사용자 DA 연결 검증은 PIE 캡처로 수행한다.

<a id="mount-idle-정렬이-적용되지-않은-실제-캡처와-수정"></a>
#### Idle 정렬이 적용되지 않은 실제 캡처와 수정

2026-10-02 23:18 저장된 사용자 로그의 마지막 PIE 구간에서 실제 무기 DA는 `DA_Greatsword_Presentation`, 몸체 DA는 `DA_HGP_Greatsword`, 요청 모드는 `PrimaryGripContact`였다. 그런데 발도 중 `UnsupportedWeaponRootScale` 또는 `UnsupportedHandScale`로 대체되어 `appliedPalmMount=0`, 실제 소켓은 `WeaponSocket_Visual_R`였다. 공격 전·복귀 후 Idle 접촉 오차는 약 2.243cm, 방향 오차는 약 35.714도였다. 기존 목적지에 대한 `worldErrCm=0`은 대체 Visual 부착이 정확하다는 뜻이며 Palm 접촉 성공을 뜻하지 않는다.

이전 소켓에서 상속된 Root 월드 스케일을 새 Palm 목적지의 허용 조건으로 검사하던 오류를 제거했다. Grip-in-Root도 작성된 로컬 데이터만으로 계산한다. 목적지 몸체·손 및 작성 변환의 스케일 검사는 공통 함수로 유지하되, float 포즈가 double로 변환될 때 생기는 수치 차이는 최대 축 대비 0.01%까지 허용한다. 실제 비균일·반전·영 스케일은 계속 거부한다. 기존 로그는 소수 2자리여서 `UnsupportedHandScale`이 미세 오차인지 실제 비균일인지 확정할 수 없으며, 새 `AttachmentScale` 로그로 구분한다.

확인할 성공 조건은 발도 Idle의 `reason=PrimaryGripContact`, `appliedPalmMount=1`, 실제 부착 Hand 및 `WeaponContact.calibratedErr` 감소다. 무기 Actor 원점을 Palm에 놓는 방식이 아니라 **검의 Primary Grip을 몸체 Palm 접촉점에 맞추는 방식**이다. 소켓 이름에만 매달리는 Visual 보정과 구분한다. 새 코드를 적용한 사용자 PIE 검증은 별도로 필요하다.

수정 후 직접 UBT 빌드 성공, 컴파일 경고 없이 종료 코드 0이다. `HandContactConversion`, `PrimaryGripAttachment`, `VisualMeshOwnership` 자동화 3개 모두 Success, 종료 코드 0이다. 로그는 `Saved/Logs/IdleAttachmentScale_Automation.log`. 이전 Root 월드 스케일이 (2,3,4)여도 동일한 Palm 목적지를 계산하고 재부착 후 접촉이 일치함을 검사했다. 수치 오차 수준의 몸체·손 스케일은 허용하고 실제 비균일 스케일은 계속 대체 경로로 보낸다. 기존 접촉·공격·복귀·진단 관측 불변성 검증도 통과했다. 사용자 `Project_J.log`는 덮어쓰지 않았다.

<a id="mount-검증-결과"></a>
### 검증 결과

2026-10-02 직접 `UnrealBuildTool.exe`의 `Project_JEditor Win64 Development` 빌드 성공. 최종 변경 빌드에 컴파일 경고가 없다. 아래 자동화 12개가 모두 Success, 각 프로세스 종료 코드 0이다.

- 접촉/솔버/몸체: `HandContactConversion`, `HandContactRig`, `GuidedArmContact`, `GuidedBendStability`, `GuidedIKTraceIsolation`, `VisualMeshOwnership`, `PrimaryGripAttachment`.
- 장비·기존 Grip·판정: `WeaponPresentationIdentity`, `WeaponPresentationTeardown`, `StableGripTargetsAndAuthoredAlpha`, `CanonicalMeleeTrace`, `CanonicalBladeTrajectory`.

로그는 `Saved/Logs/PrimaryGripAttachment_Automation.log`, `PrimaryGripAttachment_Lifecycle.log`다. 새 부착 테스트는 무기 메시 상대 이동/회전/균일 스케일, 몸체 스케일, 기존 기본값, 순환 억제, 공격·비항등 복귀, 공격 중 보정 변경, 기존 Actor 유지, 몸체 메시 교체, handoff 없는 exit blend, IK 없는 직업, 누락 Palm, 비균일·Absolute 변환 거부와 납도를 검사한다. 테스트용 소켓/메시는 메모리에서만 만들고 프로젝트 에셋을 저장하지 않는다.

저장된 `ABP_Greatsword_Woman_RunTIme` 한 개의 범위 컴파일도 오류 0·ABP 경고 0·로드 실패 0·종료 코드 0이다. 로그는 `Saved/Logs/PrimaryGripAttachment_Blueprint_Scoped.log`. 프로세스 요약의 경고 1개는 설치된 MCP 플러그인 시작 안내이며 MCP 도구 호출은 없다.

이후 사용자가 에디터에서 Palm 부착 설정을 적용하고 Idle 파지가 정상 동작함을 확인했다. 기존 Visual 소켓을 제거하는 구성도 정상 동작한다고 보고했다. 이는 사용자 시각 확인이며 새 PIE 로그의 수치 오차를 자동 재측정한 결과는 아니다. 공격 중 양손 파지 연결과 추가 검수 범위는 [보조 손 접촉](Weapon_Hand_Contact_System.md#secondary)을 따른다.

---

<a id="drive"></a>
## 무기 궤적과 손 파지의 구동 정책

[보존한 전체 원문](../../Archive/SourceDocuments/2026-10-03/Animation/Architecture/Weapon_Grip_Drive_Policy.md)

<a id="drive-무기-궤적과-손-파지의-소유권"></a>
원제: **무기 궤적과 손 파지의 소유권**

갱신일: 2026-10-03. 접촉 복귀는 [Weapon_Contact_Recovery.md](Weapon_Hand_Contact_System.md#recovery), 몸체와 노드는 [Guided_Hand_Contact.md](Weapon_Hand_Contact_System.md#body), 파지 방식과 왼팔 연결은 [Secondary_Hand_Contact.md](Weapon_Hand_Contact_System.md#secondary)를 기준으로 한다.

<a id="drive-런타임-규칙"></a>
### 런타임 규칙

| 상태 | 무기의 움직임 | 오른손 IK | 왼손 IK |
| --- | --- | --- | --- |
| 발도 Idle, VisualHand 공격 | 선택한 정상 부착: Palm 접촉 자동 정렬 또는 기존 Visual 소켓 | Palm 모드는 항상 0. 이미 붙인 검을 같은 손이 다시 추적하지 않음 | WeaponGrip_L 추적 가능 |
| SourceAnimation 공격, 소스 주도 기본 공격 | 소스 `WeaponSocket_Greatsword_Combat` + 선택적 Motion Keys | WeaponGrip_R 접촉 추적 | 해당 공격 가중치로 WeaponGrip_L 추적 |
| 접촉 복귀 | 소스 무기 포즈에서 같은 정상 부착 목적지로 블렌드 | 독립 목표와 남은 몽타주 가중치로 해제 | 기존 발도/양손 정책으로 전환 |
| 납도 | 선택한 등 소켓 | 납도 정책 | 납도 정책 |

공격 AttackTag와 프레젠테이션 상태는 기존 전투 경로에서 전달한다. 클라이언트는 활성 공격 정의와 실제 소스 몽타주에서 무기 소유권을 고른다. 무기·손 변환을 프레임마다 복제하지 않는다. 서버 판정은 기존 소스 메시의 히트 노티파이·검증을 유지한다.

비플레이어는 OwnerPresentationProfile/PresentationCombatStyle 어댑터와 활성 공격 이벤트를 제공해야 한다. 어댑터가 NPC의 모션 매칭·공격 복제를 자동 추가하지 않는다.

<a id="drive-weapon-motion-스테이트-없는-공격"></a>
### Weapon Motion 스테이트 없는 공격

무기 DA에서 Supports Independent Motion과 Source Driven Montage Attacks가 켜져 있으면, `WeaponDrive = WeaponDefault`인 몽타주 공격은 Weapon Motion 노티파이 없이도 소스 소켓 궤적을 사용한다.

- 특정 공격의 손 주도는 AttackDefinition의 `VisualHand`.
- 특정 공격의 소스 주도는 `SourceAnimation`.
- `Override Grip IK`는 공격별 양손 가중치.
- Weapon Motion 노티파이는 구간의 키와 손 가중치로 활성 공격 기본 정책을 일시적으로 덮어쓴다.
- Two-Hand Grip IK 스테이트는 접촉 구간을 별도로 덮어쓴다. 손 주도·소스 주도·독립 Weapon Motion에서 동일하게 적용하며 검 궤적은 변경하지 않는다. 일반 접촉의 작성 커브가 있으면 커브가 최우선이며, 한손 프로필의 Secondary 비활성화는 모든 경로를 막는다.
- Motion Keys가 비어 있으면 추가 오프셋은 항등 변환이다.
- 노티파이가 끝나면 활성 공격 기본 정책으로 복귀한다.

스테이트 생략은 소스 궤적 선택에 대한 규칙이다. 다른 몸체의 손·팔·의상과 접촉까지 자동으로 같은 포즈가 된다는 뜻은 아니다. 구간별 오프셋/손 해제가 필요하면 노티파이를 작성한다.

<a id="drive-전환"></a>
### 전환

소스 진입은 현재 표시된 무기 포즈를 캡처하고 움직이는 소스 소켓으로 블렌드한다. 자동 진입과 노티파이 종료 후 활성 공격 복귀는 `Attack Entry Blend Seconds`를 사용한다.

`Use Contact Handoff`가 켜진 자동 공격 복귀는 `Follow Montage Blend Out`에 따라 outgoing 몽타주의 실제 기여 가중치를 사용한다. 현재 독립 소스 목표를 따라가며 끝에서 정상 부착과 오른손 Alpha 0으로 돌아간다. Palm 모드의 비항등 Root-in-Hand도 같은 목적지를 쓴다. 이전의 즉시 KeepWorld 부착/IK 해제 설명을 대체한다.

노티파이 소유 또는 몽타주 미확보의 대체 복귀는 `Contact Recovery Seconds`의 유한 타이머를 사용한다. 0은 명시적 snap이다. 새 공격·장비/메시 교체·발도/납도·파괴는 수명에 맞게 복귀를 취소/정리한다. 상세 조건은 접촉 복귀 문서를 따른다.

<a id="drive-에디터-설정"></a>
### 에디터 설정

1. 소스 Drawn Socket Name은 소스 스켈레톤의 `WeaponSocket_Greatsword_Combat`, 보이는 Visual Drawn Socket Name은 `WeaponSocket_Visual_R`이다. 서로 다른 메시의 소유권을 구분한다.
2. 검의 `WeaponGrip_R/L`은 손잡이의 접촉점이다. 몸체 손목 위치로 옮겨 문제를 숨기지 않는다. 몸체의 `PalmGrip_R/L`이 실제 손바닥 접촉 위치·방향을 정의한다.
3. 몸체 DA `DA_HGP_Greatsword`를 런타임 ABP Class Defaults → Project J → IK → Config → Hand Grip Profile에 지정한다. Guided 노드 Primary Body Profile을 선택하고 기존 Right Grip 또는 RightWristTarget과 RightGripAlpha를 사용한다. Effector는 이미 변환된 컴포넌트 공간 손목 목표다.
4. 왼팔 Guided 노드는 Secondary Body Profile을 사용한다. 기본 Effector에는 LeftWristTarget을 연결하며, Target Space 핀에는 `bUsePrimaryHandSpaceGrip`, LeftGripInPrimaryHandSpace, PrimaryHandBoneName을 연결한다. true면 이번 평가의 기준 뼈 포즈에서 합성하고 false면 컴포넌트 공간 목표를 쓴다. Alpha는 LeftGripAlpha다.
5. 그래프는 리타깃 → 오른팔 Guided → 왼팔 Guided → 의상/헤어 물리 순서다. 같은 팔에 FABRIK·Two Bone IK·Guided를 중복 적용하지 않는다. 왼팔 새 연결은 에디터에서 추가하고 기존 RigidBody는 유지한다.
6. 현재 대검을 구간 스테이트로 제어할 때 Drawn/Attack Secondary Alpha는 0으로 두고 몽타주의 Two-Hand Grip IK 구간에서 Secondary Alpha 1을 사용한다. 스테이트 없이 소스 공격 전체를 양손으로 잡으려면 Attack 기본값 1을 선택한다. 항상 양손 Idle은 Drawn 1, 한손 무기는 Enable Secondary Grip Contact를 끈다. 몸체 프로필에는 파지 상태가 아닌 해부학적 기준을 둔다.

Idle와 공격을 같은 기준으로 맞추려면 무기 DA의 `Drawn Attachment Mode = Primary Grip to Body Palm`을 선택한다. Palm·몸체 Hand Offset·WeaponGrip으로 정상 부착도 계산한다. Socket 기본값은 기존 Visual 부착을 유지한다. [설정·제한](Weapon_Hand_Contact_System.md#mount)을 따른다.

<a id="drive-검증진단"></a>
### 검증·진단

- 대표 몽타주를 스테이트 없이 실행해 소스 궤적 선택을 확인하고 VisualHand 예외도 확인한다.
- 진입/종료, 캔슬·연계, AO, 낮은 평가 빈도에서 소유권 순환과 접촉 전환을 확인한다.
- 오른팔·왼팔 각각의 접촉과 양손 동시 접촉을 비교한다. 고정 팔 길이 밖 목표와 몸통 Reach는 보조 손 자동 정렬이 해결하는 범위가 아니다.
- 서버 판정·원격 외형과 의상/장비 교체, LOD·가시성·텔레포트를 확인한다.
- 진단은 기본 off. 성능 측정은 로그·포즈 복사 진단을 끈다.

PIE에서 `ProjectJ.Presentation.GripTraceHz 60`, `ProjectJ.Presentation.GripTrace 1`을 입력하고 재현 후 `ProjectJ.Presentation.GripTrace 0`으로 끈다. 필요하면 GuidedIKTrace도 함께 켠다. Event는 소유권 전환, Timing은 몽타주 가중치와 포즈 시각, Arm은 도달 범위, WeaponContact는 실제 표시 검과 Palm 접촉을 기록한다.

유효하지 않은 planeValid=0으로 팔꿈치 flip을 단정하지 않는다. 복귀 중 독립 목표 오차를 실제 손–검 분리로 해석하지 않는다. frame·액터·메시·월드를 일치시켜 입력/솔버/최종 물리 단계를 비교한다. 정확한 필드와 측정 제한은 [측정 기록](../Diagnostics/Weapon_Grip_Trace_2026-10-02.md)에 둔다.

---

<a id="secondary"></a>
## 왼팔·양손 구간·한손 호환과 목표 공간

[보존한 전체 원문](../../Archive/SourceDocuments/2026-10-03/Animation/Authoring/Secondary_Hand_Contact.md)

<a id="secondary-보조-손-접촉과-직업별-파지-설정"></a>
원제: **보조 손 접촉과 직업별 파지 설정**

갱신일: 2026-10-03. 기존 `WeaponGrip_L`, `PalmGrip_L`을 사용한다. 새 소켓이나 별도 왼손 전용 DA가 필요하지 않다. 소스 Motion Matching·상체 레이어·공격 궤적과 기존 오른손 Palm 부착을 유지한다.

<a id="secondary-데이터-소유권"></a>
### 데이터 소유권

- **몸체 Hand Grip DA:** Primary/Secondary 팔 역할, 손바닥 위치·방향, 몸체 보정과 팔꿈치 안정화. 직업 이름으로 노드 안에서 분기하지 않는다.
- **무기 Presentation DA:** Grip 소켓과 보조 접촉 허용 여부, 발도/납도/소스 공격의 기본 Alpha. 같은 메시를 쓰더라도 파지 정책이 다르면 해당 장비 구성에 맞는 프로필을 선택한다.
- **공격 데이터/작성 커브:** 특정 공격에서 손을 잡거나 놓는 가중치. 무기 기본값을 사용하거나 기존 공격별 Override를 사용한다.
- **Guided 노드:** 현재 몸체의 실제 길이와 입력 포즈 굽힘으로 팔을 보정한다. 파지 시작/종료 타이머나 무기 생성, 네트워크 상태를 소유하지 않는다.

Primary/Secondary는 역할이며 반드시 R/L 이름일 필요는 없다. 현재 대검의 Primary는 오른손, Secondary는 왼손이다.

<a id="secondary-현재-대검과-다른-파지-방식"></a>
### 현재 대검과 다른 파지 방식

무기 DA의 Weapon Motion / Motion Presentation에서 설정한다. 새 `Enable Secondary Grip Contact` 기본값은 true로 기존 에셋을 유지한다.

| 파지 방식 | Enable Secondary Grip Contact | Default Drawn Secondary IKAlpha | Default Attack Secondary IKAlpha | Default Sheathed Secondary IKAlpha |
| --- | --- | --- | --- | --- |
| 현재 대검: Idle 오른손 / 지정 공격 구간 양손 | 켬 | 0 | 0 | 0 |
| Idle 오른손 / 소스 공격 전체 양손, 스테이트 생략 | 켬 | 0 | 1 | 0 |
| Idle부터 양손 / 공격 양손 | 켬 | 1 | 1 | 0 |
| 한손 무기: 보조 손 접촉 없음 | 끔 | 0 | 0 | 0 |
| 특정 공격만 양손 | 켬 | 0 | 0 | 0 |

Drawn 기본값은 발도 Idle와 일반 이동에서 적용된다. 공격 기본값은 소스 주도 자동 몽타주 공격의 기본 접촉이다. 기존 Attack Definition의 `Override Grip IK`가 켜져 있으면 그 공격의 `Secondary Grip IKAlpha`를 사용한다. Weapon Motion 스테이트가 남아 있으면 그 구간의 Secondary Alpha가 독립 모션 설정을 덮어쓴다. 노티파이를 모든 공격에 추가할 필요는 없다.

<a id="secondary-현재-대검-기존-two-hand-grip-ik-스테이트로-구간-지정"></a>
#### 현재 대검: 기존 Two-Hand Grip IK 스테이트로 구간 지정

1. 무기 DA의 `Enable Secondary Grip Contact = true`, Drawn/Attack/Sheathed Secondary Alpha는 모두 0으로 설정한다.
2. **소스에서 재생되는 공격 몽타주**의 원하는 파지 구간에 기존 **Two-Hand Grip IK** 노티파이 스테이트를 배치한다. `Secondary IKAlpha = 1`, `Override Primary IK = false`로 둔다. 공격별 Grip Override나 남아 있는 Weapon Motion의 기본 Secondary 값도 구간 밖에서 0을 사용해야 한다.
3. 시작 시 보조 Alpha가 1을 향하고 종료 시 기본 0으로 돌아간다. 오른손은 기존 독립 공격 Alpha 또는 정상 Palm 부착 정책을 유지한다. 스테이트가 검을 다시 부착하거나 Motion Keys·소스 소켓·진입 시간을 변경하지 않는다.
4. 왼팔 Guided 노드와 `WeaponGrip_L` / `PalmGrip_L` 연결은 아래 설정을 유지한다. 새 파지 스테이트를 별도로 만들 필요가 없다.

기존 Two-Hand 스테이트가 소스/독립 Motion에서 무시되던 경로를 보완했다. 접촉 기본값을 먼저 고른 뒤, 발도 중 활성 Two-Hand 구간을 모든 무기 구동 방식에 적용한다. `LeftHandIK` 커브가 존재하면 일반 접촉에서는 계속 최우선 가중치다. 스테이트만으로 제어하려면 해당 커브를 쓰지 않거나 런타임 ABP의 `Left Hand IK Curve Name = None`으로 두어 이 몸체의 커브 조회를 끈다. 같은 손에서 구간 스테이트와 커브를 함께 쓰면 의도한 우선순위인지 확인한다.

겹친 스테이트는 **가장 최근에 시작해 아직 살아 있는 구간**의 가중치를 사용한다. UE의 `FAnimNotifyEventReference.GetNotifyInstanceID()`로 재생별 요청을 구분하며 shared Notify UObject에 실행 상태를 저장하지 않는다. 종료는 자신의 요청만 제거한다. 중첩 구간 종료 후 살아 있는 이전 값 복원, 먼저 시작한 구간이 먼저 끝나는 콤보, 중복 Begin/End 및 초기화 뒤 늦은 End를 처리한다. 장비 외형 파괴·전투 해제·EndPlay의 기존 정리 경계에서 요청을 비운다. 기존 Blueprint Begin/End 호출은 별도의 균형 잡힌 LIFO 요청으로 호환하며, 키가 있는 몽타주 구간을 종료하지 않는다.

소스 애니메이션의 `LeftHandIK` 커브가 존재하면 일반 접촉 구간의 커브 값이 Alpha보다 우선한다. 손을 놓는 특정 구간을 작성하는 데 활용하되 0 커브 때문에 DA의 1이 적용되지 않는 상황을 구분한다. 복귀는 기존 몽타주 가중치 기반 정책을 따른다. `Enable Secondary Grip Contact = false`는 목표 자체를 비활성화하므로 기존 커브, Two Hand 상태, Motion Alpha 또는 복귀에 남은 Alpha가 보조 접촉을 다시 켤 수 없다. 장비 교체와 누락 Grip에서도 유효성과 Alpha를 초기화한다.

<a id="secondary-에디터에서-왼팔-연결"></a>
### 에디터에서 왼팔 연결

`ABP_Greatsword_Woman_RunTIme`의 AnimGraph에서 오른팔 노드 다음, RigidBody 이전에 **Project J Guided Hand IK**를 하나 추가한다.

```text
Retarget Pose From Mesh → Local To Component
→ Guided 오른팔 → Guided 왼팔
→ 기존 RigidBody → Component To Local → Output
```

왼팔 상세 설정:

- `Arm Definition Source = Secondary Body Profile`.
- `Match Wrist Rotation = true`.
- `Use Explicit Elbow Guide = false`로 시작한다. DA의 Secondary Bend Stability를 사용한다.
- 몸체 `DA_HGP_Greatsword`의 Secondary Palm은 `PalmGrip_L`, Shoulder/Elbow/Hand는 `Bip01-L-UpperArm`, `Bip01-L-Forearm`, `Bip01-L-Hand`다. 소켓의 실제 부모가 지정한 Hand와 일치해야 한다.
- 검의 무기 DA `Secondary Grip Socket Name = WeaponGrip_L`을 유지한다.

Contact / Target Space의 세 설정을 핀으로 노출한다. 입력 연결은 다음과 같다.

| 왼팔 노드 입력 | ABP 변수 |
| --- | --- |
| Alpha | `Left Grip Alpha` |
| Effector Transform | `Left Wrist Target` |
| Use Bone Space Effector | `Use Primary Hand Space Grip` (`bUsePrimaryHandSpaceGrip`) |
| Effector Bone Space Transform | `Left Grip In Primary Hand Space` |
| Effector Space Bone Name | `Primary Hand Bone Name` |

컴포넌트 공간 목표는 `Left Wrist Target`을 직접 연결하는 방식을 권장한다. 기존 `Left Grip Location`과 `Left Grip Rotation`을 Scale 1의 Make Transform으로 합성해도 현재 코드에서는 같은 값이다. 뼈 공간 목표는 `Left Grip In Primary Hand Space` Transform을 그대로 연결하며 Palm 오프셋을 다시 적용하지 않는다. 왼팔도 FABRIK/Two Bone IK 대신 같은 Guided 솔버를 쓰므로 기존 팔꿈치 안정화를 공유한다. 이전 왼팔 Two Bone IK는 포즈 경로에서 분리한 채 둔다. 오른팔 설정을 왼팔에 복사해 Primary 역할을 그대로 두지 않는다.

컴파일·저장 후 현재 대검은 Idle에서 왼팔 Alpha가 0이며, 공격의 Two-Hand Grip IK 구간에서 1을 향해 진입하고 구간 종료·복귀 시 0으로 돌아간다. 항상 양손 파지는 Drawn 기본값 1로 설정하며 같은 그래프를 사용한다. 한손 무기는 보조 접촉 옵션을 끄면 왼팔 노드가 Alpha 0으로 우회한다.

<a id="secondary-오른팔-설정-확인"></a>
#### 오른팔 설정 확인

오른팔은 `Arm Definition Source = Primary Body Profile`, `Alpha = Right Grip Alpha`, `Match Wrist Rotation = true`, `Use Bone Space Effector = false`, `Use Explicit Elbow Guide = false`로 둔다. Effector Transform은 `Right Wrist Target`을 직접 연결하거나 기존 `Right Grip Location / Rotation → Make Transform` 연결을 유지한다. 왼팔의 Primary 손 기준 입력 세 개를 오른팔에 복사하지 않는다.

Primary/Secondary Body Profile을 선택하면 각 팔의 뼈 역할과 굽힘 안정화는 몸체 DA에서 읽는다. 노드 상세의 Forearm/Upper Arm이 None이어도 DA의 해당 Arm이 올바르게 설정되어 있으면 된다. 오른팔은 무기 접촉 목표를 풀고 왼팔은 상태에 따라 독립 컴포넌트 목표 또는 같은 평가의 Primary 손 기준 목표를 사용한다.

<a id="secondary-목표-공간과-평가-순서"></a>
### 목표 공간과 평가 순서

1. **소스 주도 공격:** 검이 몸체 손과 독립적으로 움직인다. `Use Primary Hand Space Grip = false`; 두 팔은 각 Grip에서 역산한 컴포넌트 공간 손목 목표를 사용한다.
2. **손 주도 상태 및 부착 복귀:** 검이 보이는 Primary 손에 붙어 있다. 보조 목표는 Primary 손 기준 값으로 전달하고, 왼팔 노드가 **이번 평가에서 오른팔 노드가 Alpha까지 반영한 입력 포즈**를 읽어 컴포넌트 공간으로 합성한다. 이전 최종 손 위치의 월드 조회를 목표로 사용하지 않는다.
3. **현재 대검 Idle:** 보조 Alpha가 0이므로 목표가 있더라도 보조 팔을 풀지 않는다. 항상 양손 Idle에서는 같은 뼈 공간 경로로 움직이는 Primary 손을 따라간다.

활성 뼈 공간의 기준 뼈가 누락되거나 LOD로 제거되면 잘못된 월드 목표로 대체하지 않고 해당 노드를 우회한다. 기준 뼈가 자신이 푸는 팔의 서브트리 안에 있으면 자기 추적 구성으로 거부한다. 뼈 공간↔컴포넌트 공간 또는 기준 뼈 변경 시 굽힘 이력을 초기화한다. 대상 뼈는 변경/LOD 캐시 단계에서 해석하며 워커는 포즈 값과 Compact Bone 인덱스를 사용한다.

<a id="secondary-제한과-mmorpg-검증"></a>
### 제한과 MMORPG 검증

- 추가 본 변환 RPC, 복제 상태, 상시 컴포넌트 틱 또는 게임 스레드 포즈 쓰기를 추가하지 않는다. 기존 클라이언트 외형/전용 서버/품질 등급 경계를 유지한다.
- 손을 무기에 맞추는 두 팔 보정이며, 쇄골·몸통 Reach를 포함한 양손 동시 전신 제약 솔버는 아니다. 길이가 다른 몸체에서 도달 범위 밖 목표는 뼈를 늘리지 않고 제한한다. 접촉을 유지하려면 그 조합의 리타깃/공격 궤적 또는 별도 Reach 정책을 검토한다.
- 손가락 파지, 관절 제한, 비틀림 분배와 의상 관통은 별도 검수 항목이다. RigidBody가 팔·손을 다시 움직이지 않도록 영향 본을 확인한다.
- Idle 오른손만, 공격 양손, 항상 양손, 한손 장비로 교체, 연계·취소·피격·납도, 원격/URO/LOD와 몸체 교체를 확인한다.
- 기존 GripTrace의 `[WeaponContact] role=Secondary`와 GuidedIK의 Secondary Hand 노드 기록을 함께 본다. Guided 진단의 목표는 공간 변환 후 컴포넌트 공간 값이다.

개별 ABP·DA는 사용자 에디터에서 연결/저장한다. 코드에서 에셋을 자동 변경하지 않는다.

<a id="secondary-검증-결과"></a>
### 검증 결과

2026-10-03 직접 `UnrealBuildTool.exe`의 `Project_JEditor Win64 Development` 빌드 성공, 종료 코드 0이다. `SecondaryHandContact`, `HandContactRig`, `HandContactConversion`, `VisualMeshOwnership`, `PrimaryGripAttachment`, `GuidedBendStability`, `GuidedIKTraceIsolation` 자동화 7개 모두 Success, 종료 코드 0이다. 로그는 `Saved/Logs/SecondaryHandContact_Automation.log`다.

새 테스트는 서로 다른 두 팔 길이와 임의 뼈 이름, 손바닥 오프셋을 사용한다. 오른팔 Alpha 0.55의 엔진 포즈 블렌드 뒤 왼팔이 같은 평가의 목표 위치·회전을 잡는지, 후속 평가의 기준 포즈 변경, 독립 목표로의 공간 전환, 누락/자기 팔 기준 뼈/기준 뼈 LOD 제거를 검사한다. 실제 소스/임포트 몸체 fixture에서는 공격만 양손, 복귀 중 보조 접촉 비활성화, 노티파이와 Motion Alpha의 강제 활성화 차단, 오래된 레거시 추적 경로 차단 및 양손 정책 재활성화를 검사한다.

저장된 `ABP_Greatsword_Woman_RunTIme` 한 개의 범위 컴파일은 오류 0·ABP 경고 0·로드 실패 0·종료 코드 0이다. 로그는 `Saved/Logs/SecondaryHandContact_Blueprint_Scoped.log`. 설치된 MCP 플러그인의 시작 안내 경고는 ABP 경고와 구분하며 MCP 도구는 호출하지 않았다. 이 범위 컴파일은 사용자 최종 노드 연결 이전의 저장본 검증이다.

기존 Two-Hand Grip IK 구간 통합 후 직접 UBT 빌드는 26.77초에 성공했다. `SecondaryHandContact`, `TwoHandIKTransitionAndCurve`, `PrimaryGripAttachment`, `StableGripTargetsAndAuthoredAlpha`, `VisualMeshOwnership` 5개는 `Saved/Logs/TwoHandGripWindows_Automation.log`에서, `ProjectJ.NPCGameplay.WeaponPresentationTeardown`은 `Saved/Logs/TwoHandGripWindows_Teardown.log`에서 Success다. 두 실행 모두 종료 코드 0으로, 이번 통합 관련 검증은 총 6개다.

추가 검증은 실제 Notify Begin/End의 재생 ID, 중첩 구간 값 복원, 순서가 다른 종료, 중복 호출, 초기화 뒤 늦은 종료, 레거시 호출과의 분리를 포함한다. 소스 자동 공격과 명시적 독립 Motion 모두에서 보조 파지 구간이 적용되며 무기 월드 변환·부착 부모·구동 주체는 바뀌지 않는지 확인했다.

2026-10-03 사용자가 에디터에서 오른팔 Primary / 왼팔 Secondary Body Profile, 왼팔의 다섯 입력 및 오른팔의 컴포넌트 목표 연결을 적용한 뒤 정상 동작을 확인했다. 이는 사용자 플레이 확인이며 최종 연결본을 도구로 다시 컴파일하거나 접촉 오차를 수치 측정한 결과는 아니다. 연속 공격·공격 취소·피격·납도, 다른 몸체·무기 조합, 원격/URO/LOD와 다수 플레이어 성능 검수는 후속 확인 항목이다.

---

<a id="recovery"></a>
## 몽타주 복귀·대체 시계·수명과 네트워크

[보존한 전체 원문](../../Archive/SourceDocuments/2026-10-03/Animation/Architecture/Weapon_Contact_Recovery.md)

<a id="recovery-무기-접촉-복귀와-몸체-프로필"></a>
원제: **무기 접촉 복귀와 몸체 프로필**

갱신일: 2026-10-02. 손 솔버와 소켓 작성은 [Guided 손 접촉](Weapon_Hand_Contact_System.md#body), 공격별 무기 궤적 정책은 [무기 소유권](Weapon_Hand_Contact_System.md#drive)을 함께 읽는다.

<a id="recovery-소유권과-적용-범위"></a>
### 소유권과 적용 범위

휴머노이드 마스터, 이동 Motion Matching, 전투 레이어, 소스 몽타주, 게임플레이 루트 모션, 서버 타격 검증은 기존 소유권을 유지한다. 여기서는 소스 주도 무기 모션 이후의 외형 접촉과 부착 복귀를 처리한다. 접촉의 Palm→손목 변환은 솔버와 독립적이며 Guided IK의 몸체 프로필 모드와 기존 수동 그래프가 같은 목표를 사용한다.

| 상태 | 무기의 변환 | 오른손 목표 | 종료 |
| --- | --- | --- | --- |
| 소스 모션 | 작성된 소스 소켓 + 선택적 Motion Keys | 무기 접촉을 Palm→손목 변환 | 복귀 또는 다음 소스 구간 |
| 자동 몽타주 복귀 | 현재 소스 포즈에서 보이는 손 부착으로 블렌드 | 현재 독립 소스 접촉 | 소스 몽타주 가중치 0에서 정상 부착 |
| 노티파이/타이머 대체 복귀 | 캡처한 상대 오프셋에서 선택한 정상 부착 변환으로 블렌드 | 캡처한 보이는 메시 컴포넌트 공간 접촉 | 정해진 종료 시각에 정상 부착 |
| 손 부착 | Palm·Grip으로 계산한 Root-in-Hand 또는 기존 Visual 소켓 | 자기 추적 방지를 위해 오른손 IK 억제 | 발도/납도 또는 새 소스 모션 |

무기 DA의 `Primary Grip to Body Palm` 모드에서는 Palm·WeaponGrip·Body Offset으로 Idle 부착을 역산한다. 복귀도 같은 비항등 부착 변환을 목적지로 사용한다. Socket 기본값과 Visual 소켓은 호환/특수 부착을 유지한다. [Palm 정상 부착](Weapon_Hand_Contact_System.md#mount)에 선택 절차와 스케일·수명 제한을 둔다.

<a id="recovery-몽타주-가중치로-복귀"></a>
### 몽타주 가중치로 복귀

`bFollowMontageBlendOut`은 기본 true다. 자동 공격 중 소스 몽타주 instance ID를 기록한다. `Montage_IsActive`는 블렌드 아웃이 시작될 때 false가 될 수 있지만 현재 포즈는 아직 기여한다. 복귀는 별도 0.12초 타이머 대신 해당 instance의 실제 남은 가중치 / 전환 시점 가중치를 따라간다.

손목 목표는 공격 끝의 위치를 고정하지 않고 현재 독립 소스 소켓을 따라간다. 목표와 무기 소스 변환은 최종 오른손 IK 포즈로부터 독립적이다. 늦은 컴포넌트 틱에서 평가된 Visual 소켓을 기준으로 부착 블렌드를 다시 계산한다. 몽타주 가중치 위에 두 번째 easing을 적용하지 않는다.

일시 정지 중 몽타주 시간이 진행되지 않으면 이 복귀도 진행되지 않는다. 소스 instance/메시/애님 인스턴스가 사라지면 정상 부착을 완료한다. 새 공격은 기존 복귀를 취소한다. 최종 소스 가중치가 0이면 정상 부착으로 정확히 돌아간다.

<a id="recovery-타이머-대체-경로"></a>
### 타이머 대체 경로

`ContactRecoverySeconds` 기본값은 0.12초다. 노티파이 소유 모션, 몽타주 따라가기 비활성, 종료 시 기여하는 몽타주가 없는 경우에 쓴다. 0은 정상 소켓으로 즉시 부착하는 명시적 선택이다.

대체 경로의 접촉은 컴포넌트 이동을 따르지만, 이미 IK로 움직인 손이나 그 손에 붙은 검에서 다시 만들지 않는다. 같은 smoothstep 시계로 부착 오프셋과 오른손 IK를 감쇄한다. 복귀 시작에 표시 중이던 알파를 사용하고 별도 IK 보간을 중복 적용하지 않는다. 복귀 가중치는 소스 몽타주 IK 커브보다 우선한다. 왼손은 현재 부착 무기와 기존 발도/양손 정책으로 전환한다.

타이머는 평가 횟수가 아니라 월드 경과 시간을 사용한다. 애니메이션 조회와 늦은 틱이 시간을 두 번 진행하지 않는다. URO로 업데이트를 건너뛰면 종료 시각 이후 다음 갱신에서 완료한다. 낮은 평가 빈도의 부드러움까지 보장하는 것은 아니므로 원격·저빈도 시각 검증이 필요하다.

<a id="recovery-수명과-네트워크"></a>
### 수명과 네트워크

새 소스 모션은 현재 표시된 검 포즈를 캡처한 뒤 이전 복귀를 취소한다. 발도/납도 명시적 부착, 장비 교체, 파괴와 EndPlay는 복귀 상태를 정리한다. 소스 모션을 계속 쓰는 연계는 공격 사이에 손 부착으로 돌아가지 않는다.

추가 RPC나 프레임별 무기/손 변환 복제는 없다. 서버 전투 판정은 기존 경로를 유지한다. Idle에서 복귀와 공격 추적이 끝나면 진단이 켜져 있지 않은 한 프레젠테이션 틱을 멈춘다. 전용 서버는 외형 프레젠테이션을 만들지 않는다.

<a id="recovery-몸체-설정과-직업의-분리"></a>
### 몸체 설정과 직업의 분리

몸체 프로필의 필드·해석 우선순위·팔 역할·노드 연결은 [몸체 보정 상세](#body-4-몸체-프로필과-에디터-연결)를 기준으로 한다. 이 절의 동일 계약은 그곳에 통합했고 [이전 전체 원문](../../Archive/SourceDocuments/2026-10-03/Animation/Architecture/Weapon_Contact_Recovery.md#몸체-설정과-직업의-분리)에 보존했다.

비플레이어 소유자는 무기 컴포넌트의 `OwnerPresentationProfile`, `PresentationCombatStyle`을 제공할 수 있다. 클래스 전투 스타일은 대체 경로다. 호출자가 전투 진입/종료와 활성 공격 이벤트를 전달해야 한다. 플레이어 설정은 계속 장비 시스템이 소유하고 NPC 어댑터가 덮어쓰지 않는다. 이 어댑터가 NPC의 Motion Matching이나 새 공격 복제를 자동 구성하지는 않는다.

<a id="recovery-에디터-점검"></a>
### 에디터 점검

1. 빌드 후 재시작·런타임 ABP 컴파일. 기존 Guided 연결과 Right Grip 핀을 유지할 수 있다. `RightWristTarget`은 Make Transform 없이 같은 손목 목표를 제공한다.
2. 일반 자동 몽타주 공격은 `Follow Montage Blend Out`을 켠다. 이때 `Contact Recovery Seconds`를 늘려도 몽타주 가중치 복귀는 튜닝되지 않는다.
3. 몸체 DA를 런타임 ABP 클래스 디폴트의 `Project J → IK → Config → Hand Grip Profile`에 지정한다. 현재 `DA_HGP_Greatsword`의 Palm은 `PalmGrip_R/L`, 팔 역할은 실제 Bip01 UpperArm/Forearm/Hand다.
4. 왼손은 연결 여부부터 확인한다. 소스 주도 구간에는 컴포넌트 공간 목표를, 오른손 주도 구간에는 `bUsePrimaryHandSpaceGrip`가 true일 때 현재 오른손 포즈 기준의 목표를 사용한다. 한 공간만 계속 사용하는 그래프는 두 소유권 모드를 모두 표현하지 못한다.
5. RigidBody 3개의 피직스 에셋과 영향 본을 확인한다. 스크린샷만으로 통합 가능하다고 판단하지 않는다. 의상 물리가 팔·손 접촉을 덮어쓰지 않아야 한다.
6. 평타, 연계, 캔슬, 납도, 무기/몸체 교체, 원격, LOD·가시성·저빈도와 텔레포트를 검증한다. 성능 측정 시 진단을 끈다.

<a id="recovery-진단-해석"></a>
### 진단 해석

```text
ProjectJ.Presentation.GripTraceHz 60
ProjectJ.Presentation.GripTrace 1
ProjectJ.Presentation.GripTrace 0
```

세 명령은 순서대로 사용하며 마지막은 캡처 종료다. 기본 필터는 로컬 플레이어다. 원격 재현은 `ProjectJ.Presentation.GripTraceActor 이름일부`로 지정한다.

- Sample: 접촉 목표 오차, 팔꿈치 각도, 검/팔꿈치 월드 속도, 부착 부모와 상대 오프셋.
- Timing: 소스/보이는 메시 포즈 프레임, 목표/평가 시점, 컴포넌트/액터 위치, 소스 소켓, 몽타주 instance·위치·가중치·stopped와 `recoveryClock`.
- Arm: 실제 팔 길이, 어깨–손목 목표 거리, reach ratio/excess, 손목 회전 오차.
- ContactRecoveryBegin/End: 전환 경계.
- WeaponContact: 화면에 표시되는 무기 소켓과 Palm의 실제 접촉. 몸체 오프셋 적용 후 오차와 독립 목표와의 차이를 구분한다.

`reachRatio > 1`은 현재 측정한 체인의 도달 범위 밖 목표다. bind pose 길이 측정은 아니며 stretching 설정과 함께 해석한다. 거의 펴진 팔의 `planeValid=0`은 방향 뒤집힘 증거가 아니다. 같은 프레임 번호도 내부에서 소비한 포즈 epoch를 완전히 증명하지 않으며 URO/보간/저품질 갱신의 시각을 확인해야 한다.

<a id="recovery-검증과-남은-작업"></a>
### 검증과 남은 작업

자동화는 독립 목표, 변환, 중단/0초/경과 시간 전환, 프로필 재사용과 기존 프레젠테이션 수명을 검증한다. 최신 직접 UBT 빌드와 자동화 12개, 저장된 런타임 ABP 한 개의 범위 컴파일은 성공했다. 비항등 Palm 부착과 몸체 교체를 포함한 상세 로그는 [Palm 정상 부착 검증](Weapon_Hand_Contact_System.md#mount-검증-결과)에 둔다.

실제 최종 포즈·관절 해부학·루트 모션/카메라·지연·의상·다른 직업/무기·군중 비용은 별도 시각/성능 검증 대상이다. 팔 길이 밖의 접촉은 리타깃/궤적 적응 또는 제한된 상체 Reach 정책이 필요하다. Idle 자동 접촉 부착은 선택 모드로 구현했고 에디터에서 무기 DA에 활성화한다.
