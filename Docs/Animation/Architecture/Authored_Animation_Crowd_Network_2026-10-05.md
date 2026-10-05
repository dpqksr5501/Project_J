# 실제 캐릭터 애니메이션 CPU·소켓 검증 — 2026-10-05

[MM 복귀·군중 검색 예산](MotionMatching_Return_Crowd_2026-10-05.md) 이후, 에디터를 직접 조작하지 않고 진행하는 CPU 실측·실제 원격 경로 검증·측정에 근거한 코드 비용 절감의 후속 기록이다. 데이터 튜닝과 화면에서의 연결 품질 판정은 별도 범위다.

## CPU 측정 구조

`ProjectJ.Animation.AuthoredCPU`는 실제 `BP_GreatSword`와 `DA_Greatsword_Equip`을 생성한다. Blueprint construction과 원래 ABP, 리타깃, 시각 follower를 유지한다. 각 캐릭터는 authority이며 AI Controller를 해제한 뒤 PlayerState를 보존해 비로컬 정책을 검사한다. 네트워크 Role을 조작하지 않는다.

1·50·100·200명마다 Near/Far/Hidden/AttackBurst와 ABA off/on을 두 번 측정한다. 경우별 120회 warmup 뒤 120회 `World::Tick(1/60)`의 wall time을 기록한다. 매 latent update에 한 번만 World를 tick하므로 engine frame counter에 의존하는 예산 로직도 프레임 경계를 받는다. 공격은 실제 ASC의 전투 태그와 LMB 입력으로 실행하며 montage 시작과 즉시 ABA 이탈을 검사한다.

이동은 제어된 velocity·transform이며 render timestamp와 significance를 명시적으로 공급한다. NullRHI에서 각 경우가 실제 Near/Far/Hidden 정책을 거쳤는지 별도로 검사한다. 일반 경우는 URO off를 기준으로 ABA만 비교한다. 프로젝트 자체의 MM 거리 정책까지 끄는 비교는 아니다. ABA 목표는 2ms이며 총 World 비용의 상한이 아니다.

`bone_finalizations`에는 보간이 포함된다. `mm_result_observations`는 유효한 캐시 결과의 관측 횟수이고 실제 PoseSearch 실행 횟수가 아니다. CPU trace는 마지막 인원 수에서 수집하고 warmup 밖의 `ProjectJ.AuthoredCPU_*` region별 timer를 추출한다. 렌더링·GPU·draw·외부 엔진 프레임 비용은 포함하지 않는다. 측정만으로 발 미끄러짐이나 자세의 자연스러움을 판정하지 않는다.

## 실제 소켓 경로

기존 `ProjectJNetworkFixture`에 `-ProjectJAnimationFixture` 선택지를 추가한다. `UnrealEditor-Cmd -game`의 별도 dedicated-server 모드와 실제 클라이언트 프로세스가 localhost socket으로 접속한다. 기존 Iris AOI, owner-only inventory, equipment FastArray, 종료 검증을 유지하면서 실제 authored player로 교체하고 원격 Role이 `SimulatedProxy`인지 검사한다. 패키징된 Server target 검증은 아니다.

Start → Stop → 착지 → 숨김 중 착지 취소 → 한 net update 안의 Start/Land/Cancel/Stop 경계를 보낸다. 매 단계마다 최종 move sequence, landing revision, fall-off counter, landing active 값을 검사한다. 원격 ABP의 실제 선택 결과와 유한한 animation time을 기록하고, 하나의 착지 identity에서 presentation epoch가 추가로 생성되지 않는지 검사한다. owner는 production `SkipOwner` 계약에 따라 원격 assertions에서 제외한다.

이 fixture는 이동 물리를 검증하는 시험이 아니다. Entry 맵의 초기 자유 낙하가 통제된 시나리오에 섞이지 않도록 임시 collision floor와 pre-actor-tick 걷기 모드·이동 tick 통제를 사용한다. 이 처리는 명시적인 fixture CLI에서만 동작하고 delegate는 GameInstance 종료 시 해제한다. RPC의 초기 actor reference가 아직 해석되지 않으면 replicated PlayerState의 안정된 fixture 식별자로 다음 관측에서 다시 찾는다. 이벤트 값이나 production 상태를 덮어써서 통과시키지 않는다.

새 복제 필드와 RPC는 테스트용 controller 안에만 있다. 실제 player의 replication payload, snapshot 소유권, Locomotion·StateController·Proxy의 요청 수명은 유지한다. 생성 actor/component는 transient이고 실제 에셋이나 레벨을 저장하지 않는다.

최종 snapshot의 수렴과 실제 graph 실행을 검증하는 범위이며 모든 중간 Start/Stop 클립의 화면 재생을 보증하지 않는다. OTM/Strafe, Pivot, mount와 jump의 전체 실제 소켓 조합 검증도 이 다섯 단계의 범위를 넘어선다. 해당 수명 계약은 기존 회귀와 별도 PIE 확인을 함께 참고한다.

