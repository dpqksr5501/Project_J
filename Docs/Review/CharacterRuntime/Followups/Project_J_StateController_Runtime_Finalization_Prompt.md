# State Controller 런타임 마무리 요청

이 문서는 캐릭터 런타임 책임 분리 후 남은 State Controller 책임을 정리하기 위해 작성한 당시의 요청 기록이다. 실제 변경과 검증 결과는 [마무리 결과](Project_J_StateController_Runtime_Finalization_Result_2026-09-24.md)를 확인한다.

## 0. 대상

Repository:
https://github.com/dpqksr5501/Project_J

Branch:
Character_Test

작업 시작 시 현재 HEAD를 다시 확인한다.

확인 당시 HEAD:
289007178460409bedcc63ed1f5abca438dadb5e

Engine:
Unreal Engine 5.8

이 작업은 이전 Character Runtime Responsibility Refactor의 후속 마무리 감사/리팩토링이다.

이전 작업에서 다음은 이미 충분히 분리되었다고 본다.

- FProject_JMotionMatchingRuntime
- FProject_JMotionMatchingSelectionPolicy
- FProject_JLocomotionContextBuilder
- FProject_JRemoteLocomotionRuntime
- FProject_JTurnInPlacePresentationRuntime
- UProject_JLocomotionAnimStateComponent의 semantic locomotion ownership
- AProject_JPlayerCharacter의 TIP presentation 세부 정책 분리

따라서 이번 작업의 기본 대상은 다음 두 개다.

- UProject_JCharacterAnimInstance
- FProject_JStateControllerRuntime

기존 MM / Locomotion / PlayerCharacter / GAS / SSR / Retarget / Mount 구조를 다시 전면 리팩토링하지 않는다.

---

# 1. 먼저 해야 할 일 — 왜 이전 작업이 부분 분리에 그쳤는지 분석

이전 작업은 GPT-6 Sol로 수행되었고, FProject_JStateControllerRuntime을 만들었지만 CharacterAnimInstance가 여전히 State Controller의 많은 내부 필드를 직접 조작한다.

현재 감사 시점의 대표 상태:

- Project_JCharacterAnimInstance.cpp: 3600줄 이상
- NativeUpdateAnimation: 약 800줄
- FillLocomotionStateThreadSafeData: 약 350줄 이상
- ResolveStateControllerPresentationStateWithPlaybackHold: 약 350줄 이상
- EvaluateStateControllerAnimationChooserOnGameThread: 약 350줄 이상

또한 CharacterAnimInstance.cpp가 다음 Runtime 내부 state를 직접 다수 접근한다.

- StateControllerRuntime.PlaybackHoldState
- StateControllerRuntime.PlaybackHoldStartedAtSeconds
- StateControllerRuntime.bGroundStopConsumed
- StateControllerRuntime.HeldLandingPresentationRevision
- StateControllerRuntime.bForceTurnInPlaceReselect
- StateControllerRuntime.Pivot...

확인 당시 단순 검색 기준 runtime public field 직접 접근은 약 90회 수준이었다.

반면 PrepareDesiredState(), ConsumeTurnSequence(), Reset() 같은 runtime API를 통한 조작은 상대적으로 적었다.

---

# 2. 무조건 추가 리팩토링하지 말고 먼저 이유를 판정

먼저 현재 코드를 읽고, 이전 작업이 StateController를 부분 분리까지만 한 이유가 다음 중 무엇인지 판단하라.

A. 의도적으로 적절한 엔진/asset boundary를 남긴 것
B. 위험을 줄이기 위한 단계적 migration
C. Blueprint / AnimBP / Chooser reflection 계약 때문에 완전 분리가 부적절했던 것
D. UObject / UAnimSequence / Chooser / PoseSearch 접근 때문에 AnimInstance adapter가 필수였던 것
E. 테스트 가능한 state만 먼저 runtime으로 옮긴 것
F. 시간/범위상 실제 state-machine logic 이동이 덜 끝난 것
G. 단순히 state storage만 추출하고 ownership 이전을 충분히 마치지 못한 것

한 가지가 아니라 여러 이유일 수 있다.

코드 근거를 들어 판정하라.

---

# 3. 반드시 답해야 할 질문

## Q1
왜 FProject_JStateControllerRuntime의 많은 필드가 public이고, AnimInstance가 직접 수정하도록 남겨졌는가?

이게 필요한 adapter boundary인지, 임시 migration 상태인지, 잘못된 encapsulation인지 판단한다.

## Q2
다음 로직을 Runtime 안으로 옮기면 실제 책임 분리가 좋아지는가?

