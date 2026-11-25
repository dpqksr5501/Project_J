# 게임플레이 공간 통합 — 현재 구현과 남은 연결

이 문서는 공간 저작과 실제 게임플레이의 연결을 설명한다. 장소별 공간 근거는 Source Registry/GenerationRecord와 `Saved/Validation/StarterWorld/Phase2`를 함께 확인한다. B01의 저장 연결과 전투 판정은 아래의 독립 검증으로 확인한다.

## 원천과 소비 경계

| 대상 | 제작 원천 | 현재 실제 소비 | 아직 연결하지 않은 기능 |
|---|---|---|---|
| 장소 | Manifest/layout/Location Registry | Native WorldDefinition의 Place·마커·WorldState | 퀘스트의 장소 조건, 서버 간 지역 이동 |
| 사냥터 | `MainWorld.hunting.json` | 정적 전투 경계·진입/스폰/순찰 후보·환경 기하·저장본/보행 검사 | 군집 AI, 리스폰/리셋, Nav, 드롭·퀘스트 |
| 보스 | `MainWorld.bosses.json` | Arena/집결 미리보기·접근/우회/전투길·기존 Native WorldEncounter. B01은 NPC·전투·개인 보상 연결 | B01 추적·리셋·최종 보스 연출. B02 NPC·전투·보상 연결 |
| 지면 | 원본 R16와 명시적 `authored_surfaces` | Native Landscape/광산·계단·부두 기하, 검사의 기대 지지면 | 전체 Nav/탈것·낙하 피해·수영 |

JSON과 Python은 오프라인 제작 원천이다. 현재 게임이 이 JSON을 직접 읽어 스폰하는 구조는 아니다. Runtime은 저장된 Native DA/Actor를 소비한다. Source 원형 경계와 Native Box는 같지 않으며, Box는 NoCollision/OverlapFalse의 저작 미리보기다. 자동 스폰 Trigger나 전투 봉쇄벽이 아니다.

## 단일 보스 이벤트 소유권

`Source/Project_J/World/Project_JWorldEncounter.cpp`의 기존 `AProject_JWorldEncounter`를 재사용한다. B01/B02는 각각 `WorldBoss.Starter.Lv1_30.B01/B02`로 지역을 포함한 ID를 사용한다. 같은 ID의 소유자가 있으면 builder가 중복 생성을 거부한다. 정적 수목/부재는 비복제·공간 로드이고, 이 이벤트 소유자는 복제·항상 로드된다.

- BeginPlay의 서버만 Registry에 등록한다. 등록에 실패하거나 NPCClass가 비어 있으면 Disabled로 끝난다. B02는 이 상태를 유지한다.
- B01의 활성 반경 48m 안에 플레이어가 들어오면 Registry 갱신이 비동기 준비를 시작한다. 안전 집결지와 공개 우회로는 반경 밖에 있다. AI 추적·퇴각 범위는 별도 검증이 필요하다.
- 로드 완료 뒤 실제 지면 충돌을 확인하고 기존 NPCCharacter를 deferred spawn한다. 네트워크 관련성 예산은 별도로 제한한다. 에셋 로드 성공이 WP 충돌 준비 완료를 뜻하지 않는다.
- B01은 기존 `BP_FieldBoss_Test`, `DA_NPC_MMOTestBoss_Activation`, `DA_LootTable_FieldBoss_Starter`를 재사용한다. Native WorldEncounter가 처치 기여자 보상 정산과 300초 재등장 대기를 관리한다. 완료 이벤트 `WorldBoss.Starter.Lv1_30.B01`을 소비하는 제작 퀘스트는 후속이다.
- 현재 실행은 [권위 월드 이벤트](../World/World_Event_Runtime.md)의 중앙 Registry 순회·공통 observer snapshot·준비/스폰 예산을 따른다. EndPlay는 consumer token·NPC·Registry 등록을 정리하고, 취소된 native load 예약은 실제 완료까지 보존한다. 이 수명 위에 두 번째 스폰/상태/리스폰 관리자를 만들지 않는다.

