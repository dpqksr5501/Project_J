# Project_J 전체 애니메이션 자연스러움: 구현과 검증

작성일: 2026-10-06. 기준 HEAD: `94eac862f97e5d210d082f2e6cb1590a833a0419`. 엔진: 설치된 UE 5.8, Win64 Development Editor.

긴 낙하의 실제 출력과 FallOff→FallLoop 연결, 큰 이동 반전의 감속 중 수명, 외부 단발 OW 커브 적용을 수정했다. 일반 이동·작은 곡선·속도 변화·동작 연결·최종 follower·원격·군중을 함께 확인했다. **C++/그래프/출력 계약 검증을 완료한 범위와, 화면 자연스러움·접지·입력 지연의 미검증 범위를 구분한다. 전체 시각 품질 개선이 완료됐다는 판정은 하지 않는다.**

01–05는 과거 조사 snapshot으로 유지한다. 이번 문서는 새 구현 권한과 실행 결과에 관한 후속 문서다. 원래 조사 권한 제한을 이번 구현 작업에 적용하지 않는다.

## 시작 상태와 조사 이후 확인

- 시작 시 git 변경 목록과 Source/Config/관련 기존 에셋을 `Saved/Validation/Naturalness_20261006/Baseline`에 보존했다. 기존 MovingTurn/Strafe 수정, Combat 프로필, 새 Combat Turn PSD, 진단용 Config는 사용자 작업이었다. 이번 변경으로 간주하거나 되돌리지 않았다.
- 운영 pawn은 `/Game/Character_BPs/GreatSword/BP_GreatSword.BP_Greatsword_C`, source는 `CharacterMesh0`, 운영 AnimClass는 `/Game/Animation_Logic/ABPs/ABP_Humanoid_Master.ABP_Humanoid_Master_C`다.
- 보이는 follower는 `Greatsword`, AnimClass는 `/Game/Animation_Logic/ABPs/GreatSword/ABP_Greatsword_Woman_RunTIme.ABP_Greatsword_Woman_RunTIme_C`다. headless 실제 평가에서 source 89 bones/Visible=0, follower 338 bones/Visible=1을 확인했다. `ABP_Player`를 운영 그래프로 취급하지 않았다.
- Master는 로드 직후 dirty=false, Status=3이었다. 최초 read-only audit에서 이미 로드된 `/Game` dirty package도 기록했다. 당시 실행 중인 에디터가 없었으며, 다른 실행 세션의 저장되지 않은 메모리 변경을 덮어쓰지 않았다.
- 일반/Combat Run Cycle·Turn·Sprint Cycle의 실제 참조와 엔트리 수는 각각 15/16/5, 76/4/7이다. 새 후보 확대나 데이터 삭제는 하지 않았다. 단일 PSD 선행 선택과 MM 내부 Input→Result는 유지됐다.
- 수정 전 Master SHA256은 `3126F64F1227447B42D09F3C06BD060934652814469304E3A440988AFD4A7893`으로 기존 조사 지문과 일치했다. 외부 OW 오연결과 InAirLoop override 불일치, active Turn의 최소 속도 재검사는 현재 소스/그래프에도 남아 있었다.
- Unreal MCP는 명시적으로 허용됐지만 이번 세션에 호출 가능한 Unreal 도구가 없었다. computer-use의 native app 표면도 사용할 수 없었다. 대신 설치 엔진 API를 사용하는 좁은 Editor 자동화로 운영 에셋을 읽고, Master만 컴파일/저장했다. GASP는 수정·컴파일·저장하지 않았다. 레벨 저장·Save All·재임포트·엔진 변경·플러그인 설치는 하지 않았다.

## 현재 책임과 경계

| 책임 | 기존 owner / 이번 판단 | 갱신·고정 값 |
| --- | --- | --- |
| 입력·물리 이동·예측 | Player/CMC/기존 trajectory 수집. 유지 | 최신 input/world velocity/actor·controller 방향/trajectory를 GT에서 수집. 클립 캐시 때문에 고정하지 않음 |
| 상태·단발 수명 | LocomotionAnimStateComponent와 AnimInstance StateControllerRuntime. 필요한 수명 경계만 수정 | command/episode/epoch/revision·선택 클립·authored 시작 시점을 유지. 새 명령·취소·복귀는 기존 owner |
| 후보 선택 | native context→AssetSet의 단일 PSD 또는 State Controller Chooser. 유지 | 신규 pose 검색에 허용된 PSD만 제공. AirLoop 같은 사건의 mode/foot 변화로 Chooser 반복 평가하지 않음 |
| MM 검색 | 기존 Proxy와 generated MM nodes. 유지 | 최신 worker snapshot, continuing pose, pending explicit edge, 검색 후 소비. 일반 budget 복원 |
| 재생·연결 | Master의 MM/external Blend Stack 선택과 Inertialization. AirLoop 출력만 수정 | stack player별 asset/time/weight. 공중 loop의 clock을 단발 context 변경으로 재시작하지 않음 |
| 회전·루트 | CMC capsule, native TIP, 외부 OW/TIP Steering, OffsetRoot. OW 핀만 수정 | 물리 yaw와 시각 보정 구분. 기존 world facing 목표와 component-space pose 처리 유지 |
| 발·리타깃 | Master FootPlacement→LegIK→PoseHistory, Greatsword follower. 유지 | source와 follower의 실제 평가 단계/본 이름을 구분. 보정 상수·가중치를 추측으로 변경하지 않음 |
| 예산 | 기존 optimization tier/visibility/significance/ABA/URO. 유지 | local/remote/near/mid/far/hidden 의미 유지. 진단이 hidden follower를 깨우거나 task 완료를 기다리지 않음 |

