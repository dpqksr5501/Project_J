# Mover·플레이어 애니메이션 전환 후 소스 재점검

2026-10-20 사용자 요청으로 [GASP 5.8 이관](../../Animation/Architecture/Player_Locomotion_Migration_2026-10-19.md) 이후의 소스와 연결 계약을 재점검했다. OTM/Strafe, Strafe 전방 3방향 Sprint와 현재 공중 재선택/playhead를 보존한다. 기존 사용자 변경은 되돌리지 않았다.

## 검토 범위

전체 Source의 `ACharacter`/CMC 의존과 이동 API 소비자를 검색하고, 전환에 직접 연결된 다음 경로를 따라 읽었다. 저장소 모든 기능의 모든 줄을 재검증한 것으로 해석하지 않는다.

| 경계 | 확인 내용 |
| --- | --- |
| 입력 → Mover | semantic/실제 입력, Controller 상실·재소유, gait/jump 승인, 명령 전송·이력과 재시뮬레이션 |
| 물리 → 표현 | SmoothWalking/Falling, jump/landing sequence, landing braking, based orientation, Attached mode와 하차 복원 |
| 물리 → ABP | GT snapshot·proxy 소비·GC 참조, native 상태/선택, 공중 재선택, 초기화·delegate 해제 |
| GAS·전투 | ASC 아바타 교체, montage root motion과 종료·section/rate 변경, 전신 Slot과 원격 표현 |
| 주변 소비자 | budget/coordinator, 리타깃·손 접촉, Foley·원격 이벤트, NPC 경로/Mass 경계와 진단 |

## 재현 후 수정

### Strafe Sprint의 시뮬레이션 승인

producer는 앞쪽 3방향을 제한했지만 `ResolveMoverPolicy`는 실제 방향을 받지 않았다. GAS Sprint가 활성화되면 다른 방향의 명령도 Sprint/585cm/s로 승인될 수 있었다.

native 정책 조회에 같은 명령의 `FCharacterDefaultInputs`를 전달한다. 기록된 world-space 이동과 `ControlRotation`으로 기존 `SupportsStrafeSprint` 규칙을 적용하고, 거절 시 같은 정책에서 Run 속도를 계산한다. 현재 controller 회전이나 held input cache를 사용하지 않는다. producer 제한은 입력 반응을, simulation 제한은 예측/서버 승인을 담당하므로 모두 유지한다. simulation에서 GAS 능력을 별도로 취소하지 않는다. Jump 같은 permission 조회는 방향 입력을 요구하지 않는다.

`ProjectJ.Mover.StrafeSprintCommandAdmission`은 실제 GAS/Combat 상태에서 yaw 0/90/179도 × 8방향+정지의 27개 명령을 검사한다. controller의 현재 회전은 명령과 다르게 두고, 승인 방향·속도·이력 보존·GAS 소유권을 확인한다.

### Custom 명령의 숫자와 방향 계약

기존 serializer는 archive 오류만 검사했다. NaN offset, 무한대 yaw rate, 정의되지 않은 방향 6/7이 유효한 명령이었으며 Falling은 offset을 회전에 직접 더했다.

- wire layout을 유지하며 유한한 회전 값과 방향 0–5만 직렬화 성공으로 인정한다.
- wire를 거치지 않는 로컬/예측 입력도 있으므로 `OnPreSimulate`의 working copy에서 offset을 ±180도로 제한하고 비정상 값은 중립 값으로 정리한다.
- 역사 입력을 덮어쓰거나 새 timer/state를 만들지 않았다. 독립 BP solver 호출도 방어하는 기존 Ground 검사는 유지했다.

`ProjectJ.Mover.CommandNumericAdmission`이 실제 MemoryWriter/Reader와 simulation 승인을 검사한다. 유효한 큰 yaw rate와 jump ID의 기존 round-trip 검사도 유지한다.

### Controller 없는 Pawn의 입력 수명

`PopulateMoverInput`의 조기 반환은 기본 Invalid input type과 이전 held/edge jump를 남겼다. 이제 Controller가 없어도 0 directional intent를 만들고 held/edge jump를 해제한다. request sequence는 유지하여 새 edge가 이전 ID를 재사용하지 않는다.

