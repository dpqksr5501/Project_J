# 이동 중 하차 후 탈것 이동 정리 — 2026-10-05

하차와 재탑승 입력은 정상 동작하지만, WASD 이동 중 F로 내리면 탈것이 계속 이동한다는 보고에 대한 수정이다. 기존 공통 하차 경로는 rider 관계와 possession을 정리하면서 탈것의 속도를 명시적으로 정리하지 않았다. 무컨트롤러 물리 설정이나 비행 모드에서 이 속도가 남을 수 있었다.

하차의 권한·비행 허용·출구 충돌 검사가 통과한 후, rider를 분리하기 전에 `ConsumeMovementInputVector()`와 `StopMovementImmediately()`를 한 번 실행한다. CMC의 StopMovementImmediately는 StopActiveMovement를 통해 속도·가속도·요청 이동도 정리한다. 기존 ForceNetUpdate 경로로 최종 상태를 전달한다. 하차가 거부되면 이동 입력·속도·possession을 유지한다. 강제 lifecycle 하차에도 같은 정리를 적용한다.

이동 정리를 위해 Tick, IMC 변경, 비행 모드/상태 강제 변경, root motion 또는 외부 물리 힘 일괄 제거를 추가하지 않았다. 비행 중 자율 이착륙 동작은 기존 상태 머신이 담당한다. 외부 충격이나 이동 플랫폼 등 새로운 이동 원인은 이 수정의 대상이 아니다.

직접 UnrealBuildTool로 Editor/Game 빌드 및 기존 111개와 신규 `ProjectJ.MountDismountMotion.SuccessAndRejection`, 총 112개 회귀를 검증했다. [검증 기록](Project_J_Mount_Dismount_Motion_Validation_2026-10-05.json)에 최종 로그와 소스 hash를 기록한다. 신규 테스트는 지상/비행의 이동 속도와 미소비 입력, 하차 거부 시 보존, 성공/강제 하차 시 정리, 이동 모드와 비행 상태 유지, 재탑승 후 새 이동 입력을 검사한다. 기존 fixture 경고 8건은 그대로다.

PIE에서 이동 키를 누른 채 F 하차 후 탈것의 이동이 멈추는지 확인한다. 같은 탈것에 재탑승해 이동·하차를 반복하고, 출구가 막히거나 비행 하차가 금지된 경우에는 기존 이동이 유지되는지도 확인한다. 실제 BP의 모션 재생 및 클라이언트 replication의 시각 확인은 별도이며, BP·레벨·에셋을 저장하지 않았다.
