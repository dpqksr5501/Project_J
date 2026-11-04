# MMORPG 아키텍처 완료도·재사용 범위 점검 — 2026-11-04

## 판정

**최신 사용자 범위는167에서 마감했다.** 사용자는 현재 기반의 필수 수정·최종 검증/인계까지만 진행하고 아키텍처 작업을 종료하기로 합의했다. 이 문서의 장기 잔여·과거 active 표기는 보존 기록이며 새 구현을 자동 추가하는 지시가 아니다. 후속 서비스/콘텐츠/실제 규모 검증은 별도다.

**전체 MMORPG 요소가 AAA급으로 구현·통합·성능 검증까지 완료된 상태는 아니다.** 여러 기능군에서 공통 권위·실행·작업 예산·수명·복제·저장 기반과 실제 소비 경로를 구현했다. 이 기반을 새 콘텐츠에 재사용할 수 있지만, 임의의 캐릭터·애니메이션·스킬·맵을 추가하면 모든 기능과 최적화가 자동 적용된다는 의미는 아니다.

콘텐츠 전체 제작의 미완료를 아키텍처 결함으로 세지 않았다. 다만 경제의 다중 소유자 자산 이전, 서버 권위 저장·복원, 분산 writer, 실제 인스턴스·시즌 운영처럼 **새 콘텐츠 데이터만 추가해서 완성되지 않는 실행·통합 경계**도 남아 있다. 이를 카탈로그나 인터페이스의 존재로 완료 처리할 수 없다.

최초 점검은 기존 전체 감사와 129–157차의 관련 근거를 재사용하고 Source/Config의 주요 진입→실제 소비 경로를 대조한 구조 점검이다. 모든 함수·BP 그래프·에셋을 새로 전수 검사하거나 모든 플랫폼·기능을 실행했다는 뜻은 아니다. 최초 대조의 소스/설정/스크립트 등1259항목·host 포함 runtime39항목은157 최종 지문과 일치했다. 후속158–164차의 실제 변경·검증과 현재 기준의 재확인은 아래에 구분했다. 이번 전체 재점검은 기존 기능군 표·실행 근거와 현재 소스의 소비 경계를 재사용했다. 생산 Source/Config/에셋 변경과 새 빌드·엔진·부하·Cook 실행은 없다.

**이전 후속163–164:** 위 읽기 전용 점검 이후 활성128 상세와 완료64×1024 페이지, Format4 원자 저장/레거시 이관·owner 요약/32ID 저널 조회를 실제 Source에 연결했다.163의17 native와 Editor/Game이 통과했고164의 관련2 native·최종 Editor358/Game123도 경고/실패0이다. 실제2socket의32/32/1 조회·foreign PS 양성 대조와 개인 상태 비공개·Append66 뒤 Stale/재조회가 정상/지연손실 모두 native0×3·timeout0으로 통과했다. 현재1268 Source 항목·runtime39·보호28asset과164 최종 지문을 확인했다. 아래161–162의128 lifetime 제한과 설계 미적용 기록은 당시 상태다. 당시 남았던 XP+아이템 bundle 원자 실행은 아래165에서 보강했다. 운영 영속 보상·반복/계정/시즌 규칙·전체 성능은 미완료다. 상세와 원자료는 [현재 작업 계획의163–164차](../../Architecture/Planning/Project_J_MMO_Architecture_Work_2026-10-26.md)를 따른다.

**165 적용 전 전체 입력 재확인:** 2026-11-04 06:40 KST에 기존 `Source-Provenance.ps1` 수집·비교로 소스/설정/스크립트 등1268항목과 프로젝트 runtime38+엔진 host1이 `Saved/ArchitectureBatch164QuestHistoryWire/FinalWorkspace164.json` 전체 지문과 일치함을 확인했다. `AfterAssets158.json`의 보호28입력도 변경0이다. 따라서164까지의 관련 근거를 재사용한다. 이 비교 자체는 새 부하·시각 품질·운영 검증이 아니다. 신뢰 세션/Standalone 저장/Repository/카탈로그, 실제 Quest Claim·제작 포장기·Encounter·동료, NPC 조립·animation worker·Ability·판정·표현·WP·AssetManager 진입을 대조했다. NPC의158 조립 보강과163–164 완료 이력 개선은 현재 잔여로 다시 세지 않았다.

