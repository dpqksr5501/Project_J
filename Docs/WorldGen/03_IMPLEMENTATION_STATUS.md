# 월드 구현 상태 — 2026-10-18

**사용자 요청으로 작업 중단.** 최신 변경·실패·미검증·재개 조건은 [인수인계](../World/WORLD_HANDOFF_2026-10-18.md)를 따른다. 반복 작업 일지와 수치 나열은 정리했다. 아래 표는 현재 Source 레지스트리의 기록이며 이번 정리에서 에디터를 다시 검증한 결과가 아니다.

정착지9·사냥터9·보스2의 초기 공간 배치는 기록돼 있다. 출시 수준의 환경 아트, 활성 게임플레이, Nav/탈것/멀티플레이, 전체 원경과 성능 합격은 별도이며 미완이다.

| 기획 ID | StableID | 역할 | Source 상태 |
| --- | --- | --- | --- |
| S01 | S01 | capital_city | SPATIAL_BLOCKOUT |
| S02 | S02 | farm_village | SPATIAL_BLOCKOUT |
| S03 | S04 | forest_village | SPATIAL_BLOCKOUT |
| S04 | S03 | lake_village | SPATIAL_BLOCKOUT |
| S05 | S06 | harbor_village | SPATIAL_BLOCKOUT |
| S06 | S05 | mountain_village | SPATIAL_BLOCKOUT |
| S07 | S07 | river_hamlet | SPATIAL_BLOCKOUT |
| S08 | S00 | road_watch_starting_hamlet | SPATIAL_BLOCKOUT |
| S09 | S08 | north_gateway_camp | SPATIAL_BLOCKOUT |
| H01 | H01 | hunting_ground | SPATIAL_BLOCKOUT |
| H02 | H02 | hunting_ground | SPATIAL_BLOCKOUT |
| H03 | H03 | hunting_ground | SPATIAL_BLOCKOUT |
| H04 | H04 | hunting_ground | SPATIAL_BLOCKOUT |
| H05 | H05 | hunting_ground | SPATIAL_BLOCKOUT |
| H06 | H06 | hunting_ground | SPATIAL_BLOCKOUT |
| H07 | H07 | hunting_ground | SPATIAL_BLOCKOUT |
| H08 | H08 | hunting_ground | SPATIAL_BLOCKOUT |
| H09 | H09 | hunting_ground | SPATIAL_BLOCKOUT |
| B01 | B01 | rock_guardian_arena | SPATIAL_BLOCKOUT |
| B02 | B02 | ancient_tree_ruins_arena | SPATIAL_BLOCKOUT |

현재 수도는43필지·7도로의 초기 구도이며 대규모 성곽 도시가 아니다. 마을 배치·건물 접합·생활 밀도와 산림·지피·물은 개선 대상이다. 수목 레이어 등록을 저장했지만 전역 HLOD 빌드와 PIE 전환은 미검증이다. 수량·저장 확인이나 이전 도보 통과를 최종 품질 판정으로 쓰지 않는다.

실제 입력은 [장소 레지스트리](../../Content/World/MainWorld/Source/MainWorld.locations.json), [생성 기록](../../Content/World/MainWorld/Source/MainWorld.generation.json), [ID 대응표](01_EXISTING_TO_TARGET_MAPPING.csv)다. 현재 배치와 상태를 바꿀 때 Source·Native 정의·기존 안정 ID의 연결을 함께 확인한다. 생성 기록의 예전 로컬 report 경로 중 정리된 자료는 재검증 증거로 재사용할 수 없다.
