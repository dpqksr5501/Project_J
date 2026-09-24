# MMORPG 콘텐츠 확장 카탈로그

205개 연결 항목. **구현 완료 목록이 아니다.** 공통 기반과 미래 콘텐츠의 의존성·주 상태 소유자를 정의한다.

기존 구현 여부, 연구 출처, 권한·저장·스레드 규칙은 [설계 보고서](MMO_Foundation_2026-09-12.md)를 따른다. 상태 소유자는 주 aggregate 범위이며, 거래처럼 여러 소유자가 참여할 수 있다. 모든 항목은 기본적으로 비활성 확장 계약이다. Foundation 항목도 실제 구현 범위는 설계 보고서에서 구분한다.

## Foundation

| ID | 기능 | 주 상태 소유자 | 필요 계약 |
|---|---|---|---|
| `Foundation.Identity` | 안정 식별자 | Service | — |
| `Foundation.Access` | 권한·소유권 검증 계약 | Service | Foundation.Identity |
| `Foundation.Persistence` | 버전·저장 계약 | Service | Foundation.Identity |
| `Foundation.Commands` | 요청 수명·소유 단위 조정 | Service | Foundation.Access |
| `Foundation.Events` | 이벤트·중복 소비 계약 | Service | Foundation.Identity |
| `Foundation.Clock` | 서버 시간·리셋 기준 | Service | — |
| `Foundation.Transport` | 외부 서비스 연결 계약 | Service | Foundation.Commands |
| `Foundation.Observability` | 계측·운영 추적 | Service | Foundation.Identity |

## Identity

| ID | 기능 | 주 상태 소유자 | 필요 계약 |
|---|---|---|---|
| `Identity.Account` | 계정 프로필 | Account | Foundation.Access, Foundation.Persistence |
| `Identity.Characters` | 캐릭터 슬롯·생성 | Account | Foundation.Access, Foundation.Persistence |
| `Identity.Roster` | 계정 공유 성장 | Account | Foundation.Access, Foundation.Persistence |
| `Identity.Entitlements` | 이용 권한 | Account | Foundation.Access, Foundation.Persistence |
| `Identity.Session` | 접속 세션·재접속 | Account | Foundation.Access, Foundation.Persistence |
| `Identity.Appearance` | 외형·이름 변경 | Account | Foundation.Access, Foundation.Persistence |
| `Identity.Preferences` | 계정 설정 | Account | Foundation.Access, Foundation.Persistence |
| `Identity.Returnee` | 복귀 지원 | Account | Foundation.Access, Foundation.Persistence |

## Items

| ID | 기능 | 주 상태 소유자 | 필요 계약 |
|---|---|---|---|
| `Items.Inventory` | 인벤토리 | Character | Foundation.Commands, Foundation.Persistence |
| `Items.Equipment` | 장비 장착 | Character | Foundation.Commands, Foundation.Persistence |
| `Items.Storage` | 창고 | Character | Foundation.Commands, Foundation.Persistence |
| `Items.Loot` | 드롭·획득 권리 | Character | Foundation.Commands, Foundation.Persistence |
| `Items.Enhancement` | 강화·실패 누적 | Character | Foundation.Commands, Foundation.Persistence, Economy.Wallet |
| `Items.Durability` | 내구도·수리 | Character | Foundation.Commands, Foundation.Persistence |
| `Items.Sockets` | 보석·각인 | Character | Foundation.Commands, Foundation.Persistence |
| `Items.Affixes` | 옵션 재설정 | Character | Foundation.Commands, Foundation.Persistence |
| `Items.Binding` | 귀속·거래 제한 | Character | Foundation.Commands, Foundation.Persistence |
| `Items.Salvage` | 분해·추출 | Character | Foundation.Commands, Foundation.Persistence |
| `Items.Loadouts` | 장비 프리셋 | Character | Foundation.Commands, Foundation.Persistence |
| `Items.Expiration` | 기간제 아이템 | Character | Foundation.Commands, Foundation.Persistence |

## Economy

