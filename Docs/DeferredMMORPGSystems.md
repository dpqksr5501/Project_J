# Deferred MMORPG Systems

이 문서는 Project J의 현재 개발 단계에서 일부러 미뤄야 할 MMORPG 시스템을 기록한다.

현재 프로젝트는 기본 Locomotion, 캐릭터/GAS/장비 골격, MMO 식별자와 PlayerState/GameState 기반을 잡는 단계다. 아래 항목들은 장기적으로 중요하지만, 지금 구현하면 개발 속도를 크게 늦추거나 아직 확정되지 않은 설계에 코드를 묶을 가능성이 높다.

## 판단 기준

- 지금 구현하지 않아도 현재 Locomotion, 캐릭터, 기본 전투 실험을 막지 않는다.
- 엔진 기능만으로 끝나지 않고 운영, 백엔드, 배포, 보안, 데이터 정책이 함께 필요하다.
- 실제 플레이어 수, 컨텐츠 구조, 경제 규모, 라이브 운영 요구가 생긴 뒤 결정해야 한다.
- 지금은 인터페이스, ID 모델, 데이터 경계만 열어두고 본격 구현은 미룬다.

## 지금 미뤄야 할 시스템

### 진짜 메가서버

메가서버는 단순히 같은 맵을 여러 서버가 나눠 여는 기능이 아니다. 플레이어 배치, 파티/길드 affinity, 언어/지역 정책, map copy 선택, 인구 밀도 관리, cross-map world state가 함께 필요하다.

현재는 `WorldId`, `ZoneId`, `InstanceId`, `ChannelId` 같은 식별자만 유지한다. 실제 megaserver balancer는 필드 컨텐츠와 동시 접속 병목이 확인된 뒤 검토한다.

다시 검토할 시점:

- 같은 zone에 여러 channel/map copy가 필요해질 때
- 파티원이 서로 다른 copy에 배치되는 문제가 실제로 생길 때
- public event, world boss, 채널 선택 UI가 현재 구조를 압박할 때

### 서버 간 실시간 handoff

Unreal의 World Partition과 seamless travel은 대형 월드 스트리밍과 맵 전환을 돕지만, 상용 MMORPG식 다중 서버 경계 통과를 자동으로 제공하지 않는다. 실시간 handoff는 backend routing, ghost replication, authority switch, reconnect 정책, 실패 복구까지 포함한다.

현재는 `Project_JHandoverManager`를 스텁 또는 인터페이스 후보로만 둔다. 실제 구현은 여러 dedicated server process를 운용할 이유가 생긴 뒤 진행한다.

다시 검토할 시점:

- 단일 dedicated server가 감당 못 하는 open world 규모가 확정될 때
- zone boundary 이동을 seamless하게 만들어야 할 컨텐츠가 생길 때
- backend가 server node registry와 transfer ticket을 제공할 수 있을 때

### Iris 본격 채택

Iris는 대규모 replication 최적화를 목표로 하지만, 프로젝트 전체의 네트워크 정책과 디버깅 방식을 바꿀 수 있다. Replication Graph와도 함께 사용할 수 없으므로 초기에 성급하게 고르면 되돌리기 어렵다.

현재는 기본 replication, relevancy, dormancy, FastArray를 우선 사용한다. Iris 관련 placeholder는 실험용 메모 수준으로 둔다.

다시 검토할 시점:

- actor 수와 replication cost가 실제 병목으로 측정될 때
- RepGraph와 Iris 중 하나를 선택할 수 있을 만큼 네트워크 요구가 명확해질 때
- 별도 검증 브랜치에서 PIE/멀티클라이언트 테스트를 돌릴 여유가 있을 때

### RepGraph 커스텀

Replication Graph는 많은 actor와 connection을 다룰 때 유용하지만, 지금 단계에서는 기본 relevancy와 dormancy가 더 단순하고 안전하다. 커스텀 RepGraph는 컨텐츠별 replication bucket, distance policy, team/party visibility 같은 정책이 정해진 뒤 가치가 커진다.

현재는 Character, Equipment, nearby event objective처럼 명확한 대상에만 일반 replication/FastArray를 사용한다.

다시 검토할 시점:

