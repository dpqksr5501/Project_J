# 플레이어 애니메이션 실행 흐름 비교와 보강

현재 제작 기준은 [로코모션 아키텍처](Player_Locomotion_Architecture.md)다. 이 기록은 Epic Game Animation Sample 5.8.3의 실제 Mover BP/ABP와 Project J의 생산 경로를 비교한 범위, 변경 이유와 검증 한계를 설명한다. 샘플의 모든 기능을 활성화하거나 모든 포즈가 시각적으로 같음을 보증하는 기록은 아니다.

## 조사한 대상과 근거

원본 `SandboxCharacter_Mover_ABP`의 중첩 그래프 포함 136개, 최상위 그래프·함수 78개와 `SandboxCharacter_Mover`의 그래프 36개를 열거하고, 실제 TextExporter의 노드·핀·바인딩을 native 구현과 대조했다. CMC BP/ABP도 비교 자료로 수집했다. MCP DSL 조회가 빈 문자열을 반환한 항목은 조사 근거로 세지 않았다.

Project J는 마스터, Player/GreatSword BP, 전투 레이어, 대검 runtime ABP, 탑승 ABP·인터페이스 7개 생산 자산을 조사했다. 마스터는 75개 그래프, BP 소유 변수 0개, native 소유 변수 101개다. 원본과 공통된 포즈 노드 경로 153개를 비교했으며 Manny mirror table·발 Rig·blend profile, 공중 콜백과 프로젝트 슬롯/레이어 변경을 구분했다. 원본과 현재의 전체 노드 수 차이를 곧바로 기능 누락으로 판정하지 않았다.

세션 원자료는 `Saved/Validation/AnimationFlowAudit_20261021/`에 둔다. 이 폴더는 로컬 진단 자료이며 다른 작업 환경의 런타임 의존성이 아니다. 공유 가능한 독립 비교 입력과 기대값은 `Scripts/Tests/Data/PlayerLocomotionReferenceQueries.json`에 보관한다. 원본 ABP CDO의 경계값을 임시 변경해 함수 결과를 수집한 뒤 값을 복원했고 원본 프로젝트는 저장 없이 종료했다. CDO 변경으로 에디터 내부 노드 캐시가 재구성되었으므로 전후 TextExporter 전체가 동일하다고 주장하지 않는다. 디스크 원본 패키지는 변경하지 않았다.

## 값의 소유권과 실제 실행 시점

| 경계 | 원본의 생산 흐름 | Project J의 writer → consumer / 실행 시점 |
| --- | --- | --- |
| 입력·회전 | Mover BP의 입력 생성과 회전 커브 Chooser | `AProject_JMoverPawn::PopulateMoverInput` → 승인된 Mover 명령. 카메라 상대 입력, 방향 hysteresis, Strafe Sprint 허용 방향을 같은 시점에 계산 |
| 이동 결과 | PostFinalize의 `CacheInputsFromMover`, `GetLastInputCmd`, floor/based delta | Mover sync state·last input → `NativeUpdateAnimation`의 GT 수집. 예측 입력과 실제 승인 상태를 혼용하지 않음 |
| 물리 점프·착지 | 원본의 Mover 상태·속도와 ABP 전환 질의 | rollback 소유 lifecycle → GT air frame → worker 선택. 프로젝트의 공중 재선택·수평 관성·playhead 연속성 유지 |
| 궤적 | `InitializeMoverPredictor`, `Update_Trajectory` | GT predictor → `PublishPlayerLocomotionSourceFrame`/proxy → `UpdateNativeTrajectory`. 원본의 sampling rate 60은 엔진 predictor 기본값과 같아 중복 설정을 추가하지 않음 |
| 프레임 계산 | `Update_Logic`의 trajectory → essential → states → aim → lean | `UpdateNativeLocomotionLogic`에서 같은 순서. worker는 게시된 source frame을 소비하고 Pawn/BP 상태를 다시 탐색하지 않음 |
| 전환·선택 | 상태별 StateEntry, OnUpdate, notify, Pose History | native transition/query와 state-entry Chooser 호출 → BlendStack 입력. 상태별 clock·현재 후보·notify를 읽으므로 프레임 시작의 공통 캐시로 합치지 않음 |
| 포즈 보정 | Steering/OffsetRoot/Orientation·Stride/FootPlacement의 노드별 callback | native NodeRef 질의 → 기존 AnimGraph 노드. 노드의 재생 시각·커브·BecomeRelevant 시점을 유지 |
| 최종 표현 | 샘플의 절차적 포즈·레이어 | Manny source → 프로젝트 직업/전신 슬롯 경계 → 호환 몸체 직접 재생 또는 Family RTG → 몸체 손 접촉·추가 뼈 |
| 종료 | sample owner 유효성 및 component 수명 | AnimInstance 초기화/해제 시 source admission 무효화. 전용 서버·teardown의 무기 콜백은 기존 표현 admission으로 차단 |