**165는 실제 Source 적용·집중 검증을 마쳤다.** 선택 RewardItems를 canonical 등록 정의와 연결하고 기존 제작 포장기의 PlanGrant를 native 권위/Standalone 영속 Claim이 공유한다. XP+아이템+완료를 함께 설치/커밋하며 빈 보상의 기존 해시·형식·native 잠금을 보존한다. 마지막 ASC getter 뒤 exact binding/참가자·규칙을 검사하고 모든 값을 설치한 뒤 알리며, callback 신원 교체는 후속 이전 알림을 중단한다. 지급 후 퇴역한 결과는 원래 profile 재열기로 같은 item IDs를 복구한다. 최종 Editor363/Game124 full0, 고유12개 관련 사례는 최초 유효6+후속 유효5+fixture 수정 후1이며 테스트 경고0이다. 첫 진단 hook 시점 assert와 기본 quest 빈 목록 전제 실패는 보존했고 생산16파일은 세 실행 사이 불변이다. 최종1274 Source 항목/runtime39·보호28asset 변경0과 native 전후/최종 Editor runtime을 대조했다. 근거는 `Saved/ArchitectureBatch165QuestRewards/Verification165.json` SHA0be975ff4fc1037426d6c4a21d9b6325e4d49b7ff9222d7bc882a9628b14c4d9, `FinalWorkspace165.json` SHA0233a0a52ab7d7e1619743675374c7e6a54618e740a280447576cfa8f410d429다. 이전 Saved V1/V2/설계 문구는 당시 미적용 상태로 보존한다. 네트워크 서버 영속 보상·전체 성능·새 release 완료로 세지 않는다.


**후속167의 필수 마감:** 기존 node start/visit을 typed hit 요청에 실어 target Query를 유지하면서 source 실행 시계만 결합했다. 서버 owner connection 기반의 제한된 lead·현재 key/visit/공간/열린 packet·최초 clock 고정을 검증하며 두 RPC는 하나의 예산/효과 본문을 공유한다. 고정75ms/loss0의 실제 전용 서버+두 NullRHI client·native 이동 대상에서 수락 Seq1/key4·authority 접촉0·GE1/HP1000→990·정리를 확인했고, 원 trace에서 SSRRequest⊇ApplyEffect⊇ApplySpec 중첩을 검증했다. 과거 정규식의 false는 원자료를 보존한 채 같은 functional gate의 offline 재판독으로 구분했다. 고유7 native(기존 유효6+최종1)·파서27개 성공, 최종 Editor367/Game126 full0·Source1274/runtime39·보호28asset 변경0이다. 테스트 준비 순서의 실패2회는 보존했으며 wire 뒤 생산 코드는 불변이다. 근거 `Saved/ArchitectureBatch167ExecutionClock/Verification167.json` SHA29186b2e0778d8ead2010d61ab79eb3cf0b483abf2fee9958188e99f0b94d48f, `FinalWorkspace167.json` SHA01a175a5d479440d3037bfc536da35c62c7610e8e39f3275728ab233992a7f1f. 이는 해당 실행 시계 불일치의 원인 수정이며 exact rendered phase·모든 latency/geometry·전체 A09/CCU/GPU 품질 승인은 아니다.

**이전166의 A09 진단 경계:** 기존 실제 LMB/GAS/몽타주·native 이동 대상을 고정75ms/loss0에서 관측하고, 읽기 전용 거절 로그를 보강해 같은 trigger를 재현했다. owner 접촉6/5·authority0·GE0/HP1000 유지이며 두 실행 모두 정상 native0×3·정리 완료지만 strict 결과는 실패다. 진단 request Seq1/key4의 query42.582311500280966은 active start42.012281108647585 이후이나 첫 authority packet42.678044106811285보다95.7326ms 앞서 기존50ms head 범위 밖이었다. 이 실패는 geometry 이전의 source packet 선택까지 좁혀졌고 당시 accepted SSR이나 clock/기하 해결을 입증하지 않았다. 코드 변경은 기존 Verbose 진단 cpp1곳, Editor364/Game125 full0·Source1274/runtime39·보호28asset 변경0·wire 전후/최종 지문을 대조했다. 이 원래 trigger는 위167에서 수정·검증했다. `Saved/ArchitectureBatch166AutonomousSSR/Verification166.json` SHA3e707a21d0b402ba351dc8317df82dcb74ed5602f75a077a6af261ab7b531f05와 실패 원자료를 보존한다. 실제 서버 영속/분산 권위·A03/A07·전체 부하와 release 잔여는 최신 보류 범위를 따른다.

## 완료를 구분하는 기준

