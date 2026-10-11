# Motion Matching 복귀 검색과 군중 검색 예산 — 2026-10-05

2026-10-11 후속: 첫 검색 계약을 유지하면서 실제 단발 포즈와 MM 포즈를 겹치는 [Live Return 전환](OneShot_Live_Return_2026-10-11.md)을 추가한다. 아래 기록은 당시의 관성 전환과 검증 이력이다.

대상은 외부 State Controller Blend Stack에서 MM으로 돌아오는 경계와 기존 거리 예산의 worker 검색 연결이다. 기존 AnimInstance, immutable snapshot, Proxy, LocomotionProfile과 Animation Budget Allocator를 사용한다. 새 관리자·컴포넌트·Tick·복제 필드는 추가하지 않는다. [작업 인계](../../Handoffs/MotionMatchingCrowd_2026-10-05/MotionMatching_Crowd_Handoff_2026-10-05.md)의 미착수 작업에 대한 후속이며, 당시 기록은 그대로 보존한다.

## 복귀 요청과 최신 상태

Proxy는 snapshot을 매 publication마다 최신 값으로 교체한다. 요청 serial과 노드별 처리 revision만 별도로 보관한다. 명시 reselect, 지상 Idle edge, 외부 override의 true → false와 선택 DB 변경을 합성한다. snapshot이 다시 들어오거나 그래프가 URO/ABA로 생략됐다는 이유로 미처리 요청을 지우지 않는다.

외부 override가 활성인 동안 강제 복귀 검색을 적용하지 않는다. 노드가 실제 검색을 처리하면 그 노드의 revision만 완료한다. 다른 비활성 노드의 요청은 다음 실제 업데이트까지 남고, 처리한 노드에는 같은 요청을 반복 적용하지 않는다. 캐릭터를 깨우거나 URO/ABA를 해제하는 호출은 이 경로에 없다. 기존 remote urgent update coordinator와 Presentation/GameplayPose 수요가 갱신 기회를 결정한다.

MM 비활성화·탑승은 요청을 취소하며 initialize는 snapshot, 처리 이력과 노드 캐시를 초기화한다. generated AnimClass가 바뀌면 이전 노드 메모리에 대한 처리 이력을 재사용하지 않는다. 이미 처리된 요청의 옛 inactive 노드 잔여분은 새 class로 넘기지 않는다. DB나 이동 상태를 요청 발생 시점의 값으로 붙잡지 않고, 처리할 때의 최신 snapshot과 C++ 선택 DB를 사용한다.

## 실제 검색의 확인과 엔진 의존 범위

UE 5.8 `AnimNode_MotionMatching.cpp`의 regular 검색 경로는 `ElapsedPoseSearchTime`을 0으로 만든 뒤 `UPoseSearchLibrary::MotionMatch`를 호출한다. throttle skip은 delta를 누적하고, relevancy reset은 infinity로 초기화한다. 단순 node update·cached weight·선택 animation 변경은 검색 완료 증거로 쓰지 않는다.

현재 엔진에는 public 검색 완료 counter가 없다. 기존 reflection 기반 노드 설정 방식과 함께, reflected transient `MotionMatchingState`의 검색 timer를 **이번 traversal 동안만** 엔진 reset 값인 infinity로 표시하고 throttle을 0으로 적용한다. 검색 후 0이 된 것을 관찰하면 요청을 처리한다. 비활성·missing history에서는 완료하지 않으며, 남은 marker와 일시 throttle·force interrupt는 정리한다. 활성 interaction의 선택 소유권도 침범하지 않는다. 이 어댑터는 private C++ offset이나 엔진 패치에 의존하지 않지만, 엔진 timer 의미에 의존하므로 엔진 업그레이드 때 실제 검색 테스트와 위 소스를 다시 확인해야 한다.

검색을 실행했다는 것은 유효한 후보를 얻거나 다른 animation을 골랐다는 뜻이 아니다. 빈 후보 결과도 검색 실행이며, 같은 후보가 재선택되는 경우도 정상이다. 실제 후보/index/asset 품질은 기존 결과 관측과 Rewind Debugger로 별도 확인한다.

## 복귀 query와 자세 연속성

일반적인 이동 redirect는 기존 `ForceInterrupt`를 유지한다. 외부 one-shot 복귀만 `ForceInterruptAndInvalidateContinuingPose`로 한 번 처리한다. UE는 이 모드에서 이전 MM continuing pose를 query의 기준으로 사용하지 않고 Pose History로 query를 만든다. 이전 MM pose가 실제 외부 Blend Stack의 마지막 pose와 다를 수 있기 때문이다. 이때 Pose History 자체나 내부 Blend Stack player를 삭제하지 않는다. 기존 Stop/Idle의 강한 무효화와 Strafe Dynamic/Settled 연속성 계약은 보존한다.

사용자 제공 현재 사진에서 Offset Root의 Translation/Rotation getter는 각각 해당 Mode에 연결되어 있고, 외부/MM Bool 전환은 양쪽 0.2초 Inertialization이다. MM 내부 graph는 Input → Output이다. 이 작업에서 ABP, Chooser, PSS, PSD, DA, 레벨을 수정하거나 저장하지 않는다. 현재 collector의 샘플 시간·최종 IK가 반영된 pose와 query 연결은 실제 재생에서 확인해야 하며, 코드상 즉시 검색만으로 자연스러움을 보증하지 않는다.

## worker 거리 예산

GT의 `CurrentOptimizationPolicy.MotionMatchingUpdateInterval`을 snapshot의 `MotionMatching.MinimumSearchInterval`로 전달한다. worker는 node 기본값·phase 검색 억제와 budget floor의 최댓값을 사용한다. 긴급 reselect는 그 위에 일시적으로 0을 적용하고 처리 후 정상값으로 복원한다.