원본 Mover의 일부 CharacterProperties 출력은 연결되지 않아 acceleration/deceleration/landing 필드가 기본값이다. 이를 CMC 방식으로 임의 채워 원본과 같다고 처리하지 않았다. 프로젝트에서 실제 소비하는 착지 데이터는 물리 lifecycle이 소유한다.

## 원본 함수군과 현재 대응

아래는 78개 최상위 항목을 책임별로 묶은 대응표다. 이벤트/포즈 함수도 포함한다. 모든 함수의 전 입력 조합을 실행한 것은 아니며 동적 비교 범위는 아래 검증 절에서 따로 표시한다.

| 원본 함수·그래프 | 현재 대응·판정 |
| --- | --- |
| `AnimGraph`, `EventGraph`, `BlueprintThreadSafeUpdateAnimation`, `Update_Logic`, `Update_PropertiesFromCharacter`, `Update_CVarDrivenVariables`, `InitializeMoverPredictor` | native 초기화·GT 수집·worker update + 마스터 포즈 연결. CVar 읽기는 GT |
| `Update_Trajectory`, `Update_EssentialValues`, `Update_States`, `UpdateStateValues` | `UpdateNativeTrajectory/EssentialValues/States`, native 상태별 clock. mode 0.2초/다른 상태 0.1초의 recency 계약 유지 |
| `IsMoving`, `IsStarting`, `IsPivoting`, `ShouldTurnInPlace`, `ShouldSpinTransition` | `EvaluateIsMoving/IsStarting/IsPivoting/TurnInPlace/SpinTransition`. 원본의 끊어진 role 분기나 현재 사용하지 않는 MM database tag writer는 복원하지 않음 |
| `JustLanded_Light/Heavy`, `Get_LandVelocity`, `JustTraversed`, `JustTeleported` | native 착지/teleport 질의와 프로젝트 물리 lifecycle. Traversal을 새 이동 기능으로 활성화하지 않음 |
| `Get_TrajectoryTurnAngle`, `Get_TotalFacingDelta`, `Get_DesiredFacing`, `EnableSteering` | trajectory query, OffsetRoot 기준의 누적 facing, native NodeRef 기반 steering/facing. 카메라 각도로 몸 방향을 강제 reset하지 않음 |
| `Get_OffsetRootRotationMode`, `Get_OffsetRootTranslationMode/HalfLife/Radius`, `OnUpdate_OffsetRootBone` | native root mode/query/update, 노드 OnUpdate 시점 유지 |
| `Get_OrientationWarpingWarpingSpace`, `Get_StrafeWarpDirection`, `Get_StrideWarpAlpha`, `Get_StrafeWarpAlpha`, `Get_ProceduralTargetTime` | native `EvaluateOrientationWarpingSpace/StrafeWarpDirection/WarpAlpha/ProceduralTargetTime`. 해당 player의 시간·커브를 소비 |
| `SetBlendStackAnimFromChooser`, `IsAnimationAlmostComplete`, `Get_DynamicPlayRate`, `Get_BlendSpaceInputs` | native state selection·completion·play rate·BlendSpace 입력. 재생 데이터는 Chooser/프로필에 유지 |
| `OnStateEntry_IdleTransition/IdleLoop/IdleBreak`, `OnStateEntry_LocomotionTransition/LocomotionLoop`, `OnUpdate_LocomotionTransition` | native state entry/update와 ABP callback binding. 상태에 진입한 뒤 선택하는 순서 유지 |
| `OnStateEntry_InAirTransition/InAirLoop` | native entry + `UpdateProjectAir` 콜백. 프로젝트의 공중 방향 재선택을 유지하는 의도적 차이 |
| `Update_AimOffset`, `Update_AdditiveLean`, `AimOffset`, `AdditiveLeans` | native 계산 + Manny 프로필/포즈 함수. 모든 프레임 상태를 BP에 중복 보관하지 않음 |
| `AllowFootPinning`, `AllowSlopeWarping`, `Biped_FootPlacement_OnBecomeRelevant`, `Procedural_Feet/PreLayering/FullBody` | native 허용 조건/reset + 발 Rig/절차적 포즈 그래프. 원본의 연결되지 않은 `IsMoving &&`를 유효 조건으로 오인하지 않음 |
| `Get_PoseHistoryReference`, `Get_PoseHistory`, `Get_Gait`, `Get_Trajectory`, `Set_NotifyTransition_ReTransition/ToLoop` | native 조회/notify 계약과 포즈 히스토리 인터페이스. 소비 시점에 읽음 |
| `Update_MotionMatching`, `Update_MotionMatching_PostSelection`, `Get_MMBlendTime/InterruptMode/NotifyRecencyTimeOut` | 퇴역한 독립 MM 재생 경로. 현재 State Controller/Chooser 선택에 중복 writer로 추가하지 않음 |
| `OnStateEntry_SlideTransition/SlideLoop/DebugFlyingLoop`, `Get_SlideSlopeRotation/Offset` | 현재 활성 이동의 Ground/InAir 범위 밖. 일부 enum·제작 데이터·포즈 참조 보존을 새 기능 구현 완료로 표시하지 않음 |
| `Update_InteractionConstraints`, `Update_MMIConstraints`, `Get_InteractionTransform`, `Set_InteractionTransform`, `DebugDraws` | 샘플의 interaction/MMI/진단용 계약. 현재 프로젝트의 gameplay 권위로 이식하지 않으며 진단은 opt-in native 관측을 사용 |