## 동시 공격에서 확인한 비용과 수정

최초 200명 실행은 모든 CPU row를 생성했으나 GAS의 기본 `AbilitySystem.AbilityTask.MaxCount=1000` 경고·ensure 때문에 실패했다. 엔진의 자동 task dump는 1,000개 시점에 `GA_Greatsword_C` 999개, WaitGameplayEvent 889개, PlayMontageAndWait 111개를 기록했고 최종 동시 task 수는 1,800개까지 증가했다. 앞선 1·50·100명은 이 한도 아래에서 통과했다. 실패한 200명 결과는 정상 성능 baseline으로 취급하지 않는다.

Melee ability는 콤보에서 참조하는 입력마다 별도 WaitGameplayEvent를 만들었다. 이 입력 대기만 ability 소유의 **단일 ASC tag-container 구독**으로 바꾼다. hit와 combo-window의 기존 task, montage task, root motion, notify, damage·SSR 처리는 유지한다. 대검 한 공격의 AbilityTask는 9개에서 3개가 된다. 엔진 한도나 프로젝트 설정을 올리지 않는다.

기존 WaitGameplayEvent의 기본 `OnlyMatchExact=true`를 확인하고 container callback에서도 `HasTagExact`로 동일한 입력 집합을 제한한다. payload의 EventTag는 실제 dispatch tag로 채운다. 콤보 입력 라우팅도 활성화 시 구독한 집합을 사용해 매 입력마다 데이터의 전체 노드를 다시 순회하지 않는다. 취소·완료 시에는 처음 구독한 ASC에서 delegate를 제거하고 입력 집합과 owner를 비운다.

ASC는 gameplay-event delegate를 복사한 뒤 호출한다. 따라서 먼저 호출된 외부 callback이 공격을 끝내고 다시 시작하면 복사된 오래된 구독이 남을 수 있다. 구독 revision과 active/ending guard를 함께 검사해 이전 activation의 입력이 새 공격을 큐에 넣지 못하게 한다. `ProjectJ.Combat.ComboInputSubscription`은 실제 authored ability로 exact 입력·payload tag·버퍼링·관계없는 이벤트·취소·재활성화·늦은 입력을 검사한다.

이는 발견한 동시 task 수와 객체 생성 비용을 줄이는 수정이다. task 감소율을 World CPU 감소율이나 FPS 개선율로 환산하지 않는다. CPU의 IK·리타깃과 렌더 비용 절감은 별도 데이터·LOD 검증을 거쳐야 한다.

## 재실행

엔진·빌드·Live Coding 프로세스가 실행 중이면 새 측정을 시작하지 않는다. 모든 프로세스는 정상 종료를 기다린다. 각 실행 폴더는 재사용하지 않고 source snapshot·SHA256·HEAD·CPU·명령행·결과를 보존한다.

```powershell
./Scripts/Validation/Measure-AuthoredAnimation.ps1 -RunName MyCPU -Repeats 2
./Scripts/Validation/Measure-AuthoredAnimation.ps1 -RunName MyCPUTrace -Counts 200 -Repeats 1 -Trace
./Scripts/Validation/Export-DTrace.ps1 -RunRoot Saved/Validation/AuthoredAnimation_20261005/MyCPUTrace/Count200 -Animation
./Scripts/Validation/Measure-Network.ps1 -RunName MyAnimationNet -Animation
./Scripts/Validation/Measure-Network.ps1 -RunName MyAnimationNetLag -Animation -PacketLag 50 -PacketLoss 3
```

## 결과

Ryzen 7 9800X3D에서 최종 `Final_Full`의 64개 경우가 통과했다. 아래 값은 두 반복의 평균 World tick ms이며 각 셀은 **ABA off → on**이다. 비교 측정에는 trace를 켜지 않았고, 프로파일링은 별도 `Profile_Final_200` 실행의 8개 경우에서 수집했다. 각 경우의 p50/p95/max, 정책 tier, bone finalization과 보호 상태의 원본 값은 [검증 JSON](Authored_Animation_Crowd_Network_Validation_2026-10-05.json)에 보존한다.

| 인원 | Near ms | Far ms | Hidden ms | AttackBurst ms | 동시 AbilityTask |
| --- | --- | --- | --- | --- | --- |
| 1 | 0.66 → 0.68 | 0.67 → 0.66 | 0.39 → 0.39 | 0.66 → 0.63 | 3 |
| 50 | 3.94 → 4.01 | 3.76 → 4.18 | 3.37 → 1.72 | 4.38 → 4.48 | 150 |
| 100 | 8.39 → 7.48 | 8.34 → 8.04 | 5.83 → 3.58 | 9.15 → 9.06 | 300 |
| 200 | 21.67 → 16.74 | 22.45 → 16.82 | 16.28 → 8.12 | 25.00 → 24.60 | 600 |