| 구분 | 현재 판단 |
| --- | --- |
| MMORPG 전체를 고려한 책임·의존성 지도 | 존재한다.20개 domain·205항목은 확장 계약이다. |
| 여러 기능군의 실제 공통 실행 기반 | 존재하며 선택한 실제 콘텐츠·패키지·소켓 소비를 연결했다. |
| 모든 domain의 실행·서비스·실패 복구 완결 | 미완료다. 계약 수준·부분 구현·운영 미연결이 함께 존재한다. |
| 규칙에 맞게 연결한 새 콘텐츠의 공통 정책 재사용 | 가능하다. 해당 기반 클래스·컴포넌트·정의·등록 경로를 사용해야 한다. |
| 임의의 미래 에셋·로직의 자동 기능화/자동 최적화 | 보장되지 않는다. 제작·호환·비용·실행 범위의 확인이 필요하다. |
| 대규모 환경의 FPS·CCU·지연·메모리·품질 수용 | 현재 근거로 전체 인증할 수 없다. 상한 설정값과 실제 수용량은 다르다. |

[FeatureCatalog](../../../Source/Project_JMMO/Public/MMO/FeatureCatalog.h)는 ID·domain·소유자·의존성을 기술하고, [FoundationSubsystem](../../../Source/Project_J/MMO/Project_JMMOFoundationSubsystem.cpp)은 그 의존 순서를 해석한다. 실행기나 서비스의 자동 설치를 제공하지 않는다. [205개 카탈로그](../../Architecture/Extensions/MMO_Content_Catalog.md)도 구현 완료 목록이 아님을 명시한다.

## 전체 기능군 대조

`연결된 기반`은 실제 생산 진입과 소비가 있음을 뜻한다. 해당 기능군의 모든 제품 규칙·콘텐츠·규모 검증 완료를 뜻하지 않는다.