`ProjectJ.Mover.ControllerlessIdleInput`으로 중립 명령, 재소유 후 이전 점프 미전달과 새 점프 edge를 검사한다.

### 진단 명령과 테스트의 복사 경계

- `DumpAnimationExecutionPolicy`, `DumpAnimBudget`은 `ACharacter` cast 대신 `FindPoseSource`를 사용한다. Mover source mesh와 CMC 탈것을 모두 조회한다.
- 설치 엔진의 `FMoverDataCollection`은 assignment가 deep copy이고 implicit copy constructor는 data block을 공유한다. 새 Sprint 테스트의 이력, 기존 jump effect 출력과 `PublishMoverFrame`의 수정 가능한 snapshot을 명시적 대입으로 바꿨다. jump 적용 후 시작 프레임의 속도가 보존되는 assertion을 추가했다. 읽기 전용 공유는 유지했다.
- `Run-GASP58Regression.ps1`은 전체 `ProjectJ.Mover`, `ProjectJ.NetworkPrediction`, `ProjectJ.Warnings` prefix와 최소 30개를 요구한다. 구형 검사를 성공 stub으로 남기지 않았다.

### 실제 초기화·종료 경고의 원인 수정

- 직업 registry는 Config 배열만 확인한 뒤 비어 있다고 경고했으며 PrimaryAsset 로드는 persistence 소비자의 별도 호출에 의존했다. GameInstance subsystem 초기화에서 기존 PrimaryAsset 목록을 합쳐 검증한 뒤 게시한다. 실패 시 부분 registry를 공개하지 않는다. 제작 검증도 Config와 실제 게시된 정의를 모두 검사한다. 새 직업 데이터를 임의 생성하지 않았다.
- prototype 시작 장비는 authority Pawn의 BeginPlay가 PlayerState 소유보다 빠르면 실패했다. PlayerState가 준비된 기존 PossessedBy 경로에서 장착하며, 준비 후의 실제 EquipmentManager 누락 경고는 유지한다.
- 첫 네트워크 연결에서 늦게 로드되던 엔진 `PerfCounters` 모듈을 Editor/Server의 게임 모듈 시작에서 로드한다. Iris serializer 등록이 확정된 뒤 모듈 목록이 바뀌던 경고의 실제 지연 의존성을 앞당겼다.
- 현재 NPC는 표준 AIController/path following과 NavMover를 사용하며 DetourCrowd 소비자는 확인되지 않았다. Recast 경로 탐색은 유지하고 사용하지 않는 CrowdManager 생성을 끈다.
- 월드 종료 중 GAS 취소는 로컬 End/태그/Task 정리를 수행하되 이미 제거된 Iris 객체로 취소 RPC를 보내지 않는다. 일반 플레이 취소는 엔진 경로를 그대로 사용한다. 종료 중 카메라의 새 ViewTarget/RPC 전환도 막는다.
- locomotion source가 없는 비게임 월드의 에디터 미리보기는 PoseMatch 선택을 시작하지 않는다. 실제 게임 월드의 초기화·재선택 경로는 유지한다.
- 실제 PIE에서 추가 재현된 `DDCvar.AnalogInputStyle` 조회 성능 경고는 Mover 입력 생성의 반복 이름 검색이 원인이었다. AnalogInputStyle/StrafeStyle의 등록 포인터를 캐시하고 값은 매 명령 읽는다. 늦은 등록의 재조회와 실시간 설정 변경을 보존한다.

`ProjectJ.Warnings`의 실제 GameInstance/PrimaryAsset 부트스트랩, GAS 일반·종료 취소와 카메라 일반·종료 전환 3개를 추가했다. 더 이상 발생하지 않는 빈 registry 경고를 기대하던 8개 테스트의 obsolete expectation도 제거하고 해당 저장·복원·UI 수명 경로를 다시 실행했다.

## 검토 후 유지