새 manager, Tick, RPC, 복제 필드, 매 프레임 강제 검색, 전역 다중 PSD 검색은 추가하지 않았다. 정상 경로에 새 반복 Chooser 평가나 진단용 allocation을 넣지 않았다. 추가 graph traversal/문자열·본 조회는 기본 off인 기존 진단 안에서만 실행된다. 테스트의 샘플 수집 비용은 게임 정책과 별개다.

## 변경 원인과 구현

| 문제·재현 | 확인 근거 | 변경 owner·기대 효과 | 비용·회귀 위험 |
| --- | --- | --- | --- |
| 실제 낙하 중 FallLoop를 선택해도 MM/external 출력 조건이 loop를 제외 | native chooser 출력 조건과 Master bool blend 연결 | AnimInstance가 on-foot physical air의 InAirLoop만 external owner로 승인 | 조건 분기와 기존 stack 평가. 지상/탑승 loop까지 출력 우선권을 넓히는 위험을 좁은 조건·테스트로 제한 |
| 로컬 FallOff 마지막 자세가 낙하 중 유지됨 | 실제 local 재생에서 진입 asset time=1.600에 정지, loop 미출력 | 기존 State Controller가 완료된 FallOff를 loop로 해제하고 같은 의미 플래그의 재획득 방지 | 기존 completion/early window/max hold 사용. 새로운 JumpStart까지 막지 않는 회귀 확인 |
| 정상 반전 감속에서 active Turn PSD가 닫힘 | component eligibility와 phase builder 모두 최소 속도를 재검사 | component/policy의 entry qualification과 lifetime 분리, phase 소비자는 승인된 active request 유지 | 후보 확대 없음. 저속 신규 진입·실제 Stop 중 유지되는 위험을 별도 guard로 차단 |
| 외부 OW가 authored enable_warping을 Alpha로 사용하지 않음 | 활성 핀·설치된 5.8 OW 구현. TargetTime=0 | Master 외부 stack의 Alpha와 per-player time 핀 수정 | 기존 커브 조회 재사용. 커브가 없는/0인 구간의 OW 억제는 의도된 동작 변화이며 화면 효과 크기는 미검증 |
| 선택 로그만으로 실제 pose owner와 follower를 오인할 수 있음 | 캐시된 PSD_Idle 결과와 실제 외부 FallLoop 출력이 동시에 존재 | 기본 off AnimFlow의 branch weight·player time·source/follower 본 좌표 추가 | 대상 제한·샘플링·최대 rows 적용. trace on 비용을 shipping/기본 off 비용으로 오인하지 않음 |

### 1. 공중 loop 선택·출력 계약

파일: `Source/Project_JCharacter/Private/Animation/Project_JCharacterAnimInstance.cpp`, 함수 `EvaluateStateControllerAnimationChooserOnGameThread`.

이전에는 `hasSelected && IsTransitionState && !loop`만 external 출력에 들어갔다. 이제 이 계약에 **presentation=InAirLoop && GT snapshot의 CMC::IsFalling && OnFoot**을 추가한다. Idle/Cycle 등 다른 loop는 MM owner를 유지한다. montage/slot·상체·탑승의 downstream 우선권을 바꾸지 않았다.

InAirLoop 캐시는 동일 Chooser table과 physical-air/on-foot 조건에서 선택 asset/output/start time/revision을 유지한다. live movement/trajectory snapshot은 계속 갱신한다. physical ground 또는 다른 presentation으로 빠지면 기존 chooser 갱신·override 해제·PoseHistory 기반 MM return request로 복귀한다. return request의 검색 전 유지/검색 후 소비 구현은 수정하지 않았다.

`Project_JAirLoopTests.cpp`는 실제 `CHT_Player_StateControllerAnimations` 결과, 180회 mode/foot/속도 변화의 revision/재생 pulse 유지, 지상·disabled·mount 제외, stale landing flag와 실제 재낙하, worker return query를 검증한다. test friend는 기존 private 계약 테스트 방식으로 한 개 추가했다.

### 2. FallOff 완료와 같은 사건의 재획득

같은 파일의 `ResolveStateControllerPresentationStateWithPlaybackHold`를 수정했다. 첫 실제 재생 fixture는 nonlocal authority라 성공했으나, `SetAsLocalPlayerController()`로 로컬 입력 owner를 활성화하자 **5개 중 1개 실패**했다. 이 실패는 숨기지 않고 `FinalLocal`에 보존했다.

FallOff 의미 플래그의 약 2초 수명과 authored 진입 playback의 완료 수명이 달랐다. 기존 약 0.65초 max hold/completion 판정 뒤에도 DesiredState가 TransitionToInAir라 hold가 다시 시작돼, replay pulse 없이 clip 끝 자세가 유지될 수 있었다.