## 이번에 실제 수정한 부분

1. **AnimInstance 수명:** proxy를 초기화해도 이전 owner의 `PlayerHasOwningActor/PlayerHasLocomotionSource/PlayerUseThreadSafeUpdateAnimation`이 남았다. 초기화·해제 시 세 플래그를 무효화해 새 owner의 첫 정상 source frame 전까지 worker 계산을 허용하지 않는다. 실패를 재현한 회귀가 수정 후 통과했다.
2. **회전 Chooser 입력:** Mover가 매 입력마다 UserDefinedStruct를 만들고 문자열 접두사로 byte 필드를 찾던 경로를 `FProject_JPlayerRotationOffsetQuery`와 native enum으로 바꿨다. 실제 `CHT_RotationOffsetCurve`의 context·두 열 binding·enum 이름도 함께 저장했다. 기존 8행 순서·비교 정책·커브는 유지했다. 기존 UDS는 제작/비활성 참조 가능성을 고려해 이번에는 삭제하지 않았다.
3. **비표현 월드의 무기 콜백:** 전용 서버에는 무기 액터를 만들지 않는데 전투 종료/notify가 부착까지 시도하며 잘못된 소켓 경고를 냈다. 기존 `CanCreatePresentation` 계약을 부착에도 적용했다. 실제 source와 대검 표시 메시 모두 등 소켓이 있으므로 스켈레톤·소켓을 새로 만들지 않았다. 정상 클라이언트의 부착과 잘못된 소켓 진단은 유지한다.
4. **PIE 검사 소유권:** 단순히 첫 world/subsystem을 고르거나 Python에서 GAS 능력을 직접 호출하던 검사를 수정했다. 실제 local pawn의 같은 world 입력 subsystem으로 전투 액션을 보내고 입력·회전·상태를 복원한다. 다중 PIE의 잘못된 RPC world 문맥으로 재귀했던 검사 경로를 제거했다.

### Run 회전 수정과 원본의 차이

원본 GASP 5.8.3의 Run slopes는 marker sync=true / phase match=false이고 Walk는 false/true다. Project J의 Walk/Run은 **marker sync=false / phase match=true**로 정규화 시간 정책을 사용한다. 원본 Run의 체크박스를 그대로 복제한 수정이라고 설명하지 않는다.

