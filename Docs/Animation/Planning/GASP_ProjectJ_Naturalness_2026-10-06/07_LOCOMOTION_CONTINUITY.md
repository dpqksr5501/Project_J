# 이동 회전과 단발 동작의 연속성

2026-10-06 후속 구현. 이전 `06_IMPLEMENTATION.md`의 공중/감속 오류 수정 위에 적용한다. GASP 조사 `02_GASP.md`의 후보 구성·continuing pose·플레이어별 Steering·시각 루트 계약을 참고했다. 단일 PSD가 모든 어색함의 확정 원인이라는 전제는 사용하지 않는다. 코드·운영 그래프를 구현하고 아래 조건에서 검증했다. 최종 화면 품질은 PIE 비교가 필요하다.

## 구현한 책임

| 책임 | 동작 |
| --- | --- |
| CMC | 기존 물리 이동·가감속·캡슐 yaw·네트워크 예측 유지 |
| native locomotion | 기존 Run 반전 자격·수명·취소를 결정. 정렬 종료에만 continuing 권한을 제공 |
| AssetSet | 자격 있는 로컬 Turn에서 같은 gait/rotation family의 dynamic Cycle을 두 번째 후보로 제공. 최대 2개 |
| Proxy / PoseSearch | 생성된 MM 노드와 native fallback에 같은 후보 전달. 실제 검색 후 요청 소비와 정상 throttle 복원 유지 |
| State Controller | Start/Stop/Pivot/Land/TIP/air의 사건 ID·선택·clock·취소·override 유지 |
| per-player graph | 각 MM/external blend player's asset/time/mirror를 사용한 ResetRoot/Steering |
| OffsetRoot | 로컬 일반 이동·움직이는 Start/Land의 시각 rotation 소비·잔여 보정. Translation Release 유지 |
| follower / feet | 기존 retarget·FootPlacement·LegIK·예산 유지 |

## 검색 후보와 유지

`FindTurnCycleCompanion`은 기존 Primary를 유지하고 제한된 companion 하나만 추가한다. 평소 Cycle·작은 곡선·옆/뒤 걷기는 기존 데이터에서 검색한다. settled Loop는 Turn의 companion이 아니다. Sprint fallback, legacy chooser 결과, 다른 combat/gait 데이터에 후보를 섞지 않는다. remote/NPC·공중·탑승·외부 단발에는 companion을 추가하지 않는다.

스키마가 다르거나 정규화 기준이 비교 가능하지 않으면 단일 PSD로 fallback한다. Normalize 계열은 같은 NormalizationSet이 실제로 양쪽 DB를 포함해야 한다. 운영 OTM Run과 Combat Run에는 각각 새로운 공통 정규화 에셋을 연결했다. 데이터 entry/클립/notify/검색 간격/continuing bias 자체를 바꾸지 않는다. **정규화 변경은 각 DB의 특징 비용에도 영향을 주므로 원격의 Cycle 선택에도 영향을 줄 수 있다.** 후보 확대의 로컬 제한과 데이터 비용 변경의 범위는 구분한다.

정렬을 완료한 같은 Turn에서는 Cycle 복귀 때문에 강제 검색/interrupt를 새로 만들지 않는다. Turn의 현재 continuing pose는 최신 query와 정상 검색 예산에서 경쟁하며 진행한다. 새 target·입력 해제·모드 변경·air/action/montage·timeout은 완료로 취급하지 않는다. 완료 권한이 취소되면 primary가 같은 Cycle이어도 이전 Turn query를 invalidate하는 요청을 만든다. 요청은 해당 노드가 실제 검색할 때까지 유지한다.

Combat의 loop-only 최적화는 실제 선택 결과가 아직 그 Run Turn이면 dynamic Cycle을 유지한다. 실제 Cycle이 선택되면 기존 settled routing을 다시 사용할 수 있다. continuing permission 자체가 새 Turn 후보를 계속 열지는 않는다.

`PreUpdate` 이후 `NativeUpdateAnimation`이 새 값을 발행할 수 있으므로, proxy는 가장 바깥 그래프 실행 전에 최신 publication revision을 소비한다. 후보 목록·phase·Steering 소유권·trajectory가 같은 발행 프레임을 사용한다. 중첩 linked layer는 같은 snapshot을 유지하고, 이미 소비한 revision은 재복사하지 않는다. 후보 개수의 thread-safe 디버그 값도 이 경계에서 함께 캡처한다.

## 보정 경로