- held FallOff가 기존 `bTransitionAnimationAlmostComplete`에 도달했고 실제 낙하 중이며 새 JumpStart가 아니면 DesiredState를 InAirLoop로 넘긴다.
- 이미 같은 낙하에서 InAirLoop에 들어간 뒤 남은 FallOff 플래그만으로 TransitionToInAir를 재획득하지 않는다.
- 실제 `bIsJumping`에 의한 새 JumpStart, landing/new command, air-direction reselect의 기존 progress 계약은 보존한다. component의 의미 타이머를 바꾸거나 새 hold 상수를 추가하지 않았다.

이 수정 전 local 증거는 Frame 90/120에서 `M_Neutral_Jump_F_Off_Run_Lfoot` time=1.600, 실제 pose weight=1.0이었다. 수정 후 Frame 45/60/90에서 `M_Neutral_Jump_Loop_Fall` time=0.733/0.983/1.483, 실제 pose weight=1.0이다. 이후 loop wrap은 정상 재생이며 사건 revision 재시작으로 해석하지 않는다. 최종 4초 fall+2초 landing/recovery fixture의 Frame 60–239는 **180/180 physical-air loop 승인, 180/180 selected external player weight>0.9**였다. Frame 255/270은 Land override, Frame 330은 override=false 및 `PSD_Run_Cycle` 복귀를 기록했다.

이 전후 비교는 **FallOff 수명 보완 직전/직후**다. 처음 작업 시작 상태의 전체 N1–N6 영상 baseline으로 오인하면 안 된다.

### 3. MovingTurn entry와 active lifetime

관련 파일·함수:

- `Private/Project_JLocomotionAnimStateComponent.cpp::UpdateMovingTurnPolicy`
- `Public/Animation/Project_JMovingTurnPolicy.h::FInput/Update/DescribeNextUpdate`
- `Public/Animation/Project_JLocomotionContextBuilder.h::ResolvePhaseFamily`
- `Private/Tests/Project_JMovingTurnTests.cpp`, `Project_JStrafeFacingRedirectTests.cpp`

`bEligible`은 local/on-foot/physical ground/Run/input/Locomotion owner 및 action·montage·root motion·Start·Pivot·TIP·Land 제외를 뜻한다. finite nonnegative speed와 유효한 최신 move world direction도 요구한다. `bEntryQualified`는 기존 최소 속도 및 유효한 old horizontal velocity를 뜻한다. 신규 진입·rearm은 둘 다 필요하고, **active episode는 정상 감속만으로 종료하지 않는다**.

active 요청은 phase builder에서 Land/Air/Stop/TIP/Pivot/Start 우선권 이후, `bHasMoveInput && GroundMode=Locomotion`일 때 Turn으로 유지한다. 기존 작은 OTM 회전의 angle/speed 조건은 별도 경로로 남겼다.

입력 해제·실제 Stop·air/action/montage/mount·mode 변경, target 변화 >60도, move/facing 불일치 >45도, alignment exit, 기존 0.75초 timeout·0.1초 cooldown·rearm은 그대로다. 속도 179/80/0/100에서 같은 active Turn을 유지하는 순수 policy·phase와 **실제 component integration** 테스트를 추가했다. 저속 신규 진입 및 종료한 요청의 부활은 금지한다. 물리 acceleration/deceleration/yaw rate는 수정하지 않았다.

### 4. 외부 OW 그래프

변경 에셋은 `Content/Animation_Logic/ABPs/ABP_Humanoid_Master.uasset` **하나**다. 대상 graph/node:

`AnimGraph.AnimGraphNode_BlendStack_0.AnimationBlendStackGraph_0.AnimGraphNode_OrientationWarping_0`.

| 핀 | 이전 | 이후 |
| --- | --- | --- |
| Alpha | `K2Node_Knot_0.OutputPin` = native CombatStrafe OW gate | `K2Node_PromotableOperator_0.ReturnValue` = gate × enable_warping(asset, player time) |
| CurrentAnimAssetTime | 위 curve product | `K2Node_Knot_3.OutputPin` = 해당 stack player's 실제 asset time |
| CurrentAnimAsset / LocomotionAngle / TargetTime | player asset / native one-shot strafe angle / 0 | 유지 |

5.8 엔진은 TargetTime>0일 때 미래 root extraction에 CurrentAnimAssetTime을 사용한다. 따라서 이번 핵심 동작 변화는 **authored curve가 실제 Alpha를 제어**한다는 것이다. 잘못된 time 핀을 고쳤다는 사실만으로 이전 화면의 큰 오차를 입증하지 않는다.

`Project_JNaturalnessGraphTests.cpp`의 repair는 `-ProjectJRepairExternalWarping` opt-in, exact old topology 확인, dirty Master 거부, 해당 두 핀만 재연결, Blueprint compile error=0 확인, Master만 SavePackage하도록 구성했다. repair 자동화 1/1 성공했고, 최종 read-only audit는 `-ProjectJExpectWarpingContract`로 새 핀을 검증했다. 자동 로드/일반 테스트로 에셋을 쓰지 않는다. 이미 수정된 topology의 재실행은 no-op이다.

## 보정 순서·좌표·시점

