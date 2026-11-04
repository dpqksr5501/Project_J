# 권위 월드 이벤트 실행

`AProject_JWorldEncounter`는 encounter의 phase·NPC·respawn·해결 알림을 소유한다. `UProject_JWorldEventRegistry`는 같은 월드의 고유 ID와 실행 자원을 관리한다. 새로운 spawn/phase 저장소나 이벤트별 timer를 추가하지 않는다. 현재 구현·검증 진행은 [MMORPG 작업](../Architecture/Planning/Project_J_MMO_Architecture_Work_2026-10-26.md)을 따른다.

## 제작·시간 계약

- 배치 인스턴스의 `EncounterId`는 안정적인 고유 ID다. 재사용 BP 템플릿은 비워 둘 수 있지만 실제 인스턴스에서는 필요하다.
- `NPCClass`가 없으면 Disabled를 유지한다. 보스 아레나나 manifest 항목의 존재는 전투 구현 완료가 아니다.
- `ActivationRadius`는 준비 시작의 거리이며 복제 거리·전투 추적/리셋 거리와 다르다. 현재 기본값은1,500m다.
- `bUseSchedule=false`이면 상시 입장이다. true이면 `StartUtc <= 현재 UTC < EndUtc`에서만 새 준비·spawn을 승인한다. 종료는 시작보다 늦어야 한다.
- 일정이 닫혔을 때 이미 Active인 보스는 정상 해결할 수 있다. 아직 Preparing이면 spawn 승인 단계에서 다시 기간을 확인한다. 운영 서버의 UTC 동기화가 필요하다.
- cooldown은 해당 월드의 서버 시간이다. 재시작 간 cooldown·기여도·보상 권리는 아직 영속화하지 않았다. 클라이언트 시각이나 재접속으로 보상을 확정하지 않는다.

## 실행과 자원

Registry는 서버/standalone Game·PIE 월드에서만 실행한다. 클라이언트는 복제된 Phase/SpawnRevision/NextSpawnServerTime을 소비한다.

활성화는 공통 observer snapshot의 정확한3D 존재 판정(`HasAny`)을 사용한다. 첫 match에서 끝내고 전체 후보 배열·정렬은 만들지 않는다. 전체 후보가 필요한 NPC scoring은 기존 안정 ID 순서를 유지한다.

| 경계 | 현재 제한과 포화 정책 |
| --- | --- |
| 등록 | 월드당2048 ID, 중복 owner 충돌/retirement/overflow 거절 |
| 순회 | tick당128개 방문·8개 update, round-robin. event별 정상 갱신1초, 초기 phase 분산 |
| GT 시간 | soft budget1ms. 한 actor의 spawn/BeginPlay나 observer snapshot은 원자적 작업이므로 초과 가능; 실제 시간을 계측 |
| 플레이어 검색 | 0.5초마다 필요할 때 공통 snapshot, 최대4096 controller 방문.1km 공간 cell과 정확한3D 거리, 불완전 coverage면 activation 보류·overflow 계측 |
| 클래스 준비 | 최대8개 native 예약. 같은 encounter의 중복 예약 거절, 초과 시 Dormant에서 다음 update로 보류 |
| 준비 완료 적용 | async callback은 ready만 표시. 오래된 예약부터 tick당2개 spawn 시도, ground collision 확인 뒤 deferred spawn |
| timeout | 준비30초가 지나면 consumer를 실패 cooldown으로 보냄. 실제 load는 끝날 때까지 예약을 유지 |
| 취소 | owner 퇴장/교체는 weak consumer와 token을 퇴역. 진행 중인 package 작업의 예약은 조기 재사용하지 않음 |
| 종료 | world teardown/EndPlay에서 승인 중지, weak owner·등록·load handle·NPC 정리. 늦은 callback은 새 이벤트에 적용되지 않음 |

`FinishSpawning`은 외부 BeginPlay/Blueprint/GC 호출 경계다. 반환 후 owner의 종료·현재 preparation token·phase를 확인하고, 퇴역 중 만들어진 NPC가 남지 않도록 한다. `OnResolved(EncounterId, SpawnRevision)`는 기존 해결 알림이며 자동 보상 writer가 아니다. 보상 연결 시 별도 claim/receipt와 durable 소유권 계약을 사용한다.

## 관측·검증

`ProjectJ.WorldEvents.Stats`는 update/방문/적용 수·누적 spawn 시도, 준비 슬롯과 peak, observer 수/overflow, defer·timeout, GT ms·최대 update 지연을 출력한다. trace scope는 `ProjectJ_WorldEvents`, `ProjectJ_WorldEventObservers`다.

`ProjectJ.Foundation.WorldEvents.BoundedExecution`은 실제 Encounter burst가 Registry를 통해 실행되는지, update/spawn/예약 상한, observer snapshot 재사용, 중복 예약, UTC 경계, world retirement를 확인한다. `PreparationLifecycle`은 실제 async callback과 ground collision을 거쳐 NPC를 spawn하고 consumer 취소·NPC 정리를 확인한다. 실제 맵·보스 AI·다수 원격·운영 재시작/일정 변경 검증과 구분한다.

2026-10-26 최종 native 실행에서 `PreparationLifecycle`은 NPC4개·spawn 적용4회를 확인했고 준비 slot peak는1이었다.32개 burst의 상한 검증은 별도 `BoundedExecution` 결과다. 이 실행의 peak1을8개 동시 실제 로딩의 부하 증거로 사용하지 않는다. 자세한 결과는 위 작업 문서의 검증 절을 따른다.

이 scheduler는 한 권위 월드의 실행 기반이다. 다중 서버 LiveOps 일정·leader election·분산 fencing·운영자 인증·재시작 복구는 실제 service endpoint와 저장 계약을 연결한 뒤 확장한다. 여러 서버가 같은 전역 보상을 독립 확정하도록 구성하지 않는다.
