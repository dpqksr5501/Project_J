# W5 지급 소비자 연결 전 확인사항

기준 d4077428. 기존 구현을 다시 읽었다. 개발 전용 XP와 영속 claimed 표시 연결을 검토 중이며 운영 보상·외부 서버를 임의 도입하지 않는다.

재사용 가능한 경로:
- LocalProfileSubsystem::SubmitEdit: 기존 플레이어 영속 변경 진입점.
- SQLiteRepository::CommitProfile / FindProfileReceipt: 동일 프로필 stream/명령 intent 검증.
- SQLiteRepository::CommitWithIntent / FindReceipt: 세션과 구별된 논리 명령 조회 기반.
- WorldEncounter::OnResolved: EncounterId와 SpawnRevision만 전달. 이것만으로 재시작 후 중복 지급 방지 ID나 검증된 참가 자격이 성립하지 않는다.

주의: profile_receipts는 epoch 교체/확정 sequence 정리에 따라 삭제될 수 있다. 보상 키를 회차+캐릭터로 생성하는 것만으로 영구적인 중복 방지가 증명되지 않는다. event entitlement 또는 완료 상태가 같은 원자 트랜잭션으로 남아야 하며, 운영 재시작 후 round ID 재사용 정책도 필요하다.

확정된 사용자 범위와 개발용 구현 경계:
1. 기존 WorldEncounter 필드 적 처치가 첫 소비자다. 개발 전용 configurable XP 보상으로 검증할 수 있으며 운영 경제 수치는 별도다.
2. 진행 중 합류, 서버 인정 유효 피해 기여, 연결 종료 뒤 회차 기여 유지, 재접속·재요청 포함 캐릭터별 회차당 1회 보상은 사용자 확정 범위다. 임의의 파티 공유·추가 최소 기여 조건을 만들지 않는다.
3. 수령 방식: 온라인 자동 지급인지, 재접속 후 청구인지. 오프라인 캐릭터 writer의 권한과 소유권을 별도로 정해야 한다.
4. 지속성 범위: 현재 단일 서버 개발 SQLite만인지, 서버 재시작 이후에도 회차·자격·지급 상태를 보존할지.

가장 작은 검증 가능한 연결은 대표 이벤트 하나의 완료 자격을 영속화하고, 온라인 캐릭터가 기존 SubmitEdit으로 보상을 청구하며 동일 트랜잭션에 claimed 표시를 남기는 방식이다. 응답 유실 후 재접속, 동일 키 다른 intent 거절, 재청구 한 번, 인벤토리 용량 부족, 세션 epoch 교체, 저장 실패/결과 불명, 회차 보관 한계 테스트가 필요하다. 해당 소비자 없이 금액 속성이나 추상 지급 서비스만 추가하는 것은 완료가 아니다.

## 후속 구현

개발 전용 설정형 XP 소비자와 캐릭터별 append-only claimed 원장을 기존 CommitProfile 원자 트랜잭션에 연결했다. 일반 SubmitEdit은 이벤트 지급 종류를 거절하고 실제 WorldEncounter의 확정 자격·고정 XP만 받는다. 연결 종료/재시도와 영수증 epoch 정리 뒤 중복 지급 방지 회귀를 작성했다. 네이티브 실행은 아직 하지 않았다. 상세와128회차/1024지급 한도, 프로세스 종료 시 미지급 자격 소실 경계는 [개발 이벤트 보상](../../Networking/Architecture/Development_World_Event_Rewards.md)을 따른다.