- gameplay capsule/actor world yaw·translation은 CMC/기존 native TIP가 소유한다. 이번 변경은 CMC나 서버 이동 권위를 변경하지 않는다.
- GT의 최신 world velocity/input/controller facing으로 snapshot과 trajectory를 만든 뒤 Proxy가 worker에 전달한다. source bone post-evaluation 진단과 cached MM 결과는 서로 다른 시점이므로 별도 필드로 기록한다. cached 결과를 최종 출력으로 간주하지 않는다.
- external stack은 **Input→LocalToComponent→OW→TIP Steering→ComponentToLocal→Result**다. OW는 기존 component-space RootBoneTransform 경로와 one-shot 방향 각도를 쓰며, TIP Steering은 기존 world desired-facing quaternion과 enable_turninplacesteering gate를 쓴다. ProceduralTargetTime=10000/AnimatedTargetTime=0.5는 유지했다. 일반 MM Steering이나 두 번째 actor yaw 적용을 추가하지 않았다.
- 각 blend player subgraph의 tag 기반 asset/time getter를 유지했다. Alpha의 curve도 그 player의 asset/time을 쓴다. 블렌드 중 이전 클립을 마지막 선택 클립 시간 하나로 보정하지 않는다.
- MM/external 선택 뒤 Inertialization, 기존 slot/상체 additive·AO, OffsetRoot, FootPlacement→LegIK, PoseHistory, on-foot/mounted output 선택이 이어진다. OffsetRoot 일반 Rotation/Translation Release는 잔여 offset의 decay다. 전역 Accumulate나 즉시 zero 정책으로 바꾸지 않았다.
- source의 `foot_l/foot_r`, follower의 `Bip01-L-Foot/Bip01-R-Foot`를 구분했다. follower에 `root`가 없어 이전 socket fallback을 실제 root bone 값처럼 보고할 수 있었으므로 이제 `Root=MissingBone`과 `ComponentWorld`를 나눈다. pending follower는 `Pending`으로 표시하고 강제 평가하지 않는다. 좌표는 world이며 **접촉/plant·ground truth가 없는 좌표를 foot drift로 계산하지 않는다**.

## 유지하기로 판단한 구조·데이터

| 대상 | 판단과 이유 |
| --- | --- |
| CMC 가감속·trajectory collision 예측 | 유지. 물리 경로 문제/입력 지연의 실제 재현·측정 없이 animation 원인만으로 gameplay 이동을 변경하지 않음 |
| Cycle·Arc/Diamond/Hourglass 등 활성 coverage | 유지. 실제 작은 곡선·속도 변화에서 운영 그래프 평가를 확인. 기존 좋은 데이터 제거/각도별 Turn 강제 근거 없음 |
| 단일 PSD routing / PSS 특징·가중치 / continuing pose / 제외 구간 / search intervals | 유지. 전역 후보 경쟁·고빈도 검색의 비용 대비 품질 근거 없음. 실제 edge request 소비 회귀는 기존 엔진 검색 테스트로 확인 |
| Start/Stop/Land/Pivot/TIP의 direct Chooser·stack | 유지. 사건 ID·발 위상·start time·캐시·취소 소유권을 일반 MM으로 합치지 않음 |
| 과거 Pivot 재시작 / Land mode-change 복귀 | 이미 수정된 구현을 유지하고 회귀 검증. 이번 신규 버그나 해결 성과로 재분류하지 않음 |
| MM 내부 general Steering/OW, Distance Matching, Stride Warping | 도입하지 않음. 실제 보폭/회전 오차와 대상·중요도별 효과를 입증하지 못했으며 새 보정의 이중 회전·CPU 비용을 정당화할 수 없음 |
| Lean/AO/FootPlacement/LegIK/리타깃 설정 | 유지. 실제 follower 출력은 확인했지만 평지/경사/계단의 최종 접지 품질 판정이 없어 상수를 바꾸지 않음 |
| remote tier·hidden·ABA/URO | 유지. 로컬 camera intent를 원격에서 추정하지 않음. 실제 네트워크 및 동일 조건 군중 CPU 측정으로 회귀 관측 |

## 실행 검증과 한계

### 빌드·자동화·네트워크

| 실행 | 실제 결과 | 판정 범위 |
| --- | --- | --- |
| 시작 direct UBT + 기존 관련 자동화 | build 성공, **21/21** | 시작 상태의 기존 계약 baseline. 렌더 baseline 아님 |
| OW repair | **1/1**, Master compile error=0·save 성공 | 두 핀 변경과 패키지 저장 |
| 최종 direct UBT `FootDiagnosticBuild.log` | **Succeeded**, 5.25초 | Project_JEditor Win64 Development. packaged/client/server target 빌드는 수행하지 않음 |
| `FinalOutput/Automation/index.json` | **28/28**, failed/notRun/inProcess=0 | air/turn/search/Pivot/Land/TIP/mount/trace/recovery 및 local 실제 AnimBP 평가 |
| `FinalNonLocal/Automation/index.json` | **1/1** | 동일 6개 playback 시나리오의 nonlocal authority. simulated proxy라고 부르지 않음 |
| 기존 network fixture, `-Animation`, 2 clients/100 NPC, `-PktLag=80 -PktLoss=2` | server/client 모두 success·exit=0, timeout=0 | 별도 프로세스 real sockets. AOI/FastArray·Start/Stop/Land·취소/병합·실제 simulated proxy 관측. 해당 flags의 유효한 RTT를 따로 실측한 것은 아님 |

