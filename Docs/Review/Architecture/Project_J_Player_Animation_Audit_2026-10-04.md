# 플레이어·궤적·애니메이션 구조 재점검

기준: 2026-10-04, UE 5.8, HEAD `33b574a1d6775306a26357f67bc3d79311e7189b`와 현재 미커밋 R01–R14 구현을 포함한 작업 트리.

플레이어의 소유권, 이동·입력, 궤적, OTM/Start/Stop/Pivot/TIP, Motion Matching, AnimInstance·Proxy, 장비·전투·스킬·탑승, UI·카메라·복제 연결을 재점검했다. **궤적과 애니메이션에도 개선할 항목이 남아 있다.** 기존 역할 분리를 유지하고, 소유권·데이터 유효성·실패 복구·시간 기준을 보강하는 방향이 적절하다.

이번 작업은 감사이며 runtime 코드를 추가 수정하지 않았다. 관련 자동화 **87개: 일반 성공 84, 경고 동반 성공 3, 실패·미실행·실행 중 0**이다. 새 조건부 경계 사례를 모두 실행 재현했다는 의미는 아니다.

## 범위와 근거

[이전 캐릭터 감사](Project_J_Character_Architecture_Maturity_Audit_2026-10-03.md)의 23개 네이티브 프로젝트 컴포넌트·23개 DA 검토와 [R01–R14 적용 기록](Project_J_Architecture_Maturity_Implementation_2026-10-03.md)을 기준으로 현재 호출 경계를 확인했다.

- 이전 상세 감사에 연결된 144파일 중 **137파일은 SHA-256이 같고 7파일은 최근 구현으로 변경**됐다. 7파일은 최근 적용 검증의 현재 hash와 대조했다.
- 현재 **65파일의 관련 본문·수명·호출·정책 경계를 재확인**했다. 거대한 AnimInstance의 모든 행을 다시 읽었다고 표현하지 않는다. 변경되지 않은 세부 검토는 이전 감사와 연결한다.
- 이 두 범위의 합집합은 **159파일**이다. [범위표](Project_J_Player_Animation_Coverage_2026-10-04.csv)는 이전 감사 재사용, 현재 대상 경계 검토, 최근 구현 검증을 구분한다.
- 최근 구현의 변경 코드 59파일도 저장된 hash와 모두 일치했다. 이번 감사에서 기존 사용자·구현 변경을 덮어쓰지 않았다.
- Unreal MCP는 사용하지 않았다. 기존 read-only `AnimationContractAudit`가 Greatsword fixture의 master ABP와 combat layer를 읽었다. 이를 모든 Blueprint·Chooser·DA 연결 확인으로 확대하지 않는다.

**코드 경로**는 소스에서 조건과 결과를 확인한 항목, **조건부**는 특정 구성·중첩·시간 정책에서 검증할 항목, **구조 제안**은 현재 오류를 단정하지 않는 확장 개선이다. P1은 소유권 누수·조작 복귀, P2는 실행 경계·제작 안정성, P3는 관측·측정 후 선택할 개선이다.

## 개선 항목