| 기능군 | 실제 연결된 기반 | 남은 아키텍처·통합 또는 입증 |
| --- | --- | --- |
| Foundation·공통 실행 | PS/Avatar 소유권, 값 snapshot→worker→최신성 재검증, 요청·grant·lease·취소, 작업량 제한·계측 | 임의 UObject/새 Blueprint 로직의 자동 병렬화는 없다. 각 새 작업이 기존 실행·예산 경로를 소비해야 한다. |
| Identity·계정·접속 | 호스트 내 session attempt/lease, 단일 현재 writer, 이전 명령 회수와 takeover | 실제 principal/캐릭터 소유권 verifier, 끊긴 접속의 영속 복원, 전역 분산 writer. 기본 trusted-session 모드는 opt-in이다. |
| Items·장비·인벤토리 | stable item instance, 소유자 전용 FastArray와 공개 외형 분리, grant·장착·소비·local edit | 운영 드롭/공유 수령 권리·창고·강화/옵션 등 규칙과 다중 소유자 자산 이동의 실제 소비. |
| Economy | CAS·receipt·원자 커밋·복구 등 재사용 저장 기반. 장시간 제작의 로컬 escrow는 실제 구현 | 범용 opaque payload 커밋은 재화 원장·거래소·경매·우편·양측 거래의 typed 실행/정산을 구현하지 않는다. |
| Social | in-memory 파티·길드 membership, leader·snapshot/tombstone·복제 투영 | 운영 영속·서버 간 권한·초대/역할·공격대·길드 창고·presence 등. |
| Community | 기존 소유권·소셜·UI 기반과 확장 계약 | 친구·차단·채팅·모집·신고의 실제 서비스/권한·운영 연동 전체. |
| Combat·스킬·능력치 | GAS·근접/몽타주/notify·GE·서버 hit 검증, canonical 경로와 실행 payload 수명.167의 bounded 실행 clock·실제 autonomous accepted SSR |167은 고정75ms/loss0·native 이동 대상 한 조건의 원인 수정/인과 검증이다. 모든 광역·투사체·마법·채널링·warped/aerial 판정과 모든 지연/렌더 시각을 일반화한 것은 아니다. |
| Progression·직업 | int64 XP, class/advancement 정의와 grant source, 버전 있는 로컬 진행 | 새 직업·특성 규칙의 consumer, 계정 공유 성장·환불/migration과 운영 저장 연결. |
| Quest·스토리 진행 | 실제 목표/추적·XP+아이템 bundle 확정, 활성/완료 이력 분리, Format4/receipt·복구·저널·owner 페이지 통신 | 실제 네트워크 서버 영속 보상과 반복/계정/시즌별 완료 범위·정의 변경 복구는 별도다. |
| PvE·보스·던전/레이드 | 중앙 Encounter/UTC 기간·cooldown, NPC 준비·행동/전투 연결 | 보스 phase·참가/기여·전리품·리셋·던전/인스턴스 수명과 재시작 복원 전체. |
| PvP·공성 | 공통 전투·이동·권위 기반 재사용 가능 | 결투/전장/공성·진영·승패·관전자·부활·랭크/시즌의 실제 규칙과 권위 통합. |
| Crafting | 즉시/장시간 제작 계획, 로컬 재료 escrow·환급·UTC 복원/수령 | 네트워크 서버 저장·공유 작업/제작 의뢰·운영 경제의 원자성/복구. |
| Life·채집 | 실제 노드·거리/상태 검증·예약·상호작용·진행/보상 연결 | 현재 영속 consumer의 Standalone 경계. 생활 종류별 실행과 공유 자원/재생·서버 영속성. |
| World | WorldDefinition/state·clock, 공간 index·Encounter 중앙 예산·비동기 준비 | 전체 월드의 활성 구역·Nav/물리·권위 지역 분할, 영속 자원/날씨·이벤트 복원과 실제 규모. |
| Travel·이동·탈것 | Mover 예측/NavMover 실제 경로, CMC 탈것/비행·탑승 복구, 목적지 permit·접지/캡슐 준비 | 물리 저빈도/Mass 인계, 임의 backend·다인승/탑승 전투, 비행/고속 도착과 월드 전체 준비/품질. |
| Housing | 소유자·의존성의 확장 계약과 재사용 아이템/월드 원시 기능 | 배치 권한·충돌/수정 delta·저장량·영지/임대·실제 소비 실행. |
| Collections | 퀘스트·아이템·성장 원시 기능 및 확장 계약 | 계정 업적·도감·칭호/해금의 권위 상태·사건 소비·공유/영속 통합. |
| Companions | 소유 아이템 기반 bounded 로드·소환·추적과 정확한 해제 | 전투 펫의 명령·전술·스킬·성장·수집 규칙과 서버 영속성 전체. |
| LiveOps·시즌 이벤트 | 서버 UTC 기간을 사용하는 Encounter admission과 진단 | 글로벌 시즌/보상/랭킹·기능 배포·운영 권한·영속 스케줄/재시작·감사 서비스. |
| Platform·제작 | 모듈 분리·AssetManager·선택 Data Validation·에셋/소스 provenance·제한 배포 통합 | 새 플랫폼·에셋 경로/종류의 등록/검증·현행 release/독립 Server·배포 호환성. |
| Operations | trace/CSV·native 종료·제한 HTTP·실패/timeout/취소·복구 계약 | 실제 인증/DB/호스팅, 상시 관측·경보·backup/restore·rollout/rollback·장기 soak/장애 훈련. |
| 애니메이션·렌더·VFX·오디오·UI | snapshot/proxy/worker·ABA/cloth 정책·표현 lease/큐/pool·Foley·이벤트 기반 UI/갱신 병합 | 모델·재질·리그·cloth·효과 자체의 비용, 원경 HLOD/VRAM·영향 범위·시각 품질·접근성 전체. |

실제 도메인 소유의 예는 [PlayerState의 컴포넌트 조립](../../../Source/Project_J/Game/Project_JPlayerState.cpp)이다. 이 실제 실행과 catalog만 있는 기능을 구분했다. [Repository](../../../Source/Project_JMMO/Public/MMO/Repository.h)의 일반 커밋을 특정 경제 서비스의 사업 규칙으로 해석하지 않았다. 현재 상태의 상세 소비 경로는 아래의 로컬 보조 감사에 기록했다.

## 새 에셋에 적용되는 조건