- MM player: `Input → LocalToComponent → ResetRoot → General Steering → ComponentToLocal → Result`.
- external player: 기존 `OW → TIP Steering` 사이에 `ResetRoot → General Steering`을 삽입. 기존 OW와 TIP 연결은 보존한다.
- 양쪽 general Steering과 ResetRoot의 Alpha는 동일한 native snapshot gate를 사용한다. gate=0이면 기존 pose가 통과한다.
- actual asset/time/mirror/table은 해당 blend player's input tag에서 읽는다. 마지막 선택 클립의 시간 하나로 이전 플레이어를 보정하지 않는다.
- 목표는 최신 trajectory의 미래 world facing quaternion을 shortest-path Slerp한 값이다. camera/actor yaw 숫자를 worker에서 다시 읽지 않는다. 기본 lookahead 0.5초, procedural 0.4초, animated 2초는 GASP 조사와 엔진 5.8 구현을 참고했다.
- ground local primary player, Cycle/Turn 또는 moving Start/Land만 사용한다. stale/incomplete trajectory, 입력 해제, Pivot/Stop/TIP/air/mount, action/montage/root-motion owner, remote는 gate=0이다.
- Steering은 root-motion attribute를 바꾼다. 따라서 gate가 켜진 경로에서 OffsetRoot rotation을 Interpolate로 소비하고 잔여 offset을 감쇠한다. 무제한 Accumulate를 도입하지 않았다. 기존 TIP 정책은 따로 유지한다.
- visual yaw error는 기본 45도, profile에서 0–90도 범위로 조정한다. translation은 Release이며 CMC yaw/RPC/복제 필드를 추가하지 않았다.

운영 Master만 노드를 수정/컴파일/저장했다. Run PSD 4개에는 공통 정규화 참조만 추가했고 정규화 에셋 2개를 생성했다. 레벨·GASP·Chooser·애니메이션 시퀀스·PSS·Combat profile은 이번 후속 작업에서 저장하지 않았다. editor authoring 테스트는 `-ProjectJApplyLocomotionContinuity`가 있을 때만 저장한다. 일반 검증은 읽기 전용이다.

## 검증

증거 폴더: `Saved/Validation/LocomotionContinuity_20261006`.

직접 UBT 빌드와 운영 Master 컴파일을 수행했다. 신규 테스트는 후보 호환성·검색 요청 수명·정렬 완료/취소·시각 보정 소유권·운영 그래프·실제 정규화 연결을 확인한다. 기존 authored playback은 실제 AnimBP/선택/stack/bone 평가를 사용한다. 추가 반전 fixture는 local Combat forward input을 유지하면서 controller/actor/path를 제어한다.

| 검증 | 최종 결과와 증거 |
| --- | --- |
| 직접 UBT | `Build8.log`: Succeeded, 77.87초. 실행 중 빌드 중단/중복 실행 없음 |
| 회귀와 신규 계약 | `Final2/index.json`: 기존 28개 + 신규 4개, **32/32 통과** |
| 실제 local 재생 | `Final2Playback.txt`: 기존 6개 + Combat 반전 1개. 작은 곡선 Steering 179프레임, source root/component 최대 yaw 차이 11.299°. Strafe/반전 최대 45.000° |
| 실제 Turn 유지 | 반전의 paired 후보 30프레임, 완료 권한 50프레임. Cycle phase 복귀 후 같은 Turn 클립의 시간은 frame 105/120/135에서 1.6942/1.9342/2.1742초로 진행. 입력 해제 후 추가 후보 닫힘 |
| non-local 재생 | `NonLocal/index.json`: 1/1 통과, 6개 시나리오 모두 Steering 활성 프레임 0 |
| 실제 소켓 네트워크 | `Saved/Validation/GroupD_20260910/LocomotionContinuityNetwork_20261006/verification.json`: 서버 + 클라이언트 2개 + NPC 100, 지연 80ms/손실 2%, passed=true, 세 프로세스 exit 0, timeout 0 |

네트워크 fixture는 운영 Start/Stop/Land/숨김 취소와 원격 소유권 회귀를 포함한다. 네트워크 바이트에는 fixture 제어 트래픽도 포함한다. 엔진 Toolset Python 초기화 오류는 이전 검증과 같이 별도로 기록했고 테스트 결과를 로그 전체 무오류로 표현하지 않는다.

CPU 증거: `Saved/Validation/AuthoredAnimation_20261005/LocomotionContinuityCPU_20261006`의 Count1/Count200. 각 8행, 각 automation 1/1 통과, completed=true. 조건당 warmup 120 tick / 측정 120 tick, 반복 1회. world tick wall time이며 렌더·전체 프레임 시간이 아니다.