| ID | 기능 | 주 상태 소유자 | 필요 계약 |
|---|---|---|---|
| `Economy.Wallet` | 재화·원장 | Account | Foundation.Commands, Foundation.Persistence |
| `Economy.Vendor` | 상점 구매·판매 | Account | Foundation.Commands, Foundation.Persistence, Economy.Wallet, Items.Inventory |
| `Economy.Trade` | 개인 거래 | Account | Foundation.Commands, Foundation.Persistence, Economy.Escrow, Items.Binding |
| `Economy.Market` | 거래소·경매 | Account | Foundation.Commands, Foundation.Persistence, Economy.Escrow, Items.Binding |
| `Economy.Mail` | 우편·첨부 수령 | Account | Foundation.Commands, Foundation.Persistence, Economy.RewardClaims, Items.Inventory |
| `Economy.Escrow` | 대금·아이템 보관 | Account | Foundation.Commands, Foundation.Persistence |
| `Economy.BuyOrders` | 구매 예약 | Account | Foundation.Commands, Foundation.Persistence |
| `Economy.Taxes` | 수수료·세금 | Account | Foundation.Commands, Foundation.Persistence |
| `Economy.Buyback` | 재구매 | Account | Foundation.Commands, Foundation.Persistence |
| `Economy.CraftingOrders` | 제작 의뢰 | Account | Foundation.Commands, Foundation.Persistence |
| `Economy.RewardClaims` | 보상 수령 | Account | Foundation.Commands, Foundation.Persistence |
| `Economy.Gift` | 선물·수신 제한 | Account | Foundation.Commands, Foundation.Persistence |

## Social

| ID | 기능 | 주 상태 소유자 | 필요 계약 |
|---|---|---|---|
| `Social.Party` | 파티 | Group | Foundation.Access, Foundation.Persistence, Foundation.Events |
| `Social.RaidGroup` | 공격대 편성 | Group | Foundation.Access, Foundation.Persistence, Foundation.Events |
| `Social.Guild` | 길드·역할·권한 | Group | Foundation.Access, Foundation.Persistence, Foundation.Events |
| `Social.Alliance` | 길드 연합 | Group | Foundation.Access, Foundation.Persistence, Foundation.Events |
| `Social.Friends` | 친구·접속 상태 | Group | Foundation.Access, Foundation.Persistence, Foundation.Events |
| `Social.Blocklist` | 차단·무시 | Group | Foundation.Access, Foundation.Persistence, Foundation.Events |
| `Social.Invitations` | 초대·수락 | Group | Foundation.Access, Foundation.Persistence, Foundation.Events |
| `Social.Recruitment` | 모집·지원 | Group | Foundation.Access, Foundation.Persistence, Foundation.Events |
| `Social.GuildBank` | 길드 창고 | Group | Foundation.Access, Foundation.Persistence, Foundation.Events, Social.Guild, Items.Storage |
| `Social.GuildProjects` | 길드 연구·공동 목표 | Group | Foundation.Access, Foundation.Persistence, Foundation.Events |
| `Social.Mentorship` | 멘토·사제 | Group | Foundation.Access, Foundation.Persistence, Foundation.Events |
| `Social.Relationships` | 유저 관계 | Group | Foundation.Access, Foundation.Persistence, Foundation.Events |

## Combat

| ID | 기능 | 주 상태 소유자 | 필요 계약 |
|---|---|---|---|
| `Combat.Abilities` | 스킬·자원 | World | Foundation.Access, Foundation.Events |
| `Combat.Damage` | 피해·치유 | World | Foundation.Access, Foundation.Events |
| `Combat.Status` | 상태 이상·면역 | World | Foundation.Access, Foundation.Events |
| `Combat.Threat` | 위협도·어그로 | World | Foundation.Access, Foundation.Events |
| `Combat.Death` | 사망·부활 | World | Foundation.Access, Foundation.Events |
| `Combat.Assist` | 기여도·지원 판정 | World | Foundation.Access, Foundation.Events |
| `Combat.Rulesets` | 전투 규칙 세트 | World | Foundation.Access, Foundation.Events |
| `Combat.Duels` | 결투 | World | Foundation.Access, Foundation.Events |
| `Combat.Training` | 훈련장·허수아비 | World | Foundation.Access, Foundation.Events |
| `Combat.Spectator` | 관전 | World | Foundation.Access, Foundation.Events |

## Progression