- gameplay 착지 lease와 물리 Mover sequence는 권위·수명이 다르다. 시각 착지와 authoritative event를 하나의 boolean writer로 합치지 않았다.
- profile의 허용 속도 상한과 SmoothWalking의 gait target/회전·가속 튜닝은 역할이 다르다. solver는 승인 상한을 넘지 않으며 이 튜닝들을 일괄 제거하지 않았다.
- pose node는 엔진 native 구현이고 ABP/Chooser/Rig는 제작·연결 데이터다. 이전 독립 MM/facing 경로를 다시 켜지 않았다.
- CMC 탈것과 공용 movement query의 CMC 분기는 실제 소비자가 있다. 기존 Mass adapter는 CMC만 정지시키므로 **현재 Mover NPC의 Mass handoff는 지원하지 않으며 guard를 유지**한다. worker/spacing 시험을 Mover handoff나 200명 성능 검증으로 포장하지 않는다.
- asset 경로·형식 재이주나 대규모 코드 분할은 필요하지 않았다. 설치 엔진/원본 GASP는 수정하지 않았다. 후속 보간 수정은 아래 프로젝트 플러그인 재정의를 사용한다.

## 검증

- 수정 전 새 검사 3개가 모두 실패하여 결함을 재현했다. 첫 수정 후에는 52개 통과, 새 Sprint 테스트의 shallow copy 전제 1개 실패였다. 이를 수정하고 공유 fixture도 보완한 최종 실행은 **53개 Success, succeededWithWarnings/failed/notRun 모두 0**이다. 서로 다른 실행의 통과 수를 합산하지 않는다.
- 확장 회귀는 Mover/GASP, GAS 아바타 교체·소유권, montage budget, 탑승/하차·입력 소유권, 리타깃/메시 LOD, follower 수명과 NPC 실제 nav/추적을 포함한다.
- 최종 direct UBT Editor 성공(14.26초), Game 성공(46.21초). 기존 엔진/빌드 프로세스가 없는 상태에서 순서대로 실행하고 정상 종료까지 기다렸다.
- 로컬 근거는 `Saved/GASP58Upgrade_20261020/SourceReaudit`의 `Baseline`, `Expanded`, `ExpandedFinal/Automation/index.json`, `Build_Editor_Final.log`, `Build_Game_Final.log`다. 변경 전 소스는 `Before`에 보존했다. Saved 원자료는 저장소 공유 산출물이 아니다.
- 초기화·종료 수정 후 확장 회귀는 **64개 Success, succeededWithWarnings/failed/notRun 모두 0**이다. 위 53개에 경고 계약 3개와 registry 소비자 8개를 포함한 한 번의 실행 결과다. 12,289회 실제 인벤토리 명령과 저장·복원도 포함한다. 근거는 `Warnings/Regression/Automation/index.json`이다.
- 마지막 CVar 조회 수정 후 공식 집중 실행기의 **28개가 모두 Success**, 경고·실패·미실행 0이다. `Saved/Validation/GASP58/SourceReauditWarningsFinal_20261020/Automation/index.json`이 근거다. direct UBT 최종 Editor 4.75초, Game 10.05초 성공이며 컴파일 경고는 없었다.

## 설치 엔진의 한국어 Smoke Test 오류

설치 엔진 자체 `System.Core.Experimental.UnifiedError` 검사 14개를 문화권별로 재현했다. 한국어에서는 `CreateErrorMessage`와 `CreateErrorMessageWithContext` 2개 실패(12개 성공), `-culture=en`에서는 14개 모두 성공이다. 엔진 `Core/Tests/Experimental/UnifiedError/UnifiedErrorTests.cpp`는 현지화된 FText 결과를 영문 고정 문자열과 비교한다. 이 때문에 시작 로그의 `LogAutomationTest: Condition failed` 15줄이 발생한다.

프로젝트 로직 오류와 구분하며, 사용자 언어를 바꾸거나 Smoke Test/로그 출력을 끄지 않았다. 설치 엔진의 사전 빌드 Core 바이너리를 수정하지 않았으며 한국어 상태의 해당 엔진 검사는 해결되지 않았다. 격리 재현 근거는 `Warnings/EngineSmokeKo`와 `Warnings/EngineSmokeEn`이다.

## 보간 버퍼 경고의 후속 수정