| ID | 우선순위·판정 | 문제 또는 개선 여지 | 권장 방향 | 이전 기록 |
| --- | --- | --- | --- | --- |
| P01 | P1·코드 경로 | 장비 효과 회수 시 Avatar에서 ASC를 다시 찾는다. PlayerState가 해제·교체되면 원래 ASC의 효과·grant를 회수하지 못할 수 있다. | 부여한 원래 ASC와 source·handle을 ledger에 보관한다. Gameplay 상태는 지속 소유자, Avatar는 시각 표현을 담당하도록 단계적으로 정리한다. | A01 재확인 |
| P02 | P1·코드 경로 | Mounted tag 회수 전에 Rider의 PlayerState가 해제된다. 강제 하차도 위치 검사 실패 시 종료하고 Mount EndPlay에는 rider 복구가 없다. | 탑승 세션이 원래 ASC를 보관해 정확히 회수한다. 일반 하차 위치 검사와 강제 수명 정리를 분리하고 teardown에서는 재possess 여부를 구분한다. | A02/A03 재확인 |
| P03 | P2·코드 경로 | 생성이 중단된 궤적 sample을 현재 미래 속도로 조회할 수 있다. | 조회가 eligibility·age·reset 이후 생성 여부를 검사한다. 무효면 이미 존재하는 CMC 속도·가속·제동 추정으로 fallback한다. | A04 재확인 |
| P04 | P2·조건부 정책 불일치 | 궤적·Locomotion은 leader mesh의 렌더 시각만 보고, AnimInstance는 보이는 follower·장비도 고려한다. | 공통 presentation eligibility를 사용하되 gameplay pose 수요와 시각 가시성은 별도 입력으로 둔다. hidden leader/visible follower 구성에서 생성·갱신 정책을 일치시킨다. | 이번에 구체화한 항목 |
| P05 | P2·구조 제안 | 입력·이동·궤적·선택 revision은 존재하지만 어느 프레임의 데이터를 소비하고 reset했는지 공통으로 추적하기 어렵다. | 기존 snapshot에 frame provenance·source·invalid/reset reason을 정리하고, 이번 Stop 검색이 history를 소비한 뒤 reset하도록 적용 순서를 유지한다. | A08 재확인 |
| P06 | P2·코드 경로/조건부 | Mounted Layer 로드 실패가 완료 callback→Refresh→재요청으로 이어진다. class 일치만으로 mesh/AnimInstance 재초기화 후 연결까지 보장하지 못한다. | 실패 상태·재시도 한도·request generation·binding identity를 보관한다. 기본 pose fallback, 비동기 로드, EndPlay 취소는 유지한다. | A05 재확인 |
| P07 | P2·조건부 중첩 경로 | ComboWindow는 공통 event tag와 magnitude 1/0으로 현재 GA의 bool을 바꾼다. 이전 창의 늦은 End를 구분하지 않는다. | 공격/노드 generation과 Notify instance에 속한 window token으로 열고 닫는다. 입력 버퍼와 다음 노드 소비도 같은 generation에 귀속한다. | 신규: R04는 WeaponMotion/MeleeHit만 개선 |
| P08 | P2·조건부 중첩/재시작 경로 | VFX Notify End가 현재 attack의 같은 CueTag를 종료한다. looping cue도 per-attack 시작 중복 집합에 남아 Stop 뒤 재시작이 차단된다. | 실행 lease를 종료 인자로 전달한다. one-shot 중복 방지와 looping cue의 소유·재시작 정책을 구분한다. | 신규 |
| P09 | P2·구성/제작 검증 공백 | 서로 다른 grant source가 같은 InputTag를 부여할 수 있다. Instant 장비 GE, 16개를 넘는 command 입력열, montage section 등 제작 계약에도 공백이 남아 있다. | 기존 CombatConfiguration을 확장해 최종 grant/input/attack/presentation 조합을 검증한다. runtime과 editor validator가 같은 capability·한도를 사용한다. | A06/A07 재확인; R10 전체 대체 아님 |
| P10 | P2/P3·조건부 확장 | 입력 해석과 GA 실행은 분리됐지만 실행 중 전직·장비·style 변경의 유지/취소/다음 실행 적용 정책은 더 명시할 수 있다. | 실행 시작 시 effective configuration revision을 보관하고 행위별 정책을 정의한다. 이미 구현된 입력 alias 회수와 held-state 정리는 재사용한다. | A09 재확인 |
| P11 | P3·관측 개선 | Proxy의 대표 MM 결과는 같은 DB를 가진 첫 결과를 선호한다. 비활성 graph에 남은 결과와 이번 traversal의 실제 결과를 대표값만으로 구분하기 어렵다. | producer node ID·update/traversal provenance·relevance를 기록하거나 node별 결과를 표시한다. 선택된 대표 결과를 최종 화면 pose로 단정하지 않는다. | 신규; 실제 pose 오류를 입증한 항목 아님 |
| P12 | P3·클래스 실측 목록/성능 조건부 | 대표 master ABP에는 43개의 exposed bound-function 경로가 남아 있다. multi-thread 옵션만으로 전체 FastPath나 실제 worker 실행을 보장할 수 없다. | Insights에서 비용이 큰 경로를 먼저 찾고 snapshot/property access로 옮길 수 있는 데이터 조회만 줄인다. node function을 일괄 제거하지 않는다. | 이번 read-only fixture inventory |
| P13 | P2·시간 정책 조건부 | StateController hold와 TIP root 추출은 플랫폼 경과 시간을 사용하지만 애니메이션은 delta 기반으로 재생된다. pause/time dilation을 사용할 때 기준이 어긋날 수 있다. | actual asset playback time 또는 명시적인 animation clock을 사용한다. hidden/URO 생략 시간의 정책과 실시간 watchdog은 구분한다. | 신규; 실제 slow-motion 동작은 미재현 |

