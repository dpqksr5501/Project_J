# 보조 손 접촉과 직업별 파지 설정

갱신일: 2026-10-03. 기존 `WeaponGrip_L`, `PalmGrip_L`을 사용한다. 새 소켓이나 별도 왼손 전용 DA가 필요하지 않다. 소스 Motion Matching·상체 레이어·공격 궤적과 기존 오른손 Palm 부착을 유지한다.

## 데이터 소유권

- **몸체 Hand Grip DA:** Primary/Secondary 팔 역할, 손바닥 위치·방향, 몸체 보정과 팔꿈치 안정화. 직업 이름으로 노드 안에서 분기하지 않는다.
- **무기 Presentation DA:** Grip 소켓과 보조 접촉 허용 여부, 발도/납도/소스 공격의 기본 Alpha. 같은 메시를 쓰더라도 파지 정책이 다르면 해당 장비 구성에 맞는 프로필을 선택한다.
- **공격 데이터/작성 커브:** 특정 공격에서 손을 잡거나 놓는 가중치. 무기 기본값을 사용하거나 기존 공격별 Override를 사용한다.
- **Guided 노드:** 현재 몸체의 실제 길이와 입력 포즈 굽힘으로 팔을 보정한다. 파지 시작/종료 타이머나 무기 생성, 네트워크 상태를 소유하지 않는다.

Primary/Secondary는 역할이며 반드시 R/L 이름일 필요는 없다. 현재 대검의 Primary는 오른손, Secondary는 왼손이다.

## 현재 대검과 다른 파지 방식

무기 DA의 Weapon Motion / Motion Presentation에서 설정한다. 새 `Enable Secondary Grip Contact` 기본값은 true로 기존 에셋을 유지한다.

| 파지 방식 | Enable Secondary Grip Contact | Default Drawn Secondary IKAlpha | Default Attack Secondary IKAlpha | Default Sheathed Secondary IKAlpha |
| --- | --- | --- | --- | --- |
| 현재 대검: Idle 오른손 / 지정 공격 구간 양손 | 켬 | 0 | 0 | 0 |
| Idle 오른손 / 소스 공격 전체 양손, 스테이트 생략 | 켬 | 0 | 1 | 0 |
| Idle부터 양손 / 공격 양손 | 켬 | 1 | 1 | 0 |
| 한손 무기: 보조 손 접촉 없음 | 끔 | 0 | 0 | 0 |
| 특정 공격만 양손 | 켬 | 0 | 0 | 0 |

Drawn 기본값은 발도 Idle와 일반 이동에서 적용된다. 공격 기본값은 소스 주도 자동 몽타주 공격의 기본 접촉이다. 기존 Attack Definition의 `Override Grip IK`가 켜져 있으면 그 공격의 `Secondary Grip IKAlpha`를 사용한다. Weapon Motion 스테이트가 남아 있으면 그 구간의 Secondary Alpha가 독립 모션 설정을 덮어쓴다. 노티파이를 모든 공격에 추가할 필요는 없다.

### 현재 대검: 기존 Two-Hand Grip IK 스테이트로 구간 지정

1. 무기 DA의 `Enable Secondary Grip Contact = true`, Drawn/Attack/Sheathed Secondary Alpha는 모두 0으로 설정한다.
2. **소스에서 재생되는 공격 몽타주**의 원하는 파지 구간에 기존 **Two-Hand Grip IK** 노티파이 스테이트를 배치한다. `Secondary IKAlpha = 1`, `Override Primary IK = false`로 둔다. 공격별 Grip Override나 남아 있는 Weapon Motion의 기본 Secondary 값도 구간 밖에서 0을 사용해야 한다.
3. 시작 시 보조 Alpha가 1을 향하고 종료 시 기본 0으로 돌아간다. 오른손은 기존 독립 공격 Alpha 또는 정상 Palm 부착 정책을 유지한다. 스테이트가 검을 다시 부착하거나 Motion Keys·소스 소켓·진입 시간을 변경하지 않는다.
4. 왼팔 Guided 노드와 `WeaponGrip_L` / `PalmGrip_L` 연결은 아래 설정을 유지한다. 새 파지 스테이트를 별도로 만들 필요가 없다.

기존 Two-Hand 스테이트가 소스/독립 Motion에서 무시되던 경로를 보완했다. 접촉 기본값을 먼저 고른 뒤, 발도 중 활성 Two-Hand 구간을 모든 무기 구동 방식에 적용한다. `LeftHandIK` 커브가 존재하면 일반 접촉에서는 계속 최우선 가중치다. 스테이트만으로 제어하려면 해당 커브를 쓰지 않거나 런타임 ABP의 `Left Hand IK Curve Name = None`으로 두어 이 몸체의 커브 조회를 끈다. 같은 손에서 구간 스테이트와 커브를 함께 쓰면 의도한 우선순위인지 확인한다.