| 추가 대상 | 규칙을 맞추면 재사용되는 처리 | 에셋/기능별로 필요한 연결 |
| --- | --- | --- |
| 캐릭터·NPC·장비 | owner/Avatar·grant·작업 예산·비동기 취소·모듈러 메시/표현 수명 | 프로젝트 기반/컴포넌트, ASC 소유권, 캐릭터/장비 정의·등록, skeleton·pose mode·부착 socket·collision. |
| 애니메이션·다른 skeleton | 공통 상태 snapshot·proxy·지원되는 worker 계산/품질 정책 | AnimInstance/graph·BlendSpace·slots·curves·IK/retarget/profile·root motion 호환. NPC는 non-MM 경로를 유지한다. |
| 스킬·전투 클립 | GAS 실행/권위·승인/종료·hit/표현 기반 | grant/tag/style/combo/attack, montage/notify/socket/shape·피해 규칙. client cache-payload 사용 시 instanced 공통 Ability 기반·지원 정책이 필요하다. canonical 사용 시 source에 맞는 bake/validation이 필요하다. |
| VFX·오디오 | 등록된 presentation/Foley 경로의 큐·pool·lease·예산 | profile·cue·attach·loop/end·EffectType/concurrency·플랫폼 비용. 필수 경고는 제작자가 Purpose를 지정해야 한다. |
| 맵·지형·원경 | WorldDefinition·예측 source·도착 준비·취소·Encounter 실행 | WP/grid/layer·collision/Nav agent·실제 HLOD/LOD와 렌더 데이터·Cook 등록·고속 이동 경로의 품질/메모리 검증. |
| 네트워크·저장 도메인 | 해당 경로의 owner-only 복제·request/lease·schema/receipt·실패 처리 | Actor/PS/Ability 연동·relevancy/우선순위·서버 권위 DTO/저장 규칙·신원 검증. 새 RPC/변수를 만들었다고 자동으로 보호/저장되지 않는다. |

공통 로직을 콘텐츠마다 복제할 필요가 없다는 점은 재사용성의 성과다. 그러나 에셋 임포트만으로 rig·판정·Nav·HLOD를 만들거나, 모든 메시를 자동 인스턴싱하고 draw call/cloth/material 비용을 일정하게 만드는 범용 변환을 구현·검증한 것은 아니다. 프로젝트 경로를 우회한 Blueprint Tick, 임의 Niagara/Audio spawn, 외부 movement/AnimInstance도 자동으로 같은 예산에 들어오지 않는다.

### 확인된 조립·기본값 경계

1. **NPC의 필수 전투 조립은 점검 후 보강했다.**157 당시 [NPCAttack](../../../Source/Project_JCharacter/Private/Combat/Project_JGameplayAbility_NPCAttack.cpp)의 HitValidation 요구를 fixture가 별도로 보충하던 경계가 있었다. 후속158의 [StartCombatActions](../../../Source/Project_JCharacter/Private/Project_JNPCCharacter.cpp)는 HitValidation·Presentation을 GAS grant 전에 필요할 때 준비하고 기존 exact-one 컴포넌트·설정을 재사용한다. [실제 RenderedCrowd 소비자](../../../Source/Project_J/Testing/Project_JRenderedCrowdFixture.NPC.cpp)의 보충을 제거한 실행도 통과했다. optional profile·rig/graph/socket·공격 정의는 여전히 제작 계약이다. 기존 BP 전체가 고장났다는 판정이나 임의 신규 NPC의 모든 전투가 무설정으로 동작한다는 주장은 하지 않는다.
2. **GAS payload는 명시적 기반 계약이다.** [공통 Ability](../../../Source/Project_JGAS/Public/Project_JGameplayAbility.h)와 [실제 승인 경로](../../../Source/Project_JGAS/Private/Project_JAbilitySystemComponent.cpp)가 instanced·현재 실행·정책·세션을 확인한다. 일반 AbilitySet은 여전히 `UGameplayAbility`를 허용한다. 외부 기반·NonInstanced의 일반 활성화가 가능하다는 사실과 client payload 지원은 별개다.
3. **모든 최적화 후보가 기본 활성인 것은 아니다.** Ground trajectory의 지원되는 값 경로는 기본1이고 Flying은 명시2다. shared physics bounds는 이득을 입증하지 못해 기본0이다. 정확성 실패/전체 비용 증가를 숨기고 임의 rig에 켜지 않았다. canonical trajectory도 Attack별 opt-in·호환 제작 입력을 요구한다.
4. **필수 경고의 의미는 추론하지 않는다.** 새/legacy notify의 기본은 Cosmetic이다. GameplayWarning·태그·기간·서버 전달과 실제 presentation binding을 제작해야 한다. tickless20-line marker는 기본 주의 신호이며 피해 형상·화면 밖 HUD·피해 이전 도착 보증이 아니다.
5. **발견·배포도 등록 경로를 따른다.** [AssetManager 설정](../../../Config/DefaultGame.ini)의 타입/디렉터리와 validation/Cook 입력이 존재한다. 새 경로·종류·필드는 별도 등록·schema·호환성 검토가 필요하다.