현재 엔진·Pose Search 계약에서 두 플래그를 함께 켜면 마커 위상 맞춤이 leader의 이전 시각을 재설정해 root delta가 0이 되는 경로가 있다. Steering의 속도 조건도 통과하지 못해 물리 yaw와 표시 root가 분리된다. 원본 Walk의 정규화 시간 방식을 Run에도 적용해 검색 위상 계약과 실제 root delta를 동시에 유지했다. `ValidateSampleData`로 내부 캐시까지 재계산했다. 원본 발 마커·커브·노티파이는 삭제하지 않았다. 사용자가 저작한 Sprint→Run 조기 종료 notify도 보존했다.

## 검증 범위

- 원본 Blueprint CDO의 10개 실제 질의를 91개 경계 입력으로 실행한 **910개 반환값**과 native 결과가 일치한다. speed/turn 임계값, 6개 mode, 착지 Z, teleport 거리, root 설정, angular velocity를 포함한다. 전체 78개 함수를 동적으로 비교한 수치는 아니다.
- 실제 생산 Chooser의 **6 mode × 6 direction = 36개** 선택 결과가 이전 커브 계약과 일치한다. 이미 native인 table에 제작 변환을 다시 적용해도 dirty가 생기지 않는다.
- 무기 콜백 변경을 포함한 최종 direct UBT `Project_JEditor Win64 Development`가 성공했고 native 회귀 **67개가 경고·실패·미실행 0개**로 통과했다. 결과는 `Saved/Validation/PlayerLocomotion/AnimationFlowCallbacks_20261021/Automation/index.json`, 빌드 로그는 `AnimationFlowAudit_20261021/BuildCallbacks.log`다.

### 실제 PIE 결과

| 검사 | 최종 자료 / 결과 |
| --- | --- |
| 완만한 루프 회전 5종 | `LoopTurning_20261021_185533_494606.json`, 1,322표본. 실제 루프 평가 구간 root/actor 최대 차이 1.770/1.761/1.686/1.769/1.774도 |
| 지상 시작·정지·OTM/Strafe | `Ground_20261021_190747_913516.json`, 2,059표본. 실제 Start/Stop, 양쪽 회전 모드와 16종 재생 후보 확인 |
| Strafe Sprint 공중 8방향 | `SprintAir_20261021_191811_344994.json`, 3,738표본. 재선택 증가 8, 누락·playhead 되감기 0 |
| OTM/Strafe 공중·착지 8조건 | `AirLanding_20261021_191922_058457.json`, 4,740표본. 공중의 실제 회전 모드, 착지·재선택·연속성 확인 |
| OTM/Strafe 급회전 8조건 | `SprintTurn_20261021_192035_120719.json`, 5,700표본. 170/179/181/190도, 안정 구간 facing 최대 오차 0.191도 미만, 속도 581.86 이상 |
| 전신 몽타주 6개 | `MontagePose_20261021_192127_658134.json`, 281개 full-weight 포즈 표본. authored 하체 대비 최대 관절 회전 오차 약 0.060도, base player clock 진행과 slot 해제 확인 |

자료는 `Saved/Validation/PlayerLocomotion/`에 있다. Loop/Ground는 최종 Chooser·source 수명 수정 이후, 무기 콜백 admission 추가 이전 실행이다. 나머지 네 검사는 무기 콜백까지 포함한 최종 바이너리에서 실행했다. 근거를 합친 목록은 `AnimationFlowAudit_20261021/FinalLiveChecks.json`이다. 카메라 ControlRotation과 Enhanced Input 액션을 구동한 검사이며 수정 후 사용자의 실제 W·마우스 하드웨어 입력을 다시 검수한 결과는 아니다.

검사기 중간 실패도 보존한다. 초기 전투 입력 pulse가 callback 시작 지연으로 누락된 경우, Python enum 문자열 대소문자를 잘못 비교한 경우, 마지막 Sprint 공중 구간이 기본 바닥 밖으로 나간 경우를 구분해 수정했다. 마지막 경우 실제 위치 X≈8,669에서 바닥 경계 X≈8,167을 벗어난 것이 확인됐다. 재검사는 authority/client **PIE 바닥만** 약 49,005-unit 너비로 넓혔고 생산 레벨은 수정하지 않았다. 착지를 강제하거나 물리/애니메이션을 reset해 통과시키지 않았다.

이 단계의 GUI 세션에서는 Pose Search 위상 경고, PredictionKey 재귀, 무기 Sheathe 부착 경고가 재발하지 않았다. 당시 초기화 구간의 `LogAutomationTest: Condition failed` 출력과 첫 네트워크 PIE의 Voice 모듈 지연 로드 직후 Iris `BroadcastLoadedModulesUpdated` 경고 1개는 미해결 항목으로 남겼다. 이후 원인 확인과 해결 결과는 아래 최종 재점검 절을 따른다. 로그를 숨기거나 검증 결과에서 실제 테스트 실패를 제외하지 않았다.