| ID | 기능 | 주 상태 소유자 | 필요 계약 |
|---|---|---|---|
| `Progression.Experience` | 레벨·경험치 | Character | Foundation.Persistence, Foundation.Events |
| `Progression.Talents` | 특성·스킬 트리 | Character | Foundation.Persistence, Foundation.Events |
| `Progression.Classes` | 전직·전문화 | Character | Foundation.Persistence, Foundation.Events |
| `Progression.Quests` | 분기·연속 퀘스트 | Character | Foundation.Persistence, Foundation.Events |
| `Progression.Objectives` | 목표 진행 | Character | Foundation.Persistence, Foundation.Events |
| `Progression.Achievements` | 업적 | Character | Foundation.Persistence, Foundation.Events |
| `Progression.Reputation` | 평판·세력 | Character | Foundation.Persistence, Foundation.Events |
| `Progression.Unlocks` | 콘텐츠 해금 | Character | Foundation.Persistence, Foundation.Events |
| `Progression.Codex` | 지식·도감 | Character | Foundation.Persistence, Foundation.Events |
| `Progression.AccountMilestones` | 계정 공유 업적 | Character | Foundation.Persistence, Foundation.Events |
| `Progression.Catchup` | 후발 주자 성장 보정 | Character | Foundation.Persistence, Foundation.Events |

## PvE

| ID | 기능 | 주 상태 소유자 | 필요 계약 |
|---|---|---|---|
| `PvE.Dungeons` | 던전 | World | Combat.Rulesets, Foundation.Clock |
| `PvE.Raids` | 레이드 | World | Combat.Rulesets, Foundation.Clock |
| `PvE.WorldBosses` | 월드 보스 | World | Combat.Rulesets, Foundation.Clock |
| `PvE.PublicEvents` | 공개 협동 이벤트 | World | Combat.Rulesets, Foundation.Clock |
| `PvE.SoloInstances` | 솔로 인스턴스 | World | Combat.Rulesets, Foundation.Clock |
| `PvE.Scaling` | 인원·레벨 스케일링 | World | Combat.Rulesets, Foundation.Clock |
| `PvE.Affixes` | 주간 변형 규칙 | World | Combat.Rulesets, Foundation.Clock |
| `PvE.Lockouts` | 입장·보상 귀속 | World | Combat.Rulesets, Foundation.Clock, Foundation.Persistence |
| `PvE.Challenges` | 도전·시간 제한 | World | Combat.Rulesets, Foundation.Clock |
| `PvE.CompanionSupport` | NPC 파티 지원 | World | Combat.Rulesets, Foundation.Clock |
| `PvE.Hunting` | 현상 수배·사냥 의뢰 | World | Combat.Rulesets, Foundation.Clock |

## PvP

| ID | 기능 | 주 상태 소유자 | 필요 계약 |
|---|---|---|---|
| `PvP.Arena` | 투기장 | World | Combat.Rulesets, Foundation.Clock |
| `PvP.Battlegrounds` | 전장 | World | Combat.Rulesets, Foundation.Clock |
| `PvP.OpenWorld` | 필드 PvP | World | Combat.Rulesets, Foundation.Clock |
| `PvP.Siege` | 공성전 | World | Combat.Rulesets, Foundation.Clock |
| `PvP.Territory` | 영토·거점 점령 | World | Combat.Rulesets, Foundation.Clock |
| `PvP.GuildWars` | 길드 전쟁 | World | Combat.Rulesets, Foundation.Clock |
| `PvP.Matchmaking` | 매칭·대기열 | World | Combat.Rulesets, Foundation.Clock |
| `PvP.Rating` | 레이팅·랭킹 | World | Combat.Rulesets, Foundation.Clock, Foundation.Persistence |
| `PvP.Seasons` | 경쟁 시즌 | World | Combat.Rulesets, Foundation.Clock |
| `PvP.Tournaments` | 대회·대진 | World | Combat.Rulesets, Foundation.Clock |
| `PvP.Crime` | 범죄·성향·현상금 | World | Combat.Rulesets, Foundation.Clock |
| `PvP.FactionCampaign` | 진영 공동 전쟁 | World | Combat.Rulesets, Foundation.Clock |

## Crafting

