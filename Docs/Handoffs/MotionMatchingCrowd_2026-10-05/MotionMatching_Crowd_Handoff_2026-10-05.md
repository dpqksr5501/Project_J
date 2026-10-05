# Project J 모션 매칭 전환과 군중 최적화 작업 인계

작성일: 2026-10-05, 한국 시간. 대상 저장소: `C:/Users/I/Documents/GitHub/Project_J`. 이 문서는 새 채팅에서 아직 구현하지 않은 애니메이션 개선 작업을 이어가기 위한 인계다. 조사 결과, 사용자 요구, 현재 코드, 제안과 검증 방법을 함께 기록한다.

**문서의 범위:** 현재 작업에 필요한 상세 정보와 이 채팅의 주요 요청·결정·기존 작업 결과를 복원하는 인계 문서다. 모든 메시지와 도구 출력을 한 줄씩 복제한 원문 대화록은 아니다. 초기 전체 감사의 세부 내용은 아래에 연결한 원래 보고서·범위표·검증 기록까지 읽으면 추적할 수 있다. 이 문서만 보고 전체 프로젝트·실제 에셋이 모두 같은 깊이로 검증됐다고 해석하지 않는다. 2026-10-05 재점검에서 첨부 사진 15장 보존, 초기 감사 이력, ABP 세부 값과 실제 엔진 파일 경로를 보완했다.

**현재 상태: 관련 코드와 UE 5.8 엔진 소스를 조사했고, 사용자는 구현 진행을 승인했다. 그러나 이번 MM 전환·군중 최적화 작업에 대한 코드 수정, 수정 전 백업, 테스트 추가, 빌드, 성능 측정은 아직 시작하지 않았다. 사용자가 채팅을 옮기기 위해 구현을 멈추고 이 인계 문서를 요청했다.** 기존 소스의 수정 표시를 이번 작업의 완료로 해석하지 말 것.

## 1. 새 채팅에 전달할 요청

다음 내용을 새 채팅에 보내고 이 파일을 읽게 하면 된다.

> 이 인계 문서를 읽고 Project J의 Blend Stack → Motion Matching 복귀 개선과 기존 군중 애니메이션 예산 정책 연결 작업을 이어서 진행해줘. 현재 움직임은 대체로 만족스럽고 복귀 순간만 조금 어색해. 수백 명이 보이는 MMORPG를 고려해서 로컬·중요 캐릭터 품질과 군중 비용을 함께 챙기고 싶어. 기존 AnimInstance·Proxy·예산 Subsystem을 활용하고 새 관리자나 컴포넌트는 필요할 때만 추가해줘. 오버 엔지니어링과 이전 수정의 회귀를 피하고, 필요한 테스트와 Editor/Game 빌드로 검증해줘. Unreal MCP는 별도로 허용하지 않았어. 인계 시점에는 이번 작업의 코드 수정은 시작하지 않았으니 먼저 현재 소스 상태를 확인해줘.

현재 요청은 기존 전체 소스 감사를 처음부터 다시 하는 작업이 아니다. 이전 감사와 수정 결과를 존중하면서, 아래 남은 작업에 집중한다.

## 2. 사용자 의도와 범위

- 기존 프로젝트는 OTM과 Combat Strafe를 모두 지원하는 Motion Matching 구조다. Start, Pivot, Jump 등의 일부 전환 모션은 별도 Blend Stack으로 재생한다.
- 사용자는 현재 이동 자체에 특별히 큰 어색함은 없다고 말했다. 주된 품질 관심은 **외부 Blend Stack에서 MM으로 돌아갈 때 약간 느껴지는 연결의 어색함**이다.
- 목표는 고품질 애니메이션과 수백 명이 동시에 보일 수 있는 MMORPG 비용 제어를 함께 확보하는 것이다. 전원에게 로컬 플레이어와 같은 비용을 지불하는 구조를 요구한 것은 아니다.
- 현재 구조도 Astra를 이용해 리팩터링한 상태라서, 이를 무조건 교체하거나 복잡한 새 프레임워크를 만드는 것을 원하지 않는다.
- C++ 작업은 필요한 부분만 한다. 기존 AnimInstance, thread-safe snapshot, Proxy, LocomotionProfile, Animation Budget Subsystem을 우선 활용한다.
- 성능 개선율이나 수백 명 지원 가능 여부는 실제 측정 없이 확정하지 않는다. 자동화 테스트 통과와 애니메이션의 시각적 품질도 별도 증거다.
- 공식 자료뿐 아니라 다른 개발자의 실제 구현 자료도 참고하는 것을 허용했다. 아래 연구 링크를 먼저 활용하고, 불확실한 부분만 추가 조사한다.

## 3. 환경과 반드시 지켜야 할 규칙

| 항목 | 인계 시점 기준 |
| --- | --- |
| 저장소 | `C:/Users/I/Documents/GitHub/Project_J` |
| 프로젝트 파일 | `Project_J.uproject` |
| 엔진 | `C:/Program Files/Epic Games/UE_5.8` |
| 셸 | PowerShell |
| 브랜치 | `main` |
| HEAD | `33b574a1d6775306a26357f67bc3d79311e7189b` |
| 상태 | 이전 작업과 사용자 변경이 많이 남은 dirty worktree. HEAD만으로 실제 상태를 재현할 수 없음 |

새 채팅 시작 시 [AGENTS.md](../../../AGENTS.md)를 읽고 현재 변경과 프로세스를 다시 확인한다.

1. Unreal C++ 빌드는 **엔진의 직접 `UnrealBuildTool.exe`**로 실행하고 완료까지 기다린다. 실행 중인 빌드를 중단하지 않는다.
2. UBT, dotnet, Unreal Editor, UnrealEditor-Cmd, Live Coding, MSBuild, ShaderCompileWorker 등이 실행 중이면 다른 빌드를 시작하지 않는다. 이전에 에디터를 종료했다는 답변은 새 채팅의 현재 상태를 보증하지 않는다.
3. dotnet 예외 대화상자가 뜨면 최초 UBT/컴파일 오류와 Windows Application Event Log부터 확인한다. 경로 문제라고 추정해서 변경·삭제하지 않는다.
4. **Unreal MCP 사용은 허용되지 않았다.** 사용자가 새로 명시적으로 허용하기 전에는 에셋 조회에도 사용하지 않는다. Source/Config/docs 분석을 우선하고, 필요한 에디터 확인은 사용자가 할 수 있게 안내한다.
5. 실제 에셋이나 레벨을 사용자 명시 요청 없이 저장하지 않는다. 이번에 ABP/DA를 코드 분석만으로 일괄 변경하지 않는다.
6. 기존 사용자 변경과 이전 작업을 보존한다. `git reset`, 전체 되돌리기, 무관한 포맷 변경은 하지 않는다. 이번 파일들의 기존 diff를 먼저 확인한다.
7. 일부 광범위한 Git 조회는 LFS 권한 오류를 만날 수 있다. 좁은 경로의 status/diff로 확인하고, 이를 이유로 사용자 파일을 삭제하거나 초기화하지 않는다.
8. 현재 요청에는 서브에이전트 사용 지시가 없다. 별도 명시 지시 없이 새 에이전트를 만들지 않는다.

문서 작성 규칙은 [문서 관리](../../Maintenance/README.md)에 있다. 새 문서는 주제 README에 연결하고 문서 카탈로그와 링크 검사를 갱신한다.

## 4. 현재 코드에서 확인한 문제와 아직 확인할 점

### 4.1 강제 재검색 요청과 실제 검색 시점이 다를 수 있다

`Project_JCharacterAnimInstanceProxy.cpp`의 `QueueGameThreadData`는 현재 다음처럼 요청을 덮어쓴다.

```cpp
bForceMotionMatchingReselect = bInForceMotionMatchingReselect || bEnteredGroundIdle;
```

즉, GT에서 요청을 받았더라도 MM 노드가 비활성·미갱신 상태인 동안 다음 snapshot이 들어오면 요청이 사라질 수 있다. `ForceReselectMotionMatchingNodes`는 노드의 interrupt mode를 설정하지만 실제 검색 실행을 확인하지 않는다.

UE 5.8 엔진 소스에서 확인한 사실:

- `ForceInterrupt` 설정만으로 `SearchThrottleTime`이 항상 우회되는 것은 아니다.
- `ElapsedPoseSearchTime < SearchThrottleTime`이고 기존 결과를 계속 진행할 수 있으면 검색을 건너뛰는 경로가 있다.
- `NextUpdateInterruptMode`는 노드 update 끝에서 `DoNotInterrupt`로 초기화된다.

따라서 **강제 요청을 설정했다는 것과 첫 복귀 프레임에 실제 재검색이 일어났다는 것은 다르다.** 이 경로는 코드로 확인했지만, 사용자가 느끼는 작은 어색함의 유일한 원인이라고 확정하지 않았다. 실제 generated MM 노드의 평가 경로와 재검색 시점을 회귀 테스트·PIE 관찰로 확인해야 한다.

### 4.2 자연 종료 시 일반적인 MM 복귀 이벤트가 명시적이지 않다

입력 변경에 따른 one-shot 취소는 AnimInstance에서 `bForceReselect=true`를 설정하는 경로가 이미 있다. 반면 Proxy에는 모든 외부 one-shot override의 `true → false` 복귀를 공통으로 다루는 명시적 이벤트가 보이지 않았다. 자연 종료와 입력 취소가 동일한 복귀 계약을 사용하는지 확인하고 필요한 작은 보완만 한다.

단순히 복귀할 때마다 continuing pose를 무조건 invalidate하면 기존 Stop/Idle·Strafe 연속성이 깨질 수 있다. 일반 재검색과 기존 Idle에서의 강한 invalidate 정책을 구분해야 한다.

### 4.3 거리 예산의 MM 갱신 주기와 worker 검색 주기가 분리되어 있다

`CurrentOptimizationPolicy.MotionMatchingUpdateInterval`은 현재 GT의 chooser/DB 선정 빈도를 주로 제어한다. Proxy가 실제 MM 노드에 적용하는 검색 간격은 phase 정책과 노드 기본값에서 결정된다. 거리 tier의 간격을 worker PoseSearch에 전달하는 계약은 현재 thread-safe snapshot에서 확인되지 않았다.