## 궤적·애니메이션의 구체적 근거

### P03/P04: 조회 유효성과 시각 가시성

`MotionMatchingTrajectoryComponent.cpp:184–188`은 generation 불가 시 sample을 남기고 반환한다. `TryGetFuturePlanarVelocity`(`:304`)는 sample query helper를 호출하며 generation eligibility/age를 검사하지 않는다. 소비자 `LocomotionAnimStateComponent.cpp:696`은 성공한 값으로 미래 속도·이동 여부를 판정한다. 같은 함수 안에 현재 속도·가속·제동 기반 추정이 이미 있으므로, 무효 sample일 때 이 경로로 돌아가도록 연결하면 된다.

기존 generation/reset revision, age, last frame을 재사용한다. 로컬과 희소 원격 갱신의 주기가 다르므로 임의의 공통 1프레임 한도를 적용하지 않는다. 전체 trajectory 배열을 새로운 context에 다시 복사하는 방식도 피한다.

`MotionMatchingTrajectoryComponent.cpp:238`과 `LocomotionAnimStateComponentBase.cpp:74–77`은 leader mesh의 `WasRecentlyRendered`를 사용한다. 반면 `CharacterAnimInstanceBase.cpp:136`의 시각 가시성은 follower, 다른 시각 mesh, 보이는 leader를 순서대로 고려한다. 따라서 follower를 렌더링하고 leader를 숨기는 구성에서 판정 기준이 다르다. 실제 렌더 timestamp·shadow 정책에 따라 나타나는 조건부 문제이며 모든 원격 플레이어에서 생성이 중단된다고 단정하지 않는다.

현재 `Animation.HiddenLeaderVisibleFollower`는 follower 탐색과 AnimInstance budget tier를 검사한다. 궤적 producer와 Locomotion의 eligibility까지 검사하지 않는다. 이 테스트가 성공했다는 사실만으로 세 계층의 정책 일치를 보장하지 않는다.

### P05: Reset 순서를 보존하는 개선

Player Tick은 이동 정책→궤적 생성→Locomotion 의미 판정 순서다. AnimInstance는 GT에서 snapshot과 StateController Chooser를 처리하고 `:1028`에서 acceleration-stop reset을 적용한 뒤 Proxy로 전달한다. snapshot에는 궤적 배열과 generation/reset/age/eligibility가 이미 들어간다(`:2557–2562`). 로컬 combat strafe history 보존 예외도 존재한다(`:3426`).

이 구조를 유지하면서 어느 입력/이동 revision과 궤적 generation을 소비했는지, history reset을 언제 적용했는지를 공통 context와 trace로 연결한다. 궤적 producer가 있다는 이유만으로 모든 reset을 생성 직전으로 이동시키면 Stop 검색의 방향 선택이 달라질 수 있다.

CMC/Actor 순서가 반드시 한 프레임 늦는다는 주장도 채택하지 않았다. 로컬 UE `UMovementComponent`는 기본 `bTickBeforeOwner=true`이고 등록 시 owner tick에 movement tick prerequisite를 추가한다. 프로젝트의 실제 trace·custom 설정 없이 지연 버그를 단정할 근거는 부족하다.

### P07/P08: 남은 Notify 실행 경계

`AnimNotifyState_ComboWindow.cpp:12/28`은 Begin/End에서 공통 event의 magnitude를 각각 1/0으로 보낸다. `GameplayAbility_Melee.cpp:309`는 이 값을 현재 `bIsComboWindowOpen`에 적용한다. A 창→B 창→A End 순서나 이전 노드의 blend-out End에서 현재 창을 닫을 수 있는 코드 경로다. 실제 콘텐츠가 이런 중첩을 만들지 않으면 재현되지 않을 수 있다. 최신 WeaponMotion·MeleeHit의 token 계약을 콤보에도 확장하는 것이 자연스럽다.