겹친 스테이트는 **가장 최근에 시작해 아직 살아 있는 구간**의 가중치를 사용한다. UE의 `FAnimNotifyEventReference.GetNotifyInstanceID()`로 재생별 요청을 구분하며 shared Notify UObject에 실행 상태를 저장하지 않는다. 종료는 자신의 요청만 제거한다. 중첩 구간 종료 후 살아 있는 이전 값 복원, 먼저 시작한 구간이 먼저 끝나는 콤보, 중복 Begin/End 및 초기화 뒤 늦은 End를 처리한다. 장비 외형 파괴·전투 해제·EndPlay의 기존 정리 경계에서 요청을 비운다. 기존 Blueprint Begin/End 호출은 별도의 균형 잡힌 LIFO 요청으로 호환하며, 키가 있는 몽타주 구간을 종료하지 않는다.

소스 애니메이션의 `LeftHandIK` 커브가 존재하면 일반 접촉 구간의 커브 값이 Alpha보다 우선한다. 손을 놓는 특정 구간을 작성하는 데 활용하되 0 커브 때문에 DA의 1이 적용되지 않는 상황을 구분한다. 복귀는 기존 몽타주 가중치 기반 정책을 따른다. `Enable Secondary Grip Contact = false`는 목표 자체를 비활성화하므로 기존 커브, Two Hand 상태, Motion Alpha 또는 복귀에 남은 Alpha가 보조 접촉을 다시 켤 수 없다. 장비 교체와 누락 Grip에서도 유효성과 Alpha를 초기화한다.

## 에디터에서 왼팔 연결

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

### 오른팔 설정 확인

오른팔은 `Arm Definition Source = Primary Body Profile`, `Alpha = Right Grip Alpha`, `Match Wrist Rotation = true`, `Use Bone Space Effector = false`, `Use Explicit Elbow Guide = false`로 둔다. Effector Transform은 `Right Wrist Target`을 직접 연결하거나 기존 `Right Grip Location / Rotation → Make Transform` 연결을 유지한다. 왼팔의 Primary 손 기준 입력 세 개를 오른팔에 복사하지 않는다.

Primary/Secondary Body Profile을 선택하면 각 팔의 뼈 역할과 굽힘 안정화는 몸체 DA에서 읽는다. 노드 상세의 Forearm/Upper Arm이 None이어도 DA의 해당 Arm이 올바르게 설정되어 있으면 된다. 오른팔은 무기 접촉 목표를 풀고 왼팔은 상태에 따라 독립 컴포넌트 목표 또는 같은 평가의 Primary 손 기준 목표를 사용한다.

## 목표 공간과 평가 순서

1. **소스 주도 공격:** 검이 몸체 손과 독립적으로 움직인다. `Use Primary Hand Space Grip = false`; 두 팔은 각 Grip에서 역산한 컴포넌트 공간 손목 목표를 사용한다.
2. **손 주도 상태 및 부착 복귀:** 검이 보이는 Primary 손에 붙어 있다. 보조 목표는 Primary 손 기준 값으로 전달하고, 왼팔 노드가 **이번 평가에서 오른팔 노드가 Alpha까지 반영한 입력 포즈**를 읽어 컴포넌트 공간으로 합성한다. 이전 최종 손 위치의 월드 조회를 목표로 사용하지 않는다.
3. **현재 대검 Idle:** 보조 Alpha가 0이므로 목표가 있더라도 보조 팔을 풀지 않는다. 항상 양손 Idle에서는 같은 뼈 공간 경로로 움직이는 Primary 손을 따라간다.

활성 뼈 공간의 기준 뼈가 누락되거나 LOD로 제거되면 잘못된 월드 목표로 대체하지 않고 해당 노드를 우회한다. 기준 뼈가 자신이 푸는 팔의 서브트리 안에 있으면 자기 추적 구성으로 거부한다. 뼈 공간↔컴포넌트 공간 또는 기준 뼈 변경 시 굽힘 이력을 초기화한다. 대상 뼈는 변경/LOD 캐시 단계에서 해석하며 워커는 포즈 값과 Compact Bone 인덱스를 사용한다.

## 제한과 MMORPG 검증