network의 Start/Stop은 이벤트 sequence와 상태 정합성 검증이다. Landing은 실제 TransitionToLand/external pose 관측도 요구한다. client별 52/54 observations 모두 role=1(simulated proxy), stage 1–5였고, CSV에는 Idle와 Light Land 클립이 기록됐다. 이 테스트를 remote Start/Stop 클립의 자연스러움 또는 remote 긴 낙하의 검증으로 확대 해석하지 않는다. fixture가 render timestamp/velocity를 제어하므로 실제 화면 visibility/네트워크 smoothing 품질까지 입증하지 않는다.

UBT는 설치 엔진의 직접 `UnrealBuildTool.exe`를 사용했고 매 실행이 끝날 때까지 기다렸다. 실행 중인 editor/dotnet/UBT/Live Coding/MSBuild/ShaderCompileWorker를 중단하거나 중복 빌드하지 않았다. 최초 sandbox 실행은 AppData 로그 접근에서 거부돼 compiler에 도달하지 않았으며, Application event log를 확인하고 승인된 escalation으로 실행했다. 테스트 harness의 include/API/링크/fixture 가정 오류는 해당 최초 오류를 따라 수정한 뒤 재검증했다. 의미 있는 local FallOff 실패는 앞 절의 근거로 남겼다.

headless engine startup의 ToolsetRegistry/EditorToolset Python `AgentSkill`/`PythonTestRunner` 누락 오류가 로그에 있다. network verification은 이를 별도 목록으로 보존한다. 애니메이션/네트워크 assertion 성공과 로그 전체 무오류를 혼동하지 않는다. 엔진/플러그인을 변경하여 숨기지 않았다.

### N1–N6 전체 평가 범위

**요청된 수정 전 N1–N6의 화면·입력 baseline은 확보하지 못했다.** 시작 소스/활성 그래프·21개 기존 계약·CPU baseline은 확보했고, 수정 후 authored fixture는 실제 운영 BP/장비/source/follower/AnimBP를 실행했다. CMC tick을 끄고 velocity와 input을 주입했으며 NullRHI, 바닥 없는 transient world다. 이를 실제 사람 입력·물리 이동·PIE 영상 비교로 대체하지 않는다.

| 항목 | 이번에 실행한 것 | 남은 판정 |
| --- | --- | --- |
| N1 Idle/Start/Stop | 180 ticks, idle→input/가속→유지→해제/감속→idle, actual stack/branch/time·source finalization | 짧은 탭/반복 입력·양발·입력부터 화면 출력까지 지연·자세 튐 비교 |
| N2 속도 전환 | 180 ticks, 150/500/700/250 prescribed speed, Sprint 시작/해제, 실제 graph/clip/time | 150 speed가 authored Walk intent 전체를 뜻하지 않음. 실제 CMC 가감속·보폭/play rate·OTM/Strafe 비교 |
| N3 작은 곡선 | 180 ticks, 최대 60도 sin 곡선, 실제 Cycle/pose 출력 | 개별 15/30/45/60도 실제 입력 영상과 발 위상·clip 교체 품질 비교 |
| N4 Strafe facing | 180 ticks, Combat tag와 완만한 control yaw, 경로 유지, source/follower 좌표 | 사람이 움직인 camera·경로 변경, AO/visual yaw/발 미끄러짐 비교 |
| N5 공중/착지 | 360 ticks, 4초 physical fall + HandleLanded + 2초 recovery. local/nonlocal actual FallLoop와 Land→Run Cycle | 정지/이동 JumpStart, Light/Heavy·Sprint별 실제 바닥 충돌·출력 영상 |
| N6 최종 visual | 운영 Greatsword follower visible/AnimClass/338 bones, actual left/right feet world positions와 source 89 bones | 평지/경사/계단·plant/contact·pelvis·상체/리타깃의 실제 접지·foot drift |

각 시나리오 source finalization은 N1–N4/N6 각각 180, N5 360회다. 이것은 정상 평가의 증거이며 애니메이션이 자연스럽다는 assertion은 아니다. `SearchDueResultObservations`는 relevant MM node의 elapsed search time=0 관측 수다. certified 실제 검색 실행 횟수·검색 빈도로 보고하지 않는다. `MMCrowd.ActualEngineSearch/NestedGraphSearch/ReturnRequestLifetime/WorkerSearchPolicy`는 별도로 실제 엔진 edge 검색 및 pending-consumption 계약을 검증했다.

### S1–S15 회귀 범위

