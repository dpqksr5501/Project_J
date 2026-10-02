# 손바닥 접촉 기준과 Guided Hand IK

갱신일: 2026-10-02. 현재 구현과 에디터 설정의 기준 문서다. 초기 FABRIK 비교와 이후 개선 이력은 [측정 기록](Weapon_Grip_Trace_2026-10-02.md)을 함께 읽는다.

## 1. 적용 범위와 현재 그래프

공용 마네킹의 Motion Matching, 전투 상체 레이어, 공격 몽타주, 루트 모션과 서버 타격 판정의 소유권을 유지한다. 임포트된 몸체는 런타임 리타깃으로 기본 포즈를 받고, 몸체에 맞춘 손 접촉 보정을 외형 물리 전에 수행한다.

```text
공용 소스 ABP: 이동 모션 매칭 → 전투 레이어·몽타주·AO
  → 보이는 몸체: Retarget Pose From Mesh
  → Local To Component
  → Project J Guided Hand IK (오른팔)
  → 기존 RigidBody 3개
  → Component To Local → 출력
```

현재 왼팔 Two Bone IK는 연결되지 않은 상태다. 양손 정책을 검증하기 전 일괄 활성화하지 않는다. 상체 Reach, 추가 손가락 파지, 관절 제한과 자동 비틀림 분배는 구현되지 않았다.

## 2. 소켓의 역할과 Idle 조정

| 기준점 | 소유 데이터 | 현재 역할 |
| --- | --- | --- |
| `WeaponSocket_Visual_R` | 보이는 몸체의 손 부착 소켓 | Idle과 손 주도 구간에서 무기 액터를 배치한다. |
| `PalmGrip_R` / `PalmGrip_L` | 몸체의 실제 손 뼈에 붙인 접촉 소켓 | 손바닥의 접촉 위치와 방향을 정의한다. |
| `WeaponGrip_R` / `WeaponGrip_L` | 무기 메시의 접촉 소켓 | 손이 잡아야 하는 손잡이 위치와 방향을 정의한다. |
| `WeaponSocket_Greatsword_Combat` | 소스 스켈레톤의 발도 소켓 | 소스 주도 공격의 무기 궤적 기준이다. |
| `WeaponSocket_Back` 또는 별도 Visual 등 소켓 | 소스 또는 보이는 몸체 | 납도 부착 기준이다. |

Idle에서는 무기가 보이는 손을 따르므로 오른손 IK를 기본적으로 0으로 만든다. 공격에서는 독립 무기 목표에 손을 맞춘다. 공격 복귀는 몽타주의 남은 가중치에 맞춰 접촉을 해제하고 Visual 부착으로 돌아간다. 붙인 무기를 같은 오른손이 다시 추적하는 순환을 막는다.

**Idle에서 검이 손바닥 위에 뜬다면** 보이는 스켈레탈 메시에서 `WeaponSocket_Visual_R`에 `SM_Sword`를 프리뷰로 추가하고 해당 소켓의 위치·회전을 조절한다. 실제 플레이의 Idle과 공격 복귀까지 확인한다. 프리뷰는 무기 원점을 소켓에 붙이며 무기 액터의 추가 메시 상대 변환까지 자동 재현한다고 보장하지 않는다.

공격 중에만 접촉이 어긋나면 Palm 접촉 축과 위치, 몸체 프로필의 Hand Offset, 무기의 Grip을 확인한다. Palm에 검을 프리뷰로 붙이면 검의 원점이 맞춰지므로 실제 WeaponGrip 정렬과 같다고 해석하지 않는다. 손목이 맞아도 손가락이 열려 있으면 손가락 포즈를 별도로 작성해야 한다.

### 현재 남은 수동 설정 중복

실행 소유권은 분리되어 있지만 Idle의 Visual 부착과 공격의 Palm 접촉은 현재 따로 작성한다. 한쪽만 바꾸면 두 상태의 파지가 달라질 수 있다.