사용자의 추가 요청으로 버퍼 경고도 별도 원인 계측·수정했다. `ProjectJ.CaptureMoverBuffers [초]`는 활성 Game/PIE 월드의 수신·시뮬레이션·보간 시간을 프레임 끝에서 읽고 종료 시 CSV 한 번을 저장한다. 상태를 수정하거나 프레임마다 MCP/파일 작업을 수행하지 않는다.

원본 정책의 90초 측정에서 첫 접속 `fell behind` 489ms와 `starved` 6회가 재현됐다. 244/275ms 정지 프레임에서 보간 시간이 마지막 수신 상태를 168ms까지 넘었다. `Buffer/LiveBaseline.log`, `Saved/Validation/MoverBuffers/20261020_202930.csv`가 근거다. 전송 주기만 올리거나 버퍼를 확대하는 수정으로 다루지 않았다.

- [프로젝트 NetworkPrediction 패치](../../../Plugins/NetworkPrediction/PROJECT_J_PATCH.md)는 최초 수신 시 서버 epoch에서 시작하고, 0.9–1.1의 작은 시계 조절로 기존 목표 버퍼를 회복한다. 진행 시간은 수신된 구간에 한정하며 패킷이 없을 때 마지막 유효 구간을 유지한다. 재수신 시 되감지 않는다.
- 100/250ms 설정, Fixed tick, prediction/rollback·직렬화·public API·클래스 메모리 배치는 유지한다. 실제 backlog와 잘못된 시간에 대한 기존 경고를 유지한다.
- UE 모듈 의존성 규칙상 엔진 Mover가 프로젝트 NetworkPrediction에 의존할 수 없으므로 같은 5.8.3의 Mover도 프로젝트 플러그인으로 함께 빌드한다. 원본 Mover 소스/Config/Resources 183개는 해시 비교상 변경 0개다. NetworkPrediction 원본 57개 중 production 변경은 WorldManager cpp 한 곳이며 private helper·회귀 검사·패치 설명을 추가했다. 설치 엔진 사본을 덮어쓰지 않는다.
- 보간 수정 후 Mover/GASP·소유권·NPC nav·종료와 새 시계 검사 2개를 포함한 확장 **58개 Success, 경고·실패·미실행 0**이다. `Buffer/Regression/Automation/index.json`을 따른다. 늦은 접속, 275ms 정지, 패킷 미수신/복구와 실제 비정상 시간의 진단 보존을 검사한다.
- direct UBT Editor 118.28초, Game 94.03초 성공, 컴파일 경고 0개다. 런타임 로그에서 프로젝트 `Plugins/Mover`와 `Plugins/NetworkPrediction` DLL 로드를 확인했다.
- 수정 후 90초 2인 PIE에서 275ms 정지 3회를 주입했다. 원격 시점 537샘플·4개 역할, 이동/공중/7개 몽타주 573샘플 검사가 모두 통과했다. 보간 경고와 `Interpolation out of sync`는 0회다.
- CSV의 클라이언트 **9,320프레임**에서 수신 범위 초과·시간 역행 0회, 최소 버퍼 1ms, 최대 프레임 283.948ms, 마지막 안정 구간 최소 버퍼 76.75ms를 확인했다. buffer 설정은 100/250ms로 유지됐다. 같은 검증기에 원본 CSV를 넣으면 수신 범위 초과로 실패하고 수정 CSV는 통과한다.
- 오프라인 검증은 `python Scripts/Validation/Validate-MoverBufferCapture.py <CSV> --require-hitch-ms 200`이다. `Buffer/PatchedCaptureValidation.json`, `Buffer/InjectedHitches.json`, `Buffer/NetworkPatched.json`, `Buffer/LivePatched.log`와 `Saved/Validation/MoverBuffers/20261020_204128.csv`가 로컬 근거다.
- 프로젝트 플러그인 전환 후 단일 PIE도 다시 확인했다. SprintTurn 5,757프레임, Ground 2,400프레임의 실제 Start/Stop, SprintAir 3,838프레임/8개 경우·재선택 10회가 통과했다. 누락 선택·playhead 되감기 0이다. `Buffer/SinglePatched.json`을 따른다.