- Playback hold lifecycle
- Pivot commit/cancel/suppress
- TIP sequence consumption
- Landing presentation revision ownership
- Ground Stop consumed state
- Full-body montage interruption/reset
- Combat Intro/Outro presentation boundary reset
- same-state re-entry / stale state prevention

## Q3
반대로 다음은 AnimInstance에 남겨야 하는가?

- Chooser API invocation
- PoseSearch API invocation
- UAnimationAsset / UAnimSequence access
- reflection mirror publication
- AnimGraph getter
- Proxy publication
- contact curve read
- montage weight sampling

## Q4
완전 캡슐화가 오히려 과도한 parameter passing, output struct 복사, UObject dependency 재도입, 양방향 synchronization, 복잡한 adapter를 만든다면 어느 경계에서 멈추는 게 더 좋은가?

---

# 4. 분석 결과에 따른 분기

## Case A — 현재 부분 분리가 의도적으로 적절함

코드 근거상 현재 구조가 이미 가장 안전한 경계라면 억지로 runtime에 모든 로직을 이동하지 않는다.

대신 다음 정도만 수행한다.

- direct public field 중 정말 필요한 것만 유지
- 상태 소유권 문서화
- accessor/method로 바꾸면 명확해지는 일부만 정리
- stale state/reset 계약 보강
- 테스트 보강

그리고 종료한다.

## Case B — 실제로 미완성인 책임 이동이 존재함

현재 StateControllerRuntime이 사실상 state bag이고 AnimInstance가 여전히 state machine owner라면 의미 있는 policy/lifecycle을 Runtime으로 이동한다.

단 다음 경계를 지킨다.

AnimInstance에서 엔진 API 호출
Runtime에서 pure/state policy

---

# 5. 최종 목표

권장 목표는 다음과 같다.

CharacterAnimInstance
- Build snapshot
- Read engine state
- Invoke Chooser / PoseSearch
- Publish reflected mirrors
- Publish proxy
- StateControllerRuntime.Update(...)

StateControllerRuntime
- playback hold lifecycle
- pivot lifecycle
- TIP sequence lifecycle
- landing presentation lifecycle
- stop consumption
- montage-boundary reset policy
- stale re-entry prevention

중요: StateControllerRuntime은 gameplay state owner가 아니라 animation presentation runtime이다.

Gameplay / locomotion semantic Source of Truth는 기존 GAS, CombatStateComponent, LocomotionAnimStateComponent에 남는다.

---

# 6. StateControllerRuntime 캡슐화

가능하면 public field 직접 조작을 줄인다.

현재 예:
- StateControllerRuntime.PlaybackHoldState = ...
- StateControllerRuntime.Pivot.Cancel()
- StateControllerRuntime.bForceTurnInPlaceReselect = ...

이를 무조건 getter/setter 수십 개로 바꾸라는 뜻은 아니다.

오히려 의미 있는 operation으로 묶는다.

예:
- ResetForFullBodyActionStart(...)
- ResetForCombatPresentationBoundary(...)
- CommitPivot(...)
- CancelPivot(...)
- SuppressPivot(...)
- ConsumeTurnInPlaceSequence(...)
- BeginHold(...)
- ReleaseHold(...)
- ConsumeGroundStop(...)
- OnLandingPresentation(...)

실제 이름은 현재 코드에 맞게 설계한다.

---

# 7. 피해야 할 잘못된 encapsulation

값 하나마다 setter를 만드는 것은 피한다.

목표는 값 단위 API가 아니라 의미 있는 state transition API다.

---

# 8. Runtime Input / Output 구조

필요하다면 다음 형태를 고려한다.

- FProject_JStateControllerRuntimeInput
- FProject_JStateControllerRuntimeOutput

하지만 매 프레임 거대한 struct를 새로 복사하거나 AnimInstance snapshot 전체를 복제하지 않는다.

실제 필요한 값만 받는다.

---

# 9. NativeUpdateAnimation 정리

현재 NativeUpdateAnimation 안에는 State Controller 관련 Pivot reset, Full-body montage start/end, Combat presentation start/end, cached chooser reset, landing suppression, MM force reselect, TIP state cleanup이 상당히 있다.

이 중 engine observation과 policy reaction을 분리한다.

예:
AnimInstance: "FullBody montage just started"
Runtime: "그러면 어떤 state를 reset/cancel/consume할지 결정"

AnimInstance가 세부 StateController 필드를 직접 초기화하는 코드를 줄인다.

---

# 10. FillLocomotionStateThreadSafeData 정리

이 함수는 가능한 한 Locomotion semantic state -> animation snapshot adapter에 가까워져야 한다.

현재 들어 있는 Pivot cancel, Pivot supersede, Pivot redirect, suppression, active pivot state mutation 같은 presentation state-machine logic은 Runtime으로 이동 가능한지 우선 검토한다.

