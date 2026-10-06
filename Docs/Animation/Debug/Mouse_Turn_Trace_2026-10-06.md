# 마우스 회전 끊김 콘솔 진단

2026-10-06. 마우스 회전 중 화면/몸이 끊기는 현상을 사용자 실행 로그로 구분하기 위한 read-only 진단이다. 입력·CMC·회전 보간·애니메이션·카메라 설정을 변경하지 않는다. 기본 off, non-shipping local player만 대상이며 기존 Player Tick과 Look callback에 연결했다. 새 Tick/RPC/복제 필드/에셋 변경은 없다.

## 기록 방법

PIE 실행 후 콘솔에 각각 입력:

```text
p.ProjectJ.MovingTurnTrace 0
p.ProjectJ.TIPTrace 0
p.ProjectJ.AnimFlow 0
p.ProjectJ.MouseTurnTrace 1
```

기존 `Config/DefaultEngine.ini`의 사용자 설정 `MovingTurnTrace=2`는 그대로 보존했다. 위 명령은 이번 실행에서만 매 프레임 기존 로그를 끄므로, 대량 로그 자체의 끊김 영향을 분리할 수 있다. 다른 진단 설정도 Begin 행에 기록한다.

정지/이동 중 마우스를 좌우로 움직이고, 가능하면 일반/전투 각각 재현한다. 재현 뒤:

```text
p.ProjectJ.MouseTurnTrace 0
```

`Saved/Logs`의 해당 실행 최신 `.log`를 전달한다. 로그 접두사는 `MouseTurnTrace`다. Output Log 창의 필터만 켜거나 끄는 것은 파일 로그 발생 비용을 끄는 것과 다르다. 위 명령은 실제 CVar를 끈다.

1은 약 10Hz로 네 행(Frame/State/CameraRig/PostCamera)을 출력한다. 그 사이 매 프레임의 최대 tick gap·delta·회전 step도 기록한다. 자세한 프레임 순서가 필요하면 짧은 재현 구간에만 `p.ProjectJ.MouseTurnTrace 2`를 사용한다. 2는 매 player tick 출력하므로 로그 쓰기가 현상에 영향을 줄 수 있다. 시작 CVar를 낮은 출력량의 1로 둔 이유다.

## 값의 의미

| 그룹 | 기록값 / 해석 |
| --- | --- |
| 시간 | Frame, world T/WorldDtMs, EngineDtMs, 실제 wall-clock 기준 연속 Player Tick 간격 TickGapMs. CPU 함수 실행 시간이 아니다. 요약 구간 PeakTickGapMs/PeakGapFrame/PeakWorldDtMs는 한 프레임의 spike를 놓치지 않도록 기록 |
| 입력 | LookFrame/LookCalls, 합산 EnhancedLook, callback 뒤 PC RotationInput의 QueuedYaw/Pitch, PCMouse delta, LookIgnored. EnhancedLook는 modifier 처리 후 axis이며 raw hardware counts가 아님. 같은 프레임 callback이 없으면 input 값=0. LookFrame으로 시점 구분. 최초 callback 전 LookFrame=18446744073709551615(MAX_uint64)는 입력 관측 없음의 sentinel |
| 회전 | control/capsule actor yaw와 frame step. ±180 경계는 shortest signed delta로 처리. 요약 구간 peak는 절댓값. 회전값이나 입력을 바꾸지 않음 |
| 카메라 | PlayerCameraManager 존재, 마지막 사용 가능한 view cache의 CameraT/CameraAgeMs/CameraUpdated, yaw/pitch/position·ViewTarget. Player Tick 시점의 캐시이므로 현재 프레임 최종 렌더 POV라고 단정하지 않음. 한 프레임 정도 이전 timestamp만으로 camera stall 판정하지 않음 |
| 카메라 갱신 후 | PostCamera는 설치된 UE 5.8의 UpdateCameraManager 이후 OnWorldPostActorTick에서 CameraT/age/ControlCameraError/CameraStep을 관측. 같은 Frame의 이른 Frame 행과 구분. boom tick/interval/pawn-control/inherit-yaw와 actor interval도 기록. 이 단계도 GPU Present/VR late-update까지 포함하는 최종 렌더 측정은 아님 |
| 상태·재생 | Combat/속도/air, CMC rotation flags/rate, phase/Turn180/TIP target/facing, presentation/revision/override, 선택된 외부 clip/start/hold time, cached MM PSD/clip/time. 선택·cached 결과가 최종 출력 weight를 뜻하지 않음 |
| 실제 메시 | source/follower mesh yaw·root bone world yaw. follower의 실제 reference bone 0 이름을 함께 기록. pending 병렬 평가면 Pending, 없는 bone이면 MissingBone. 강제 평가/대기하지 않음. 렌더 포즈의 동일 시점 보증은 아님 |
| 카메라 rig | boom target yaw/arm length, collision test/fix 여부, position/rotation lag와 속도·substep/max step, FollowCamera transform. 기존 lag/collision 설정을 수정하지 않음 |

큰 TickGap/EngineDt spike가 회전과 함께 생기는지, wall time은 정상인데 input/control/camera/actor 단계가 멈추거나 점프하는지, clip/revision/phase 경계와 맞물리는지를 비교한다. 10Hz 요약에는 현재 행의 LookCalls만 들어가므로 짧은 input의 모든 callback 순서가 필요하면 2를 사용한다. TickGroup/카메라 갱신 순서를 바꿔 시점을 맞추지 않고 timestamp로 구분한다.

## 변경·검증