| 조건 | node 기본 0.05초인 경우 |
| --- | --- |
| Local/Near floor 0 | 0.05초 유지 |
| Mid floor 0.033 | 0.05초 유지; 자동 비용 감소를 주장하지 않음 |
| Far floor 0.083 | 0.083초 |
| Hidden floor 0.10 | 0.10초; 실제 mesh tick을 깨우지 않음 |
| phase suppression 3600 | 기존 억제 유지 |
| 처리하지 않은 복귀/강제 요청 | 필요한 실제 traversal의 검색에 0 적용 |

숨김 상태의 native sampling skip에서는 기존 의도대로 이동 sampling을 생략하지만 budget 값은 매 publication마다 갱신한다. 오래된 force pulse를 다시 이벤트로 발급하지 않는다. montage 재생·root motion·notify·GameplayPose의 기존 보호 조건은 변경하지 않는다. 이 간격은 engine update 간격에 따라 양자화되며, wall-clock FPS나 PoseSearch CPU의 개선율과 같지 않다.

## 검증

직접 `UnrealBuildTool.exe`로 Editor/Game Win64 Development build를 성공했다(exit 0). 최종 로그는 `Saved/Validation/MMCrowd_20261005/BuildEditor_Final2.log`(26.08초)와 `BuildGame.log`(105.51초)다. 실행 중인 build/editor를 중단하거나 겹쳐 실행하지 않았다.

기존 112개 + 새 MM 테스트 3개 + 기존 crowd schedule 테스트 1개, 총 **116개가 통과**했다. 정상 성공 112개, 경고 포함 성공 4개, 실패·미실행·실행 중 0이다. 기존 warning event 8개의 test/message를 전회 report와 대조해 차이 0을 확인했다. 새 fixture의 missing-history 오류는 해당 테스트가 의도적으로 요구하고 소비하는 expected error다. report는 `Saved/Automation/MMCrowd_20261005/index.json`, 로그는 `Saved/Validation/MMCrowd_20261005/Regression.log`다. 기존 목록에 조건부 crowd asset audit를 무작정 추가하지 않았다.

[검증 JSON](MotionMatching_Return_Crowd_Validation_2026-10-05.json)에 최종 결과, 소스 8개(기존 7개·새 테스트 1개)의 작업 전후 SHA256과 로그/report hash를 기록한다. backup은 `Saved/Validation/MMCrowd_20261005/Before/`에 있다. 인계의 6개 source hash와 작업 시작 backup hash는 모두 일치했다.

중간 기록도 보존했다. 최초 sandbox 실행은 UBT 사용자 로그 폴더 접근 거절로 컴파일 전 종료했으며, 최근 Windows Application 로그에서 대응 .NET/Application Error를 찾지 못했다. 경로 변경·삭제 없이 권한을 높여 재실행했다. 첫 실제 compiler 오류는 새 테스트 helper에서 존재하지 않는 `FindFPropertyChecked` 이름을 사용한 것이었고, 엔진 API `FindFProperty`와 명시 check로 수정했다. 이후 숨김 재전달과 새 snapshot의 force pulse를 명시적으로 구분한 최종 소스를 다시 빌드했다.

새 회귀는 최신 snapshot과 요청 수명 분리, 자연 복귀·force 합성·mount 취소, 숨김 snapshot 재전달과 동일 semantic revision의 새로운 force pulse, engine history-provider → MM → MotionMatch 실제 호출, missing-history update와 검색 구분, 같은 빈 결과의 재검색, 노드별 소비, interaction 보호, 정상 throttle 복원과 worker budget 합성을 확인한다. 빈 후보의 native fixture는 실제 authored pose/DB 품질이나 generated ABP 전체를 대신하지 않는다. crowd schedule 테스트의 512개 schedule은 GT 분산 계약이며 군중 CPU/FPS 실측이 아니다.

## 남은 시각·성능 확인

OTM/Strafe run/sprint Start의 자연 종료와 입력 취소, Pivot 자연 종료/redirect, TAB facing, 점프·착지·Stop/Idle, mount 왕복을 실제 캐릭터에서 확인한다. `p.ProjectJ.MMTransitionDebug 1`의 `MMReselectSearch`는 실제 regular 검색 소비 기록이며, 최종 pose 기여 증거는 아니다. Rewind Debugger에서 복귀 query, 선택 animation/time, 발 위상·pelvis 속도와 root 보정을 비교한다. 외부 `OnUpdate_StateMachine` 내부는 제공 사진에 없어 실제 콜백 부작용은 추가 확인 대상이다.

같은 레벨·카메라·장비로 1/50/100/200명 등 변경 전후를 측정하고 GT chooser, worker PoseSearch, completion, IK/retarget, draw/GPU를 나눠 본다. 로컬 fixture 통과를 수백 명 지원이나 시각 품질 측정으로 표현하지 않는다.

## 참고

- [Epic Motion Matching](https://dev.epicgames.com/documentation/en-us/unreal-engine/motion-matching-in-unreal-engine): schema query와 node 설정 기준. 2026-10-05 재접근.
- [Epic Animation Budget Allocator](https://dev.epicgames.com/documentation/en-us/unreal-engine/animation-budget-allocator-in-unreal-engine): mesh 업데이트 예산과 significance 기준. 2026-10-05 재접근.
- [현재 MM 계약](MotionMatchingNextSteps.md), [원격 one-shot 수요](../Locomotion/RemoteOneShotReplication.md), [기존 Strafe 검색 연속성](../../Review/Architecture/Project_J_Strafe_Facing_Redirect_2026-10-05.md).