- 추가 본 변환 RPC, 복제 상태, 상시 컴포넌트 틱 또는 게임 스레드 포즈 쓰기를 추가하지 않는다. 기존 클라이언트 외형/전용 서버/품질 등급 경계를 유지한다.
- 손을 무기에 맞추는 두 팔 보정이며, 쇄골·몸통 Reach를 포함한 양손 동시 전신 제약 솔버는 아니다. 길이가 다른 몸체에서 도달 범위 밖 목표는 뼈를 늘리지 않고 제한한다. 접촉을 유지하려면 그 조합의 리타깃/공격 궤적 또는 별도 Reach 정책을 검토한다.
- 손가락 파지, 관절 제한, 비틀림 분배와 의상 관통은 별도 검수 항목이다. RigidBody가 팔·손을 다시 움직이지 않도록 영향 본을 확인한다.
- Idle 오른손만, 공격 양손, 항상 양손, 한손 장비로 교체, 연계·취소·피격·납도, 원격/URO/LOD와 몸체 교체를 확인한다.
- 기존 GripTrace의 `[WeaponContact] role=Secondary`와 GuidedIK의 Secondary Hand 노드 기록을 함께 본다. Guided 진단의 목표는 공간 변환 후 컴포넌트 공간 값이다.

개별 ABP·DA는 사용자 에디터에서 연결/저장한다. 코드에서 에셋을 자동 변경하지 않는다.

## 검증 결과

2026-10-03 직접 `UnrealBuildTool.exe`의 `Project_JEditor Win64 Development` 빌드 성공, 종료 코드 0이다. `SecondaryHandContact`, `HandContactRig`, `HandContactConversion`, `VisualMeshOwnership`, `PrimaryGripAttachment`, `GuidedBendStability`, `GuidedIKTraceIsolation` 자동화 7개 모두 Success, 종료 코드 0이다. 로그는 `Saved/Logs/SecondaryHandContact_Automation.log`다.

새 테스트는 서로 다른 두 팔 길이와 임의 뼈 이름, 손바닥 오프셋을 사용한다. 오른팔 Alpha 0.55의 엔진 포즈 블렌드 뒤 왼팔이 같은 평가의 목표 위치·회전을 잡는지, 후속 평가의 기준 포즈 변경, 독립 목표로의 공간 전환, 누락/자기 팔 기준 뼈/기준 뼈 LOD 제거를 검사한다. 실제 소스/임포트 몸체 fixture에서는 공격만 양손, 복귀 중 보조 접촉 비활성화, 노티파이와 Motion Alpha의 강제 활성화 차단, 오래된 레거시 추적 경로 차단 및 양손 정책 재활성화를 검사한다.

저장된 `ABP_Greatsword_Woman_RunTIme` 한 개의 범위 컴파일은 오류 0·ABP 경고 0·로드 실패 0·종료 코드 0이다. 로그는 `Saved/Logs/SecondaryHandContact_Blueprint_Scoped.log`. 설치된 MCP 플러그인의 시작 안내 경고는 ABP 경고와 구분하며 MCP 도구는 호출하지 않았다. 이 범위 컴파일은 사용자 최종 노드 연결 이전의 저장본 검증이다.

기존 Two-Hand Grip IK 구간 통합 후 직접 UBT 빌드는 26.77초에 성공했다. `SecondaryHandContact`, `TwoHandIKTransitionAndCurve`, `PrimaryGripAttachment`, `StableGripTargetsAndAuthoredAlpha`, `VisualMeshOwnership` 5개는 `Saved/Logs/TwoHandGripWindows_Automation.log`에서, `ProjectJ.NPCGameplay.WeaponPresentationTeardown`은 `Saved/Logs/TwoHandGripWindows_Teardown.log`에서 Success다. 두 실행 모두 종료 코드 0으로, 이번 통합 관련 검증은 총 6개다.

추가 검증은 실제 Notify Begin/End의 재생 ID, 중첩 구간 값 복원, 순서가 다른 종료, 중복 호출, 초기화 뒤 늦은 종료, 레거시 호출과의 분리를 포함한다. 소스 자동 공격과 명시적 독립 Motion 모두에서 보조 파지 구간이 적용되며 무기 월드 변환·부착 부모·구동 주체는 바뀌지 않는지 확인했다.

2026-10-03 사용자가 에디터에서 오른팔 Primary / 왼팔 Secondary Body Profile, 왼팔의 다섯 입력 및 오른팔의 컴포넌트 목표 연결을 적용한 뒤 정상 동작을 확인했다. 이는 사용자 플레이 확인이며 최종 연결본을 도구로 다시 컴파일하거나 접촉 오차를 수치 측정한 결과는 아니다. 연속 공격·공격 취소·피격·납도, 다른 몸체·무기 조합, 원격/URO/LOD와 다수 플레이어 성능 검수는 후속 확인 항목이다.