- `Source/Project_JCharacter/Private/Project_JPlayerCharacter.MouseTurnTrace.cpp`: opt-in CVar, GT 수집과 제한된 로그.
- `Public/Project_JPlayerCharacter.h`: non-shipping 진단용 값 상태와 함수. 동적 buffer/복제 없음.
- `Private/Project_JPlayerCharacter.cpp`: 기존 Tick 끝의 read-only hook, EndPlay 진단 delegate 해제.
- `Private/Components/Project_JPlayerInputBindingComponent.cpp`: AddControllerYaw/PitchInput 뒤의 read-only hook.

원본 세 파일은 `Saved/Validation/MouseTurnTrace_20261006/Before/Source/...`에 복사했다. 이전 자연스러움 작업 파일/에셋/Config는 이번에 되돌리거나 수정하지 않았다.

직접 UBT의 Project_JEditor Win64 Development 빌드는 성공했다(29.61초). `MouseTurnTrace=1` 상태에서 기존 `ProjectJ.Internal.Camera.Possession`과 `ProjectJ.Animation.Naturalness.AuthoredPlayback` **2/2**가 통과했고 Begin/Frame/State/CameraRig 행이 실제 로그에 출력되는 것을 확인했다. NullRHI authored fixture는 마우스 hardware callback이나 렌더링된 camera frame을 생성하지 않으므로 이 실행의 LookCalls=0/CameraT=0을 실제 PIE 끊김의 증거로 쓰지 않는다.

검증 결과는 `Saved/Validation/MouseTurnTrace_20261006/Build.log`, `Automation/index.json`, `Automation.log`에 보존한다. 실제 사용자 마우스 입력과 렌더링된 끊김 재현은 사용자 실행 로그를 기다린다.

## 사용자 15:44 로그 분석과 추가 관측

사용자가 전달한 `Saved/Logs/Project_J.log`는 종료된 2026-10-06 15:44:28 실행이다. 원본은 `Saved/Validation/MouseTurnTrace_20261006/UserRun_1544/Project_J.log`에 복사했고 parsed/summary JSON을 함께 보존했다. 원본 파일을 수정하지 않았다.

- MouseTurnTrace=1인 두 구간, 총 114 frame samples(10+104). 두 번째 구간은 0.1039–10.8960초, 실제 autonomous client(role=2)다. MovingTurnTrace/TIPTrace/AnimFlow/MMNetDebug 모두 0으로 기록됐다.
- WorldDt/EngineDt 중앙값 8.333ms, wall TickGap 중앙값 8.473ms. 시작 시 최대 26.4ms world dt/25.388ms wall gap이 있지만, 이동·회전 중 대부분 8–11ms 수준이었다. 기록된 GT 구간에서 큰 지속적인 frame stall은 확인되지 않았다. GPU/Present jitter는 미측정이다.
- 입력이 있는 31개 sampled frame 모두 `ControlStep == QueuedYaw`(0.002도 이내), callback count 최대 1이다. 이 sampled frame에서는 입력이 늦게 먹거나 중복 callback으로 더해지는 현상을 확인하지 못했다. 전체 unsampled frame의 input 계약을 증명한 것은 아니다.
- 순식간 yaw 변화가 요약 구간 peak 최대 46.025도였다. frame 7737/T=9.846의 window이고 peak를 만든 정확한 frame/input 값은 요약 모드에서 없어 원인을 확정할 수 없다. camera 회전 lag는 0, collision fix는 관측된 모든 rig 행에서 0이다. camera lag를 강제로 켜거나 sensitivity를 변경하지 않았다.
- state samples는 Combat=0/Turn180=0/TIPTarget=0이며 일반 Cycle이 81개다. 따라서 sampled 이동 구간의 큰 Turn/TIP/Combat 외부 OW가 직접 활성화됐다는 증거는 없다. 이것만으로 모든 animation/retarget 품질 문제를 배제하지 않는다.
- 기존 카메라 행의 CameraAge는 world dt 한 프레임과 일치한다. 이 행은 Player Tick의 **카메라 갱신 전 캐시**다. 일부 ControlStep!=0/CameraStep=0을 실제 렌더 camera 멈춤으로 해석하면 잘못된 시점 비교가 된다.

따라서 게임플레이 수정 대신 PostCamera 관측을 추가했다. local 진단을 켠 동안에만 기존 world delegate에 등록하고 off·local owner 상실·EndPlay에서 해제한다. 틱 순서·카메라 lag·이동·애니메이션 정책은 변경하지 않는다. 전체 프레임 input과 카메라 갱신 후 POV를 함께 보려면 최신 빌드에서 `p.ProjectJ.MouseTurnTrace 2`로 짧게 재현해야 한다. 추가 빌드/검증 경로는 `PostCameraBuild.log`, `PostCameraAutomation/index.json`, `PostCameraAutomation.log`다.

추가 direct UBT 빌드 성공(29.46초), 상세 모드 2의 동일 카메라/실제 authored 재생 자동화 **2/2 성공**. PostCamera 행 발생과 tick/interval/pawn-control 필드가 기록되는 것을 확인했다. 해당 NullRHI transient fixture의 PC view cache는 CameraT=0으로 남아 있으므로 실제 camera-update POV 품질을 검증했다고 해석하지 않는다. 사용자의 새 렌더링된 PIE 실행에서 그 값을 확인해야 한다. 원본 사용자 로그 SHA256: `F46F903DC99557494DE2E3B92BC1574CBC5E6F704B756AB4A0CD8B6BAC4A9A67`.
