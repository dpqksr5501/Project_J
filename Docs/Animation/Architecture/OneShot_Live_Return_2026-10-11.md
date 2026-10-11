# 외부 Blend Stack → Motion Matching의 실제 포즈 크로스페이드

후속: [Start/Land 입력 반응과 첫 MM 검색](OneShot_Input_Response_2026-10-11.md)은 단발의 유지/복귀 판단과 입력 변화의 후보 연결을 다룬다. 이 문서의 crossfade와 별도 정책이다.

현재 큰 튐은 보이지 않으며, 목표는 Inertialization 복귀에서 움직임의 연결을 더 자연스럽게 만드는 것이다. 첫 복귀 검색을 보장한 [기존 복귀 검색](MotionMatching_Return_Crowd_2026-10-05.md)은 유지하고, 전환 중 실제 외부 포즈를 계속 평가하는 경로를 추가한다.

기존 [GASP CMC 조사](../Planning/GASP_ProjectJ_Naturalness_2026-10-06/02_GASP.md)의 Pose History·플레이어별 보정·출력 이후 루트/발 보정의 역할을 참고했다. 해당 조사에서는 기본 MM과 Experimental State Machine이 서로 다른 실행 경로다. 이번 변경은 Project J의 직접 Blend Stack 경로에 실제 outgoing 포즈를 겹치는 설계이며, GASP와 동일한 전환 방식이라고 표현하지 않는다.

## 구조와 소유권

`One Shot → Motion Matching Handoff`는 엔진 `FAnimNode_BlendListByBool`을 확장한다. True 포즈는 기존 MM 경로, False 포즈는 기존 외부 Blend Stack이다. 기존 selector 자리만 교체하며, 그 뒤 Inertialization·Slot·OffsetRoot·FootPlacement·LegIK·Pose History는 유지한다.

Active Value는 기존과 같이 `!GetThreadSafeStateControllerShouldOverrideMotionMatching()`을 사용한다. override getter를 반전 없이 연결하면 두 경로가 뒤집히므로, 기존 NOT 연결을 보존한다.

실행 노드는 `Project_JCharacter` runtime 모듈, 그래프 편집 노드는 기존 `Project_JAnimationNodes` UncookedOnly 모듈에 둔다. Editor 전용 모듈에 런타임 Blueprint용 K2 노드를 정의할 때 생기는 컴파일 경고를 제거했으며, 게임 타깃은 그래프 편집 코드에 의존하지 않는다.

- Start·Stop·Land 복귀: 기존 Pose History로 첫 MM 검색을 실행하면서, 실제 단발 포즈와 MM 포즈를 엔진의 표준 블렌드로 섞는다.
- 외부 스택은 복귀 직전 선택된 에셋·진입 시간·Loop·Blend Profile을 유지한다. 새 Cycle chooser 결과가 외부 스택에 들어가거나 ForceBlend가 단발 에셋을 다시 시작하지 않는다.
- 외부 플레이어의 실제 재생 시계는 엔진이 계속 진행한다. native hold 시간으로 재생 시간을 다시 설정하지 않는다. 클립 끝에 도달했다면 마지막 포즈가 유지되며, 존재하지 않는 후속 움직임을 만들지 않는다.
- 보관 수명은 엔진의 외부 포즈 가중치가 0이 될 때까지다. 별도 고정 타이머나 MM의 상시 백그라운드 실행은 없다. 보관 에셋은 generated anim node의 reflected transient property가 GC 참조를 유지한다.
- 실제 포즈 합성의 담당자는 외부 handoff 노드 한 곳이다. MM 내부 Blend Stack은 MM 후보 사이의 기존 블렌딩을 담당한다. 한 번의 복귀에 두 개의 외부 블렌드 요청을 겹치지 않는다.

외부 플레이백 getter의 보관 뷰는 handoff 노드의 worker Update 동안만 적용한다. 최신 gameplay snapshot, MM의 DB·trajectory·입력·모드·검색 요청은 보관하지 않는다. nested traversal 종료 시 이전 뷰를 복원하며 GT로 포인터를 게시하지 않는다.

## 유지하는 경계