## 점검 후 실제 보강 — 158–162차

위 NPC 공통 진입을 생산2파일과 검증 소비자3파일에서 보강했다. 새 Tick/manager 없이 필요한 두 컴포넌트를 조립·재사용하고, 중복/미등록·콜백/GC·외부 claim·ActorInfo 변경의 실패 경로를 확인했다. Editor/Game 빌드와 집중6개 고유 사례가 최종 관련 결과 기준 경고/실패0이다. 유효한 최초5개 성공은 재사용했다. 실제 non-MM NPC1+별도 recipient1의 RHI 실행에서도 helper 조립·몽타주·피해1회·자연 종료·예산 복귀와 정리가 통과했다.

최초1개 실패는 저장 canonical 궤적의 오래된 제작 지문이었다. NPC·플레이어 두 공격의 공개 미리보기에서 각각34개 좌표·시간·바인딩이 정확히 같고 stamp만 다른 것을 확인한 뒤 원본 백업과 기존 공개 Bake로 갱신했다. 엄격한 전체 struct 비교를 유지한 새 실행과 별도 선택 Data Validation2/2·경고0이 통과했다. 소스 montage/mesh/판정 값은 그대로이며 보호28입력 중 두 파생 데이터만 변경됐다. 실제 RHI 실행은 지문 갱신 전 입력으로, 이후 수치 궤적의 동일성을 별도로 확인했다. 이는 제작 스키마 변경에 따른 파생 데이터 재검증이며 hash 검사를 약화한 우회가 아니다.

159차에서는 실제 native NPC를 두 RHI socket에 연결해 동적 pair 생성·늦은 접속의 현재 경고 상태 복구·같은 컴포넌트 재사용·정상 파괴를 확인했다. 정상/지연·손실 조건 모두 통과했고, 늦은 클라이언트의 최초 시작 RPC0과 실제20선 marker 복구를 대조했다. actor 참조가 최초 control RPC보다 늦게 도착하는 검증기 경계는 정확한 참조의 제한 재전달로 보강했으며 client 생성이나 관련성 설정 변경은 없다. 이 증거는 grant 없는 공통 native NPC이며 실제 원격 montage/피해·모든 rig/asset·AOI·경고 픽셀/피해 이전 도착·규모를 입증하지 않는다.

160차에서는 PS 공유 명령 예산 거절 후에도 아이템 사용·가방·correlated 장비의 Reliable 실패 응답이 계속 생성되던 실제 생산 경계를 수정했다. 승인된 도메인 오류·저장 Pending/최종 완료와 단일 과금은 보존했다. 관련 고유4개 native 사례가 최종 유효 결과 기준 통과했고, 최종 Editor/Game 빌드도 성공했다. 새 서비스·queue·timer·UI 재시도 정책은 없으며 실제 byte/메모리/FPS 절감 측정과 서버 저장/경제 구현을 완료로 세지 않는다.

161차는 실제 저널 행과 command content를 현재 QuestId/source별로 재사용하고 owner/source 교체·GC·이전 버튼·이전 UI cleanup을 검증했다.162차는 no-Rewind coarse AABB 확정 대신 실제 native contact와 선택된 server piece의 LOS를 소비하고 두 기존 notify의 질의 정책을 공유했다. 관련 고유8개 native 사례, Editor/Game 빌드와 기존 실제 SSR6사례가 통과했다. widget identity·native geometry/GE 근거이며 UI 픽셀·전체 부하/ms/FPS나 A09 autonomous gameplay accepted SSR을 완료로 세지 않는다.

당시 발견한 누적 진행 경계는163–164에서 보강했다. 현재는 활성 상세128과 완료64×1024 페이지를 분리하며, Claim의 상세 제거·완료 보존·재수락/재보상 방지와 저장 이관·저널·원격 조회까지 연결됐다. 아래161–162 기준의 설명은 당시 발견 기록으로 유지한다. XP+아이템 bundle은 후속165에서 보강했다. 네트워크 서버 영속 보상·반복/계정/시즌 규칙은 계속 별도다.

상세 계약·원자료 지문·실패 보존·남은 경계는 [현재 작업 계획](../../Architecture/Planning/Project_J_MMO_Architecture_Work_2026-10-26.md)의158–162차에 기록했다. 새 Cook/Shipping·대표 성능이나 아래 전체 잔여의 완료로 합치지 않는다.

## 161–162차 최종 기준 재확인