| ID | 기능 | 주 상태 소유자 | 필요 계약 |
|---|---|---|---|
| `Crafting.Recipes` | 제작법·숙련도 | Character | Items.Inventory, Foundation.Persistence |
| `Crafting.Processing` | 가공 | Character | Items.Inventory, Foundation.Persistence |
| `Crafting.Cooking` | 요리 | Character | Items.Inventory, Foundation.Persistence |
| `Crafting.Alchemy` | 연금 | Character | Items.Inventory, Foundation.Persistence |
| `Crafting.Quality` | 품질·부산물 | Character | Items.Inventory, Foundation.Persistence |
| `Crafting.Research` | 연구·발견 | Character | Items.Inventory, Foundation.Persistence |
| `Crafting.Stations` | 제작대·시설 | Character | Items.Inventory, Foundation.Persistence |
| `Crafting.Queues` | 제작 예약 | Character | Items.Inventory, Foundation.Persistence |
| `Crafting.Specialization` | 생산 전문화 | Character | Items.Inventory, Foundation.Persistence |

## Life

| ID | 기능 | 주 상태 소유자 | 필요 계약 |
|---|---|---|---|
| `Life.Gathering` | 채집 | Character | Items.Inventory, Foundation.Clock |
| `Life.Mining` | 채광 | Character | Items.Inventory, Foundation.Clock |
| `Life.Logging` | 벌목 | Character | Items.Inventory, Foundation.Clock |
| `Life.Fishing` | 낚시 | Character | Items.Inventory, Foundation.Clock |
| `Life.Hunting` | 수렵 | Character | Items.Inventory, Foundation.Clock |
| `Life.Farming` | 농사 | Character | Items.Inventory, Foundation.Clock |
| `Life.Breeding` | 교배·육성 | Character | Items.Inventory, Foundation.Clock |
| `Life.Trading` | 무역·운송 | Character | Items.Inventory, Foundation.Clock |
| `Life.Workers` | 일꾼·노드 생산 | Character | Items.Inventory, Foundation.Clock |
| `Life.Sailing` | 항해·물물교환 | Character | Items.Inventory, Foundation.Clock |
| `Life.Archaeology` | 고고학·발굴 | Character | Items.Inventory, Foundation.Clock |

## World

| ID | 기능 | 주 상태 소유자 | 필요 계약 |
|---|---|---|---|
| `World.Zones` | 지역·채널 | World | Foundation.Events, Foundation.Clock |
| `World.Instances` | 인스턴스 수명 | World | Foundation.Events, Foundation.Clock |
| `World.Weather` | 날씨·환경 | World | Foundation.Events, Foundation.Clock |
| `World.DayNight` | 낮밤·시간 | World | Foundation.Events, Foundation.Clock |
| `World.Spawns` | 스폰·자원 재생 | World | Foundation.Events, Foundation.Clock |
| `World.Phasing` | 퀘스트 위상 | World | Foundation.Events, Foundation.Clock |
| `World.Discovery` | 탐험·지도 해금 | World | Foundation.Events, Foundation.Clock |
| `World.DynamicEvents` | 동적 월드 이벤트 | World | Foundation.Events, Foundation.Clock |
| `World.Hazards` | 지역 위험·환경 효과 | World | Foundation.Events, Foundation.Clock |
| `World.Interaction` | 상호작용·사용 권리 | World | Foundation.Events, Foundation.Clock |
| `World.PublicObjectives` | 서버 공동 목표 | World | Foundation.Events, Foundation.Clock |

## Travel

| ID | 기능 | 주 상태 소유자 | 필요 계약 |
|---|---|---|---|
| `Travel.Mounts` | 탈것 | Character | World.Zones, Foundation.Access |
| `Travel.Vehicles` | 다인승 차량 | Character | World.Zones, Foundation.Access |
| `Travel.Ships` | 선박 | Character | World.Zones, Foundation.Access |
| `Travel.Stable` | 마구간·보관 | Character | World.Zones, Foundation.Access |
| `Travel.Routes` | 자동 이동·교통편 | Character | World.Zones, Foundation.Access |
| `Travel.Teleport` | 순간이동·귀환 | Character | World.Zones, Foundation.Access |
| `Travel.FastTravel` | 거점 이동 | Character | World.Zones, Foundation.Access |
| `Travel.Handover` | 서버·채널 이동 | Character | World.Zones, Foundation.Access, Platform.Routing |
| `Travel.Passengers` | 탑승자·좌석 권한 | Character | World.Zones, Foundation.Access |

## Housing