마지막 PIE를 종료하고 background throttle 원래 값(true)을 복원했다. 미저장 content/map 0개를 확인한 뒤 두 프로젝트 에디터를 정상 종료했다. `FinalClose.json`, 종료 로그와 최종 패키지 해시를 보존했다. 원본 ABP, 저장한 회전 Chooser와 Run BlendSpace의 해시는 PIE 이후에도 유지됐다. fresh registry에서 구형 rotation query UDS의 referencer는 0개지만 제작 자산 삭제 근거로 단정하지 않아 보존했다. 기존 사용자 변경은 유지했으며 이 절의 구현·검증 단계에서는 커밋·push를 수행하지 않았다.

## 유지한 경계와 한계

포즈 노드·StateEntry/OnUpdate/BecomeRelevant·Pose History·슬롯/레이어 연결은 ABP에 유지한다. 이는 동일 상태 계산을 BP/C++ 양쪽에 두는 중복이 아니다. Chooser·PSD·커브·리타깃/몸체 프로필은 디자이너의 제작 데이터이므로 C++ 상수로 치환하지 않는다.

OTM/Strafe, Strafe Sprint 전방 세 방향, 공중 재선택·관성·playhead, Manny 라이브러리와 Bip01 대검 몸체의 책임을 유지했다. 새 애니메이션 일괄 수입, Neutral/Relaxed 재구성, crouch/traversal/slide 활성화는 수행하지 않았다. 자동 입력 검사와 수치 일치는 실제 W·마우스의 최종 체감 검수, 전체 448클립의 모든 프레임 외형 검사, 새 군중 성능·Cook/Shipping·장시간 네트워크 검증을 대신하지 않는다.

## 후속 최종 재점검: 초기화와 Iris

원본 디스크 패키지 해시가 이전 조사와 같은 것을 확인한 뒤 확보한 노드·핀 자료를 재사용했다. trajectory/essential/states/aim/lean, 동적 play rate와 커브 부재 fallback, desired facing의 0.1–1.5초 샘플, root translation, State Controller의 collapsed pivot 조건·상태 clock까지 다시 대조했다. 현재 활성 Ground/InAir 흐름에 추가 누락은 발견하지 못했다. 이것은 샘플의 모든 부가 기능을 구현했다는 의미가 아니다. 독립 MM, slide/traversal/debug flying/interaction 분기는 위 대응표의 의도적 차이다.

실제 MCP 연결로 생산 BP/ABP 7개의 계약을 새로 읽었다. 마스터의 75개 그래프·BP 변수 0개·native 변수 101개와 6개 관련 자산의 의미상 연결은 이전 조사와 같았다. 마스터의 연결되지 않은 `ErrorTolerance` 핀 6개는 에디터 재구성으로 ID만 달랐으며 이름·기본값·형식·링크가 같은 것을 별도로 확인했다. 직업 BP의 ConstructionScript 8개 호출은 각 의상 파츠의 native `AttachAndSetLeader` 연결이며 매 프레임 상태 계산의 중복이 아니다. 자료는 `Saved/Validation/AnimationFinalAudit_20261021/FreshContractsAndCulture.json`과 focused pin 기록에 있다.

### 초기화 오류의 원인과 실행 조건 보정

`LogAutomationTest Log`로 시작 검사의 이름을 표시하니 오류 15개는 엔진 5.8.3의 `FUnifiedErrorTest_CreateErrorMessage`(7), `FUnifiedErrorTest_CreateErrorMessageWithContext`(4), `FStructuredLogFormatTest`(4)였다. 엔진 소스에서 localized `FText` 결과를 고정 영문 문자열과 비교하는 것을 확인했다. 한국어 언어 상태에서는 올바른 번역문도 실패로 판정되었다.