사용자의 전체 완료·신규 에셋 적용 범위 질문에 따라 기존 기능군 표와 근거를 재사용했다. 현재 소스/설정/스크립트 등1262항목과 프로젝트 runtime38개+엔진 host1개의 전체 지문이 `Saved/ArchitectureBatch161QuestProjection/FinalWorkspace161162.json`과 일치했다. 선택 보호 에셋28개의 SHA도158 이후 기준과 모두 같다. 이는 기존 실행 근거를 해당 입력에 재사용할 수 있다는 확인이며, 새 실행이나 미검증 기능의 성공 증거가 아니다.

문서만 재독한 것이 아니라 PlayerState의 실제 컴포넌트 조립·owner-only 복제, NPC의 `StartCombatActions` capability 조립과 `TaskParallelFor` 배치 제출/최신성 검사, animation proxy의 값 계산/GT 반영, 목적지 WP source·셀 완료와 캡슐 배치, 신뢰 세션 진입 및 Standalone 저장 guard, 퀘스트 수락/보상·snapshot codec·repository 승인 경계를 현재 소스에서 다시 대조했다. NPC 기반은 여전히 non-MM이다. FeatureCatalog의 의존성 해석은 실행 서비스 설치가 아니며205항목을 구현 완료 수로 계산하지 않는다.

| 우선 확인할 경계 | 현재 확인된 상태 | 아키텍처 완료로 세기 위한 조건 |
| --- | --- | --- |
| 장기 퀘스트 진행 | 161–162의128 한도는 claimed 행까지 포함했다. 후속163은 활성 상세128과 완료 페이지·Format4를 분리했고164는 실제 owner page/foreign privacy·revision 복구를 정상/지연손실에서 확인했다. | 기존 저장·복원·이관·UI/읽기 통신에 이어165의 XP+아이템 bundle 원자 실행과 source 퇴역 복구도 연결됐다. 네트워크 서버 영속 보상·반복/계정/시즌 범위 규칙은 별도다. |
| 서버 권위 저장·복구 | `LocalProfileSubsystem.h:34`와 cpp의 `NM_Standalone` guard는 의도적인 개발 경계다. native 신뢰 세션은 외부 principal·캐릭터 소유권 검증을 호출자에게 요구한다. | 실제 서버의 검증된 세션→캐릭터 load/commit/restore→재접속/실패 복구를 연결한다. 공급자 미정이라는 이유로 개발 guard를 해제하지 않는다. |
| 도메인별 영속 실행 | 공통 CAS/영수증과 실제 로컬 제작 escrow가 있다. 이것이 양측 거래·우편·공동 보상·인스턴스/시즌 운영의 상태와 규칙까지 구현한 것은 아니다. | 도입할 기능의 최소 typed 상태·소유자·자산/보상 확정·취소/재시작 경계를 기존 저장/실행 기반의 실제 consumer에 연결한다. 콘텐츠 대량 제작이나 domain마다 새 서버를 만드는 조건은 아니다. |
| 엔진·네트워크 통합 | A03 물리 저빈도/Mass 인계, A07 strict 동일 live Actor/Iris 재등록, A09 실제 autonomous accepted SSR은 기존 미완료 경계다. | 각 원래 조건에서 실행/호환성을 입증한다. 정상 AOI·통제된 SSR RPC·authority 선행 피해를 다른 조건의 성공으로 대신하지 않는다. |
| 신규 에셋·규모 수용 | 공통 예산·수명·worker·복제 경로는 재사용되지만 rig/graph/socket·정의·Cook·Nav/HLOD·실제 비용은 별도 입력이다. 일부 최적화 후보는 이득이 없어 기본 비활성이다. | 지원 제작 계약과 등록을 확인하고 대표 hardware/품질·혼합 부하에서 CPU/worker·GPU/VRAM·복제·스트리밍·장기 수명을 판정한다. 설정 상한을 FPS/CCU 보장으로 표현하지 않는다. |

`Saved/ArchitectureBatch163QuestHistory/DesignRuntime.md`, `DesignPersistence.md`, `DesignAudience.md`는 최초 미채택 초안이다. 후속 V2 설계를 Source에 적용한163의 계약은65536 완료 예산·immutable64ID 저장 페이지·owner32ID 조회이며1024 평생 한도/전체 이력 복제를 채택하지 않았다. `Verification163.json`은 관련 로컬 통합과17개 native 성공의 근거이고 원격 page 전달이나 전체 MMORPG 완료 선언이 아니다. 전체 목표는 active다.

