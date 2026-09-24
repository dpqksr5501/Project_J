# Project_J 코드 감사 결과

대상: `Character_Test` HEAD `52b481f`, Unreal Engine 5.8. 이 감사는 사용자의 선택에 따라 `Source`, `Config`, 저장소 문서와 로컬 빌드/자동화 테스트만 사용했다. AnimBP, Blueprint, Data Asset 내부 연결과 실제 화면 품질은 확인하지 않았다.

## 1. 종합 결과

- 코드 감사와 확인된 결함의 수정을 완료했다. 전체 콘텐츠·멀티플레이·프로파일링 완료 판정에는 아래 남은 작업의 확인이 필요하다.
- `Project_JEditor Win64 Development` 직접 UBT 빌드 성공. 최종 컴파일 오류/경고 없음.
- 관련 자동화: Presentation 1, Combat SSR 2, Animation 6, Canonical melee trace 1, Mount 4 = 14개 통과(테스트 경고 0). 결과 JSON은 `Saved/AuditTests` 아래에 있다.
- 테스트 에디터 시작 시 프로젝트의 일부 UEFN 샘플 애니메이션이 Foley Notify 및 Pose Search 데이터 종속 패키지를 찾지 못한다는 `LoadErrors`가 반복됐다. 이번 C++ 변경으로 생긴 경고는 아니지만 콘텐츠 정리가 필요하다.

## 2. 수정한 결함

### P0

- `MeleeHit` Notify의 기존 선택지는 시각용 무기 액터 소켓을 클라이언트 후보 스윕 및 서버 권위 스윕 기록에 사용할 수 있었다. 히트 위치를 Leader 메시 소켓으로 고정했다. 독립 무기 모션, Follower Retarget, Hand IK가 서버 판정 원천이 되지 않는다. 기존 직렬화 필드는 삭제하지 않고 deprecated로 유지했다.

### P1

- Retarget AnimInstance가 이미 얻던 그립의 전체 transform에서 component-space 위치와 회전을 모두 계산한다. 캐릭터 `CharacterAnimProfile`에 primary/secondary 손바닥 오프셋과 선택적 팔꿈치 타깃을 추가했다. 무기 에셋은 그립 소켓, 캐릭터 프로필은 신체별 보정을 소유한다. worker thread에는 값 스냅샷만 전달한다.
- 탈것 아이템 사용 RPC의 `MountClass.LoadSynchronous()`를 제거했다. 서버에서 비동기 로딩 후 인벤토리, 잠금, 현재 탑승 상태를 재검증한다. 요청 교체와 캐릭터 종료 시 pending handle을 취소한다.

### P2

- WeaponPresentation의 grip, ground probe, VFX 소켓 조회를 공통 캐시로 모았다. 없는 소켓도 기록해 매 프레임 반복 component scan을 막고, 무기 파괴·생성 및 프로필 소켓 이름 변경 시 캐시를 갱신한다.
- Shipping PlayerController에서는 프로파일링 군중 컴포넌트를 생성하지 않는다.
- 전투 진입 시 무기 표현 로그를 일반 Log에서 Verbose로 내렸다.

## 3. 의도적으로 유지한 구조

| 영역 | 판단 | 근거 |
| --- | --- | --- |
| GAS, PlayerState의 지속 ASC/Inventory/Equipment, FastArray | 유지 | 서버 권위와 장기 상태 소유권이 명확하고 FastArray owner가 생성자에서 설정된다. |
| Input Binding → Router → Execution | 유지 | chord grace, Completed/Canceled 처리, GAS 실행 경계가 각 컴포넌트에 있다. |
| NPC Decision/TargetScoring bounded 작업 | 유지 | outstanding 제한, world epoch, owner/token 검사 경로가 있다. |
| Leader → Follower, animation quality tier | 유지 | Dedicated Server 제외, 렌더 거리 기반 tier, follower tick 제어가 있다. |
| Motion Matching trajectory | 유지 | 서버 제외, local/remote 분리, 동일 프레임 중복 방지, 원격 렌더 gating, reset revision이 이미 있다. |
| SSR sweep history | 유지 | prediction key, attack node, hit-window 경계 및 기록 보간 검사가 있다. 구조 전체를 다시 쓰지 않았다. |
| Prototype/compatibility public properties | 유지 | Blueprint/Data Asset 참조를 코드만으로 부정할 수 없어 삭제하지 않았다. |

## 4. 제거한 경로

- 게임플레이 히트 판정에서 시각 무기 소켓으로 우회하는 실행 경로와 기존 grip 전용 중복 캐시 필드를 제거했다.
- 에셋 호환 가능성이 있는 `bUseWeaponPresentationSocket`, `WeaponSocketName`, `PrototypeStartingWeapon`, 직접 `LocomotionProfile`/`MotionMatchingAssetSet` fallback, 전투 상태 mirror는 삭제하지 않았다.

## 5. 측정이 필요한 영역

- `CharacterAnimInstance`는 여전히 큰 클래스다. 매 프레임 `BuildThreadSafeData`의 임시 값, `FTransformTrajectory` 복사, proxy 게시에서 실제 할당 횟수와 Animation Worker 비용을 Unreal Insights로 측정해야 한다. 구조를 추정만으로 분리하지 않았다.
- 원격 trajectory smoothing은 `DeltaTime * Speed` alpha를 사용하지만 기본 원격 smoothing은 CVar opt-in이다. MM 검색 품질 비교 없이 수식을 바꾸지 않았다.
- `Project_JCharacter`는 public dependency가 많다. public header의 전이 의존을 각 소비 모듈 빌드와 함께 확인해야 하므로 일괄 private 전환이나 신규 모듈 분리는 하지 않았다.
- PlayerCharacter/LocomotionAnimState의 compatibility mirror와 transition 상태는 Blueprint/AnimBP 참조 확인 전까지 유지한다.
- Foot IK 이중 평가, Draw/Sheathe의 한 프레임 visual pop, 캐릭터별 그립 보정의 실제 관절 품질은 여러 체형의 에셋과 화면 캡처가 있어야 판단 가능하다.