프로젝트의 에디터 모듈은 **UE 5.8.3 + 개발 자동화 검사**에서 이 세 검사의 Start/End 경계에만 영문 source-language 조건을 적용한다. `BackupCultureState/RestoreCultureState`로 언어·지역·asset group 상태를 보존하고, 검사 종료·취소와 모듈 종료 시 복원한다. assertion과 실패 집계는 원래대로 실행한다. 전역 에디터 언어 설정·테스트 제외·로그 suppression으로 해결하지 않는다. 실제 시작 검사와 명시적 자동화 실행에서 세 검사가 성공했고, GUI와 종료 receipt의 language=`ko`, locale=`ko-KR`도 유지됐다.

이것은 설치 엔진 테스트의 언어 가정을 프로젝트 테스트 경계에서 보정한 호환 처리다. 엔진 업그레이드 때 해당 세 테스트의 고정 영문 기대값 문제 해결 여부를 확인해 제거한다. 실행 시점의 언어 변경에 비용이 있으므로 다른 테스트·게임 프레임으로 적용 범위를 확대하지 않는다. 이 세 검사에 대해 약 0.5–0.6초/개가 관측됐다. 게임/Shipping 로코모션의 프레임 최적화로 표현하지 않는다.

### Iris 경고의 원인과 의존 모듈 수명

첫 클라이언트 연결 → OnlineSubsystemUtils의 voice interface 초기화 → `FVoiceModule::Get()`의 지연 로드 → 이미 활성인 Iris ReplicationSystem에서 module registry 변화가 발생하는 순서였다. `Voice`의 capture 설정은 꺼져 있어도 이 조회는 모듈을 로드한다. 엔진 `IrisCoreModule.cpp`의 활성 시스템 중 registry broadcast 경고와 일치한다.

기존 게임 모듈 초기화 경계에서 client-capable target의 `Voice`를 먼저 로드하고 `DynamicallyLoadedModuleNames`에도 선언했다. dedicated Server target에는 이 의존성을 추가하지 않는다. 기존 Editor/Server의 `PerfCounters` 사전 로드는 유지했다. 로그 레벨·Iris replication·voice capture 설정은 변경하지 않았다. 설치 엔진이나 특정 PC의 시작 옵션에 의존하지 않는 저장소 소스 변경이다.

### 후속 검증과 종료

- 최종 direct UBT `Project_JEditor Win64 Development`와 `Project_J Win64 Development`가 각각 7.67초/10.73초에 성공했다. `BuildEditorFinal.log`와 `BuildGameFinal.log`를 보존했다. 별도 Server/Shipping target·Cook 검증은 수행하지 않았다.
- native 로코모션·리타깃 등 67개와 위 엔진 검사 3개, 총 **70개 모두 성공**했다. runner는 warning-bearing 결과도 실패로 판정하며, 보고서는 `Saved/Validation/PlayerLocomotion/AnimationFinalWarningsComplete_20261021/Automation/index.json`에 있다. 초기화 `Condition failed`는 0개였다.
- 새로운 GUI/네트워크 PIE에서 첫 연결부터 Iris 경고가 0개였다. `LoopTurning_20261021_194305_506540.json`의 실제 루프 5종은 각각 111개 측정 표본, root/actor 최대 차이 1.661/1.660/1.593/1.663/1.666도였다.
- `AirLanding_20261021_194503_046967.json`은 4,741표본·OTM/Strafe 8조건, 재선택 증가 2, 애니메이션 누락·playhead 되감기 0으로 통과했다. 이번에는 초기화 경계만 변경했으며 이동·점프·클립 선택 계약은 유지했다.
- 관측 스크립트의 첫 전체 JSON 동일성 검사는 위의 transient pin ID 차이 때문에 실패했다. 차이를 조사해 의미상 계약과 구분했다. 이 중간 진단 실패를 애니메이션 오류 또는 최종 성공으로 잘못 합산하지 않는다.
- 첫 headless 진단 프로세스 PID 25108은 일반 QUIT 이후 남아 사용자 승인으로 그 프로세스만 강제 종료했다. GUI 검증 에디터는 미저장 content/map 0, throttle=true와 한국어 설정을 확인한 뒤 정상 종료했다. `FinalClose.json`과 종료 로그를 보존했다.
- 최종 70개 검사 프로세스도 TestExit로 종료됐다. 원본 ABP·회전 Chooser·Run BlendSpace 해시를 다시 확인했으며 기존 사용자 변경을 덮어쓰지 않았다. MCP 플러그인의 라이선스 안내는 진단 오류와 구분한다. 이 절의 구현·검증 단계에서는 커밋·push를 수행하지 않았다.