- 몬스터, NPC, public event actor 수가 크게 늘어날 때
- Net profile에서 replication list 구성 비용이 눈에 띄게 커질 때
- distance, party, combat relevance 정책이 컨텐츠 기준으로 확정될 때

### 경매장 매칭 엔진

경매장이나 중앙거래소는 Unreal actor 시스템이 아니라 backend service와 economy ledger가 중심이다. 가격 제한, 세금, 거래 가능 여부, 중복 지급 방지, rollback, 감사 로그가 필요하다.

현재는 item instance GUID, inventory slot, wallet/ledger 개념만 준비한다. 실제 marketplace matching은 뒤로 미룬다.

다시 검토할 시점:

- 플레이어 간 거래 또는 listing UI를 실제로 만들 때
- currency ledger와 item ownership backend가 준비될 때
- exploit 방지를 위한 idempotency key와 audit log 정책이 잡힐 때

### GM 백오피스

GM 포털, CS case 관리, 제재 UI, 유저 검색, 아이템 지급/회수 도구는 라이브 운영 단계의 시스템이다. 지금은 gameplay 코드보다 운영 이벤트 hook과 structured log가 더 중요하다.

현재는 remote log, admin audit log 후보, debug dump 정도만 유지한다.

다시 검토할 시점:

- 외부 플레이테스트 규모가 커져 운영 이슈 추적이 필요할 때
- 계정 제재, 보상 회수, 수동 복구가 실제로 필요해질 때
- backend persistence가 운영 도구와 연결될 준비가 되었을 때

### 정식 안티치트

상용 anti-cheat SDK는 마지막 방어선에 가깝다. 먼저 서버 권위 이동/전투/아이템/통화 경계가 분명해야 한다. 권한 경계가 약한 상태에서 제품만 붙이면 핵심 exploit을 막기 어렵다.

현재는 서버만 보상, 아이템, 통화, 전투 결과를 확정한다는 원칙을 유지한다. RPC sanity check와 authoritative GAS path를 우선한다.

다시 검토할 시점:

- 외부 테스트에서 치트/변조 위협이 실제로 의미 있는 규모가 될 때
- movement validation, hit validation, economy audit가 기본적으로 갖춰졌을 때
- 배포 플랫폼과 anti-cheat SDK 요구사항이 확정될 때

### 대규모 경제/하우징/길드 시스템

경제, 하우징, 길드는 컨텐츠 기능처럼 보이지만 실제로는 persistence, social graph, permission, placement validation, audit, server policy가 필요한 큰 도메인이다.

현재는 schema 초안과 ID 경계만 잡는다. 실제 구현은 기본 캐릭터 성장, 인벤토리, 파티, 간단한 인스턴스 컨텐츠가 안정된 뒤 진행한다.

다시 검토할 시점:

- guild membership과 party/social feature가 실제 플레이 흐름에 필요할 때
- housing placement, ownership, decoration persistence가 컨텐츠 목표로 확정될 때
- 경제 루프와 보상/소모처가 충분히 정의될 때

## 지금 유지해야 할 최소 기반

- `AccountId`, `CharacterId`, `WorldId`, `ZoneId`, `InstanceId`, `ChannelId` 같은 식별자
- 서버 권위 원칙: 이동은 CMC, 전투/스킬은 GAS, 보상/아이템/통화는 서버와 backend가 확정
- 정의 데이터와 상태 데이터 분리: DataAsset/PrimaryDataAsset은 정의, Actor/Component/FastArray/backend row는 상태
- `PlayerState`에는 공개 가능한 플레이어 메타만 둔다
- `GameState`에는 모든 클라이언트가 알아야 하는 얇은 월드/인스턴스 상태만 둔다
- 개발 단계에서는 debug dump, structured log, PIE 네트워크 테스트를 우선한다

## 설계 메모

위 시스템들은 중요하지 않아서 미루는 것이 아니다. 오히려 장기적으로 중요하기 때문에, 현재의 작은 프로토타입 코드에 성급히 묶지 않기 위해 미룬다.

지금은 경계와 이름을 잘 남기는 것이 구현보다 더 가치 있다. 실제 컨텐츠가 생기고 병목이나 운영 요구가 관측되면, 그때 이 문서를 기준으로 어떤 시스템을 먼저 당겨올지 다시 결정한다.