---

# 11. ResolveStateControllerPresentationStateWithPlaybackHold 정리

현재 이 함수가 큰 이유를 분석한다.

다음을 분리한다.

- 실제 state machine policy
- engine/asset adapter
- debug/log
- Chooser-specific glue

특히 Desired state preparation, hold enter/continue/exit, TIP sequence consumption, Landing revision guard, Stop one-shot consumption, Pivot cancel/commit, stale transition suppression은 Runtime 소유가 더 적절한지 검토한다.

---

# 12. EvaluateStateControllerAnimationChooserOnGameThread

이 함수는 무조건 Runtime으로 이동하지 않는다.

Chooser는 Unreal Engine / UObject asset boundary다.

따라서 다음 경계가 적절하다.

어떤 chooser context를 선택할 것인지 -> Runtime output/policy
Chooser API 실제 호출 -> AnimInstance

---

# 13. Cached animation asset ownership

다음은 reflection/GC 문제 때문에 AnimInstance에 남겨야 할 수 있다.

- TObjectPtr<UAnimationAsset>
- TWeakObjectPtr<UChooserTable>
- UPoseSearchDatabase*

이전 GPT-6 Sol 작업이 이를 일부러 AnimInstance에 남긴 것이라면 그 판단을 존중한다.

plain C++ runtime에 GC-managed UObject ownership을 억지로 밀어 넣지 않는다.

---

# 14. Runtime reset contract 강화

다음 상황에서 StateController runtime state가 정확히 reset되는지 확인한다.

- AnimInstance initialize/reinitialize
- Owner changed
- Possess / UnPossess
- Combat Intro start/end
- Combat Outro start/end
- Attack start/end
- Dodge start/end
- Hit React start/end
- Montage interrupted
- Landing interrupted
- Pivot interrupted
- TIP interrupted
- Mount transition
- Mesh/AnimInstance replacement

필요하면 의미 있는 reset API를 만든다.

---

# 15. Stale state 방지

특히 다음 버그를 막는다.

- Combat montage 끝난 후 이전 Land가 부활
- Attack 끝난 후 이전 Pivot이 부활
- Stop이 같은 input release episode에서 반복 재생
- 소비된 TIP sequence가 재진입
- 중단된 Pivot revision 재사용
- old Chooser asset이 transition 뒤 다시 등장

---

# 16. 이미 잘 분리된 영역은 다시 건드리지 않는다

다음은 명백한 correctness bug가 없는 한 재설계하지 않는다.

- FProject_JMotionMatchingRuntime
- FProject_JMotionMatchingSelectionPolicy
- FProject_JLocomotionContextBuilder
- FProject_JRemoteLocomotionRuntime
- FProject_JTurnInPlacePresentationRuntime
- UProject_JLocomotionAnimStateComponent semantic ownership
- AProject_JPlayerCharacter orchestration
- GAS / SSR / Retarget / Mount / Weapon Presentation

---

# 17. 성능

이번 작업의 1차 목표는 성능 향상이 아니다.

주요 효과는 다음이다.

- state interference 감소
- stale state 감소
- 수정 영향 범위 감소
- 테스트 가능성 향상
- debug 범위 축소

다만 구조 변경 결과 다음이 명백하면 적용 가능하다.

- 동일 state 반복 계산 제거
- 중복 reset 제거
- 변경 없는 chooser mirror 게시 최소화
- 불필요 debug 계산 gating

FPS 향상을 측정 없이 주장하지 않는다.

---

# 18. 테스트

기존 테스트를 삭제/약화하지 않는다.

최소 다음을 재실행한다.

- StateControllerRuntime
- TurnInPlaceAndCombatStop
- CombatStrafeRunPivotPolicy
- MotionMatching
- Animation Snapshot
- Combat transition
- Landing
- Remote locomotion
- Weapon presentation
- Mount
- SSR

---

# 19. StateControllerRuntime 신규/보강 테스트

최소 다음 시나리오를 pure runtime 수준에서 검증한다.

1. locomotion loop -> move release -> Stop 1회
2. 같은 release episode에서 Stop 재진입 금지
3. new movement -> Stop consumption reset
4. Pivot commit -> redirect -> cancel
5. Pivot A -> Pivot B supersede
6. consumed Pivot revision stale re-entry 금지
7. TIP sequence consumed -> same sequence 재진입 금지
8. local/remote TIP sequence 분리
9. Landing revision interrupted -> old Land 부활 금지
10. FullBody action start -> held one-shot clear
11. FullBody action end -> stale state 없이 current locomotion으로 복귀
12. Combat Intro/Outro boundary -> pre-transition one-shot 부활 금지
13. Reset -> 모든 transient presentation state clear