따라서 GT 선정 간격을 줄인 것만으로 실제 PoseSearch 비용이 같은 비율로 줄었다고 주장하면 안 된다. 기존 tier 정책을 worker 검색에도 연결할지 검토하고, 로컬·근거리 품질과 긴급 전환 검색을 보존해야 한다.

수치도 비교해야 한다. 현재 MM 노드 기본 throttle이 `0.05`이고 Mid 간격이 `0.033`이라면 단순 `max` 결합 시 Mid의 검색 간격은 그대로 `0.05`일 수 있다. 이것은 설정 충돌 여부를 확인할 사항이며, 모든 tier에 자동 성능 개선이 생기는 것은 아니다.

### 4.4 이미 있는 예산 구조와 보수적인 보호 조건

Animation Budget Allocator와 프로젝트 Subsystem이 이미 있다. 다시 구현할 필요가 없다.

`Project_JBudgetedSkeletalMeshComponent::CanUseBudget`는 현재 모든 montage 재생 중 예산 적용을 제외한다. 많은 원격 캐릭터가 공격·감정표현 montage를 동시에 쓰면 예산 밖으로 나올 수 있으므로 검토할 가치가 있다. 하지만 root motion, notify, 실제 공격 판정에 필요한 pose는 보호해야 한다. **안전한 분류 근거 없이 이 조건을 제거하지 않는다.** 이번 작은 작업에서 변경 근거가 부족하면 기존 보호를 유지하고 측정 후 검토 항목으로 남기는 것이 적절하다.

거리 bucket만으로 significance를 정하면 근거리 군중끼리 우선순위가 같아질 수 있다. 화면 크기·시야·전투 중요도까지 확장하는 방향은 후보지만, 현재 단계에서 새 전역 관리자를 만들지는 않는다.

### 4.5 노드 상세 패널 값과 실제 런타임 값이 다를 수 있다

Proxy의 presentation policy가 `BlendTime`, `MaxActiveBlends`, PlayRate 범위 등을 적용한다. 따라서 스크린샷에서 값만 조정해도 런타임에서 DA 값이 덮어쓸 수 있다. ABP, LocomotionProfile, Proxy의 소유권을 유지하고 설정 경로를 문서화해야 한다.

float property를 찾는 presentation helper에는 매 적용 시 `FindFProperty`를 반복하는 부분이 있다. 같은 파일을 수정하며 작은 캐시로 정리할 후보지만, 우선순위는 검색 요청의 정확성이다. 측정 없이 큰 비용 절감이라고 표현하지 않는다.

## 5. 구현 방향과 과도한 설계를 피하는 경계

아래는 **제안이며 아직 구현하지 않았다.** 다음 작업자는 현재 코드·UE API와 실제 노드 경로를 확인해 가장 작은 안전한 방식을 선택한다.

### 우선 구현 후보

1. **MM 복귀/강제 재검색 요청의 수명 보완**
   - 외부 one-shot에서 MM으로 복귀하는 edge를 기존 Proxy에서 감지한다.
   - 요청을 다음 snapshot이 무조건 덮어쓰지 않게 한다.
   - 실제 MM 노드 update에 적용될 때까지 유지하고, 처리 후 정상 검색 간격으로 돌아간다.
   - 요청 발생 프레임을 처리 완료로 간주하지 않는다. 기존 node update counter 등 실제 갱신 증거를 사용할 수 있는지 먼저 확인한다.
   - native fallback과 generated ABP 노드, linked layer, 비활성 분기, ABA skip을 고려한다. 한 노드만 처리했다고 다른 활성 노드의 요청까지 잃지 않게 한다.
   - 비활성 노드를 기다리느라 전역 force가 영원히 유지되거나 매 프레임 반복 검색하지 않게 한다. 필요 이상의 per-node 프레임워크는 피한다.
   - mounting, MM disable, AnimClass 변경, initialize/reset, 새 상태·DB로의 전환에서 오래된 요청을 정리하거나 최신 문맥으로 합성한다.

2. **복귀 첫 실제 검색만 throttle 우회**
   - 필요한 업데이트에만 검색 간격을 `0`으로 적용하는 방식 등을 검토한다.
   - `ForceInterrupt`의 정확한 의미와 적용 순서를 유지한다. 강제 요청보다 나중에 일반 정책이 throttle을 덮어쓰지 않게 한다.
   - 기존 Ground Idle invalidate 정책은 보존한다. 일반 복귀에는 continuing pose와 Pose History를 가능한 유지한다.
   - 단순 `ResetOnBecomingRelevant` 토글이나 매 프레임 invalidate로 문제를 가리지 않는다.

3. **기존 거리 tier를 실제 worker 검색 정책에 연결**
   - GT에서 결정한 최소 검색 간격 등 필요한 작은 값만 기존 snapshot으로 전달한다.
   - worker에서 Pawn, World, Controller, mutable UObject를 추가로 읽지 않는다.
   - Local/Near는 현재 품질 정책을 보존한다. Mid/Far는 노드 기본 간격과 기존 tier 간격의 관계를 명시한다.
   - 검색 억제 phase의 `SuppressedSearchThrottleTime=3600` 계약을 무너뜨리지 않는다. 복귀/긴급 전환에서는 필요한 한 번의 검색을 허용한다.
   - 숨김 상태의 native update skip이 오래된 snapshot을 다시 보내는 경로도 확인한다. policy 변경이 worker에 전달되지 않는 경우를 피한다.
   - root motion, 공격 pose demand, remote one-shot, dedicated server 처리와 합성 규칙을 보존한다.

### 이번에 바로 도입하지 않을 것

- 새로운 locomotion manager, 별도 tick component, 새 군중 프레임워크.
- CMC에서 Mover로의 이전, StateController/DA 구조 전면 교체.
- 모든 MM에 TIP/Strafe 전용 Steering 또는 Orientation Warping 값을 복사.
- PSS 가중치의 일괄 변경, foot lock·Offset Root Bone의 무근거 재튜닝.
- 전체 montage 보호 해제.
- Animation Sharing, Mesh Merge, Mass 기반 표현으로 전원을 일괄 전환.
- 새 transition cost solver나 연구 구현을 프로젝트에 바로 이식.

원거리 pose 공유, 화면 중요도, mesh/bone LOD, follower retarget 개선은 실제 병목이 확인되면 다음 단계에서 선택한다. 현재 사용자가 만족하는 이동을 넓게 바꾸지 않는다.

## 6. 파일과 책임 지도

모든 경로는 저장소 기준이다. 정확한 줄 번호는 다음 채팅에서 검색한다.

| 파일 | 확인할 책임 |
| --- | --- |
| `Source/Project_JCharacter/Private/Animation/Project_JCharacterAnimInstanceProxy.h/.cpp` | snapshot 전달, 실제 native/generated MM 노드 정책·interrupt 적용, DB 전환, 결과/trace |
| `Source/Project_JCharacter/Public/Animation/Project_JCharacterAnimInstance.h` | `FProject_JAnimThreadSafeData`, optimization/search policy와 graph getter |
| `Source/Project_JCharacter/Private/Animation/Project_JCharacterAnimInstance.cpp` | GT 데이터 수집, one-shot 상태/취소, chooser, budget tier, snapshot publish |
| `Source/Project_JCharacter/Public/Animation/Project_JMotionMatchingRuntime.h` | GT chooser selection schedule와 문맥 변경 추적. worker 요청과 무리하게 혼합하지 않을 것 |
| `Source/Project_JCharacter/Public/Animation/Project_JLocomotionProfile.h` | 검색·presentation 정책과 DA 설정 |
| `Source/Project_JCharacter/Private/Animation/Project_JLocomotionProfile.cpp` | phase별 검색 간격과 blend time 계산 |
| `Source/Project_JCharacter/Public/Animation/Project_JAnimationBudgetTypes.h` | Local/Near/Mid/Far/Hidden 정책과 거리·주기 기본값 |
| `Source/Project_JCharacter/Public/Animation/Project_JBudgetedSkeletalMeshComponent.h` | 기존 ABA mesh 및 Presentation/GameplayPose demand API |
| `Source/Project_JCharacter/Private/Animation/Project_JBudgetedSkeletalMeshComponent.cpp` | budget eligibility, demand 합성, 원래 URO/visibility/notifies 복원 |
| `Source/Project_JCharacter/Private/System/Project_JCharacterAnimationBudgetSubsystem.cpp` | 기존 프로젝트 ABA 등록·significance·상태 출력 |
| `Source/Project_JCharacter/Private/Components/Project_JAnimationUpdateCoordinatorComponent.cpp` | 원격 긴급 갱신 요청의 수명과 합성 |
| `Source/Project_JCharacter/Private/Components/Project_JCombatHitValidationComponent.cpp` | GameplayPose 필요 조건. 시각 품질 tier와 게임플레이 pose 수요를 구분 |
| `Source/Project_J/Testing/Project_JAnimationBudgetProbe.cpp` | 기존 군중 예산 probe. 신규 도구 전에 활용 가능 여부 확인 |
| `Source/Project_JCharacter/Private/Tests/Project_JCharacterRuntimeTests.cpp` | MM runtime, StateController 등의 기존 회귀 테스트 |
| `Source/Project_JCharacter/Private/Tests/Project_JCrowdAnimationTests.cpp` | 기존 군중 테스트. 파일 존재는 확인했지만 이번 조사에서는 본문 미독 |
| `Source/Project_JCharacter/Private/Tests/Project_JStrafeFacingRedirectTests.cpp` | Strafe facing 검색 연속성 회귀 |
| `Source/Project_JCharacter/Private/Tests/Project_JPlayerMaturityTests.cpp` | clock, TIP, trajectory freshness 등 보호 |
| `Config/DefaultEngine.ini` | `[SystemSettings]`에 기존 `a.Budget.Enabled=1` |

### Proxy의 현재 흐름

