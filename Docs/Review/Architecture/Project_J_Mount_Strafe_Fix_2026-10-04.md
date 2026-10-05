# F 하차·TAB Strafe 방향 전환 수정

기준: 2026-10-04, UE 5.8. 사용자가 리팩터링 전부터 재현하던 F 재입력 하차 실패와, OTM에서 S로 이동하다 TAB 전환 시 몸이 한 프레임에 카메라 방향으로 돌아가는 문제의 후속 수정이다. 기존 플레이어 고도화 변경 위에 적용하며 에셋을 수정하거나 Unreal MCP를 사용하지 않는다.

## F 하차

탑승하면 Player Pawn의 input binding이 정리되고 Mount Pawn이 조작권을 받는다. 기존 native 하차 binding은 비행 탈것에만 있으며 탈것 자체의 별도 `InteractAction`이 설정돼야 했다. 플레이어의 기존 상호작용 action을 이어받는 경로가 없었다. 실제 Blueprint 연결 전체를 조회하지 않았으므로 모든 F 실패 원인을 에셋 수준에서 확정한 것은 아니다.

- `UProject_JMountComponent`가 플레이어의 실제 bound interaction action을 보관한다. Player 입력 해제는 이 hand-off action을 폐기하지 않는다.
- 공통 `AProject_JMountCharacter`가 rider action과 기존 authored `InteractAction`으로 Started 하차 입력을 구성한다. 같은 action은 한 번만 등록하며, 서로 다르면 둘 다 지원한다. F라는 키를 C++에 하드코딩하지 않는다.
- input component 교체·반복 setup·UnPossessed·EndPlay에서 자기 binding handle만 회수한다. 다른 Blueprint/기능의 binding을 제거하지 않는다.
- owning client의 input setup 뒤 Rider가 늦게 replicate돼도 binding을 갱신한다. 이미 끝난 세션의 queued callback은 Rider/controller guard로 무시한다.
- 비행 탈것은 공통 입력을 상속하며 기존 flight-input lock을 유지한다. 일반 하차의 공중 제한과 collision-free 출구 요구를 강제 하차로 우회하지 않는다.

기존 비행 탈것의 `InteractAction` 이름과 제작 category는 유지하고 선언을 공통 base로 이동했다. 기존 Blueprint는 상속된 property를 계속 사용한다. 새 IMC·하차 키 등록을 필수 작업으로 만들지 않는다.

## TAB 방향 전환

이동 중 combat에서 `bUseControllerRotationYaw`가 켜져 `Pawn::FaceRotation`이 actor yaw를 control yaw로 즉시 복사하던 경로를 제거했다. 기존 Offset Root Bone의 Release 반감기만 조절해도 capsule의 한 프레임 회전을 제한할 수 없으므로 회전 소유자부터 변경했다.

- 지상 moving Strafe는 CMC의 `bUseControllerDesiredRotation`과 `RotationRate.Yaw`로 카메라 방향에 접근한다. 기존 movement prediction·replication·root-motion 경로를 사용한다.
- CombatAnimProfile의 `CombatFacingRotationRateYaw` 기본값은 360 deg/s다. 카메라가 고정됐고 다른 root-motion 회전이 없으면 180도 차이가 약 0.5초에 해소된다. 제작 값은 finite·범위 검사를 거친다.
- idle TIP에서는 CMC 자동 회전을 끄고 기존 authored root yaw·Blend Stack Steering 소유자를 유지한다. 공중의 기존 catch-up interpolation과 OTM의 movement-facing tuning도 유지한다.
- Strafe 방향/warping 각도는 기존 actor-relative 값이므로 실제로 돌아가는 capsule 기준을 계속 사용한다. 첨부한 Master ABP의 Offset Root Bone 및 Blend Stack 노드를 새 graph로 교체하지 않는다.
- `StrafeSettledFacingToleranceDegrees` 기본값 5도 안에 실제 actor/control facing이 들어온 뒤 기존 settle delay를 적용한다. 카메라와 입력이 고정돼 있어도 몸이 돌아가는 동안에는 turn-capable Dynamic Cycle을 유지한다. 회전 도중 Loop-only SettledCycle로 조기 진입하지 않는다.
- 원격 straight-running trajectory repair의 허용 여부는 actor의 snap flag가 아닌 combat rotation 정책을 기준으로 판단한다.

## 검증

새 회귀 4개와 기존 비행 입력 소유권 회귀 1개가 모두 정상 성공했다. 실제 Enhanced Input Started delegate의 cloned dispatch를 사용한 탑승/하차 왕복, queued duplicate, 반복 setup·서로 다른 action·늦은 Rider 연결, 30/60/120fps CMC 회전 상한·180도 wrap·idle/air/OTM 회전 소유자, 실제 facing 정렬 후 SettledCycle 진입을 검사한다.

직접 `UnrealBuildTool.exe`로 `Project_JEditor Win64 Development` 빌드를 성공(exit 0)했다. 로그는 `Saved/Validation/MountStrafeFix_20261004/BuildEditor_Retry1.log`다. 신규 회귀 4개 + 기존 InputBindingOwnership 1개는 모두 정상 성공했으며 경고·실패·미실행·실행 중 0이다. report는 `Saved/Automation/MountStrafeFix_20261004_Focused/index.json`다.

2026-10-05 에디터와 Live Coding 종료를 확인한 뒤 기존 101개와 신규 4개를 포함한 전체 회귀 **105개를 모두 통과**했다. 정상 성공 101개·경고 포함 성공 4개이며 실패·미실행·실행 중은 0이다. 경고 8건은 이전 회귀와 같은 native fixture의 미설정 socket/animation profile 경고이며 새 경고는 없다. report는 `Saved/Automation/MountStrafeFix_20261005_Regression/index.json`다.

회귀 프로세스 종료 후 직접 `UnrealBuildTool.exe`로 `Project_J Win64 Development` Game 빌드도 성공(exit 0, 43.63초)했다. 로그는 `Saved/Validation/MountStrafeFix_20261004/BuildGame_20261005.log`다. 변경 소스 16개(기존 15개 수정·테스트 파일 1개 추가)는 focused 검증 이후 hash가 동일하다. [검증 JSON](Project_J_Mount_Strafe_Fix_Validation_2026-10-04.json)에 빌드·회귀 결과, 경고 원문과 변경 소스의 작업 전후 hash를 기록했다. 실제 authored pose와 네트워크 지연에서의 시각 품질은 아래 PIE 확인 대상으로 남는다.

## 에디터 확인

1. 빌드 후 에디터를 다시 열고 플레이어·탈것 BP를 compile하여 상속된 입력 property가 정상인지 확인한다.
2. 지상 탑승 → F 하차 → 재탑승 → F 하차를 반복한다. 공중 하차 제한과 비행 전환 lock은 기존 정책을 따른다.
3. OTM에서 S를 유지한 채 TAB으로 전투 진입하고 해제한다. 전환 도중 카메라 회전·입력 변경·정지·재전환도 확인한다.
4. 원하는 느낌에 따라 사용하는 CombatAnimProfile의 `CombatFacingRotationRateYaw`를 조정한다. 240은 느리게, 360은 기본, 540은 빠르게 돌아간다. 기존 OTM 회전 속도와 별도 값이다.
5. 제자리 회전·대각 점프·공격 root motion과 2인 PIE 원격 모습을 확인한다. NullRHI 회귀는 실제 authored pose·발 미끄러짐·네트워크 지연의 시각 품질 검증과 구분한다.
