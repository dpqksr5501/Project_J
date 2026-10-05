# Pivot 입력 반응과 F 하차 후속 수정 — 2026-10-05

## Pivot 문제와 변경

> **아래 Pivot 정책은 후속 수정으로 대체됐다.** 사용자 PIE에서 새로 승인된 반전까지 일반 redirect로 소비하는 정책이 연속 Pivot을 누락시켰다. 최신 정책은 [일회성 동작의 요청 수명](../../Animation/Architecture/OneShot_Command_Lifetime_2026-10-05.md)을 따른다. 승인된 새 Pivot은 이전 Pivot을 대체하고, 일반 redirect만 MM으로 복귀한다. [입력 기준 통일](../../Animation/Architecture/Strafe_Pivot_Input_Basis_2026-10-05.md), 마우스 취소와 아래 하차 수정은 유지한다.

Start/Land의 마우스·이동 입력 방향 중단 경로에서 확정된 Pivot을 명시적으로 제외하고 있었다. 또한 Pivot 재생 중 새 이동 의도가 다른 Pivot 후보를 만들면, 입력에 따른 모션 매칭 복귀보다 새 Pivot 재생이 우선했다.

확정된 Pivot도 기존 Start의 입력 방향 임계값을 사용해 Cycle/Idle 모션 매칭으로 복귀한다. 새 Pivot asset을 확정하는 게임 스레드 시점에 카메라·이동 입력 기준을 저장하므로, Start나 이전 Pivot의 입력을 물려받아 새 Pivot이 즉시 취소되지 않는다. 선택을 유발한 최초 입력은 그대로 재생하며, 재생 중 추가 입력은 MM 복귀를 우선한다. 중단한 요청 revision을 소비하고 확정 단계에서도 재확정을 차단해 지연된 상태 스냅샷이 같은 Pivot을 되살리지 않는다.

입력을 유지하면 기존 authored 종료까지 재생하고, 입력 해제는 Stop을 우선한다. 점프·착지·전신 동작 및 탑승 경계의 기존 수명 규칙을 유지한다. 원격 Pivot의 정상 actor 회전을 로컬 마우스 입력으로 해석하지 않는다. 기존 OTM/Strafe Start와 Run/Sprint의 입력 방향 임계값, Chooser·ABP·Blend Stack 연결을 유지했다. 실제 애니메이션 블렌딩 품질은 아래 PIE 확인 대상이다.

## 하차 문제와 변경

기존 출구 후보는 탈것 actor의 Z, 즉 탈것 캡슐 중심 높이를 플레이어에게 그대로 사용했다. 탈것 캡슐이 플레이어보다 낮으면 정상적인 평지에서도 플레이어 캡슐이 바닥에 겹쳐 네 방향 출구가 모두 막힐 수 있었다. 이 결함은 소스와 물리 충돌 회귀 테스트에서 확인했으며, 사용자 BP에서 발생한 F 실패의 단일 원인으로 단정하지 않는다.

지상 하차는 후보 주변 바닥을 조회하고 플레이어 캡슐 높이에 맞춰 배치한 뒤 전체 캡슐 충돌을 검증한다. 걷기 불가능한 바닥과 막힌 출구는 거부하고, rider의 실제 collision response를 사용한다. 비행 중의 출구 배치, `Allow Air Dismount`, 이착륙 입력 잠금, 서버 권한, 강제 lifecycle 하차 규칙은 유지했다.

## 비용과 소유권

- Pivot 입력 기준 저장은 새 asset 확정 시점에만 실행한다. 입력 중단은 기존 경량 각도 비교를 사용하며, MM 강제 검색은 중단 경계에서 한 번 요청한다.
- 바닥 trace와 캡슐 overlap은 하차 요청 시에만 수행한다. 후보는 최대 네 개이며, 위치 검사용 Tick을 추가하지 않았다.
- 입력 등록·하차 요청·거부 사유 로그도 해당 이벤트에서만 출력한다. 기존 소유한 input binding handle 정리와 possession 복구를 유지한다.

## 검증과 한계

직접 UnrealBuildTool로 Editor/Game Win64 Development 빌드를 검증했다. 최종 결과는 [검증 JSON](Project_J_Pivot_Mount_Followup_Validation_2026-10-05.json)에 기록한다. 기존 108개와 신규 2개, 총 110개 회귀가 성공했다. 기존 네 테스트의 fixture socket/profile 경고 8건은 그대로다. 신규 테스트는 Pivot 중단·지연 스냅샷·입력 생성 대체 후보·Stop 우선순위와, 실제 바닥·서로 다른 캡슐 높이에서 지상/비행 탈것의 Enhanced Input 하차 dispatch 및 possession 복구를 검사한다.

첫 하차 테스트 실패는 fixture PlayerController를 local로 표시하지 않아 입력 소유권 guard가 정상 거부한 결과였다. engine의 `SetAsLocalPlayerController()`로 테스트 설정을 보완했으며, 게임의 소유권 검사는 변경하지 않았다. 중간 로그는 보존한다. Unreal MCP를 사용하거나 BP·DA·레벨 에셋을 저장하지 않았다. 실제 IMC 연결과 네트워크/애니메이션 시각 품질은 자동화로 확인하지 않았다.

## 에디터 확인

1. OTM/Strafe의 Run/Sprint Start 입력 반응이 기존과 같은지 확인한다. 실제 사용하는 Pivot을 실행하고 초기 방향을 유지하면 끝까지 재생되는지, 이후 마우스 또는 WASD 방향을 바꾸면 MM으로 복귀하는지 확인한다. 입력 해제·점프·공격·연속 전환도 확인한다.
2. 지상 탈것과 착륙한 비행 탈것에 탑승한 후 F를 누른다. Output Log에서 `Dismount`를 검색한다.
3. `DismountInput`의 `RiderAction`/`MountAction`과 `Bindings`를 확인한다. `Bindings=0`이면 플레이어의 유효 Interact Action 또는 탈것 BP Class Defaults의 `Input > Interact Action`을 확인한다. 연결할 action은 활성 IMC에서 F에 매핑된 action과 같은 객체여야 한다. 임의의 새 action을 만들지 않는다.
4. `Bindings>0`인데 `DismountInputReceived`도 `FlightInputLocked`도 없다면 `showdebug enhancedinput`으로 활성 IMC의 F 매핑, 높은 우선순위 입력 소비 및 BP 입력 override를 확인한다. `NotEnhancedInput`이면 실제 pawn/controller가 만드는 InputComponent 종류를 확인한다. 프로젝트 DefaultInput.ini의 기본 Enhanced Input 클래스 설정은 정상이다.
5. `AllExitsBlocked`면 주변 출구 충돌·캡슐 크기를 확인한다. `FlightInputLocked`면 이륙/자동 상승/착륙 상태 또는 PendingTakeOff를 확인한다. `AirDismountDisabled`면 비행 중 하차가 설정으로 금지된 상태다. 공중 하차를 의도한 탈것만 `Mount > Allow Air Dismount`를 확인한다.
6. `DismountSucceeded` 이후에도 문제가 보이면 controller possession과 BP의 `On Rider Dismounted` 후속 동작을 확인한다. 2인 PIE에서는 owning client의 요청과 authority 로그를 함께 확인한다.