| 항목 | 이번 근거 | 미검증 부분 |
| --- | --- | --- |
| S1–S3 OTM/Strafe 큰 반전 | Geometry/Lifetime/Deceleration/실제 component/PhaseAndSearchEdge | 사람 WASD+mouse, 실제 CMC 반전 영상·입력 latency |
| S4 body-only/side/backpedal | 기존 geometry·Strafe selection/trajectory·gradual facing regression | 실제 화면 이중 회전·슬라이드 |
| S5 연속 Pivot | CardinalSelection/RedirectBasis/ConsecutiveReversals 실제 chooser 계약 | 양발 입력 영상 |
| S6 170/180/190·±180 | 기존 geometry·TIP/phase contracts | 연속 실제 player 출력의 root yaw 튐·발 drift |
| S7 연속 반전 | lifetime/targetchange/rearm/cooldown·component 경계 | 50/150/300ms 실입력 영상·지연 측정 |
| S8 Start 변화 | OneShotModeContinuity·기존 취소/return 계약과 real network Start/cancel | 각 5/10/15/30·move15/30/90 입력별 local 화면 |
| S9 Land 변화 | LandingReturnContext/StopIdleInterrupt/OneShotModeContinuity·net landing/coalescence | Light/Heavy stand/run/sprint의 local 입력별 화면 |
| S10 Jump/긴 fall | authored local/nonlocal 4초 fall 실제 loop 출력·캐시·새 JumpStart 수명 | 실제 Jump 입력 reselect 영상/충돌 landing |
| S11 mode 변경 | 실제 chooser에 반복 mode/foot 변화·기존 mode-continuity 계약 | 동작별 local 화면·combat bool 지연 전체 |
| S12 상위 action/mount | 기존 TIP/CombatStop·mount owner roundtrip/input·MovingTurn guards | montage/rootmotion/action별 실제 output 교차 전체 |
| S13 환경 | 미실행 | wall/corner/slope/stair/moving platform collision·접지 |
| S14 visibility/budget | 실제 엔진 pending/search 테스트·near/far/hidden CPU·network relevance·진단 pending 보호 | mid 전환의 렌더 복귀·실제 follower/IK 개별 profiler |
| S15 remote | real sockets·simulated proxies·packet lag/loss 설정·취소/병합/복귀 | jitter/late-event/teleport별 체계적 재현·RTT/visual smoothing 품질 |

### CPU 변경 전후

실행은 기존 `Measure-AuthoredAnimation.ps1`을 동일한 count=1/50/100/200, repeats=1로 전후 수행했다. 각각 count별 8행·자동화 1/1 성공, 전체 32행 완료다. CPU는 AMD Ryzen 7 9800X3D(8 physical/16 logical cores), 약 31GB RAM이다. 각 조건 120 warmup + 120 measured ticks, ABA budget=2ms, Near/Far/Hidden/AttackBurst와 ABA off/on을 교차했다. 실제 BP/장비/ABP/constructed followers를 평가하며, 비로컬 authority의 prescribed kinematics와 simulated render timestamp/significance를 사용한다.

아래 수치는 **World::Tick wall time**으로 worker 완료를 포함하고 외부 engine frame·rendering을 제외한다. animation 전용 CPU, local input latency, GPU/FPS, 실제 MM 검색 횟수, foot/IK/retarget 단독 비용이 아니다. baseline와 after의 동일한 fixture/장비 경로를 비교했다.

| Count | Scenario | ABA | mean before → after (ms) | p95 before → after (ms) | mean Δ |
| ---: | --- | --- | ---: | ---: | ---: |
| 1 | Near | off | 0.665 → 0.668 | 0.743 → 0.740 | +0.6% |
| 1 | Near | on | 0.622 → 0.686 | 0.771 → 0.780 | +10.2% |
| 1 | Far | off | 0.647 → 0.664 | 0.762 → 0.720 | +2.6% |
| 1 | Far | on | 0.671 → 0.661 | 0.733 → 0.733 | -1.5% |
| 1 | Hidden | off | 0.394 → 0.383 | 0.466 → 0.469 | -2.9% |
| 1 | Hidden | on | 0.375 → 0.402 | 0.464 → 0.484 | +7.3% |
| 1 | AttackBurst | off | 0.626 → 0.675 | 0.748 → 0.781 | +7.8% |
| 1 | AttackBurst | on | 0.678 → 0.675 | 0.812 → 0.781 | -0.4% |
| 50 | Near | off | 3.516 → 3.520 | 3.734 → 3.756 | +0.1% |
| 50 | Near | on | 3.579 → 3.498 | 3.806 → 3.733 | -2.3% |
| 50 | Far | off | 3.518 → 3.485 | 3.785 → 3.668 | -0.9% |
| 50 | Far | on | 3.541 → 3.721 | 3.744 → 4.082 | +5.1% |
| 50 | Hidden | off | 2.657 → 2.675 | 2.765 → 2.833 | +0.7% |
| 50 | Hidden | on | 1.675 → 1.650 | 1.808 → 1.787 | -1.5% |
| 50 | AttackBurst | off | 4.043 → 3.788 | 4.758 → 4.127 | -6.3% |
| 50 | AttackBurst | on | 3.785 → 3.794 | 4.150 → 4.255 | +0.3% |
| 100 | Near | off | 7.316 → 7.255 | 7.840 → 7.597 | -0.8% |
| 100 | Near | on | 6.804 → 6.824 | 7.120 → 7.312 | +0.3% |
| 100 | Far | off | 7.349 → 8.005 | 8.214 → 9.362 | +8.9% |
| 100 | Far | on | 6.930 → 6.791 | 8.438 → 7.234 | -2.0% |
| 100 | Hidden | off | 5.238 → 5.289 | 5.536 → 5.548 | +1.0% |
| 100 | Hidden | on | 3.224 → 3.214 | 3.461 → 3.478 | -0.3% |
| 100 | AttackBurst | off | 7.944 → 7.943 | 8.686 → 8.676 | -0.0% |
| 100 | AttackBurst | on | 8.124 → 7.717 | 8.869 → 8.581 | -5.0% |
| 200 | Near | off | 15.853 → 15.788 | 16.588 → 16.411 | -0.4% |
| 200 | Near | on | 12.502 → 12.380 | 13.277 → 12.814 | -1.0% |
| 200 | Far | off | 16.085 → 16.047 | 16.861 → 17.169 | -0.2% |
| 200 | Far | on | 13.306 → 12.185 | 14.421 → 12.647 | -8.4% |
| 200 | Hidden | off | 10.727 → 11.845 | 11.389 → 14.135 | +10.4% |
| 200 | Hidden | on | 6.228 → 6.163 | 6.606 → 6.576 | -1.0% |
| 200 | AttackBurst | off | 17.273 → 17.385 | 18.764 → 18.932 | +0.6% |
| 200 | AttackBurst | on | 16.787 → 16.980 | 18.852 → 19.183 | +1.2% |