`QueueGameThreadData` → `PreUpdate`에서 snapshot 복사 → `UpdateAnimationNode_WithRoot`에서 DB/force/search policy 적용 → `Super`로 노드 update → 결과 및 Pivot trace 수집.

- `bForceMotionMatchingReselect`는 현재 단일 bool이고 Queue 시 덮어쓴다.
- node 기본 throttle은 native 값과 `DefaultSearchThrottleTimes`의 generated index별 값으로 보관한다.
- `GetGeneratedMotionMatchingNodeIndices`는 AnimClass가 바뀌면 DB/throttle 캐시를 정리한다.
- `CapturePostSelection` 결과는 대표 노드의 결과이며, 그 노드가 이번 프레임 최종 pose에 기여했다는 증거가 아니다.
- 테스트 friend가 이미 snapshot boundary, clock, StopIdleInterrupt, StrafeFacingSearch에 있다. 새 검증은 기존 테스트 구조를 우선 활용한다.

### AnimInstance의 현재 주요 함수

- `BuildThreadSafeData`, `PublishThreadSafeDataToProxy`.
- `ShouldEvaluateMotionMatchingThisFrame`: GT chooser 선정 주기. 실제 worker 검색과 구분한다.
- `BuildOptimizationPolicy`, `ApplyOptimizationPolicy`, `ShouldSkipNativeUpdate`.
- `EvaluateStateControllerChooser`: 일부 one-shot 진입은 단일 animation에 대한 MotionMatch로 시작 시간을 고른다. Pivot은 의도적으로 별도 정책을 갖는다.
- one-shot 입력 취소는 phase를 Cycle/Idle로 돌리고 force reselect를 요청한다. Pivot 취소는 committed animation과 request revision의 수명 계약을 보존해야 한다.

### 기존 예산 구조의 현재 기본값

- 거리: Near `2500`, Mid `6000`, Far `12000`.
- GT 선정 간격: Mid `0.033`, Far `0.083`, Hidden `0.10`초.
- `bDisableMotionMatchingBeyondFarDistance`의 기본값은 false다.
- Local/Near는 현재 full policy, Mid는 일부 손/retarget IK 축소, Far는 foot/hand/retarget IK 축소와 retarget LOD 정책을 사용한다.
- `ProjectJ.AnimationBudget.Enabled`, `a.Budget.Enabled`가 기존 예산 활성화에 관여한다. 상태 확인 명령은 `ProjectJ.AnimationBudget.Status`.
- Presentation과 GameplayPose 수요가 기존 mesh에서 합성된다. GameplayPose는 bones refresh와 notify 보호가 필요하다.
- modular mesh는 Leader Pose를 활용하지만 draw call 비용까지 공유되는 것은 아니다.

## 7. ABP와 PSS 정보

아래는 사용자가 이 채팅에 제공한 스크린샷 기준이다. 실제 현재 에셋을 새로 조회·수정한 정보는 아니다. ABP 이름은 `ABP_Humanoid_Master`, 관련 schema는 `PSS_Player`, `PSS_Combat`, `PSS_InAir_Player`로 보였다.

### 전체 pose 흐름

```text
StateController ──┐
                 Two Way Blend → MM 쪽 pose ──┐
Motion Matching ─┘                            Blend Poses by Bool
외부 Blend Stack ─────────────────────────────┘
 → Inertialization → Lean Mesh Space Additive → Locomotion cached pose
 → CombatUpperBody linked layer → ResolvedLocomotion cached pose
 → UpperBody slot / layered blend → DefaultSlot
 → Aim Offset Mesh Space Additive → Offset Root Bone
 → Local To Component → Foot Placement → Leg IK → Component To Local
 → Pose History → OnFoot/Mounted enum blend → Output
```

- Bool의 True pose는 MM 계통, False pose는 외부 Blend Stack. Active는 `NOT ShouldOverrideMotionMatching`.
- True/False Blend Time은 `0.2`. Transition Type은 **Inertialization**이고 바로 뒤에 Inertialization 노드가 있다.
- Hermite Cubic InOut 표시가 있지만 일반 inertial transition을 crossfade 커브 튜닝과 동일하게 해석하지 않는다.
- Two Way Blend의 Alpha가 스크린샷에서는 `1.0`으로 보인다. 숨겨진 binding까지 확인하지 않았으므로 불필요한 branch라고 단정하지 않는다.
- MM 내부 Blend Stack graph는 `Blend Stack Input → Output`만 있다.
- 외부 one-shot Blend Stack graph에는 Local To Component → Orientation Warping → Steering → Component To Local이 있다. OW는 `enable_warping` curve와 Combat Strafe 전용 alpha/angle, Steering은 `enable_turninplacesteering` curve와 TIP 전용 alpha/desired facing을 사용한다.
- 이 getter들을 MM 내부에 그대로 복사하면 일반 Cycle에서 alpha가 0이거나 과보정될 수 있다. 내부 graph가 비어 있다는 것만으로 결함이라고 판단하지 않는다.

두 번째 이미지 직접 대조에서 확인한 분기 세부:

- `CombatUpperBody`의 Base Pose는 cached `Locomotion`이고 결과를 `ResolvedLocomotion`으로 캐시한다.
- `Layered blend per bone`의 Base Pose는 cached `ResolvedLocomotion`, Blend Pose 0은 같은 cache를 거친 `Slot UpperBody`다. 표시 Blend Weight는 `1.0`이지만 bone filter·blend 설정은 사진에 없다.
- `Slot UpperBody`와 `Slot DefaultSlot`은 모두 `DefaultGroup`으로 표시된다. slot 이름만 보고 서로 다른 montage를 동시에 독립 재생한다고 가정하지 않는다.
- Aim asset은 `BS_Neutral_AO_Stand`. `Graph Aim Yaw`, `Graph Aim Pitch`가 asset 입력이고 `Graph Aim Offset Alpha`가 Mesh Space Additive alpha다. 이 변수들의 계산 graph는 제공되지 않았다.
- Lean asset은 `BS1D_Additive_Lean_Run`. `GetThreadSafeLeanAmount`의 X만 `LeanLR`에 연결되고 Y는 연결되지 않았다. `Should Apply Lean Additive` getter가 Additive 노드 Enabled를 공급한다.
- 최종 enum blend의 Default Pose는 Foot Placement/Leg IK를 통과한 Pose History 결과다. Mounted Pose는 **cached `Locomotion` → `MountedLocomotion` linked layer**의 별도 결과다. enum 입력은 `Get Thread Safe Locomotion Mode`, 두 blend time은 각각 `0.2`다. mounted branch의 내부 graph나 추가 보정은 사진에 없다.
- 사진상 연결되지 않은 Control Rig의 Alpha는 `1.0`이다. 입력·출력이 연결되지 않은 노드이므로 그 수치만으로 실행 중이라고 해석하지 않는다.

즉, master의 마지막 부분은 다음 두 경로가 합쳐지는 구조다.

```text
cached Locomotion → CombatUpperBody → ResolvedLocomotion
 → UpperBody/DefaultSlot → Aim → Offset Root → Foot Placement → Leg IK
 → Pose History ────────────────────────────────────────── Default Pose ─┐
cached Locomotion → MountedLocomotion linked layer ─────── Mounted Pose ─┤
                                         LocomotionMode enum blend → Output
```

### 처음 제공된 Offset Root Bone과 Blend Stack 보정 상세

초기 사진은 사용자가 제자리 회전과 대각 점프 때 사용하는 graph라고 설명했다. 나중의 master flow 사진과 함께 보되, 모든 phase에서 같은 alpha가 활성화된다고 가정하지 않는다.

- Offset Root Bone 입력은 Translation Mode, Rotation Mode, Translation Half Life, Max Translation Error가 각각 thread-safe getter에 연결됐다. Max Translation Error는 코드의 Translation Radius getter가 공급한다.
- 첫 상세 패널에는 Rotation Half Life `0.2`, Max Rotation Error `-1.0`, On Ground true, Ground Normal `(0,0,1)`이 보였다. Translation/Rotation velocity clamp는 꺼져 있었고 Teleport Reset도 꺼져 있었다. 핀이나 binding 값은 런타임 값으로 단정하지 않는다.
- Offset Root Bone의 On Initial Update / On Become Relevant / On Update는 None, evaluation mode는 Graph였다. experimental 노드 경고가 보였으나 그것만으로 현재 동작 버그를 뜻하지 않는다.
- Blend Stack graph는 현재 animation asset을 가져와 AnimSequence로 변환하고, 현재 asset time으로 `enable_warping`과 `enable_turninplacesteering` curve를 샘플링했다. 그 값에 각각 Strafe OW alpha와 TIP steering alpha를 곱했다.
- Orientation Warping은 현재 animation/time과 Strafe locomotion angle을 받는다. 화면에는 Target Time `0.0`, 기본 Locomotion Direction `(0,0,0)`이 보였으나 연결·노드 내부 해석까지 전부 확인한 것은 아니다.
- Steering은 desired facing rotator를 target orientation으로 받는다. Procedural Target Time `10000.0`, Animated Target Time `0.5`, Mirrored false, Mirror Data Table 미지정이 보였다. 이 수치 자체를 이번 개선에서 일괄 변경하라는 뜻은 아니다.
- 뒤쪽 Control Rig 노드는 사진상 연결되지 않았다. 이미 Control Rig까지 최종 pose에 적용 중이라고 가정하지 않는다.

**사진 사이의 연결 차이 — 현재 에셋 확인 필요:** 위의 올바른 모드 getter 연결 설명은 초기 사진 1과 사진 5를 기준으로 한다. 사진 6에서는 위쪽 **`Get Thread Safe Offset Root Rotation Mode` 출력이 `Translation Mode` 핀에 연결된 것으로 보이고**, 아래쪽 별도의 Rotation getter도 Rotation Mode에 연결돼 있다. 따라서 제공된 모든 사진의 Translation Mode 연결이 같다고 쓰면 부정확하다. 이 차이가 중간 편집 상태인지 현재 연결인지 사진만으로 알 수 없다. 다음 채팅에서 사용자가 현재 ABP 연결을 확인하거나, 별도로 명시 허용한 에셋 조회로 확인한다. 두 getter의 mode 정책은 다를 수 있으므로 연결 확인 없이 동일하다고 가정하지 않는다. 이번 문서 점검에서는 에셋을 수정하지 않았다.