| 200명 조건 | ABA 끔 평균 / p95 (ms) | ABA 켬 평균 / p95 (ms) |
| --- | --- | --- |
| Near | 22.060 / 23.488 | 17.540 / 19.488 |
| Far | 22.650 / 24.210 | 17.720 / 19.331 |
| Hidden | 14.307 / 16.220 | 7.317 / 8.005 |
| AttackBurst | 24.616 / 27.203 | 24.670 / 31.802 |

1명 평균은 조건별 0.437–0.751ms였다. 200명의 공격 보호 구간에는 기존 ABA 제외 정책이 유지된다. 이 fixture는 초기 장비 생성 뒤 unpossess한 비로컬 authority 캐릭터이며, 새 local Steering/후보 확대의 추가 비용을 직접 측정하지 않는다. 같은 환경의 변경 전/후 반복 비교가 아니므로 이 표로 성능 개선·무회귀율을 주장하지 않는다. 로컬 추가 비용과 렌더 품질은 별도 실제 플레이 측정이 남는다.

이 fixture들은 NullRHI이며 CMC tick을 끄고 속도와 actor transform을 제어한다. 렌더 화면, 실제 마우스 입력 지연, 접촉 발의 미끄러짐, 경사·계단 접지의 품질은 입증하지 않는다. source yaw 차이는 최종 follower 접지 지표와 다르다. 실제 검색 횟수는 기존 request acknowledgement와 관측된 search timer를 구분한다.

중간 실패도 보존했다. 첫 테스트의 abstract player 생성과 후속 missing PlayerState는 fixture 조건 오류였다. UE 5.8의 `APawn::IsPlayerControlled`는 PlayerState와 bot 판정을 사용한다. 운영 player fixture는 PlayerState를 가진다. 실패를 무시하거나 생산 코드의 player gate를 낮추지 않았다.

`Final`의 한 실패는 새 후보 개수와 이전 phase snapshot을 비교하면서 발견했다. 디버그 assertion을 제거하지 않고 위 그래프 경계에서 최신 값을 소비하도록 수정했고, publication/consumption 계약 테스트와 실제 재생 assertion이 `Final2`에서 통과했다. 시각 yaw 측정은 같은 조건의 보정 한도 확인이며, 기존 상태보다 자연스러움이 몇 % 좋아졌다는 지표가 아니다.

## 에디터 비교와 되돌리기

### PIE 콘솔 진단

후속 진단 CVar `p.ProjectJ.LocomotionContinuityTrace`는 기본 0이다. `1`은 local primary player의 평가 완료 후 상태 전환 + 5Hz, `2`는 상태 전환 + 10Hz와 출력 그래프 분기 정보를 기록한다. CVar를 켜는 것으로 애니메이션 선택·보정·검색 예산을 바꾸지 않는다. Shipping 빌드에는 로그 실행 경로/CVar가 없다.

PIE 시작 후 콘솔에서 다음 명령을 한 줄씩 실행한다.

```text
p.ProjectJ.MouseTurnTrace 0
p.ProjectJ.MovingTurnTrace 0
p.ProjectJ.AnimFlow 0
p.ProjectJ.LocomotionContinuityTrace 1
```

일반/Combat 각각 정지→이동, 완만한 곡선 회전, 정지, 움직이며 착지, 큰 반전→입력 해제를 30–60초 정도 실행한다. 큰 반전은 이동 경로도 돌아가는 forward 반전이어야 한다. Strafe에서 몸만 돌거나 S/backpedal하는 입력은 추가 Turn 후보의 자격과 다르다. 끝나면 `p.ProjectJ.LocomotionContinuityTrace 0`으로 끄고 `Saved/Logs/Project_J.log`를 보존한다. 필요할 때 `2`로 짧게 재현해 분기 정보를 추가한다.

- `Stage=Evaluated`: 소비한 SnapshotFrame/ResultFrame, phase/rotation/input/speed, Steering gate의 정확한 승인·거절 사유, 미래 목표 yaw, 현재 actor/control/component/root yaw, source root offset과 한도 근접 여부, trajectory 나이, 사용 중 보정 설정.
- `Stage=Selection`: 제한된 후보 개수와 primary/companion PSD, moving Turn/완료 권한, 대표 MM 결과의 PSD/클립/시간/continuing 상태, external override/사건 revision, 반환 검색 상태와 접촉 곡선.
- `Stage=ExternalPlayer`: 외부 단발이 override 중일 때 실제 blend player의 클립/시간/weight. MM 결과가 남아 있어도 그것만으로 최종 출력 소유권을 단정하지 않는다.
- `Stage=Branch`: 모드 2에서 debug traversal에 나타나는 Steering/OffsetRoot/MM 노드와 weight. 최대 8행/샘플. 내부 blend player가 debug 목록에 나타나지 않을 수 있으므로 노드 행의 부재만으로 비활성이라고 단정하지 않는다.