`AnimNotifyState_CombatPresentationCue.cpp:28`은 자신이 시작한 attack identity를 보관하지 않고 현재 `StopCue(CueTag)`를 호출한다. `CombatPresentationComponent.cpp:312`는 그 시점의 `ActiveAttackInstance`로 정지 상태를 게시한다. 원격 packet의 event order/attack instance 검사만으로 원본에서 잘못 생성한 정지 이벤트를 막을 수 없다.

또한 `PlayCueLocal`은 `StartedCueTags` 중복을 거절하지만 `StopCueLocal`은 이 집합에서 tag를 지우지 않는다. 같은 공격에서 loop A→Stop A→다시 Play A가 필요한 콘텐츠라면 재시작되지 않는다. 의도적으로 공격당 한 번만 허용한다면 제작 validator에서 이를 명시한다. 여러 lease를 합성할 때에는 one-shot 발사와 looping 자원 수명을 분리한다.

TwoHandIK는 Notify instance ID를 사용하며 최신 WeaponMotion·MeleeHit도 실행 token을 갖는다. EarlyTransition과 GroundContact는 depth/count로 중첩을 합성한다. 모든 Notify가 같은 결함이라고 묶거나 이미 해결된 R04를 미완료로 다시 세지 않는다.

### P11/P12: 대표 결과와 실제 실행 비용

Proxy `CapturePostSelection`(`CharacterAnimInstanceProxy.cpp:108`)은 generated MM node 중 결과가 있고 현재 DB와 일치하는 후보를 선호한다. 이 선택 조건만으로 이번 traversal에서 기여한 node나 최종 blend weight까지 알 수는 없다. 대표 결과는 진단값으로 취급하고 node별 출처를 추가하면 OTM/Stop 문제를 추적하기 쉬워진다.

이번 fixture inventory:

| compiled class | Nodes | Exposed handlers | Bound functions | Property copy records | Copy-only handlers | MultiThreadedUpdate |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `ABP_Humanoid_Master` | 93 | 93 | 43 | 3 | 2 | 1 |
| `ABP_Greatsword_Layers` | 5 | 5 | 0 | 2 | 1 | 1 |

이는 compiled handler 목록의 수치이며 평가 시간·실제 병렬 task 수·전체 FastPath 비율이 아니다. Master의 일부 경로는 이미 필요에 맞게 C++ snapshot을 소비한다. 남은 handler를 모두 getter로 치환하거나 이번에 실제 에셋 migration을 적용할 근거로 사용하지 않는다. 비용 측정 후 필요한 producer/property access 경로를 좁혀 변경한다.

### P13: 애니메이션 시간과 실시간

`CharacterAnimInstance.cpp:1798`은 `FPlatformTime::Seconds`를 hold의 Now로 쓰고 `:1887`은 그 경과 시간을 asset 길이와 비교한다. `PlayerCharacter.Movement.cpp:87–101`의 TIP root yaw도 같은 hold elapsed로 animation 추출 시점을 정한다. time dilation·pause가 있는 구성에서는 실제 재생 시간과 이 경과 시간이 달라질 수 있다.

현재 소스에서 해당 시간 효과의 실제 사용·재현은 확인하지 않았다. 적용할 경우 animation 시간, world/semantic 시간, 실시간 watchdog을 명시적으로 구분해야 한다. 단순히 모두 wall time 또는 모두 world time으로 바꾸는 수정은 URO·hidden 원격의 생략 시간 정책까지 바꿀 수 있으므로 회귀 검증이 필요하다.

## 유지할 구조와 영역별 판단