첫 Offset Root 상세에서 추가로 보이는 항목은 Reset Every Frame false, 비활성 Translation/Rotation Speed Ratio 각각 `0.5`, Tag None, Collision Testing Mode Disabled, Collision Test Shape Radius `30.0`, Offset `(0,0,60)`이다. collision과 velocity clamp가 비활성인 표시 상태와, 실행 중 binding 값은 구분한다.

### 주요 핀과 C++ 함수의 연결

이 표는 사진의 표시 이름과 현재 header에서 확인한 함수를 연결한다. 아래 getter들이 노드에 값을 공급한다는 것과 engine 검색 실행이 보장된다는 것은 별개다.

Offset Root modes 행은 의도한 연결과 사진 1/5 기준이다. 사진 6의 Translation Mode 연결 차이는 위의 확인 필요 항목을 따른다.

| 노드 또는 입력 | 현재 함수/경로 |
| --- | --- |
| MM Database | `GetCurrentActivePoseSearchDatabaseThreadSafe()` |
| 외부 Blend Stack Animation / Time / Loop | `GetThreadSafeStateControllerSelectedAnimation()`, `GetThreadSafeStateControllerSelectedAnimationStartTime()`, `GetThreadSafeStateControllerSelectedAnimationShouldLoop()` |
| 외부 Blend Stack Blend Time / Profile | `GetThreadSafeStateControllerSelectedAnimationBlendTime()`, `GetThreadSafeStateControllerSelectedAnimationBlendProfile()` |
| MM/외부 Blend Stack 선택 | `GetThreadSafeStateControllerShouldOverrideMotionMatching()`의 NOT |
| OW Alpha / Angle | `GetThreadSafeStateControllerCombatStrafeOrientationWarpingAlpha()`, `GetThreadSafeStateControllerCombatStrafeOrientationWarpingAngle()` |
| Steering Alpha / Target | `GetThreadSafeStateControllerTurnInPlaceSteeringAlpha()`, `GetThreadSafeStateControllerDesiredFacingRotator()` |
| Offset Root modes | `GetThreadSafeOffsetRootTranslationMode()`, `GetThreadSafeOffsetRootRotationMode()` |
| Offset Root Translation Half Life / Error | `GetThreadSafeOffsetRootTranslationHalfLife()`, `GetThreadSafeOffsetRootTranslationRadius()` |
| Foot Placement Alpha / Settings | `GetThreadSafeFootPlacementAlpha()`, `Get_FootPlacementPlantSettings()`, `Get_FootPlacementInterpolationSettings()` |
| Leg IK / Pose History Trajectory | `GetThreadSafeLegIKAlpha()`, `GetThreadSafeTrajectory()` |
| Lean Amount | `GetThreadSafeLeanAmount()`. 사진에서는 `BS1D_Additive_Lean_Run`과 Enabled bool 사용 |

외부 Blend Stack의 On Update에는 사진상 `OnUpdate_StateMachine`에 해당하는 graph callback이 보인다. callback 내부는 사진에 없으므로 실행 순서·부작용·C++ 소유 처리와의 중복을 필요할 때 확인한다. MM 노드의 callback은 None이며 둘을 혼동하지 않는다.

### Pose History 설정

| 설정 | 스크린샷 값 |
| --- | --- |
| Pose Count | `2` |
| Sampling Interval | `0.04` |
| Collected Bones | 빈 배열 |
| Collected Curves | 빈 배열 |
| Reset on Becoming Relevant | true |
| Generate Trajectory | false |
| Trajectory | thread-safe trajectory getter 연결 |
| Trajectory Speed Multiplier | `1.0` |
| Tag | `PoseHistory` |

On Initial Update / On Become Relevant / On Update는 사진상 None이다. Store Scales는 false, Root Recovery Time은 `0.0`이다. collector가 Foot Placement/Leg IK 뒤, Mounted enum blend 앞에 있으므로 수집 pose와 최종 mount pose가 같다고 가정하지 않는다.

UE 소스상 빈 Collected Bones는 0개 수집으로 단정할 수 없고 전체 transform 경로가 된다. 비용 최적화를 위해 bones를 좁힐 때는 모든 활성 schema의 필요 bone과 파생 기능을 확인한다. 무조건 2개 발만 넣거나 Pose Count를 늘리지 않는다. native fallback history 설정과 ABP collector 설정을 혼동하지 않는다.

### Motion Matching 노드 설정

- On Initial Update, On Become Relevant, On Update, On Motion Matching State Updated: 모두 None. 현재 Proxy에서 정책을 소유하므로 이벤트 함수를 새로 넣는 것이 필수는 아니다.
- Search Throttle `0.05`, Pose Reselect History `0.3`, Pose Jump Threshold Min/Max `0`.
- Blend Time `0.2`, Blend Profile 없음, Use Inertial Blend false.
- Play Rate `0.85…1.1`, Max Active Blends `3`은 **패널 표시값**이다. C++/DA가 적용하는 값은 다를 수 있다.
- Reset on Becoming Relevant true, Store Blended Pose true, Notify Filtering true, Notify Recency `0.2`, max override blend `0.03`.
- Search Needed true, Cached Channel Data Needed false, Ignore for Relevancy Test false로 보였다. Blendspace Parameter는 `(0,0,0)`, Update Mode는 Initial Only였다. node DB pin과 런타임 policy가 적용되는 경로를 우선한다.
- 사진 15의 sync 설정은 Group Name None, Method Do Not Sync, 표시 Group Role은 비활성 `Exclusive Always Leader`, leader 참여 때 위치 override 체크 상태다. 이 값만으로 foot phase sync를 적용했다고 말하지 않는다. Blendspace parameter blend half-life는 `0.0`, Tag는 None이다.
- runtime search/presentation policy 기본값에는 Default Blend `0.20`, Landing `0.50`, Jump `0.15`, Air `0.50`, Transition `0.20`, Play Rate `0.85…1.15`, Max Active Blends `4` 등이 있다. 실제 지정한 LocomotionProfile 값도 확인해야 한다.

### PSS 주요 설정

공통으로 보이는 pose query는 **Use Continuing Pose**다. 이를 무조건 Use Character Pose로 바꾸면 다른 동작의 연속성까지 달라질 수 있다. 외부 pose와 첫 MM query가 어떻게 연결되는지 먼저 추적한다.

| schema | 궤적과 pose 관련 값 |
| --- | --- |
| OTM / `PSS_Player` | trajectory 전체 weight 4. sample: -0.4 Position XY / 0.4, 0 Velocity XY + Facing XY / 2.0, 0.35 Position XY + Facing XY / 0.7, 0.7 Velocity XY + Position XY + Facing XY / 0.5 |
| Combat / `PSS_Combat` | 보이는 trajectory 항목은 OTM과 같음. 발 pose weight는 각각 1, pelvis velocity weight 5 |
| OTM pose | 발 pose weight 각각 2, pelvis velocity weight 4 |
| InAir | trajectory weight 10. sample: -0.2 Position / 0.25, 0 Velocity Direction + Facing Direction / 1, 0.2 Velocity + Position + Velocity Direction / 0.7, 0.4 XY Velocity + Position + Facing / 0.7 |
| InAir pose | 두 발 weight 1, pelvis Heading Y / Strip Z / weight 1 |

OTM/Combat pelvis velocity는 origin root, Strip XY로 보였다. Combat/OTM의 세 번째 pose bone은 스크린샷에서 접혀 있어 확인하지 못했다. OTM/Air의 보이는 schema 상단은 Normalize, 30 sample rate다. 일부 항목만 보였으므로 에셋 전체를 이미 확인했다고 말하지 않는다.

이미지 재대조에서 추가 확인한 schema 세부:

- OTM/Combat/Air의 **Pose Channel 전체 weight는 각각 `1.0`**이다. OTM 발 개별 weight `2.0`은 이 전체 weight와 다른 값이다. foot_l/foot_r flags는 모두 `Velocity | Position`이다.
- 보이는 Pose Channel에는 Use Character Space Velocities가 체크되어 있고, Permutation Time Type은 Use Sample Time이다.
- OTM/Combat pelvis Velocity Channel에는 Include Root Bone이 체크되어 있다. Sample Attribute ID `-1`, Sample/Origin Time Offset 각각 `0.0`, Use Character Space Velocities true, Normalize false, Permutation Time Type Use Sample Time, Normalization Group None, Filters 0이 보인다.
- Air pelvis Heading Channel은 Origin Bone None, Heading Axis Y, Sample Attribute ID `-1`, Sample/Origin Time Offset `0.0`, Strip Z, Use Sample Time, Normalization Group None, Filters 0이다. Include Root Bone도 체크 상태다.
- 표시된 trajectory sample의 Normalization Group은 모두 None. trajectory channel의 Sample Role None, Debug Weight Group ID `-1`, Filters 0이 보인다.
- OTM/Air에서 skeleton 배열 수는 1, channel 배열 수는 3이다. skeleton 항목은 접혀 있으므로 실제 지정 skeleton을 사진만으로 확인하지 않았다.
- OTM/Air 하단에서 Permutation Count `1`, Permutation Time Offset `0.0`이 보인다. Air의 Add Data Padding, Inject Additional Debug Channels, Inject Additional Debug Channel Data는 꺼져 있다.
- Combat schema 상단의 preprocessor/sample rate, OTM/Combat의 세 번째 pose bone, 각 PSD에 들어 있는 animation 목록·mirror 설정·index 규모는 이 사진들에서 확인하지 못했다.

Bool transition 상세 사진 7에는 Blend Profile None, Custom Blend Curve None, False 별도 Blend Profile 사용 false, Child Update Mode Default, Tag None이 보인다. bone별 transition profile이 이미 연결됐다고 가정하지 않는다.

### 스크린샷 원본