첫 측정에서 100 Far/off와 200 Hidden/off가 증가하여 100/200을 after-only repeats=2로 추가 실행했다. 두 count 모두 16행·자동화 1/1 성공했다. 초기 비교에서 200 Near/on 평균은 12.502→12.380ms, 200 Hidden/off는 10.727→11.845ms였다. 추가 결과는 다음과 같다.

| Count | Scenario | ABA | 추가 repeat 0/1 mean (ms) | 추가 repeat 0/1 p95 (ms) |
| ---: | --- | --- | ---: | ---: |
| 100 | Far | off | 7.258 / 7.299 | 7.748 / 7.703 |
| 200 | Hidden | off | 10.838 / 11.172 | 11.702 / 12.583 |
| 200 | Near | on | 12.346 / 12.859 | 12.895 / 13.568 |

100 Far/off의 최초 증가가 추가 반복에서 재현되지 않았다. 200 Hidden/off의 추가 평균은 10.838/11.172ms로 최초 after 11.845ms보다 낮지만 baseline 10.727ms보다 약 1.0%/4.1% 높다. p95도 11.702/12.583ms로 baseline 11.389ms보다 높아 원인을 완전히 해소한 성능 판정은 아니다. 모든 최초 전후 수치와 추가 반복을 함께 보존하며, after 평균을 골라서 최초 증가를 지우거나 이를 통계적 개선/무회귀 보증으로 판단하지 않는다. baseline도 반복하고 실제 renderer/CPU trace를 분리해야 비용 차이의 원인을 확정할 수 있다.

200 Hidden의 bone finalizations와 MM result observations는 전후 모두 ABA off=24,000 / ABA on=480이었다. 기존 hidden 정책이 평가를 전부 0으로 만드는 구조는 아니다. 새 진단이 hidden을 강제 깨웠다는 증거는 없고 기존 정책의 처리량은 일치했다. 일반 update/본 finalize/캐시 결과 관측은 실제 pose search 횟수와 구분한다.

원본 실행 경로: `Saved/Validation/AuthoredAnimation_20261005/NaturalnessBaseline_20261006`, `NaturalnessAfter_20261006`, `NaturalnessAfterRepeat_20261006`. 각각 manifest·Count별 cpu.json·Automation/index.json·Run.log가 있다. [최초 비교 CSV](../../../../Saved/Validation/Naturalness_20261006/cpu-comparison.csv), [추가 반복 원자료](../../../../Saved/Validation/Naturalness_20261006/cpu-repeat.json). 새로운 runtime 분기 비용과 활성 OW/긴 낙하 비용을 이 ground crowd fixture에서 따로 측정했다고 주장하지 않는다.


## 변경 파일과 재현 방법

production 변경은 AnimInstance, LocomotionAnimStateComponent, MovingTurnPolicy, LocomotionContextBuilder, FlowTrace와 Master다. 새 테스트는 `Project_JAirLoopTests.cpp`, `Project_JNaturalnessPlayback.cpp`, `Project_JNaturalnessGraphTests.cpp`; 기존 MovingTurn/Strafe tests는 추가·fixture 보완했다. `CharacterAnimInstance.h`에는 테스트 friend, `Project_J.Build.cs`에는 editor-only PoseSearch/BlendStack, `Project_JCharacterEditor.Build.cs`에는 BlueprintGraph/PoseSearch 의존성을 추가했다. Config/Chooser/profile/PSD/PSS/원본 animation은 이번에 수정하지 않았다.

실행 결과의 local 파일 링크:

- [최종 빌드](../../../../Saved/Validation/Naturalness_20261006/FootDiagnosticBuild.log)
- [최종 28개 자동화](../../../../Saved/Validation/Naturalness_20261006/FinalOutput/Automation/index.json), [활성 graph](../../../../Saved/Validation/Naturalness_20261006/FinalOutput/ActiveGraph.txt), [실제 playback](../../../../Saved/Validation/Naturalness_20261006/FinalOutput/Playback.txt)
- [수명 보완 전 local 실패](../../../../Saved/Validation/Naturalness_20261006/FinalLocal/Automation/index.json), [해당 재생](../../../../Saved/Validation/Naturalness_20261006/FinalLocal/Playback.txt)
- [최종 nonlocal 재생](../../../../Saved/Validation/Naturalness_20261006/FinalNonLocal/Playback.txt)
- [network verification](../../../../Saved/Validation/GroupD_20260910/NaturalnessNetwork_20261006/verification.json), [client1 animation observations](../../../../Saved/Validation/GroupD_20260910/NaturalnessNetwork_20261006/Client1/animation-observations.csv), [client2](../../../../Saved/Validation/GroupD_20260910/NaturalnessNetwork_20261006/Client2/animation-observations.csv)
- [CPU comparison 원자료](../../../../Saved/Validation/Naturalness_20261006/cpu-comparison.csv), [세션 파일 지문/보존 확인](../../../../Saved/Validation/Naturalness_20261006/session-files.json), [세션 변경 patch](../../../../Saved/Validation/Naturalness_20261006/session.patch)