---

# 20. Build 규칙 — 반드시 Headless CLI

Visual Studio solution/IDE를 사용하지 않는다.

금지:
- .sln Build Solution
- Visual Studio IDE build
- devenv.exe
- Unreal Editor Compile
- Live Coding

반드시 UnrealBuildTool.exe 또는 Engine\Build\BatchFiles\Build.bat을 PowerShell/터미널에서 직접 실행한다.

빌드 오류는 stdout/stderr/exit code로 판단한다.

사용자 클릭이 필요한 GUI popup이 뜨는 빌드 방식은 사용하지 않는다.

---

# 21. Build 대상

최소:
- Project_JEditor Win64 Development
- Project_J Win64 Development

가능하면:
- Project_J Win64 Shipping

외부 파일 lock이 생기면 compile error와 link/file lock error를 정확히 구분한다.

---

# 22. 완료 판정

StateControllerRuntime:
- 단순 state bag이 아니라 의미 있는 presentation lifecycle/policy owner가 됨
- public field 직접 조작이 실질적으로 감소함
- 값 단위 setter 남발 없이 semantic operation API를 사용함
- Pivot/TIP/Landing/Stop/hold stale-state 정책이 runtime에 모임
- pure runtime 단위 테스트가 가능함

CharacterAnimInstance:
- StateController 세부 state machine logic이 의미 있게 감소함
- engine UObject/Chooser/PoseSearch adapter 역할은 유지함
- AnimBP/Chooser reflection API는 유지함
- snapshot/proxy boundary 유지
- worker에서 UObject 접근 없음

기존 시스템:
- MM Runtime 재설계 안 함
- Locomotion semantic Source of Truth 유지
- PlayerCharacter orchestration 재확장 안 함
- GAS/SSR/Retarget/Mount/Weapon 구조 회귀 없음

---

# 23. 중요한 종료 기준

파일 줄 수 자체를 목표로 하지 않는다.

CharacterAnimInstance.cpp가 여전히 길어도 실제 state ownership이 명확하고 StateController policy가 Runtime에 있으며 AnimInstance가 engine adapter 역할을 한다면 종료해도 된다.

반대로 파일만 나눠서 CharacterAnimInstance_StateController.cpp를 만들었는데 같은 클래스가 같은 state를 직접 조작한다면 완료로 보지 않는다.

---

# 24. 이전 GPT-6 Sol 작업에 대한 최종 설명 포함

최종 보고서에 반드시 다음 섹션을 추가한다.

## Why the previous refactor stopped at partial extraction

여기서 코드 근거로 설명한다.

- 왜 이전 작업이 부분 추출에 그쳤는지
- 그게 의도적으로 좋은 판단이었던 부분
- 실제로 미완성이었던 부분
- 이번 작업에서 무엇을 더 옮겼는지
- 무엇은 의도적으로 AnimInstance에 남겼는지
- 왜 거기서 더 분리하지 않는지

이전 작업을 무조건 잘못됐다고 평가하지 않는다.

---

# 25. 최종 보고 형식

# StateController Runtime Finalization Result

## 1. HEAD / Build / Tests
- HEAD:
- Editor build:
- Game build:
- Shipping:
- Tests:

## 2. Why the previous refactor stopped at partial extraction
Intentional boundaries:
- ...

Incremental/risk-control reasons:
- ...

Actually incomplete responsibility:
- ...

## 3. Before
CharacterAnimInstance:
- ...

StateControllerRuntime:
- ...

## 4. After
CharacterAnimInstance:
- ...

StateControllerRuntime:
- ...

## 5. Ownership
Gameplay locomotion:
Presentation state:
Chooser:
PoseSearch:
Pivot:
TIP:
Landing:
Stop:
Montage boundary:

## 6. Direct State Access
Before:
- approximate direct accesses:

After:
- remaining direct accesses:
- why remaining ones are justified:

## 7. Regression Protection
- ...

## 8. Performance
Measured:
- ...

Unmeasured:
- ...

## 9. Remaining Work

추가 material code work가 없다면 정확히:

No material code follow-up is required for the StateController responsibility boundary.

---

# 26. 최종 지시

이 작업은 이전 리팩토링을 부정하고 다시 갈아엎는 작업이 아니다.

먼저 이전 GPT-6 Sol 작업이 왜 부분 추출에 그쳤는지 현재 코드로 분석한다.

그 결과 의도적인 engine/reflection boundary는 유지하고, 실제로 아직 AnimInstance가 직접 소유하는 presentation state-machine policy만 FProject_JStateControllerRuntime으로 이동한다.

새 UObject / Component / Tick / Subsystem을 만들지 않는다.

완료 조건을 만족하면 추가 추상화 없이 종료한다.