2026-10-05 재점검에서 이 채팅의 첨부 **15장 모두**를 이 문서 옆 `Screenshots/`에 원본 바이트 그대로 보존했다. 원본과 사본 SHA256이 모두 같고 합계 크기는 2,803,970 bytes다. [사진 목록과 체크섬](Screenshot_Inventory.json)을 참조한다. 임시 폴더가 삭제돼도 아래 사본을 볼 수 있다. 다른 컴퓨터로 문서를 옮길 경우 이 주제 폴더 전체를 함께 전달한다.

| 순서 | 내용 | 보존한 사진 |
| --- | --- | --- |
| 1 | 초기 Offset Root Bone 상세 | [사진 01](Screenshots/codex-clipboard-dde9411f-991a-4427-a731-98f2591c48b4.png) |
| 2 | 초기 OW/Steering Blend Stack graph | [사진 02](Screenshots/codex-clipboard-d0fd9f64-5f24-4c01-a35e-6e0ae4b7ef83.png) |
| 3 | StateController/MM/외부 Blend Stack과 Lean | [사진 03](Screenshots/codex-clipboard-15562ad5-d660-45c4-a06b-c571c2553b7d.png) |
| 4 | Foot Placement/Leg IK/Pose History/Mounted | [사진 04](Screenshots/codex-clipboard-57b941b1-1bc2-4d93-8830-f19b947c607b.png) |
| 5 | CombatUpperBody/Slot/Aim/Offset Root | [사진 05](Screenshots/codex-clipboard-ec81010b-5d53-4889-a7de-6f80664796fb.png) |
| 6 | Aim/Offset Root/Foot Placement | [사진 06](Screenshots/codex-clipboard-e4e5f6dc-38bc-4393-b73f-ca21094b38c7.png) |
| 7 | Bool 전환 inertialization 상세 | [사진 07](Screenshots/codex-clipboard-3ae8161b-38eb-4955-bd43-aa69f8683a60.png) |
| 8 | MM 내부 Blend Stack graph | [사진 08](Screenshots/codex-clipboard-49acf9b5-f895-414b-a474-7ac178811501.png) |
| 9 | Combat PSS 궤적/발 pose | [사진 09](Screenshots/codex-clipboard-5a820f14-8ef5-41e1-b0fc-6e34da501b33.png) |
| 10 | Combat PSS pose/velocity | [사진 10](Screenshots/codex-clipboard-ed1aac6e-9d32-4f0e-82a6-d68e7c991fce.png) |
| 11 | Air PSS 궤적/pose | [사진 11](Screenshots/codex-clipboard-acb85c96-a371-4124-ac3a-b802881e7e8f.png) |
| 12 | Air PSS pose/heading | [사진 12](Screenshots/codex-clipboard-65f0293b-9e98-4a7b-9cb2-49c117fd14fd.png) |
| 13 | OTM PSS 궤적/pose | [사진 13](Screenshots/codex-clipboard-8d9a1350-59e5-4f31-b80a-71ba9ec8fac2.png) |
| 14 | OTM PSS pose/velocity | [사진 14](Screenshots/codex-clipboard-a8d4efaf-b401-40e6-add6-322080887887.png) |
| 15 | MM 노드 상세 설정 | [사진 15](Screenshots/codex-clipboard-78a8e82f-c23c-4dd1-b068-f8f08d102cfa.png) |

아래는 원본 임시 경로에 대한 참고 기록이다. 앞으로는 위 사본을 우선한다.

- 초기 locomotion 연결: `C:/Users/I/AppData/Local/Temp/codex-clipboard-15562ad5-d660-45c4-a06b-c571c2553b7d.png`.
- Pose History와 최종 흐름: `codex-clipboard-57b941b1-1bc2-4d93-8830-f19b947c607b.png`.
- upper-body/aim/root 흐름: `codex-clipboard-ec81010b-5d53-4889-a7de-6f80664796fb.png`, `codex-clipboard-e4e5f6dc-38bc-4393-b73f-ca21094b38c7.png`.
- Bool transition 상세: `codex-clipboard-3ae8161b-38eb-4955-bd43-aa69f8683a60.png`.
- MM 상세: `codex-clipboard-78a8e82f-c23c-4dd1-b068-f8f08d102cfa.png`.
- MM 내부 graph: `codex-clipboard-49acf9b5-f895-414b-a474-7ac178811501.png`.
- Combat PSS: `codex-clipboard-5a820f14-8ef5-41e1-b0fc-6e34da501b33.png`, `codex-clipboard-ed1aac6e-9d32-4f0e-82a6-d68e7c991fce.png`.
- Air PSS: `codex-clipboard-acb85c96-a371-4124-ac3a-b802881e7e8f.png`, `codex-clipboard-65f0293b-9e98-4a7b-9cb2-49c117fd14fd.png`.
- OTM PSS: `codex-clipboard-8d9a1350-59e5-4f31-b80a-71ba9ec8fac2.png`, `codex-clipboard-a8d4efaf-b401-40e6-add6-322080887887.png`.

파일명만 적힌 항목도 모두 같은 `C:/Users/I/AppData/Local/Temp/` 폴더에 있다. 필요한 경우에만 읽고, 에셋 연결과 설정값은 스크린샷으로 확인한 정보임을 구분한다.

## 8. 연구 자료와 적용 방향

아래는 앞선 조사에서 참조한 자료다. 버전 차이는 현재 UE 5.8 소스로 확인한다. 샘플이나 다른 개발자의 구현을 그대로 도입하라는 지시는 아니다.

### 공식 자료