`Saved`는 git에 추가하지 않았다. 이 보고서의 수치·판정과 실행 경로는 보존하되, 다른 컴퓨터로 인계할 때 로그/원본 backup은 별도 전달해야 한다.

재검증 명령의 의미:

```powershell
# 먼저 모든 관련 engine/build 프로세스의 정상 종료 확인. build는 direct UBT 사용.
& 'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe' Project_JEditor Win64 Development '-Project=C:/Users/I/Documents/GitHub/Project_J/Project_J.uproject' -WaitMutex -NoHotReloadFromIDE

# 기본 read-only 테스트: repair opt-in flag를 붙이지 않는다.
# UnrealEditor-Cmd.exe에 -unattended -NullRHI -NoLiveCoding -TestExit='Automation Test Queue Empty'
# -ProjectJExpectWarpingContract 와 -ExecCmds='Automation RunTests ProjectJ.Animation.Naturalness+ProjectJ.MovingTurn+ProjectJ.StrafePivot+ProjectJ.MMCrowd; Quit'

# 별도 고유 RunName 필요. 이전 결과를 덮어쓰지 않는다.
& './Scripts/Validation/Measure-AuthoredAnimation.ps1' -RunName NewNaturalnessCPU -Counts 1,50,100,200 -Repeats 1
& './Scripts/Validation/Measure-Network.ps1' -RunName NewNaturalnessNetwork -Animation -ClientCount 2 -NPCCount 100 -PacketLag 80 -PacketLoss 2
```

화면 검증에서는 `p.ProjectJ.AnimFlow 2`, 필요할 때 `p.ProjectJ.AnimFlow.Actor <정확한 actor name>`으로 한 actor를 진단하고 종료 후 `p.ProjectJ.AnimFlow 0`으로 복원한다. 기존 사용자 Config의 `p.ProjectJ.MovingTurnTrace=2`는 이번 추가가 아니며 그대로 보존했다. verbose trace on 상태의 프레임 비용을 정상 비용과 비교하지 않는다.

## 변경별 rollback과 남은 작업

| 변경 | rollback 경계 |
| --- | --- |
| AirLoop/FallOff | AnimInstance의 이번 InAirLoop cache/override 및 completed FallOff 재획득 방지 hunk만 되돌림. 초기 사용자의 Turn selection/trace hunk는 유지 |
| Turn lifetime | 이번 bEntryQualified 분리·active phase hunk만 되돌림. 초기 사용자의 MovingTurn 자체 구현/프로필/PSD는 유지 |
| OW asset | 아래 초기 Master backup을 사용하거나 두 핀을 표의 이전 연결로 복원·해당 Master만 compile/save. 이후 사용자 asset 변경이 있으면 전체 backup 덮어쓰기 대신 핀만 복원 |
| 진단/테스트 | 이번 FlowTrace hunk, 새 test files, editor-only 의존성/friend와 기존 test 추가 hunk만 제거 |

시작 시 수정된 파일은 exact copy, 시작 시 clean이었던 텍스트 파일은 clean HEAD의 재구성 원본으로 구분하여 [session-files.json](../../../../Saved/Validation/Naturalness_20261006/session-files.json)에 기록했다. [session.patch](../../../../Saved/Validation/Naturalness_20261006/session.patch)는 **HEAD 전체 diff가 아닌 이번 세션만의 텍스트 변경**이다. 먼저 `git apply --reverse --check`로 확인하고 실제 rollback을 결정한다. 다른 변경이 끼어들었다면 수동 hunk 검토가 필요하다. `git checkout/restore`로 기존 사용자 작업 전체를 지우면 안 된다.

Master 원본: `Saved/Validation/Naturalness_20261006/Baseline/SourceSnapshot/Content/Animation_Logic/ABPs/ABP_Humanoid_Master.uasset`. 현재 에디터 정상 종료와 이후 수정 여부를 확인한 뒤 선택적으로 복원해야 한다. rollback 뒤에는 direct UBT와 관련 자동화 재검증이 필요하다. 이번 작업에서 rollback 자체는 실행하지 않았다.

남은 우선 작업은 동일 pawn/장비의 렌더링된 N1–N6 전후 비교, 실제 CMC/input-to-output 지연 측정, plant/contact 기준의 source·follower foot drift, S13 환경 충돌·접지, 170/190·연속 반전의 실제 영상이다. 증거를 얻은 다음에만 일반 MM의 per-player 보정, root recovery, PSS/coverage/stride 조정을 선택한다. CPU는 local/remote와 mid 중요도·실제 renderer·다중 반복을 추가하고, 필요하다면 Insights로 foot/retarget/search 실행 비용을 분리한다.