## 6. 구조 변경

변경 전: `MeleeHit → optional visual weapon socket → hit sweep/SSR history`; `WeaponPresentation → repeated scan on missing socket`; `mount RPC → synchronous class load`; `Follower hand IK → grip position`.

변경 후: `MeleeHit → Leader socket → hit sweep/SSR history`; `visual weapon → presentation/VFX only`; `WeaponPresentation → positive/negative socket cache`; `mount RPC → async class load → authority/inventory recheck`; `Follower hand IK → calibrated component-space position, rotation and elbow values`.

## 7. 게임플레이·애니메이션 품질 관련 변경

- Runtime Retarget와 Hand IK: 캐릭터별 grip offset, 손목 회전, 팔꿈치 타깃을 AnimGraph에 사용할 수 있는 값으로 제공한다. 기본 offset은 identity라 기존 에셋 동작을 유지한다. AnimGraph 연결과 실제 품질 평가는 수행하지 않았다.
- Foot IK, Motion Matching, Trajectory: 기존 정책을 유지했다. 소스에서 확실한 품질 이득이 확인되지 않은 수식·상태 전환 변경은 하지 않았다.
- Combat: 시각용 무기 경로와 서버 판정 경계를 강화했다. GAS/Combo/AttackDefinition 수명 흐름은 유지했다.

## 8. 성능 관련 변경

- 런타임 서버 탈것 소환의 동기 클래스 로드 제거. CharacterDataSubsystem의 동기 로드는 초기 레지스트리 구성/명시적 검증 경로라 유지했다.
- weapon grip/ground/VFX 조회에서 같은 소켓의 반복 component scan을 제거했다. 실제 프레임 시간 개선값은 측정하지 않았다.
- Shipping에서 프로파일링 군중 컴포넌트 생성 제거. 서버 판정이나 IK transform 복제는 추가하지 않았다.
- Tick 자체는 모두 없애지 않았다. Player movement, 카메라, 한 프레임 입력 조정, 독립 무기 모션, 서버 rewind의 30 Hz 기록, 제한된 subsystem scheduler는 현재 실행 이유가 있다. 프로파일링 군중 Tick은 개발 시에만 객체가 생성된다.

코드의 명시적 Tick 구현 목록과 유지 판단:

| Tick | 실행 범위 / 주기 | 판단 |
| --- | --- | --- |
| PlayerCharacter | 활성 플레이어, 프레임 | 이동 정책, 현재 프레임 trajectory/locomotion 상태가 필요하여 유지 |
| FlyingMountCharacter | 비행 상태만 활성, 프레임 | 서버 비행 단계·착지와 클라이언트 요청 만료에 사용하여 유지 |
| CameraComponent | 로컬 조종자, 프레임 | 카메라 갱신에 필요하여 유지 |
| PlayerInputBindingComponent | 완료·취소 입력 조정 시만 활성, 다음 프레임 | 동일 프레임 입력 화해에 필요하여 유지 |
| WeaponPresentationComponent | 독립 무기 모션 또는 debug 시 활성, 프레임 | authored motion/ground contact에 사용하여 유지 |
| ServerSideRewindComponent | 서버, 기본 30 Hz | 타깃 캡슐 기록에 사용하여 유지 |
| NPCDecisionExperiment | 실험 중 서버, 0.25초 | 개발/프로파일링 경로로 유지 |
| ProfilingCrowdComponent | 개발 실험 중, 프레임 | Shipping에서는 생성되지 않음 |
| NPCDecision, NPCPath, TargetScoring, VisualAsset, PresentationBudget, MassRepresentation subsystems | 작업·큐·표현 대상이 있을 때 tickable | 제한된 스케줄러/완료 적용에 사용하여 유지; 실제 비용은 측정 필요 |

## 9. 남은 작업

1. 관련 AnimBP에 새 grip rotation/elbow 값을 선택적으로 연결하고, 최소 두 체형과 양손 무기에서 손목·팔꿈치·어깨 움직임을 시각 검증한다. 현재 코드는 값만 제공하며 품질 개선을 입증하지 않는다.
2. 멀티플레이 환경에서 SSR 클라이언트 시간, 서버 공격 sweep, 타깃 rewind의 시계 일치와 packet loss/공격 취소 경계를 재현한다. 코드상 서버 GameState 시간과 World 시간이 정상 권위 실행에서 일치한다는 전제가 있다.
3. 냉시작 상태의 mount class async load와 장비 교체·아이템 제거 중 요청 취소를 실제 플레이로 확인한다. 기존 Mount 테스트는 이 새 경로를 직접 실행하지 않는다.
4. 에디터 시작 로그의 누락된 UEFN 샘플 종속 패키지를 콘텐츠 소유자가 확인한다.
5. Unreal Insights로 animation snapshot 복사, Pose Search, Retarget/IK, 원격 trajectory, 큰 모듈의 런타임 비용을 기록한 뒤에만 추가 구조 변경을 결정한다.
