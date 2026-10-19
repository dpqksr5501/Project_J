# 게임플레이 공간 통합 — 현재 구현과 남은 연결

2026-10-18 기준. 이 문서는 공간 저작과 실제 게임플레이의 연결을 설명한다. 최종 상태는 `03_IMPLEMENTATION_STATUS.md`, 장소별 근거는 Source Registry/GenerationRecord와 `Saved/Validation/StarterWorld/Phase2`를 함께 확인한다. 보스의 공간 검사 통과를 활성 보스 전투 합격으로 해석하지 않는다.

## 원천과 소비 경계

| 대상 | 제작 원천 | 현재 실제 소비 | 아직 연결하지 않은 기능 |
|---|---|---|---|
| 장소 | Manifest/layout/Location Registry | Native WorldDefinition의 Place·마커·WorldState | 퀘스트의 장소 조건, 서버 간 지역 이동 |
| 사냥터 | `MainWorld.hunting.json` | 정적 전투 경계·진입/스폰/순찰 후보·환경 기하·저장본/보행 검사 | 군집 AI, 리스폰/리셋, Nav, 드롭·퀘스트 |
| 보스 | `MainWorld.bosses.json` | Arena/집결 미리보기·접근/우회/전투길·기존 Native WorldEncounter | 실제 NPCClass, 능력/AI/추적·리셋·보상 |
| 지면 | 원본 R16와 명시적 `authored_surfaces` | Native Landscape/광산·계단·부두 기하, 검사의 기대 지지면 | 전체 Nav/탈것·낙하 피해·수영 |

JSON과 Python은 오프라인 제작 원천이다. 현재 게임이 이 JSON을 직접 읽어 스폰하는 구조는 아니다. Runtime은 저장된 Native DA/Actor를 소비한다. Source 원형 경계와 Native Box는 같지 않으며, Box는 NoCollision/OverlapFalse의 저작 미리보기다. 자동 스폰 Trigger나 전투 봉쇄벽이 아니다.

## 단일 보스 이벤트 소유권

`Source/Project_J/World/Project_JWorldEncounter.cpp`의 기존 `AProject_JWorldEncounter`를 재사용한다. B01/B02는 각각 `WorldBoss.Starter.Lv1_30.B01/B02`로 지역을 포함한 ID를 사용한다. 같은 ID의 소유자가 있으면 builder가 중복 생성을 거부한다. 정적 수목/부재는 비복제·공간 로드이고, 이 이벤트 소유자는 복제·항상 로드된다.

- BeginPlay의 서버만 Registry에 등록한다. 등록 실패 또는 NPCClass 없음이면 Disabled로 끝난다. 현재 두 보스 모두 NPCClass가 없다.
- NPCClass를 연결한 경우에만 1초 갱신이 가까운 플레이어를 확인하고 기존 AssetManager 비동기 로드를 시작한다. 활성 반경 1,500m는 이 준비 예산이다. Source의 추적/리셋 후보 48m와 역할이 다르다.
- 로드 완료 뒤 실제 지면 충돌을 확인하고 기존 NPCCharacter를 deferred spawn한다. 네트워크 관련성 예산은 별도로 제한한다. 에셋 로드 성공이 WP 충돌 준비 완료를 뜻하지 않는다.
- 사망 확인은 기존 전투 인터페이스를 소비한다. 기존 Cooldown이 NPC/핸들을 정리하며, 처치 시 `OnResolved(EncounterId, SpawnRevision)`를 전달한다. 이 delegate를 아직 퀘스트나 보상에 연결하지 않았다. 별도 자동 보상 writer는 없다.
- EndPlay는 타이머·비동기 요청·NPC·Registry 등록을 정리한다. 이 수명 위에 두 번째 스폰/상태/리스폰 관리자를 만들지 않는다.

현재 PIE 근거는 각 `B0x-boss-runtime-verification.json`의 단일 지역 ID·NPC 미지정·실제 Disabled 전이다. 활성 전투·처치·리스폰/재접속·Dedicated Server/다중 클라이언트 검증은 아니다. 실험적 MassMonsterSpawner의 기본 자동100체 설정을 공간 후보의 대체물로 배치하지 않았다.

## 전투·이동·시야 검사의 구분

B01은 암릉의 넓은 자연 전투 원과 별도 위험 안내 난간이다. B02는 수목/뿌리가 남은 숲의 외곽 안에서 열린 전투 틈과 연결·순환 경로를 검사한다. 숲 전체를 빈 원으로 가정하거나 작은 틈만으로 실제 보스의 스킬 범위를 확정하지 않는다.

B02의 실제 GreatSword CDO Capsule은 Pawn 프로파일을 사용한다. 단순 Pawn 충돌로 이동 여유를 확인하고 복합 Visibility는 가지/뿌리의 시야 엄폐 근거로 분리한다. Terrain 전용 trace가 식생을 제외해도 이동 capsule은 모든 Actor를 포함한다. 보스 후보 capsule 반경1.2m/halfheight2m는 공간 여유의 초기 기준이며 실제 보스 모델/이동 설정은 아직 없다. 카메라·공격 LoS·Projectile·큰 보스의 연속 이동과 다수인원 전투는 추가 검증 대상이다.

실제 보행 도구는 캐릭터에 이동 입력을 주고 지지면/도달 지점을 기록한다. 순간 이동으로 도달을 대신하지 않는다. 이 결과를 길 전체 폭·Nav·탈것·고속 스트리밍·자유 탐험 합격으로 확대하지 않는다. Native InstanceIndex는 보존된 PCG 결과의 검사 참조이며 재생성 후에도 불변인 게임플레이 ID가 아니다.

## 후속 통합 순서

1. 지역 도로와 장소 접근로를 실제 기하로 연결하고 수계·교량·산길의 단절을 해결한다. 보스 우회로를 필수 퀘스트/차기 지역 통행과 분리한다.
2. 실제 보스 NPC/스켈레탈 메시·캡슐·GAS 능력·AI의 기존 계약을 확인해 연결한다. 형상이 바뀌면 공간 여유와 카메라/공격 검사를 다시 수행한다.
3. 기존 이벤트/AI에 추적·퇴각·리셋과 실패 경로를 연결하고, 같은 처치 이벤트를 소비하는 퀘스트/보상 소유권 및 중복 수령 방지를 검증한다.
4. 필요한 Nav 범위/Invoker·WP 준비와 다중 클라이언트 권한·재접속·종료 정리를 확인한다. 성능 측정은 사용자의 유예 요청 이후 별도 수행한다.

날씨는 보류한다. 현재 Source 후보·마커·미연결 훅은 후속 제작 용도이므로 미참조라는 이유로 삭제하지 않는다.