| ID | 기능 | 주 상태 소유자 | 필요 계약 |
|---|---|---|---|
| `Housing.Homes` | 개인 주거 | Account | Foundation.Persistence, Foundation.Access |
| `Housing.GuildHalls` | 길드 거점 | Account | Foundation.Persistence, Foundation.Access |
| `Housing.Neighborhood` | 주거 구역 | Account | Foundation.Persistence, Foundation.Access |
| `Housing.Placement` | 가구 배치·충돌 | Account | Foundation.Persistence, Foundation.Access, Housing.Permissions |
| `Housing.Permissions` | 방문·편집 권한 | Account | Foundation.Persistence, Foundation.Access |
| `Housing.Plots` | 토지·임대 | Account | Foundation.Persistence, Foundation.Access |
| `Housing.Decor` | 장식 수집 | Account | Foundation.Persistence, Foundation.Access |
| `Housing.Facilities` | 주거 생산 시설 | Account | Foundation.Persistence, Foundation.Access |
| `Housing.Blueprints` | 배치 설계도 | Account | Foundation.Persistence, Foundation.Access |
| `Housing.Islands` | 개인 섬·영지 | Account | Foundation.Persistence, Foundation.Access |

## Collections

| ID | 기능 | 주 상태 소유자 | 필요 계약 |
|---|---|---|---|
| `Collections.Wardrobe` | 외형 수집 | Account | Foundation.Persistence, Foundation.Events |
| `Collections.Transmog` | 형상 변환·염색 | Account | Foundation.Persistence, Foundation.Events |
| `Collections.Pets` | 애완동물 수집 | Account | Foundation.Persistence, Foundation.Events |
| `Collections.MountJournal` | 탈것 도감 | Account | Foundation.Persistence, Foundation.Events |
| `Collections.Titles` | 칭호 | Account | Foundation.Persistence, Foundation.Events |
| `Collections.Emotes` | 감정 표현 | Account | Foundation.Persistence, Foundation.Events |
| `Collections.Music` | 악보·연주 | Account | Foundation.Persistence, Foundation.Events |
| `Collections.Toys` | 장난감 | Account | Foundation.Persistence, Foundation.Events |
| `Collections.Treasures` | 보물·희귀 수집 | Account | Foundation.Persistence, Foundation.Events |
| `Collections.PhotoAlbum` | 사진·전시 | Account | Foundation.Persistence, Foundation.Events |

## Companions

| ID | 기능 | 주 상태 소유자 | 필요 계약 |
|---|---|---|---|
| `Companions.Pets` | 펫 행동·획득 보조 | Character | Foundation.Persistence, Combat.Rulesets |
| `Companions.Mercenaries` | 용병 | Character | Foundation.Persistence, Combat.Rulesets |
| `Companions.Training` | 동료 육성 | Character | Foundation.Persistence, Combat.Rulesets |
| `Companions.Commands` | 동료 명령 | Character | Foundation.Persistence, Combat.Rulesets |
| `Companions.Bond` | 친밀도 | Character | Foundation.Persistence, Combat.Rulesets |
| `Companions.Expeditions` | 동료 파견 | Character | Foundation.Persistence, Combat.Rulesets |
| `Companions.PetBattles` | 펫 대전 | Character | Foundation.Persistence, Combat.Rulesets |

## Community

| ID | 기능 | 주 상태 소유자 | 필요 계약 |
|---|---|---|---|
| `Community.Chat` | 지역·파티·길드 채팅 | Account | Social.Blocklist, Foundation.Transport |
| `Community.Voice` | 음성 서비스 연결 | Account | Social.Blocklist, Foundation.Transport |
| `Community.LookingForGroup` | 파티 찾기 | Account | Social.Blocklist, Foundation.Transport |
| `Community.Calendar` | 행사·일정 | Account | Social.Blocklist, Foundation.Transport |
| `Community.Bulletin` | 게시판 | Account | Social.Blocklist, Foundation.Transport |
| `Community.Minigames` | 미니게임 | Account | Social.Blocklist, Foundation.Transport |
| `Community.Racing` | 경주 | Account | Social.Blocklist, Foundation.Transport |
| `Community.Cards` | 카드·보드게임 | Account | Social.Blocklist, Foundation.Transport |
| `Community.Performance` | 공연·합주 | Account | Social.Blocklist, Foundation.Transport |
| `Community.UserEvents` | 유저 행사 | Account | Social.Blocklist, Foundation.Transport |
| `Community.Reports` | 신고·증거 참조 | Account | Social.Blocklist, Foundation.Transport |