- [Motion Matching](https://dev.epicgames.com/documentation/en-us/unreal-engine/motion-matching-in-unreal-engine): schema, DB, query와 노드 설정의 기준.
- [Motion Matching Debugging](https://dev.epicgames.com/documentation/en-us/unreal-engine/motion-matching-debugging-in-unreal-engine): Rewind Debugger 등으로 query, 후보, 선택 결과를 관찰한다.
- [Game Animation Sample](https://dev.epicgames.com/documentation/en-us/unreal-engine/game-animation-sample-project-in-unreal-engine): 궤적, MM, 보정의 역할 분담을 비교한다. 프로젝트 전체를 이식할 필요는 없다.
- [Animation Blueprint Blend Nodes](https://dev.epicgames.com/documentation/en-us/unreal-engine/animation-blueprint-blend-nodes-in-unreal-engine): inertialization과 일반 blend를 구분한다.
- [Pose Warping](https://dev.epicgames.com/documentation/en-us/unreal-engine/pose-warping-in-unreal-engine): 적용할 상태와 공간을 맞춰 보정을 사용한다.
- [Animation Budget Allocator](https://dev.epicgames.com/documentation/en-us/unreal-engine/animation-budget-allocator-in-unreal-engine): significance와 예산으로 업데이트를 억제·보간한다. worker PoseSearch와 GPU의 모든 비용까지 보장하지는 않는다.
- [Animation Optimization](https://dev.epicgames.com/documentation/en-us/unreal-engine/animation-optimization-in-unreal-engine): GT/worker, parallel update, completion, notify, physics, render 비용을 나눠 본다.
- [Animation Sharing](https://dev.epicgames.com/documentation/en-us/unreal-engine/animation-sharing-plugin-in-unreal-engine): 공유 가능한 상태의 군중에 후보. 모든 개별 MM을 그대로 공유할 수 있다고 가정하지 않는다.
- [Working with Modular Characters](https://dev.epicgames.com/documentation/en-us/unreal-engine/working-with-modular-characters-in-unreal-engine): Leader Pose, Copy Pose, Mesh Merge의 CPU·그리기·초기 비용을 비교한다.
- [UE 5.8 Game Animation Sample update](https://www.unrealengine.com/tech-blog/download-the-latest-game-animation-sample-project-now-updated-for-ue-5-8): 조사 당시 확인한 업데이트 소개. Mover 관련 변경을 현재 CMC에도 필수라고 해석하지 않는다.

### 개발자 본인의 설명과 공개 구현

- [Coconut Lizard의 Animation Budget Allocator 설명](https://www.coconutlizard.co.uk/blog/animation-budget-allocator/): 거리·시야 등을 활용한 significance 구현 예. 오래된 UE 버전의 동작을 5.8의 버그로 옮겨 적지 않는다.
- [Daniel Holden의 Inertialization Transition Cost](https://theorangeduck.com/page/inertialization-transition-cost): pose뿐 아니라 속도 연속성도 품질에 영향을 준다는 참고. 새 solver를 구현할 필요는 아직 없다.
- [orangeduck Motion Matching](https://github.com/orangeduck/Motion-Matching): 알고리즘 참고 구현. UE용 즉시 적용 코드가 아니다.
- [PolygonHive GASPALS](https://github.com/PolygonHive/GASPALS): linked layer/overlay 구성 참고. README 등을 참조했지만 binary asset 내부까지 검증하지 않았다.
- [Scott Courtney의 Mover predictor 구현 예](https://gist.github.com/sna-scourtney/15bed77a9e0973bf0d2ec8363c8a029c): predictor 연결 참고. 다른 버전·Mover용이며 현재 trajectory/CMC를 교체할 이유로 삼지 않는다.

### 로컬 엔진 소스 확인 위치

`C:/Program Files/Epic Games/UE_5.8/Engine/Plugins/Animation/PoseSearch/Source/Runtime/` 아래 Public/Private를 읽는다. 노드 cpp는 **`Runtime/Private/AnimNode_MotionMatching.cpp`처럼 Private 바로 아래**에 있다. 이전 문구에 있던 추가 `Private/PoseSearch/` 폴더는 실제 경로가 아니므로 사용하지 않는다. include 이름 `PoseSearch/...`와 실제 파일 위치를 구분한다.

- `AnimNode_MotionMatching.cpp`: reset/relevancy, UpdateCounter, 검색 throttle, `NextUpdateInterruptMode` 소비.
- `AnimNode_PoseSearchHistoryCollector.cpp`: update 중 `FPoseHistoryProvider` graph message와 source 업데이트.
- `PoseSearchHistory` 관련: 빈 collected bones가 수집 범위에 미치는 영향.
- BlendStack plugin의 `Source/Runtime/Private/AnimNode_BlendStack.cpp`: inertial blend requester와 duration/profile 등.

과거 Saved 로그에 missing `IPoseHistory` 기록이 있지만 현재 노드가 망가졌다는 증거라고 단정하지 않았다. 필요하면 현재 generated MM과 collector 연결에서 재현되는지 확인한다.

자료 링크 재점검: 2026-10-05에 위 **공식 10개와 개발자 자료 5개, 총 15개**를 실제로 다시 열어 제목과 본문 접근을 확인했다. 웹 페이지 접근 확인과 프로젝트 적용 검증은 구분한다. 외부 저장소의 모든 코드·에셋·버전 조합을 실행 검증한 것은 아니다.

## 9. 보존해야 할 이전 수정과 참조 문서

### 이 채팅에서 이전에 진행한 수정

- F를 통한 지상·비행 mount 하차 입력. native Interact binding의 소유와 재설정을 정리했다.
- 재탑승 후 F와 WASD 입력이 사라지는 문제. 유지된 InputComponent에 native action을 회복하고 BP input setup이나 IMC를 중복 실행하지 않는다.
- 이동 중 하차 후 mount가 계속 달리는 문제. **authoritative 하차 성공 때만** pending input과 CMC velocity를 멈춘다. 하차 거부 때는 기존 motion을 유지한다.
- rider와 mount의 capsule 크기 차이를 고려한 하차 위치 판정.
- Pivot 재생 이후 입력 변경으로 외부 Blend Stack에서 돌아오지 못하는 문제. committed animation과 request revision의 수명, Stop/Redirect/Superseded 우선순위를 정리했다.
- OTM에서 S 이동 중 TAB으로 Strafe를 켜면 등이 갑자기 보이는 문제. CMC의 단계적 방향 변경, future trajectory facing, mode change 때 history 유지, MM facing catch-up 검색을 조합했다.
- 이동 중 facing catch-up은 선택적인 TurnRedirect PSD와 기존 DynamicCycle fallback을 사용한다. 단순히 걷기 pose 전체를 회전시키는 방식으로 되돌리지 않는다.
- player animation, trajectory freshness, snapshot/clock, 입력과 layer load/visibility/ownership 등의 감사 수정도 이미 있다.

이 내용은 이번 미착수 작업과 별개의 완료된 이력이다. 각각의 범위와 한계는 아래 결과 문서를 읽는다. 이전의 모든 제안이 구현 완료됐다고 말하지 않는다. 특히 ABP 실행 구성의 큰 최적화는 profiling을 기다리는 항목이 있다.

### 먼저 읽을 결과 문서

- [Player Animation 구현 결과](../../Review/Architecture/Project_J_Player_Animation_Implementation_2026-10-04.md).
- [Mount와 Strafe 수정](../../Review/Architecture/Project_J_Mount_Strafe_Fix_2026-10-04.md).
- [Strafe facing MM 개선](../../Review/Architecture/Project_J_Strafe_Facing_Redirect_2026-10-05.md).
- [Pivot과 Mount 후속 수정](../../Review/Architecture/Project_J_Pivot_Mount_Followup_2026-10-05.md).
- [재탑승 입력 수정](../../Review/Architecture/Project_J_Mount_Remount_Input_2026-10-05.md).
- [하차 때 motion 수정](../../Review/Architecture/Project_J_Mount_Dismount_Motion_2026-10-05.md).
- [Motion Matching 현재 계약과 다음 과제](../../Animation/Architecture/MotionMatchingNextSteps.md).
- [Locomotion 관련 문서](../../Animation/Locomotion/README.md), [아키텍처 검토 결과](../../Review/Architecture/README.md), [성능 자료](../../Performance/README.md).

비교 대상으로 사용자는 `C:/Users/I/Documents/GitHub/ArtisticSW2026`을 언급했다. 항상 Strafe인 프로젝트에서 4방향 roll 뒤 복귀가 각 모션에 맞는 MM 선택으로 자연스러웠다는 설명이다. 이번 작업에서 그 별도 프로젝트를 수정하라는 요청은 아니다.

## 10. 검증 계획과 완료 조건

### 구현 전

1. 위 규칙, 현재 status/diff, 필요한 코드를 확인한다.
2. 편집 대상의 현재 상태를 `Saved/Validation/<이번작업명>_20261005/Before` 등에 필요한 파일만 저장한다. 이전 기준 기록을 덮어쓰지 않는다.
3. native/generated MM의 update와 request 소비 방법을 engine API와 기존 테스트로 확인한다.
4. 일반 one-shot 자연 종료와 입력 redirect의 재현 조건을 구분한다.

### 자동화로 확인할 동작

- one-shot override 중, MM 비활성 중, 여러 snapshot이 와도 필요한 복귀 검색 요청이 사라지지 않는다.
- MM이 실제 업데이트된 때 필요한 한 번의 검색이 실행되고 일반 throttle로 돌아간다.
- 명시 force, Idle edge, 자연 복귀가 겹쳐도 무한 검색이나 불필요한 재시작을 일으키지 않는다.
- DB/AnimClass 변경, MM 비활성화, mount, reset에서 요청이 적절하게 갱신·삭제된다.
- phase의 검색 억제와 tier cadence가 기대대로 합성된다. Local/Near와 Mid/Far/Hidden을 확인한다.
- worker snapshot 경계, 기존 Stop→Idle, Strafe mode/facing, Start/Pivot cancellation이 회귀하지 않는다.
- Presentation/GameplayPose demand, montage notify/root motion, hidden leader/visible follower의 기존 보호가 유지된다.

구현을 그대로 옮겨 적은 테스트를 대량 추가하지 않는다. 엔진의 실제 update/throttle 소비를 가능한 범위에서 통과하는 회귀 테스트와 policy의 중요한 분기를 검증하는 테스트를 선택한다.

### 에디터에서 사용자에게 확인받을 동작

- OTM/Strafe 각각 run/sprint Start → 마우스/WASD 변경 → MM.
- Pivot 자연 종료·입력 redirect → MM.
- OTM의 S 이동 → TAB Strafe, 반대 방향 전환.
- 점프 start, 공중, 착지, Stop/Idle의 연속성.
- 지상/비행 mount 탑승→하차→재탑승, 이동 중 하차.
- 발 위상, 미끄러짐, 갑작스러운 pelvis/방향 변화, lean/aim/root correction 중복 여부.
- 필요하면 Rewind Debugger에서 first MM query, 선택 animation/time, continuing pose, 새 blend를 관찰한다.

### 군중 측정

같은 레벨·카메라·캐릭터/장비 구성으로 변경 전후를 비교한다. 예를 들어 1/50/100/200명으로 단계적으로 늘리고 인원은 측정 환경에 맞춘다. 근거리 밀집, 화면 안팎, idle과 다수의 montage, 장비 follower, local과 remote 조건을 나눈다.

측정에서는 Anim GT, worker/PoseSearch, completion, IK/retarget, render/draw call/GPU를 구분한다. ABA status와 frame budget만 보고 전체 최적화가 끝났다고 판단하지 않는다. 평균뿐 아니라 프레임 시간 변동과 중요한 동작의 시각 품질도 남긴다.

### 최신 기존 검증 기록

직전 mount motion 수정에서는 Editor/Game build가 성공했다. 회귀는 **112건, 108 Success와 4 SuccessWithWarnings, Failed/NotCompleted 모두 0**이었다. warning event 8건은 기준에서 변하지 않았다.

이번 MM 개선의 검증 결과는 아니다. 기록은 [하차 motion 검증 JSON](../../Review/Architecture/Project_J_Mount_Dismount_Motion_Validation_2026-10-05.json), 실제 로그는 `Saved/Validation/MountDismountMotion_20261005/`, automation report는 `Saved/Automation/MountDismountMotion_20261005/index.json`에 있다.

전회 회귀는 여러 기존 테스트를 `+`로 선택해 실행했다. 정확한 test list와 옵션은 해당 `Regression.log`의 Command Line에 있다. 단순히 `ProjectJ` 전체로 넓히면 조건부 테스트까지 포함될 수 있으므로 기존 list를 이해하고 이번 대상을 추가한다.

### 빌드 예시

**아래는 다음 구현 뒤 사용할 예시다. 출력 폴더와 결과 이름은 다음 작업에서 준비한다. 지금 문서 작성만을 위해 실행하지 않는다.** 먼저 금지 프로세스가 없는지 확인하고 Editor와 Game을 순서대로 완료까지 기다린다.

```powershell
Get-Process UnrealEditor,UnrealEditor-Cmd,LiveCodingConsole,UnrealBuildTool,dotnet,MSBuild,ShaderCompileWorker,Project_J -ErrorAction SilentlyContinue

& 'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe' Project_JEditor Win64 Development '-Project=C:/Users/I/Documents/GitHub/Project_J/Project_J.uproject' -WaitMutex -NoHotReloadFromIDE -MaxParallelActions=4 '-Log=C:/Users/I/Documents/GitHub/Project_J/Saved/Validation/MMCrowd_20261005/BuildEditor.log'

# Editor build 완료 뒤 다시 프로세스를 확인하고 실행한다.
& 'C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.exe' Project_J Win64 Development '-Project=C:/Users/I/Documents/GitHub/Project_J/Project_J.uproject' -WaitMutex -NoHotReloadFromIDE -MaxParallelActions=4 '-Log=C:/Users/I/Documents/GitHub/Project_J/Saved/Validation/MMCrowd_20261005/BuildGame.log'
```

headless automation의 전회 공통 옵션은 `-Unattended -NullRHI -NoSound -NoSplash -NoLiveCoding`, `-ReportExportPath`, `-AbsLog`, `-ExecCmds="Automation RunTests <선택한테스트>; Quit"`, `-TestExit="Automation Test Queue Empty"`다. UnrealEditor-Cmd로 실행하고 종료와 report의 failed/notcompleted를 확인한다. build와 동시에 실행하지 않는다.

코드 변경 뒤 적절한 scoped `git diff --check`를 사용한다. 기존 CRLF를 오탐하지 않는 설정 예시:

```powershell
git -c core.whitespace=blank-at-eol,blank-at-eof,space-before-tab,cr-at-eol diff --check -- <이번대상경로>
```

완료 때 변경점·실제 검증·남은 시각 확인/성능 측정을 구분하고 기존 문서 형식에 맞춘 결과와 필요한 hashes를 남긴다. 코드가 통과한 것만으로 에셋과 200명의 동작까지 검증됐다고 하지 않는다.

## 11. 인계 시점 체크섬

아래는 **이번 구현 시작 전 현재 파일**의 SHA256이다. 이전 변경을 포함하며 HEAD의 내용이 아니다. 새 채팅에서 달라져 있어도 삭제·복원하지 말고 병행 편집이나 추가 작업으로 보고 차이를 확인한다.

| 파일 | SHA256 |
| --- | --- |
| `Private/Animation/Project_JCharacterAnimInstanceProxy.cpp` | `C6712820AB6F51F2BD80B9220BC0BC1BB7D7B78AB0A44C46FC7A27AEF3FA8671` |
| `Private/Animation/Project_JCharacterAnimInstanceProxy.h` | `A7A85ABC13F407B975B4B405363333A8059F715B0D97B49B4F68E49FBE7F70FF` |
| `Private/Animation/Project_JCharacterAnimInstance.cpp` | `8613B6DCA4376CC3B4ECC0740E0CAC1F4252402DD0286CF7631D3EA8A58F63FB` |
| `Public/Animation/Project_JCharacterAnimInstance.h` | `32254F7D5014A5A0B9114FCF3B114E17C558540C114FB9998E4FA38DC14203C2` |
| `Public/Animation/Project_JLocomotionProfile.h` | `85DA59882678920CCD2D40B6D2FC012C0DF3562AD041A0FBDBD3A858BBE99EFA` |
| `Private/Animation/Project_JLocomotionProfile.cpp` | `6E7A777FDB5AEE2A5BDEC0F12E5D6E0C069CF3F993228C06216461881DDBCE9C` |

표 경로의 공통 접두사는 `Source/Project_JCharacter/`다. Proxy cpp/h, AnimInstance cpp/h와 `Project_JCharacterRuntimeTests.cpp`는 인계 확인 시점에 이미 Git modified 표시가 있었다. 이번에 새로 편집한 것이 아니다. LocomotionProfile의 위 두 파일과 CrowdAnimationTests는 이번 scoped status에 변경 표시가 없었다.

**다음 작업은 위 계약을 현재 코드로 확인하고 필요한 최소 구현과 회귀 검증을 시작하는 것이다. 목차 갱신이나 이 문서의 존재를 MM 개선 구현 완료로 취급하지 않는다.**

## 12. 채팅 전체의 요청과 결정 흐름

이 표는 현재 확인 가능한 대화의 요청 흐름을 순서대로 복원한다. 같은 의도를 반복한 짧은 메시지는 묶었다. assistant의 모든 발언과 도구 로그를 원문으로 복제한 표는 아니며, 구현 사실은 원래 결과 보고서·검증 기록을 기준으로 한다.

| 단계 | 사용자 요청·피드백 | 이어받을 의미와 근거 |
| --- | --- | --- |
| 1 | 이전 최적화 리팩터링 뒤 플레이어 컴포넌트·DA·아키텍처가 고도화됐는지 하나씩 검사 요청 | 클래스 수나 추상화보다 수명·소유권·확장·실패 복구·관측·검증을 기준으로 삼음. 초기 캐릭터 감사 A01–A10 참조 |
| 2 | 6.1 Sol과 6 Astra 중 모델 선택 질문, 이후 Astra로 변경하고 전체 소스 감사 요청 | 사용자가 Astra로 진행하려는 의사를 밝힘. 모델 우열이나 실제 선택 상태를 저장소 크기만으로 보증하지 않음. 모델 재선택은 현재 구현의 선행 조건이 아님 |
| 3 | 궤적·OTM/Start·스킬·DA 확장·통신도 더 좋은 구조가 있는지 확인 요청 | 기존 동작과 역할 분리를 유지하며 정확성과 확장 경계를 보강하는 접근 |
| 4 | 이전 감사 영역을 제외하고 남은 전체 소스도 점검했는지 질문, 추가 감사 진행 승인 | 잔여 감사 R01–R14와 파일별 coverage 기록. 모든 파일을 동일 깊이로 정독하거나 전체 에셋을 확인했다는 뜻은 아님 |
| 5 | 구조를 무너뜨리지 않고 좋게 수정하는지 확인 후 구현 승인 | R01–R14의 후속 구현. 원래 구조 유지·소유권·복구·검증 보완. 첫 A01–A10 전체 완료와 구분 |
| 6 | 각각 어떤 문제를 어떻게 해결했는지 간단한 설명 요청 | 다음 채팅에서도 항목별 문제→해결을 간단히 설명. 기존 결과 문서로 확인 가능 |
| 7 | 궤적 컴포넌트는 더 손댈 필요가 없는지, 플레이어/애니메이션도 점검 요청 | P01–P13 재점검 후 적용. 특히 freshness, 공통 가시성, frame provenance, MM 결과 관측, animation clock. P12 큰 ABP 성능 변경은 측정 뒤 결정 |
| 8 | 구현 승인 후 에디터에서 무엇을 해야 하는지 질문 | 코드 검증과 실제 ABP/DA 연결·시각 품질 확인을 분리해서 안내 |
| 9 | F 재입력 하차 실패, OTM에서 S 이동 중 TAB Strafe로 전환하면 등이 갑자기 보이는 현상 보고 | 지상·비행 입력 ownership과 Strafe facing 처리 수정. 최초 Offset Root/Blend Stack 사진 2장 제공 |
| 10 | 에디터 종료 후 빌드 요청. 단순 회전처럼 보인다는 추가 피드백 | ArtisticSW2026의 4방향 roll 후 MM 복귀를 비교 사례로 제시. 모션에 맞는 MM 선택이 필요하며 관련 에셋이 있다고 확인 |
| 11 | Strafe 수정이 괜찮다고 평가하고 Pivot은 입력을 줘도 계속 유지한다고 점검 요청 | OTM/Strafe run/sprint Start의 입력 취소→MM 복귀 동작은 기존 의도. Pivot도 committed 재생 기준으로 입력 redirect/lifetime을 처리 |
| 12 | F 하차 문제는 지상·비행 모두라고 답변. 에디터 종료 확인 | 하차 조건/입력 전달/캡슐 차이 확인. 이때 종료 확인을 현재 실행 상태로 재사용하지 않음 |
| 13 | 좋은 구조·틱 계산량도 고려하는지 질문 | 새 tick나 반복 탐색보다 기존 상태·revision·snapshot 활용. 검증 없이 성능 개선 수치 단정하지 않음 |
| 14 | 하차 후 재탑승하면 F와 WASD가 모두 안 된다고 보고 | retained InputComponent에서 native binding 복구. 중복 BP setup/IMC 변경 피함 |
| 15 | 재탑승은 잘되지만 움직이면서 하차하면 mount가 계속 움직인다고 보고 | 권한 있는 하차 성공 때 mount input/velocity 정리. 하차 실패 때 기존 움직임 유지 |
| 16 | 이전 궤적 고도화·기타 작업들이 완료됐는지 질문 | 이미 적용한 항목과 제안·실측 대기 항목을 구분. 전체 최적화 완료라는 표현 금지 |
| 17 | 현재 ABP 사진을 제공하고 더 자연스러운 방안과 자료·필요한 정보 요청 | master locomotion, 최종 IK/history/mount, upper-body/aim/root, inertial Bool, PSS, MM 상세 사진까지 총 15장. 7절과 보존 사본 참조 |
| 18 | 현재 움직임에 큰 어색함은 없고 Blend Stack→MM 때만 조금 어색하다고 구체화 | 광범위 locomotion 재설계보다 복귀 query/검색 시점/pose 연속성에 집중 |
| 19 | MM 내부 graph·함수·Pose History를 변경하는 방향인지 질문, 프로젝트에 맞는 자료 요청 | 설정과 함수를 전부 바꾸는 작업으로 해석하지 않음. 현재 producer/consumer 계약을 점검하고 필요한 부분만 변경 |
| 20 | 공식뿐 아니라 다른 유저 구현 정보도 좋다고 명시 | 공식 문서와 개발자 직접 설명/공개 코드 참고. 자료의 버전·적용 한계 함께 기록 |
| 21 | 수백 명이 한곳에 보이는 MMORPG를 가정하며 최적화와 고품질을 함께 요청 | 로컬/중요 캐릭터 보호, 거리·중요도 예산, 실제 PoseSearch 주기, IK/retarget/follower/render 비용 측정 |
| 22 | 간단히 어떤 작업인지, C++이 필요한지, 이미 있는 구조와 오버 엔지니어링 우려 질문 | 기존 AnimInstance/Proxy/ABA를 재사용하는 작은 보완 우선. 새 관리자·공유 시스템·샘플 이식은 측정 근거 뒤 결정 |
| 23 | 이전 코드도 Astra로 리팩터링했다고 설명하고 고려사항을 챙겨 구현 진행 승인 | 4–6절의 조사와 제안으로 작업 시작. 아직 구현·새 테스트·빌드는 시작하지 않음 |
| 24 | 채팅이 길어져 다른 채팅에서 이어가기 위한 모든 작업 정보의 .md 요청 | 이 문서 작성과 목차/카탈로그 연결. 구현 중단 지점을 명시 |
| 25 | ABP·자료 링크·채팅 전체 내용이 들어 있는지 꼼꼼한 재점검 요청 | 초기 감사 이력, 처음 사진 2장, graph/getter 세부, 원본 사진 15장 보존, 링크 재접근, source hash 대조, 엔진 경로 수정으로 보완 |

### 초기 감사부터 구현까지의 범위와 상태

| 기록 | 실제 범위·상태 | 원본 근거 |
| --- | --- | --- |
| 최초 캐릭터 감사 | Avatar 프로젝트 component 19 + PlayerState component 4 = 23개, C++ DA type 23개. A01–A10 제안. 당시 감사 자체는 runtime 수정하지 않았고 기존 테스트 79개 성공 | [캐릭터 감사](../../Review/Architecture/Project_J_Character_Architecture_Maturity_Audit_2026-10-03.md) |
| 잔여 전체 감사 | 기준 inventory 431파일/61,782줄. 이전 영역 144개 제외한 287개를 분류별 방법으로 검토. `Source` 자체는 400파일, 8모듈, 3 Target. test 계약 분석과 runtime 본문 검토의 깊이를 구분 | [잔여 감사](../../Review/Architecture/Project_J_Remaining_Source_Maturity_Audit_2026-10-03.md), [파일별 CSV](../../Review/Architecture/Project_J_Remaining_Source_Coverage_2026-10-03.csv), [감사 검증](../../Review/Architecture/Project_J_Remaining_Source_Validation_2026-10-03.json) |
| R01–R14 적용 | 기존 모듈/GAS/NPC/Mass 구조를 유지하며 아래 계약 보강. 당시 최종 회귀 89개. A01–A10 전부 구현했다는 뜻이 아님 | [적용 결과](../../Review/Architecture/Project_J_Architecture_Maturity_Implementation_2026-10-03.md), [적용 검증](../../Review/Architecture/Project_J_Architecture_Maturity_Implementation_Validation_2026-10-03.json) |
| 플레이어 재감사 | 이전 144파일 중 동일 hash 137개·변경 7개 대조, 현재 관련 경계 65파일 재확인. 합집합 159파일. 당시 관련 회귀 87개 성공 | [P01–P13 감사](../../Review/Architecture/Project_J_Player_Animation_Audit_2026-10-04.md), [coverage CSV](../../Review/Architecture/Project_J_Player_Animation_Coverage_2026-10-04.csv) |
| P01–P13 후속 적용 | 소유권·유효성·Notify·layer·제작·시간 계약 보강. 당시 회귀 101개. P12의 ABP 비용 재설계는 조건부 profiling 대기 | [적용 결과](../../Review/Architecture/Project_J_Player_Animation_Implementation_2026-10-04.md), [적용 검증](../../Review/Architecture/Project_J_Player_Animation_Implementation_Validation_2026-10-04.json) |
| 이후 버그 수정 | F/Strafe→Strafe MM→Pivot/하차→재탑승 입력→하차 후 motion 순서. 최신 회귀 112개. 각 실행 숫자는 별도 기록이며 합산하지 않음 | 9절의 각 결과·대응 validation JSON |
| 현재 MM/crowd 개선 | 코드/engine 조사와 참고 자료까지. 구현 미착수, 새 crowd 성능 측정 없음 | 이 문서 4–6·8·10절 |

### R01–R14 적용 내용 요약

세부 조건과 실제 코드·테스트는 원래 적용 보고서를 따른다.

| 항목 | 무엇을 보완했는가 |
| --- | --- |
| R01 | GAS Health/Mana/최댓값의 음수·비유한 값과 자원 불변식 |
| R02 | Mass 전환 중 NPC 행동 중단/복구의 context·revision·소유 token, 제한 재시도 |
| R03 | 비행 mount 착륙 정체·timeout·취소 후 Flying/입력/표현 복구 |
| R04 | WeaponMotion/MeleeHit Notify 실행별 lease/token으로 늦은 End가 새 실행을 닫는 문제 방지 |
| R05 | Handover checked apply 성공 뒤 receipt, replay/idempotency·대상·권한·payload 검증 |
| R06 | 조회/변경 명령 구분, mutation UnknownOutcome, idempotency context와 수용량 제한 |
| R07 | 그룹 전체 snapshot/revision/tombstone 복구와 index 갱신 후 알림 |
| R08 | LocalPlayer Input Mapping owner lease, 마지막 owner 정리, 외부 mapping 보존 |
| R09 | 실제 socket 기준 무기 편집 좌표와 preview/window 선택, transaction 정리 |
| R10 | searchable DA ID tag와 기존 fallback, runtime resolver를 재사용한 IK 제작 검증 |
| R11 | 선택적인 NPC 활성화가 자신이 등록·grant·소유한 자원만 rollback/정리 |
| R12 | NPC Decision/Mass의 전체 비용과 지연 관측 기반. 성능 개선율이나 scheduler 개편 완료 아님 |
| R13 | 장비 수명 fixture의 유효 pose source 준비, 기존 runtime 거절 계약 유지 |
| R14 | migration DryRun/Apply 구분, backup/hash/staging/receipt 복구 도구. 실제 에셋 Apply는 하지 않음 |

### P01–P13 적용 내용 요약

| 항목 | 무엇을 보완했는가 |
| --- | --- |
| P01 | 장비 ledger에 원래 grant ASC를 보관하고 그 ASC에서만 효과/능력 회수 |
| P02 | Mounted tag의 원래 ASC, 강제 종료 수명 정리, 일반 하차 조건과 teardown 복구 구분 |
| P03 | 궤적 eligibility/reset/age 검증과 기존 CMC 추정 fallback. 기본 유효 시간 0.25초 |
| P04 | AnimInstance·Locomotion·trajectory의 공통 visual demand. gameplay pose와 분리 |
| P05 | 궤적 생성/소비 frame provenance와 reset revision/reason. 기존 history 소비 순서 보존 |
| P06 | layer 실패 상태·generation·명시 retry·원래 master/mesh identity와 늦은 callback 보호 |
| P07 | Combo Notify 실행별 window token으로 중첩/새 노드 경계 보호 |
| P08 | VFX cue lease로 원래 실행만 종료, one-shot dedup과 looping 재시작 수명 분리 |
| P09 | 최종 ASC input/DA 조합·command 입력 최대 16·GE/finite 값/montage section 제작 검증 |
| P10 | 실행 시작 style/revision과 설정 변경 유지/취소 정책. 시각 style 변경은 별도 |
| P11 | 대표 MM node/candidate/capture frame/weight/결과 변경 관측. 최종 pose 기여 증명과 구분 |
| P12 | snapshot/worker/profiling 구조 유지. 43 bound-function inventory만으로 ABP를 일괄 변경하지 않음 |
| P13 | StateController/TIP를 native animation delta 누적 clock으로 통일. pause·dilation·URO delta 검증 |

## 13. 이번 문서 재점검 결과와 남은 정보의 경계

- **ABP:** 제공된 15장에 보이는 master flow, 내부/외부 Blend Stack 차이, Offset Root/OW/Steering, inertial Bool, IK/history/mount, MM 상세, 3종 PSS를 기록하고 원본 사본으로 연결했다. 그래프 내부의 숨겨진 binding·callback 본문·PSD 에셋 목록·접힌 bone 설정은 아직 전체 확인한 정보가 아니다.
- **자료:** 앞선 연구의 공식 10개·개발자 5개 URL을 모두 보존했고 재접근했다. 링크와 각 자료를 어떻게 사용할지, 버전·적용 한계도 기록했다.
- **대화:** 초기 감사→전체 범위 확인→기존 수정→버그 재현/해결→현재 품질·MMORPG 요구→오버 엔지니어링 제한→승인→미착수 인계 흐름을 12절에 추가했다. 과거 상세 작업은 원래 보고서/CSV/JSON을 연결했다.
- **정확성:** 이번 소스 6파일의 SHA256은 인계 표와 모두 일치했다. UE 5.8 소스에서 throttle/interrupt 초기화와 빈 history mapping의 전체 transform 수집을 다시 확인했다. 엔진 cpp 경로의 잘못된 중간 폴더 설명을 수정했다.
- **미실행:** 이번 재점검에서도 runtime 코드·Config·실제 Unreal 에셋을 수정하거나 빌드/테스트/군중 측정을 실행하지 않았다. 사진의 바이트 보존은 에셋 편집이 아니다.

다음 채팅에서 이 파일과 필요한 원래 결과 문서를 읽으면 작업 의도·결정·현재 상태·보호할 이력까지 이어받을 수 있다. **채팅의 모든 문장·도구 출력이 그대로 저장됐다고는 보증하지 않는다.** 원문 대화록이 필요하다면 앱에서 제공하는 별도 내보내기 방법으로 확보해야 하며, 이 인계 문서와 구분한다.

### 두 번째 재점검 기록

2026-10-05의 추가 확인 요청에 따라 `Screenshots/`의 **15장 모두를 이미지 도구로 다시 열어 직접 대조**했다. 사진 존재·체크섬 검사만 반복한 것이 아니다. master의 실제 wire 분기, pose cache, slot group, aim/lean 입력, mode getter, 상세 패널과 세 종류 PSS를 확인했다.

그 결과 사진 6의 Translation Mode getter 연결 차이를 새 확인 항목으로 추가했다. mounted branch가 cached Locomotion에서 갈라지는 점, 전체 Pose Channel weight와 bone별 weight의 차이, sync·profile·비활성 노드 설정 등 기존 요약에서 생략했던 세부도 보완했다. 앞서 모든 사진의 연결이 동일한 것처럼 읽힐 수 있던 문구는 사진별 기준으로 한정했다.

인계에 필요한 확인 범위는 기록했고, **현재 확인된 누락은 보완한 상태**다. 다만 다음 정보는 원래 제공되지 않았거나 실제 실행을 확인하지 않았으므로 다음 작업자가 필요할 때 확인해야 한다.

1. 현재 저장된 ABP가 어느 사진의 연결 상태와 같은지, 사진에 없는 graph·callback·state transition·bone filter·linked layer 내부.
2. 실제 연결된 모든 DA/Chooser/PSD/animation asset의 값과 현재 runtime에서 덮어쓴 결과.
3. 복귀 순간의 실제 MM 검색 여부·선택 pose/시간·발 위상·보정과 pose history의 연결 효과.
4. 네트워크 환경과 군중에서의 실제 비용·시각 품질.
5. 이 문서에 요약·연결된 과거 작업의 모든 메시지와 도구 출력 원문.

이 항목들이 미확인이라는 사실 자체를 인계에 남기는 것이 정확하다. 문서가 충분하다는 말과 실제 프로젝트 내부를 전부 실행 검증했다는 말을 섞지 않는다.