새 단발 요청은 복귀보다 우선한다. 탑승·공중·회전 모드 변경·공격·회피·피격·전신 몽타주가 개입하면 live overlap을 해제한다. 진행 중이던 live overlap은 기존 Inertialization 요청으로 MM에 넘기며, child graph 전체를 재초기화하지 않는다.

TIP·Pivot은 단발의 root 회전 완료 책임이 있으므로 기존 Inertialization 복귀를 유지한다. 동일 요청이 유지되는 동안 Pivot phase가 Cycle로 바뀌어도 root-turn episode를 기억한다. 외부 스택으로 들어가는 일반 진입도 기존 Inertialization을 유지한다.

OTM과 Strafe의 방향·Steering·DB 선택은 기존 snapshot을 사용한다. outgoing Strafe 에셋의 Orientation Warping 허용 여부는 유지하되 이동 각도는 현재 값을 사용한다. Sprint 방향 허용 범위나 Run/Sprint candidate 정책은 바꾸지 않는다.

기존 Foley와 EarlyTransition native notify는 `UAnimNotifyLibrary::IsBlendingOut`을 검사한다. 엔진 blend list는 outgoing branch를 `AsInactive()`로 업데이트하므로, 외부 에셋을 계속 평가한다는 이유로 그 branch의 발소리나 새 EarlyTransition 요청을 중복 실행하지 않는다. 다른 notify의 임의 반복·root-motion 추출은 별도 검증 대상이다.

## 데이터와 비교

프로필의 `bEnableLiveOneShotReturn`이 기능을 허용한다. `TransitionBlendTime`은 복귀 블렌드 시간이며, selector의 True Blend Time에 native getter를 연결한다. 기본값은 기존과 같은 0.2초다.

Chooser output의 `ReturnBlendTime`은 개별 단발 모션의 복귀 시간이다. 기본 -1은 프로필 사용, 0은 즉시 복귀다. Start의 진입 BlendTime과 복귀 BlendTime은 서로 다른 값이다. 기존 에셋의 authored exit·EarlyTransition notify·fallback lead time은 바꾸지 않는다. 클립 끝 이전의 움직임까지 겹치려면 해당 에셋의 적절한 authored exit 구간이 필요하며, 모든 모션을 일괄적으로 일찍 자르지 않는다.

콘솔 비교:

```text
p.ProjectJ.LiveOneShotReturn 0
p.ProjectJ.LiveOneShotReturn 1
p.ProjectJ.OneShotReturnDebug 1
```

0은 같은 handoff 노드에서 기존 관성 복귀를 사용한다. 진단은 기본 0이며, 켜면 복귀 모션·MM/외부 가중치·완료를 기록한다. 출력은 자연스러움의 주관적 평가를 대신하지 않는다.

MM 노드는 복귀가 시작되면 활성화되므로 해당 시점부터 MM 드로우 정보도 나온다. 단발 재생 중 MM이 대기할 때는 외부 Blend Stack의 재생 정보와 구분한다.

## 검증과 저장 범위

현재 검증 자료는 `Saved/Validation/OneShotReturn_20261011`에 보관한다. 직접 UnrealBuildTool.exe로 빌드하고, 실행 중인 editor/build를 중단하거나 겹치지 않는다.

적용 당시 `ProjectJ.Animation.A_Prepare.OneShotReturnGraph`의 preview 플래그는 Master를 메모리에서만 변경·컴파일했다. `A_Prepare` 그룹으로 같은 배치의 재생 테스트보다 먼저 준비했다. 실제 저장은 별도의 explicit apply 플래그를 사용해 사용자 승인 후 Master 한 파일에 한정했다. 저장·재읽기 검증을 마친 뒤 커밋 전 정리에서 이 일회성 교체·컴파일·저장 분기와 관련 include를 제거했다. 현재 테스트는 저장된 Handoff 노드와 복귀 시간 연결을 읽기만 한다. PSS·PSD·Normalization Set·Run/Sprint 데이터·레벨은 Handoff 적용의 저장 대상이 아니다.