## LiveOps

| ID | 기능 | 주 상태 소유자 | 필요 계약 |
|---|---|---|---|
| `LiveOps.Calendar` | 운영 일정 | Service | Foundation.Clock, Foundation.Persistence, Foundation.Events |
| `LiveOps.SeasonPass` | 시즌 패스 | Service | Foundation.Clock, Foundation.Persistence, Foundation.Events, Economy.RewardClaims |
| `LiveOps.Attendance` | 출석·접속 보상 | Service | Foundation.Clock, Foundation.Persistence, Foundation.Events |
| `LiveOps.DailyWeekly` | 일간·주간 리셋 | Service | Foundation.Clock, Foundation.Persistence, Foundation.Events |
| `LiveOps.Holiday` | 축제·기간 이벤트 | Service | Foundation.Clock, Foundation.Persistence, Foundation.Events |
| `LiveOps.WorldGoals` | 서버 누적 목표 | Service | Foundation.Clock, Foundation.Persistence, Foundation.Events |
| `LiveOps.FeatureFlags` | 기능 개방·점진 적용 | Service | Foundation.Clock, Foundation.Persistence, Foundation.Events |
| `LiveOps.Compensation` | 장애 보상 | Service | Foundation.Clock, Foundation.Persistence, Foundation.Events |
| `LiveOps.Campaigns` | 캠페인·프로모션 | Service | Foundation.Clock, Foundation.Persistence, Foundation.Events |
| `LiveOps.ContentVersion` | 정의 데이터 버전 | Service | Foundation.Clock, Foundation.Persistence, Foundation.Events |
| `LiveOps.Leaderboards` | 랭킹 집계 | Service | Foundation.Clock, Foundation.Persistence, Foundation.Events |

## Platform

| ID | 기능 | 주 상태 소유자 | 필요 계약 |
|---|---|---|---|
| `Platform.Authentication` | 인증 서비스 연결 | Service | Foundation.Transport, Foundation.Access |
| `Platform.ServerDirectory` | 서버 검색 | Service | Foundation.Transport, Foundation.Access |
| `Platform.Admission` | 접속 대기·수용량 | Service | Foundation.Transport, Foundation.Access |
| `Platform.Reconnect` | 재접속 복구 | Service | Foundation.Transport, Foundation.Access |
| `Platform.Presence` | 온라인 상태 | Service | Foundation.Transport, Foundation.Access |
| `Platform.Routing` | 샤드·지역 라우팅 | Service | Foundation.Transport, Foundation.Access |
| `Platform.Transfer` | 캐릭터 이전 | Service | Foundation.Transport, Foundation.Access |
| `Platform.Maintenance` | 점검·드레인 | Service | Foundation.Transport, Foundation.Access |
| `Platform.Localization` | 지역화 계약 | Service | Foundation.Transport, Foundation.Access |
| `Platform.Accessibility` | 접근성 설정 | Service | Foundation.Transport, Foundation.Access |

## Operations

| ID | 기능 | 주 상태 소유자 | 필요 계약 |
|---|---|---|---|
| `Operations.Audit` | 상태 변경 감사 | Service | Foundation.Observability, Foundation.Access |
| `Operations.Metrics` | 처리량·지연 계측 | Service | Foundation.Observability, Foundation.Access |
| `Operations.Tracing` | 요청 상관 추적 | Service | Foundation.Observability, Foundation.Access |
| `Operations.Moderation` | 제재·차단 | Service | Foundation.Observability, Foundation.Access |
| `Operations.Support` | 고객 지원 도구 | Service | Foundation.Observability, Foundation.Access |
| `Operations.Recovery` | 복구·재처리 | Service | Foundation.Observability, Foundation.Access |
| `Operations.Migration` | 스키마 마이그레이션 | Service | Foundation.Observability, Foundation.Access |
| `Operations.AbuseLimits` | 요청·경제 악용 제한 | Service | Foundation.Observability, Foundation.Access |
| `Operations.ConfigValidation` | 설정 검증 | Service | Foundation.Observability, Foundation.Access |
| `Operations.DataExport` | 운영 데이터 내보내기 | Service | Foundation.Observability, Foundation.Access |

