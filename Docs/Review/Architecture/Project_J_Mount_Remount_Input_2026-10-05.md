# 재탑승 후 F·이동 입력 복구 — 2026-10-05

사용자는 첫 하차는 되지만 재탑승 후 F와 WASD가 모두 멈춘다고 보고했다. 기존 PIE 로그에서 BP_Wyvern의 IA_Interact 연결, owning client의 `Authority=0` 하차 요청 및 서버의 정상 하차를 확인했다. 수정 범위는 탈것 입력의 소유권 복구 수명이다.

## 원인

클라이언트의 Controller 복제 값이 사라질 때 탈것은 자체 F·비행 입력 바인딩과 weak InputComponent 참조를 정리했다. 하지만 이 경로는 서버의 `UnPossessed()`와 달리 APawn의 InputComponent 자체를 파괴하지 않는다. 재탑승 시 engine `APawn::PawnClientRestart()`는 기존 InputComponent가 있으면 `SetupPlayerInputComponent()`를 생략한다. 기존 native 복구 경로는 지워진 weak 참조만 조회하거나 flight 바인딩을 다시 등록하지 않아 F와 WASD가 함께 빠질 수 있었다.

앞선 테스트는 재탑승 뒤 `SetupPlayerInputComponent()`를 직접 호출했으므로 이 클라이언트 경로를 놓쳤다. 이번 테스트는 기존 InputComponent를 유지하고 Setup을 재호출하지 않는 경로를 검사한다.

## 변경

- 공통 탈것은 `PawnClientRestart()`와 로컬 `OnRep_Controller()`에서 실제 InputComponent를 통해 F 바인딩을 복구한다.
- 비행 탈것도 같은 시점에 Move·Look·Ascend·Descend 바인딩을 복구한다. Setup과 복구는 각 도메인의 동일한 등록 helper를 사용한다.
- 정리는 기존 소유한 handle에만 적용한다. 반복 restart/replication에서 native 중복 바인딩을 만들지 않고 BP나 다른 시스템의 바인딩을 보존한다.
- 컨트롤러 상실 시 기존 cleanup과 pending takeoff 취소를 유지한다. 다른 플레이어의 탈것이나 remote proxy에는 로컬 입력을 등록하지 않는다.
- Tick, IMC 추가·제거, BP Setup 재실행 또는 asset 저장은 추가하지 않았다. 바인딩 복구는 소유권 이벤트에서만 수행한다.

## 검증

직접 UnrealBuildTool로 Editor/Game Win64 Development 빌드를 검증했다. [검증 기록](Project_J_Mount_Remount_Input_Validation_2026-10-05.json)에 로그·작업 전후 source hash를 기록한다. 기존 110개와 신규 `ProjectJ.MountRemount.RetainedInputRecovery`, 총 111개 회귀가 성공했다. 기존 fixture 경고 8건 외 새 경고는 없다.

새 테스트는 지상/비행 탈것 각각 기존 InputComponent에서 controller 상실·복구를 세 번 반복한다. Setup이 한 번만 호출되는 동안 native 바인딩이 복구되는지, 반복 restart가 중복을 만들지 않는지, unrelated binding이 보존되는지 검사한다. 비행 탈것의 실제 Move delegate가 movement input을 생성하고, 공통 F delegate가 rider possession을 복구하는 것도 확인한다. 이어서 실제 하차·재탑승·새 InputComponent 생성 경로를 검사한다. 이 테스트는 replication callback의 순서를 모델링한 native fixture이며, 실제 네트워크 PIE의 대체는 아니다.

에디터를 다시 열고 같은 탈것에서 탑승 → WASD 이동 → F 하차 → 재탑승을 3회 이상 반복한다. 클라이언트 PIE에서도 이동·시점·F·이륙/착륙 입력을 확인한다. 실패가 남으면 Output Log의 `DismountInput`, `DismountInputReceived`, `DismountRejected`를 함께 확인한다.