`Steering=1 Gate=Enabled`는 native 승인이다. 분기 weight와 실제 source root 변화는 별도로 함께 본다. `NoInput`, `NotGroundedOnFoot`, `PhaseOwner`, `Action`, `Montage`, `RootMotion` 등은 해당 소유권에서 정상적으로 꺼진 이유일 수 있다. `PredictionUnusable`/`StaleTrajectory`/`IncompletePrediction`이 지속되는지는 별도 확인한다. `TargetValid=0`이면 목표 yaw와 보정 설정값을 활성 보정 값으로 해석하지 않는다. `RootValid=0`이면 root 수치는 유효한 측정이 아니다. `RootBuffer=EvaluatedPrePhysics`는 이번 평가 버퍼의 source root이며 physics/retarget 후 follower의 최종 자세는 아니다. 엔진의 read-buffer flip 전 callback이므로 socket에서 이전 pose를 읽지 않고 완료된 평가 버퍼를 const로 읽는다. 병렬 bone 평가를 강제로 기다리거나 follower를 추가 평가하지 않는다. `NearLimit=1`은 한도에 가깝다는 관측이며 오류 판정이 아니다. 로그만으로 렌더 품질·발 미끄러짐을 확정하지 않는다.

진단 검증 증거는 `Saved/Validation/LocomotionContinuityTrace_20261006`에 별도로 보존했다. 직접 UBT `BuildFinal.log` 성공, 상세 로그를 켠 `TraceOn/index.json`에서 기존 32개 테스트 모두 통과, 기본 꺼짐 `DefaultOff/index.json`에서 7개 재생 시나리오 테스트 통과 및 새 진단 행 0개. `trace_verification.json`은 evaluated/selection/외부 player/분기 로그, 승인 및 입력 해제·공중 거절, 자격 있는 paired 후보와 완료 권한을 확인했다. 활성 source root 193개 샘플의 최대 offset 45.0°로 설정 한도 내였다. 샘플링·시계 리셋과 정확한 gate 사유도 계약 테스트에 포함했다.

빌드가 끝난 뒤 에디터를 새로 열고 production GreatSword pawn으로 PIE를 시작한다. 진단 로그는 비교 시 꺼 둔다.

```
p.ProjectJ.MouseTurnTrace 0
p.ProjectJ.MovingTurnTrace 0
p.ProjectJ.LocomotionSteering 1
p.ProjectJ.TurnCycleCandidates 1
```

일반/Combat 각각 W 이동 중 완만한 30–90도 곡선, Start 중 작은 yaw 변화, moving Land 중 작은 yaw 변화와 회복 후 회전, forward path가 함께 바뀌는 큰 반전과 input release를 비교한다. Start/Land의 기존 큰 입력 취소 한계는 유지되므로 큰 방향 변화의 단발 취소는 예상 동작이다. Combat의 S/backpedal 몸 회전과 forward 180도 자격을 혼동하지 않는다.

`p.ProjectJ.LocomotionSteering 0`은 새 보정 gate를 끈다. `p.ProjectJ.TurnCycleCandidates 0`은 단일 PSD 검색으로 되돌린다. **이 스위치는 새 공통 정규화까지 되돌리는 전체 이전 버전 비교가 아니다.** 같은 입력과 새 PIE로 두 경로를 비교한다. profile의 Steering 값과 AssetSet의 `bEnableTurnCycleCandidates`도 개별 조정할 수 있다.

전체 rollback용 기존 파일은 `Saved/Validation/LocomotionContinuity_20261006/Before`에 보존한다. 앞 작업과 사용자의 변경이 이미 들어 있는 시작 상태다. 전 프로젝트를 HEAD로 reset하지 않는다. 필요한 source/Master/PSD만 해당 before 파일로 복원하고 신규 normalization 참조를 제거한 뒤 새 두 에셋을 Content 밖에 보관한다. 에디터/UBT/관련 프로세스가 모두 종료한 상태에서 복원·직접 UBT 재빌드를 수행한다. 최종 파일 지문과 이번 텍스트 변경은 session manifest/patch로 기록한다.