`AuthoredPlayback`은 실제 BP/GAS/CMC와 충돌 바닥에서 OTM/Strafe × Run/Sprint의 Start·Stop·Land 복귀를 실행하고 실제 플레이어·가중치·포즈 이동을 기록한다. NullRHI 결과는 렌더링된 자연스러움의 승인이나 발 미끄러짐 평가가 아니다. `CommandOwnership`은 outgoing 명령 유지, scope 복원, 가중치 완료 후 추가 tick 중단, 새 단발·모드 변경의 우선권을 확인한다. TIP·Pivot phase 변경·공중·공격·회피·피격·전신 몽타주·프로필 비활성·0초 복귀도 검증한다.

2026-10-11 검증 결과:

- Editor / Game Win64 Development를 직접 UBT로 빌드했다. 모두 성공했다. 근거: `EditorBuild.txt`, `GameBuild.txt`.
- 실제 그래프 preview 컴파일은 오류 0·경고 0이다. `GraphAndGuards` 2/2 성공으로 명령 소유권과 중단 조건도 확인했다.
- 실제 CMC의 8개 재생 장면은 모두 Start 또는 Land에서 복귀했다. Start/Stop 장면은 장면당 2회 복귀·22개 live blend 프레임, Land 장면은 1회 Land 복귀·11개 live blend 프레임을 기록했다. 60Hz·기본 0.2초 설정의 결과다. 근거: `LivePreviewFinal.txt`.
- 동일 graph에서 `p.ProjectJ.LiveOneShotReturn 0`을 적용한 관성 비교도 8개 장면과 자동화 2/2를 통과했다. 실제 live frame 수는 모두 0이다. 근거: `InertialBaseline/index.json`, `InertialBaseline.txt`.
- 위 비교 실행은 콘솔 명령 구분자를 수정하기 위해 MCP로 같은 테스트 세션에서 실행했다. 테스트 완료와 controller Ready 상태를 확인한 뒤, 종료 신호에 응답하지 않는 자체 무인 세션만 종료했다. 실행 중인 빌드나 테스트를 중단하지 않았다.

- 최종 배치 `RegressionFinal`은 25/25 성공, 경고 0·실패 0이다. 새 그래프를 먼저 준비한 뒤 실제 Sprint 29개·Run/Strafe 57개 CMC 회전 시나리오, Start/Stop/Land 복귀, MM 검색·후보 수명·Steering 소유권·Pivot·EarlyTransition·입력 취소를 검증했다. `RegressionFinal.txt`에서도 8개 복귀 장면의 실제 outgoing 에셋 유지와 live 가중치를 확인했다.

### 실제 적용 상태

2026-10-11 사용자의 **Master 에셋 저장 승인**을 받아 `/Game/Animation_Logic/ABPs/ABP_Humanoid_Master` 한 개에 적용·저장했다. 기존 Bool selector를 새 Handoff 노드로 교체하고 True Blend Time에 `GetThreadSafeOneShotReturnBlendTime`을 연결했다. 기존 Active Value의 NOT 연결과 두 포즈 경로는 보존했다. 일반 에디터 실행도 저장된 Handoff 그래프를 사용한다.

`-ProjectJApplyOneShotHandoff` authoring 경로는 이미 dirty인 package를 거부하고 오류·경고 0으로 컴파일한 뒤 이 Master만 저장했다. `SavedApply/index.json`은 1/1 성공·경고 0·실패 0이다. PSS·PSD·Normalization Set·Run/Sprint 데이터·레벨은 변경하지 않았다.

저장 세션이 종료된 뒤 새 세션에서 preview/apply 플래그 없이 저장본을 다시 읽었다. `-ProjectJExpectOneShotHandoff` 검증은 그래프·AuthoredPlayback·CommandOwnership **3/3 성공, 경고 0·실패 0**이다. 근거는 `SavedVerification/index.json`과 `SavedVerification.txt`다. OTM/Strafe × Run/Sprint의 8개 장면 모두 실제 live overlap을 확인했다. Start/Stop 장면당 22개, Land 장면당 11개 live 프레임으로 preview 결과와 일치했다. 렌더링된 체감·발 접지의 최종 평가는 에디터에서 별도로 비교한다.