### 별도 소켓에서 발견한 공급 주기 문제

첫 소켓 재검증은 기능 판정이 통과해도 완료로 보지 않았다. 수신 계측에서 서버/클라이언트 시뮬레이션은 약 33ms, Pawn 설정은 100Hz이며 서버의 복제 cache frame도 매번 바뀌지만, 200m 떨어진 플레이어의 수신만 약 333ms로 내려갔다. Iris 기본 Sphere prioritizer는 100m 밖에 우선순위 0.1을 주며 30Hz 서버에서 약 3Hz 전송으로 이어졌다. 이 공급은 기존 100ms 목표와 맞지 않았다.

`UProject_JPlayerMoverPrioritizerConfig`와 클래스별 Iris 설정으로 **플레이어**의 바깥/외곽 우선순위 하한을 0.5로 적용한다. 근거리는 기존 1.0, relevance/cull 범위는 유지한다. NPC 클래스는 변경하지 않고, 전역 ForceNetUpdate·NPC 고빈도 전송이나 buffer 확대를 추가하지 않았다. 이 정책은 전송 기회를 확보하는 것이며 과부하 서버에서 무조건 일정 Hz를 보장하는 계약은 아니다.

동일한 별도 서버+2개 실제 소켓, 100개 AOI probe, 지연 50ms·손실 1%, 200m 원격 관찰 조건의 최종 `GASP58_BufferPriorityFinal_20261020`은 5개 복제/애니메이션 단계와 정상 연결 종료가 통과했다. 3개 프로세스 exit 0, timeout 0, 보간 경고·out-of-sync·SystemFault는 모두 0회였다. 원격 수신은 14.49/15.16Hz, 마지막 3초 평균 buffer 66.78/75.60ms, 최대 관측 수신 간격 166.71/166.57ms로 회복됐다. 각 클라이언트 498/501프레임에서 미래 상태 소비·되감기 0을 별도 CSV 검증했다.

근거는 `Saved/Validation/GroupD_20260910/GASP58_BufferPriorityFinal_20261020/verification.json`, 각 Client의 `mover-buffer-validation.json`과 User/Saved의 CSV다. 공급 문제를 드러낸 앞선 `GASP58_BufferClockTrace_20261020`·`GASP58_BufferPollTrace_20261020`도 보존했다. `Measure-Network.ps1 -Animation -MoverBuffers`는 향후 capture와 최소 수신 10Hz 검증을 포함하며, 새 플러그인의 소스·실행 DLL도 provenance에 포함한다.

최종 priority/진단 변경 후 direct UBT Editor 4.60초·Game 18.41초 성공, 컴파일 경고 0개다. 공식 `Run-GASP58Regression.ps1 -RunName BufferFinal_20261020`은 **30개 Success, 경고·실패·미실행 0**이다. 실행 중인 에디터·빌드·ShaderCompileWorker 등은 최종 확인에서 0개였다. GUI의 임시 floor/start만 폐기했고 수정된 content package는 없으며 PIE 설정·background throttle을 복원했다.

이는 긴 프레임 정지 자체를 제거하는 성능 최적화가 아니다. 경고 유무와 함께 수신 범위 초과·시간 역행·유효 상태 유지·버퍼 회복을 확인해야 한다. 플러그인 원본 버전과 재적용/제거 계약은 패치 설명을 따른다.

## 한계

에디터 시작/종료 전체의 무경고·무오류, 모든 플랫폼·Cook/Shipping, 모든 latency/loss·장시간 서버·대규모 군중을 인증하지 않는다. 위 소켓 검증의 100개 probe는 이동·애니메이션 NPC 100명의 성능 검증이 아니다. 이전 [이관 검증의 초기화/종료 경고](../../Animation/Architecture/Player_Locomotion_Migration_2026-10-19.md)를 후속 점검한 결과는 위와 같다. 로그를 숨기거나 네트워크 buffer를 확대하지 않았다. 엔진 한국어 Smoke Test, MCP 플러그인의 라이선스 안내와 종료 시 Epic 외부 HTTP 요청 경고는 프로젝트 수정으로 해결됐다고 표시하지 않는다. 커밋/push는 수행하지 않았다.