`Bind-StarterBossGameplay.py`는 `PROJECT_J_STARTER_BOSS_MODE`가 `APPLY`이면 기존 B01 외부 액터 패키지만 저장한다. 별도 에디터 시작에서 `VERIFY`를 실행하면 저장본을 다시 읽는다. 두 실행 모두 보고서 경로를 `PROJECT_J_STARTER_BOSS_REPORT`로 각각 지정하며 실제 NPC 캡슐의 중심 지면과 B02·공유 맵 패키지 SHA를 확인한다. 같은 보고서 환경 변수를 새 경로로 지정해 `Verify-StarterBossRuntime.py`를 실행하면 단독 PIE의 집결지 대기, 보스 생성, 전투 경고, 처치, 개인 우편을 검증한다.

2026-11-25 D3D12 단독 PIE에서 B01은 안전 집결지에서 Dormant를 유지했고 아레나 진입 후 활성화됐다. 경고 생성·해제, 실제 공격 입력 열 번으로 체력 100→0, 300초 재등장 대기 진입과 개인 우편 2,645 GOLD를 확인했다. B02는 Disabled였다. 검증기는 플레이어의 위치와 체력을 준비했다. B01의 추적·리셋, 재등장 완료, 온라인 재접속과 다인원 전투는 이 실행 범위 밖이다. 저장본 재검사 (로컬 `Saved/Validation/Continuation_20261014/MainWorldB01BindingFinal_20261125/VERIFY/verification.json`) · 전투 PIE (로컬 `Saved/Validation/Continuation_20261014/MainWorldB01CombatFinal_20261125/PIE/verification.json`)

## 전투·이동·시야 검사의 구분

B01은 암릉의 넓은 자연 전투 원과 별도 위험 안내 난간이다. B02는 수목/뿌리가 남은 숲의 외곽 안에서 열린 전투 틈과 연결·순환 경로를 검사한다. 숲 전체를 빈 원으로 가정하거나 작은 틈만으로 실제 보스의 스킬 범위를 확정하지 않는다.

B02의 실제 GreatSword CDO Capsule은 Pawn 프로파일을 사용한다. 단순 Pawn 충돌로 이동 여유를 확인하고 복합 Visibility는 가지/뿌리의 시야 엄폐 근거로 분리한다. Terrain 전용 trace가 식생을 제외해도 이동 capsule은 모든 Actor를 포함한다. 보스 후보 capsule 반경1.2m/halfheight2m는 공간 여유의 초기 기준이며 실제 보스 모델/이동 설정은 아직 없다. 카메라·공격 LoS·Projectile·큰 보스의 연속 이동과 다수인원 전투는 추가 검증 대상이다.

실제 보행 도구는 캐릭터에 이동 입력을 주고 지지면/도달 지점을 기록한다. 순간 이동으로 도달을 대신하지 않는다. 이 결과를 길 전체 폭·Nav·탈것·고속 스트리밍·자유 탐험 합격으로 확대하지 않는다. Native InstanceIndex는 보존된 PCG 결과의 검사 참조이며 재생성 후에도 불변인 게임플레이 ID가 아니다.

## 후속 통합 순서

1. 지역 도로와 장소 접근로를 실제 기하로 연결하고 수계·교량·산길의 단절을 해결한다. 보스 우회로를 필수 퀘스트/차기 지역 통행과 분리한다.
2. B01에 최종 모델·리깅·능력을 연결할 때 캡슐 형상 및 카메라·공격과 공간 여유를 다시 검증한다. B02는 고목 아레나와 맞는 별도 보스 구성이 필요하다.
3. B01의 실제 Nav·추적·퇴각·리셋과 실패 경로를 연결하고, 같은 처치 이벤트를 소비하는 제작 퀘스트와 다중 클라이언트 보상 중복 방지를 검증한다.
4. 필요한 Nav 범위/Invoker·WP 준비와 다중 클라이언트 권한·재접속·종료 정리를 확인한다. 성능 측정은 사용자의 유예 요청 이후 별도 수행한다.

날씨는 보류한다. 현재 Source 후보·마커·미연결 훅은 후속 제작 용도이므로 미참조라는 이유로 삭제하지 않는다.