적은 인원에서는 ABA의 관리 비용과 실행 변동 때문에 이득이 없거나 더 느린 경우도 있다. 고정된 순서의 두 반복·120개 표본은 폭넓은 기기 성능 보증이 아니다. 이전 실행과의 차이에는 배경 프로그램·CPU 상태·스케줄링 변동도 섞인다. 변경 전 200명 실행은 task 한도로 실패했으므로 전후 World CPU 개선율을 주장하지 않는다. 확인된 변화는 동일한 authored 공격의 동시 task가 **1,800 → 600**으로 줄었다는 점이다.

AttackBurst는 공격 시작 후 2초를 측정하므로 공격 완료 뒤 정상 예산으로 돌아오는 구간도 포함한다. 200명 각 공격 case에서 79프레임 × 200명 = 15,800회 combat-protected mesh-tick을 관측했고, 네 번의 동시 공격 모두 peak task가 600이었다. 일반 200명 case의 constructed follower는 1,800개다. CPU fixture는 실제 레벨의 지형·충돌 복잡도나 200명의 네트워크 연결 비용을 대신하지 않는다.

200명 Near/ABA on trace의 측정 구간은 120 World tick이며 주요 timer는 아래와 같다. 초 단위 값은 **여러 스레드에서 누적된 CPU 시간**으로 World wall time과 같지 않다. inclusive 값은 자식 scope를 포함하므로 행들을 더하지 않는다.

| timer | 호출 수 | inclusive 초 | exclusive 초 |
| --- | --- | --- | --- |
| IK Retarget Processor Run | 24,000 | 2.523345 | 2.523345 |
| IK Retarget | 24,000 | 3.118035 | 0.594691 |
| FAnimNode_RigidBody::EvaluateSkeletalControl_AnyThread | 72,000 | 4.464109 | 0.668583 |
| ProjectJ_AnimProxy_Update_Worker | 9,824 | 0.721175 | 0.160817 |
| PoseSearch PCA/KNN | 2,444 | 0.148892 | 0.103913 |

따라서 현재 군중 비용을 검색 간격 하나로 설명할 수 없다. source pose가 budget으로 줄어도 리타깃·물리 graph의 실행은 크게 남는다. 이 결과를 근거로 follower의 물리나 손·발 보정을 임의로 끄지 않았다. 거리별 LOD·ABP 노드 수요·리타깃과 의상 정책은 데이터 작업과 화면 검증이 필요한 후속 범위다.

최종 실제 소켓 시험은 기본 조건과 **PktLag=50/PktLoss=3** 모두 다섯 단계·정상 disconnect를 통과했다. 엔진 로그의 설정 적용도 확인했다. 각 클라이언트에서 실제 TransitionToLand의 외부 선택 포즈까지 검사하며, 최종 coalesced 착지는 재생을 요구하지 않고 취소 상태 수렴과 중복 epoch 방지를 검사한다.

전체 회귀는 **131개 통과: 정상 122개·경고 포함 9개**, 실패·미실행·실행 중 0이다. 기존 121개를 모두 포함했고 이전 경고 8개의 test/message 차이는 0이다. 추가 전투 continuity 5개에 각각 4회씩 `MissingPalm` compatibility socket 경고가 있다. 이 fixture는 native 캐릭터를 사용하며 실제 BP construction의 Palm 소켓을 갖지 않는 경로다. 새 입력 구독 회귀는 경고 없이 통과했다.

직접 UnrealBuildTool.exe로 Editor/Game Win64 Development 빌드를 성공했다. 최종 Editor 로그 `BuildEditor_Final.log`는 29.64초, 회귀 plugin 포함 확인 `BuildEditor_RegressionPlugins.log`는 up-to-date 2.20초, Game `BuildGame_Final.log`는 85.56초이며 모두 exit 0이다. 빌드와 엔진 실행은 겹치지 않았고 실행 중인 build를 중단하지 않았다. 이전 파일 잠금은 Google Drive 동기화 중지 뒤 해소됐다.

핵심 보존 경로는 `Saved/Validation/AuthoredAnimation_20261005/Final_Full/`, `Profile_Final_200/Count200/TraceExport/`, `Regression/`이며 실제 소켓은 기존 harness 루트의 `Saved/Validation/GroupD_20260910/Animation_Final_20261005/`와 `Animation_FinalLag_20261005/`다. 생성 manifest/source snapshot과 report·로그 hash는 검증 JSON에서 연결한다.

authored asset 로딩에는 기존 Foley notify·ExperimentalStateMachineData의 누락 dependency 로그와 에디터 초기 검증 메시지가 있다. fixture 통과를 전체 로그가 깨끗하다는 뜻으로 해석하지 않는다. 이 범위에서 에셋 dependency를 수정·저장하지 않았다. 화면의 발 위상·pelvis 속도·root 보정·OTM/Strafe의 자연스러운 연결은 별도 PIE/Rewind 확인 대상이다.