## 검증의 범위와 남은 핵심

기존157의 Editor349/Game118 성공, native 유효9+수정1사례, Ability BP4개 상속/메모리 내 컴파일·hash 보존, 실제 전용 Editor 서버+2RHI클라이언트의 세션 전이/정리는 유효한 근거다. 이전129/145/147/151의 특정 Development 패키지·콘텐츠 소비와153/154의 경고 정상/손실 조건도 각 범위에서 재사용한다. 이들을 현재157 전체 Shipping/독립 전용 서버·대규모 동접·모든 에셋의 검증으로 합치지 않는다.

필수 잔여는 다음과 같이 구분한다.

1. **현재 공통 경로의 콘텐츠 수용 경계:** 필수 capability/정의/rig 계약을 제작 단계에서 확인하고, 테스트 helper의 보완과 생산 조립을 구분한다. 새 도메인·미지원 계약을 도입할 때 대표 콘텐츠를 기존 진입으로 연결해 취소·종료·복제까지 확인한다. 이미 검증한 동일 입력을 반복하거나 모든 미래 콘텐츠를 미리 제작하는 완료 조건은 아니다. 빈 manager를 추가하자는 뜻도 아니다.
2. **실행·운영 통합:** 실제 verifier→세션→서버 저장 load/commit/restore→재접속/장애 복구. 거래/보상·인스턴스/시즌 등에서 필요한 typed 상태와 트랜잭션 경계. 현재 SQLite 프로필은 Standalone 전용이며 네트워크 서버 저장으로 연결된 것이 아니다. 기본 `bRequireTrustedSessions=false`도 개발 호환값이다.
3. **엔진·네트워크 경계:** A03 Mover 물리 저빈도/Mass 인계, A07 strict 동일 live Actor 재등록/외부 Iris 참조, actual autonomous accepted SSR·presentation-time 소비 근거. 정상 AOI 성공이나 DuplicateTarget 거절을 각각의 해결로 계산하지 않는다. 독립 Server/현행 목표 플랫폼 release·운영 배포도 남는다.
4. **실제 성능·품질 수용:** 사용할 hardware/품질·플레이어/NPC 밀도·스킬/VFX 조합·최대 이동 속도와 대표 구간을 정해 GT/worker·복제량/지연·CPU/RAM·GPU/VRAM·도착/원경 품질·장기 수명을 함께 확인한다. 2소켓이나 설정 상한은 CCU가 아니며, 19frame GPU 분석은 전체 GPU 용량 보증이 아니다.106 메모리 증가의 owner 귀속도 미확정이다. 기존 성공 장면을 근거 없이 반복하거나 FPS를 맞추기 위해 필수 품질을 제거하지 않는다.

**권장 우선순위는 생산 조립/제작 검증의 빈틈 → 명시된 로컬 통신·시간 경계 → 실제 서버/저장/배포 입력 → 대표 혼합 부하·품질 판정이다.** 기존8개 모듈과 데이터·컴포넌트·worker 경계를 유지하며 필요한 소비를 보강한다. 모든20개 domain을 미리 별도 프로세스/서비스로 나누는 설계는 현재 요구하지 않는다.

## 근거와 보존

- [현재 아키텍처 작업·잔여](../../Architecture/Planning/Project_J_MMO_Architecture_Work_2026-10-26.md), [프로젝트 맥락](../../Overview/ProjectContext.md), [원래 감사](Project_J_MMO_Foundation_Audit_2026-10-26.md). 원래 감사의 당시 미수정 상태를 현재 상태로 복제하지 않았다.
- 로컬 상세 대조: `Saved/ArchitectureReadiness20261104/GameplayAudit.md`, `AssetWorldAudit.md`, `NetworkOpsAudit.md`. 각각 gameplay/domain, asset/animation/world/presentation, network/service/ops/performance의 현재 source line과 기존 증거 범위를 기록했다.
- `Saved/ArchitectureReadiness20261104/ScopeVerification.json`:1259/39 지문 일치·새 runtime0, 보조 보고서 SHA. 파일 수·카탈로그 수는 완료율로 사용하지 않았다.
-157의 기존 증거는 `Saved/ArchitectureBatch157GASPayloadAdmission/Verification157.json`; 이전 실패·음성 성능 결과·운영 미연결은 그대로 보존했다. 지속 목표는 **active**이며 이 감사는 전체 완료 선언이나 새 성능 측정이 아니다.