후속 개선 후보는 Palm과 WeaponGrip의 공통 접촉 기준으로 **Idle 무기 부착 오프셋도 계산**하는 것이다. 무기 액터 루트와 메시의 상대 변환, Body Offset, 납도와 특수 부착, 기존 소켓 호환을 고려해야 한다. 이 자동 부착은 아직 구현하지 않았다. 현재 Idle·복귀가 Visual 소켓을 사용하므로 소켓을 먼저 삭제하지 않는다.

## 3. 솔버와 접촉 변환의 분리

```text
무기 Grip 목표 → 몸체 접촉 오프셋 → Palm의 손 뼈 상대 변환 역산
  → 보이는 메시 컴포넌트 공간의 손목 목표 → 팔 솔버
```

언리얼 변환 곱셈 순서에서:

```cpp
Wrist = PalmInHand.Inverse() * (BodyOffset * Goal);
PalmInHand * Wrist = BodyOffset * Goal;
```

손바닥 중심에 실제 뼈가 없어도 된다. Palm 소켓의 위치와 회전을 실제 손 뼈 기준으로 작성한다. Mesh Socket이 같은 이름의 Skeleton Socket보다 우선하므로, 같은 스켈레톤을 공유하는 다른 몸체도 각각 보정할 수 있다.

`ResolveHandContact`는 게임 스레드에서 소켓의 작성된 로컬 변환을 읽는다. 이전 프레임의 최종 IK 손 월드 포즈를 보정 기준으로 읽지 않는다. 지정한 손 뼈와 소켓 부모가 다르거나 손 뼈가 없으면 접촉을 무효화한다. 이름을 생략한 Hand는 소켓 부모에서 추론한다. 프로필에 실제 손 역할을 명시해 손가락·보조 뼈를 손목으로 오인하지 않도록 한다.

`MakeWristContactTarget`은 UObject 없는 값 변환이며 월드 공간과 오른손 뼈 공간에서 같은 수식을 사용한다. Guided IK, FABRIK, Two Bone IK, Control Rig에 같은 손목 목표를 공급할 수 있다. 오프셋은 한 번만 적용한다.

기존 `RightGripLocation/Rotation`, `LeftGripLocation/Rotation`도 이미 **손목** 목표다. 새 `RightWristTarget` / `LeftWristTarget`은 동일 위치·회전과 스케일 1을 묶은 컴포넌트 공간 Transform이다. 노드에서 Palm 역변환을 다시 적용하지 않는다. 유효성 핀은 목표 스냅샷 상태이며 실제 평가 여부는 Alpha와 품질 정책으로 결정한다.

보정 스케일은 0이 아니어야 한다. 회전된 비균일 조상 스케일로 생기는 전단까지 정확히 처리하는 접촉 모델은 아니다. 실제 임포트 몸체의 스케일과 변형은 별도 검수한다.

## 4. 몸체 프로필과 에디터 연결

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

왼팔 노드는 별도 양손 검증 후 `Secondary Body Profile`을 쓸 수 있다. Guided 노드 Effector는 컴포넌트 공간이다. 오른손 주도 구간의 왼손 뼈 공간 목표를 쓰려면 `bUsePrimaryHandSpaceGrip`에 따라 현재 오른손 포즈에서 컴포넌트 공간으로 합성하는 경로를 작성한다.

## 5. 팔꿈치 안정화 수치

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

## 6. 보조 뼈·수명·MMORPG 비용

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

## 7. 진단 로그

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

## 8. 검증 이력과 한계

2026-10-02 직접 `UnrealBuildTool.exe`로 `Project_JEditor Win64 Development` 빌드 성공. 최신 범위 자동화 6개가 모두 Success, 프로세스 종료 코드 0:

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

19:31 인게임 캡처에서 두 연계의 늦은 LMB2 팔꿈치 최대 이동이 20.107/23.428 → 5.213/5.216cm로 감소했다. 닿을 수 있는 목표의 접촉도 유지됐다. 도달 불가능한 목표, 수동 Idle 부착 보정, 추가 몸체·무기·원격 군중 검증은 남아 있다. 자세한 수치와 프레임 비교 제한은 [측정 기록](Weapon_Grip_Trace_2026-10-02.md)에 둔다.