| 영역 | 유지할 점 | 다음 검증 |
| --- | --- | --- |
| PlayerState·Avatar | 지속 ASC/Progression/Inventory/Equipment와 Avatar 표현의 분리 | 장비 ledger와 실제 possession 왕복 |
| 입력 | 의미 입력, chord router, execution, GAS의 역할 분리; binding handle·alias release·raw input 서버 검증 | 최종 input 충돌, 실행 중 configuration 변경 |
| Locomotion | 값 snapshot, 의미 gait/rotation/phase, 로컬 입력과 원격 재구성 구분 | 공통 visibility/freshness, frame provenance |
| StateController | Start/Stop/TIP 중복 소비, Pivot revision·commit·cancel, full-body/mount 경계 | 실제 chooser 재생과 시간 정책 통합 테스트 |
| MotionMatching | phase/context revision, 강제 경계 갱신, owner별 분산 schedule | 실제 node 결과 출처와 measured search 비용 |
| AnimInstance·Proxy | UObject 조회·Chooser는 GT, worker는 snapshot 소비 | orchestration 정책을 작은 값 함수로 정리하되 실행 순서 유지 |
| 전투·SSR | 판정용 leader, canonical blade 경로, 서버 검증, post-physics bounded history | 새 Notify window와 attack generation 연계 |
| IK·리타깃 | GT rig/profile snapshot, wrist 보정, helper bone 보존, LOD/teleport reset, 최근 제작 validator | 실제 rig·socket·dynamic profile 연결 |
| 장비·시각 | revision/lease 기반 비동기 시각 로드와 bounded retry, 호환 pose source 거절 | Gameplay 효과의 지속 소유권 |
| 탑승·Layer | 권한·상태 검사, 기본 pose fallback, 비동기 layer | 강제 종료·태그 회수·로드 실패·binding identity |
| UI·카메라 | 수요 기반 구독, 정확한 ASC delegate 해제, 로컬 possession에만 카메라 tick | 실제 travel/splitscreen UI |
| 복제 | jump/move/landing의 sparse semantic snapshot과 recovery, owner-only 비공개 상태 | 지연/유실/중복/오래된 상태의 네트워크 PIE |

## 검증과 후속 순서

이번 실행은 `UnrealEditor-Cmd.exe -unattended -NullRHI -nosound -NoLiveCoding -nop4 -nosplash`와 관련 suite 필터를 사용했다. 프로세스 exit 0뿐 아니라 report의 87개 상태, 실패·미실행·실행 중 0도 확인했다. 실행 중인 build/editor를 중단하거나 중첩하지 않았다. 소스가 최근 빌드 검증과 동일하므로 추가 C++ 빌드는 하지 않았다.

원본 report: `Saved/Automation/PlayerAnimationAudit_20261003/index.json`, log: 같은 폴더의 `Run.log`. 파일명은 실행을 시작한 날짜이며 이 문서는 한국 시간 날짜 변경 후 2026-10-04로 기록했다. 테스트별 상태·경고 원문·클래스 inventory는 [검증 JSON](Project_J_Player_Animation_Validation_2026-10-04.json)에 보존했다.

경고 동반 성공 3개는 TwoHandIKTransitionAndCurve(1건), WeaponPresentationIdentity(5건), StableGripTargetsAndAuthoredAlpha(1건)의 fixture Draw/Sheathe socket 미설정이다. runtime 검증을 완화하거나 경고를 삭제하지 않았다. 이 87개는 이전 suite와 겹치므로 과거 성공 수에 더해 고유 테스트 수로 발표하지 않는다.

권장 구현 순서:

1. **P01/P02:** 원래 ASC와 탑승 세션 소유권, 강제 종료의 정리 보장.
2. **P03/P04:** 궤적 조회 유효성·fallback과 공통 가시성 정책.
3. **P06/P07/P08:** Layer 실패 상태와 Combo/VFX 실행 token.
4. **P05/P13:** frame·시간 계약과 OTM/Start/Stop/Pivot/TIP 경계 재생 검증.
5. **P09/P10:** effective DA 조합·실행 capability·설정 변경 정책.
6. **P11/P12:** 실제 node 진단과 profiler 결과를 바탕으로 성능 개선 범위 결정.

추가 회귀 사례는 장비 착용 상태의 old/new Avatar 교체, PlayerState 해제 후 회수, 실제 반복 탑승·모든 하차 위치 차단·Destroy, hidden leader/visible follower의 producer eligibility, 무효·오래된 궤적의 fallback, layer 실패 요청 상한, 동일 class의 AnimInstance 재초기화, A Begin→B Begin→A End, 동일 attack tag의 새 generation, looping cue 재시작, 30/60/120fps·희소 원격 갱신·pause/time dilation이다.

현재 통과한 값 기반 테스트와 read-only fixture audit는 좋은 기반이다. 최종 시각 품질, 전체 asset 연결, cooked/package, 실제 네트워크 지연, 대규모 FPS·P95/P99까지 검증한 것으로 표현하지 않는다. 이번에는 감사 문서·범위표·검증 요약만 추가했다.
