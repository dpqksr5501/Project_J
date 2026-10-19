# 현재 재사용 계약과 미완료 범위

기존 런타임의 권위는 `Project_JWorldDefinition`/`WorldState`다. 새 런타임 관리자나 스폰/보상 구현을 복제하지 않았다. 아래 JSON은 Editor 저작 입력/검증 계약이며, 모든 JSON이 런타임에서 자동 로드된다고 가정하지 않는다.

| v4 계약 | 현재 실제 자원·소비자 | 남은 작업 |
|---|---|---|
| RegionManifest / WorldGridSpec | `MainWorld.manifest.json`; SettlementAuthoring의 좌표 변환·저장 루트, 입구 등록, 캡처가 소비 | 다른 저작 경로의 고정 좌표 제거·새 Region 시연 |
| TerrainProfile | manifest의 보존 높이/해시/고도/샘플 규격과 기존 plan | 비파괴 Edit Layer·곡률/유역 파생의 공통화 |
| BiomeProfile | `MainWorld.biomes.json`, 실제 Landscape/Grass/PCG | 지피 표시 결함·바이옴별 서브그래프/자산 주입·원경 검증 |
| LocationRegistry | `MainWorld.locations.json`; v4 alias→기존 안정 ID, 실제 Native 태그/입구 참조 | 모든 20곳의 실제 등록·전체 입력 참조 검사 |
| SettlementArchetype | `MainWorld.settlements.json`; S02 농업/S03 임업/S04 호반/S05 하구항/S06 산악 프로필·공통 scanned shell/기초/외부 계단 | 소취락의 다른 공간 알고리즘 |
| RoadNetworkData | 기존 plan 도로, 실제 S00/S01 접근부, S02 프로필 농로 | 전역 실제 교량/계단·우회로·폭/이동 검증. 후보 A*를 실제 도로로 취급하지 않음 |
| HuntingGroundProfile / BossEncounterProfile | DA의 ID/경계/레벨 + Manifest의 hunting_profiles → H01/H02의 실제 군집 공간/입출구·스폰/순찰 후보·안전/리스폰/리셋·퀘스트/서버 상태, 선택적 목책/천막 | 나머지 H/B 아레나·환경/AI·실제 서버 군집/리스폰/리셋/보상 연결 |
| WorldEdgeContract | `MainWorld.edges.json`의 네 방향 높이 표본과 7개 포트 | 기울기/재질/바이옴·인접 영역 이음과 통행 증명 |
| GenerationRecord | `MainWorld.generation.json`의 S02~S09 실제 Actor manifest·입력/높이 지문·검증 범위 | 나머지 저작 패스의 공유 기록·안전한 선택 재빌드/롤백 |

완료된 pass는 자동 덮어쓰기를 거부한다. 실패 재개는 실제 소유 Actor 집합·좌표와 완료 task가 일치하며 실패 task의 부분 Actor가 없을 때만 허용했다. 수동 편집을 없애기 위해 전역 생성기를 다시 돌리지 않는다. 마스크 raster 함수는 지표면 합성과 바이옴 제외가 함께 소비하고, 공급자 instancing 재질이 준비되지 않으면 소유 복사본의 부모 체인을 구성한다.

현재 공통 코드가 다른 Region을 완전히 데이터만으로 생성한다고 주장하지 않는다. legacy corridor/height/material 경로에는 고정 범위와 자산 경로가 남아 있다. Phase 7에서 서로 다른 바이옴/정착지 유형 및 인접 검증 영역을 실제로 만들고 기존 권역 비손상·경계·복구를 확인해야 확장성이 검증된다.

항구 Native 저작·집터 검토·검사는 `Starter-TerrainProfile.py`로 manifest의 원본 높이 지문/해상도/간격/Z 스케일을 읽는다. 기존 corridor의 에디터 world/자산 경로 제한은 여전히 남아 있으므로 새 Region 전체 재사용 증명은 미완료다.

산악 마을은 같은 높이/계단 계약과 개별 건물 yaw·plaza offset을 실제 소비했다. `Starter-SettlementSupport.py`의 순수 계단 계획은 사전 검사와 Native 저작이 공유하며, 실제 collision 검증은 독립이다. 전체 Region 복제·경계 연결 증명은 여전히 미완료다.

S07 추가는 `Starter-WorldDefinitionContract.py`의 장소 snapshot으로 기존 전체 값 보존을 검증하며 같은 Native DA에 1개를 append했다. 순수 data 추가는 서버 zone/AI spawn/NPC 권한 통합을 의미하지 않는다. 새 위치 주변 PCG는 영향 타일만 갱신하고 먼 변환 집합을 보호했다. 신규 Realm 전체 생성/경계 증명은 여전히 미완료다.

S08은 기존 S00의 8동/시작점·Place/Connection을 교체하지 않고 별도 소유 태그로 망루/검문/병영/노점 공간을 보강했다. S09는 새 안정 S08과 기존 E_N_Road 포트의 Source/Native 연결을 함께 반영했다. [경계 계약](06_WORLD_EDGE_CONTRACT.md)을 따른다. 선택적 `lookout.deck_top_m`은 지면 기반 낮은 전망대와 고가 목조 망루를 같은 보행/촬영 경로가 소비하도록 한다. 경사 부재 검사는 개수뿐 아니라 저장된 실제 끝점 높이를 확인한다.

다음 H/B 저작은 시각적 공간/스폰 후보와 활성 서버 전투를 구분한다. 기존 WorldEncounter는 단일 NPC 이벤트 소유 계약이고 실험적 MassSpawner는 기본 자동100체 설정이므로 후보 마커의 대체물로 배치하지 않는다. 기존 NPC/GAS·서버 소유권과 실제 reset/군집/보상 소비를 확인한 범위에서 통합한다.

H01은 이 구분에 따라 전투 경계 미리보기/후보 마커와 풍경 소품을 실제 저장했다. 프로필의 trails가 Native 검증/이동과 공통 surface/지피 제외의 단일 입력이다. 전투 원은 Source 후보 영역이고 Native Box는 비활성 저작 미리보기 경계다. 기존 Place/WorldState·서버 등록/스폰 writer를 복제하지 않았다. Native 조사에는 PCG와 개별 배치 식생/장식 바위를 함께 포함하며, 지형 HLOD는 장애물에서 제외한다. 보존한 기하 경계와 TerrainProfile을 소비하는 `Starter-HuntingRoutes.py`는 다음 사냥터의 초기 등고선 탐색에 재사용할 수 있으나, 이를 최종 곡선/폭/Nav·탈것 검증으로 대체하지 않는다.

H02는 같은 Native 저작/검증/게시 경로를 사용하면서 목책·천막 계약을 선택적으로 추가했다. posts_m/gates와 구조물 위치·기초/출입 계획은 프로필에 한 번 저작하며, Native 인스턴스·경로 제외·지피/지면·검증이 소비한다. 새 Runtime 관리자/스폰·보상 writer는 없다. 미래 구조물 종류는 명시적 지원을 추가해야 하며 천막으로 자동 대체하지 않는다. 목책의 simple collision과 실제 양쪽 문 통과를 확인했고 기존 H01 프로필을 유지했다.

H03은 수관 렌더 경계와 실제 줄기 충돌을 구분한다. Native BodySetup을 확인한 나무의 단순 capsule은 Instance Transform을 적용한 walking_mesh_bounds를 제공하며, 원래 blocking_mesh_bounds는 별도 보존한다. 이를 통해 기존 숲을 지우지 않고 수관 아래 길을 탐색했다. 바닥 가까운 trace/이동 캡슐 여유/정확한 tree instance의 단순 광선 차단 검사를 분리한다. Cover reference는 저작 snapshot이며 런타임 AI 인지/Projectile 계약이나 PCG 재생성 후에도 불변인 InstanceID로 확대 해석하지 않는다. 새 Region 전체 생성·경계/복구 증명은 미완료다.

H04은 Native 비회전 Cylinder의 위치/스케일/충돌을 먼저 대조한 타원형 물 제외를 사용한다. `ellipse_clearance_lower_bound`는 정확한 거리라고 부르지 않으며 스케일 노름의 Lipschitz 하한으로 외부 여유를 보수적으로 보장한다. 실제 물-지면 교차/수영 검사는 별도다. 이번 원본 호수의 떠 있는 경계는 미완료로 기록했다.

H05의 선택적 `instance_groups`는 사냥터 프로필의 단일 저작 입력을 Native HISM/저장본 검사/GenerationRecord가 소비한다. 수목260그루는 반경194m 안에서 새 소유 Actor 하나로 더했고, 기존 PCG H05 예약 반경204m는 유지했다. 전역 PCG 재생성이나 기존 식생 삭제는 없다. 향후 이 코어를 PCG로 이주할 때에는 기존 소유 그룹을 함께 퇴역해 중복 생성을 막아야 한다. Native 검사는 새 그룹의 모든 메시/인스턴스 변환과 실제 지면을 따로 확인하고, 기존 식생 보존 비교에는 새 소유자만 제외한다.

사냥터의 경사면 이동 공간 검사는 Native 바닥 법선과 수직 capsule의 하반구 지지를 고려한다. 반지름r에 필요한 지지 상승량은 `r*(1/normal.z-1)`이며 그 위에15cm의 질의 여유를 둔다. 고정된 지면+15cm만 쓰면 약37도 횡경사에서 검사 capsule이 지면에 미세하게 겹친다. 바닥 각도는 별도로 제한하고 실제 CharacterMovement 보행은 별도 수행한다. H05 모든 트레일 표본 최대39.55도는 현재 플레이어의44.765도 설정 아래지만 최종 길 폭/탈것/Nav 통행 합격은 아니다.

H06는 원본2m 지형 격자의 연결된 저지대를 일정 수면으로 잘라 만든 `MainWorld.H06.wetland.json`을 사냥터 프로필의 `water_surfaces`에서 참조한다. Geometry SHA·기하 ID는 Native StaticMesh의 소유/입력 메타데이터와 대조하며 Native 저작·저장본 검사·공통 지피 제외가 같은 경계 입력을 소비한다. GeometryScript/Chaos의 앞면 방향은 실제 상/하향 trace로 확인했다. 최종 Native 수면은 위에서 조회되고 Pawn 충돌을 무시한다. 큰 강과 연결된 흐름/수영/전체 수계 아트의 대체물이 아니다.

신규 물의 영역 안/바로 가장자리에서는 공통 surface raster가 기존 Landscape Grass를 제외한다. 손으로 저작한 물가 풀/고사리 HISM은 그 경계 밖의 단일 저작 소유자다. 이 저작은 기존 PCG 수목/관목을 재생성하지 않으며 실제 기존 식생551개 위치를 별도로 보호했다. 원본 R16은 변경하지 않았다. H04 기존 호수의 큰 경계 높이 결함은 여전히 별도 수계 수정 대상이다.

남쪽 해안은 Manifest coastal_surface_profile의 단일 Source를 Native 물 메시/검사가 소비한다. H06과 공유하는 Starter-WaterSurfaceAuthoring은 새 소유 메시와 VisibilityQuery/PawnIgnore 설정을 제공하고 기존 자산을 무조건 덮어쓰지 않는다. 바다와 연결된 음수 지형은 전역 flood 결과를 공유해9개 도달 구간으로 나눴으며 서로의 Source wet interval을 대조한다. 실제 Native 지형은00-11대각선이었다. 파생 물 경계 생성도 이를 따르고 원본 R16은 유지한다. 기존 H06의 저장 기하는 명시적 Source와 검증 허용 범위를 유지하며 자동 재생성하지 않았다. 원경/HLOD/최종 물·배·수영 및 외부 영역 연결의 재사용/성능 합격은 미완료다.

H07의 암벽은 일반 환경 소품의 선택적 vertical_anchor(kind=top,height_m)를 통해 원본 능선 높이에 맞췄다. 공급자 메시의 실제 bounds와 scale에서 하단을 산출하며 TerrainProfile/원본 R16을 변형하지 않는다. hazard는 해당 prop_id와 제외 원/완충을 단일 Source로 선언하고 Native 콜라이더의 상단 지지·주변 착지 높이를 별도로 측정한다. 현재3m 사이23.31m 실제 낙차·상단 capsule 여유를 확인했지만 상단까지의 실제 접근/Nav·AI·낙하 피해 소비는 아직 아니다. 공개 우회로/전투 원은 같은 hazard 제외를 사용한다.

### 사냥터의 저장된 바닥 참조

`MainWorld.hunting.json.authored_surfaces`는 기존 소유 Actor 경로/태그와 Source SHA를 선언한다. H08의 mine_gallery/stair_path가 첫 소비자이며 `Starter-HuntingSurface.py`가 원본 지면·광산 바닥/입구 계단·회전 디딤면의 실제 최고 상면을 합친다. 새 Actor 생성자는 없고 기존 광산/접근로 소유자가 각각 구조를 저장한다. 공통 hunting builder는 참조가 존재하는지만 확인하며 같은 구조를 재생성하지 않는다. 저장본 verifier와 실제 보행 기대 지지면은 이 참조를 소비하고, 근접 바닥 trace로 천장을 땅으로 오인하지 않는다. 입구 계단의 생성 규칙은 Starter-SettlementSupport.plan_mine_entry_steps로 기존 mine builder와 공유한다. 마스크 baker는 같은 광산 Source의 외피 면적을 지피 제외로 소비한다.

이 계약은 오프라인 레벨 제작/검증용이다. Runtime NPC/몬스터 관리나 WorldState의 새 writer가 아니며, Nav·탈것·서버 군집/보상 통합을 대신하지 않는다. authored_surfaces가 없는 기존 H01~H07은 원래 지면 계약을 유지한다. H08의 실제 계단/갱실 충돌과 플레이어 이동을 각각 확인한 뒤 첫 공간을 발행한다.

### 보스 공간과 서버 이벤트 소유권

Manifest.boss_profiles→MainWorld.bosses.json은 Arena/접근·우회/SafeStaging/Encounter/QuestHook의 제작 원천이다. 공간 builder는 기존 AProject_JWorldEncounter를 한 번만 배치하며 동일 EncounterId 소유자가 있으면 중복 생성하지 않는다. 단일 NPC/비동기 class 로드/서버 단계/종료 취소·레지스트리 해제는 기존 C++ 클래스가 소유한다. 리셋 반경과 NPC/퀘스트 후보는 실제 AI 바인딩과 구분한다. Source상의 활성 반경과 전투/추적/리셋 원은 서로 다른 역할이다.

이벤트 ID는 WorldBoss.<RegionID>.<StableBossID>로 지역을 포함해 월드 공용 Registry의 FName 키와 다음 Region의 지역 B01 이름이 충돌하지 않게 한다. 지역 StableID/마커는 B01로 유지한다. 신규 B01의 이전 비지역 ID만 Source/Actor 패키지 백업 뒤 정리했으며 실제 PIE에서 현재 ID의 한 owner/Disabled(NPC 미지정) 상태를 확인했다. 활성 보스/리스폰·보상/멀티플레이 구현을 의미하지 않는다. 바닥/공간 이동 검증과 네이티브 이벤트 인터페이스 검증을 별도 기록한다.


B02는 동일 boss_profiles/builder와 기존 WorldEncounter를 소비하면서 `combat_pockets`와 내부 이동/순환 trails를 추가했다. 수목이 있는 바깥 원 전체를 빈 전투 원으로 가정하지 않는다. Native CDO의 Pawn 프로파일 단순 충돌을 이동 검사로 사용하고 복합 Visibility 엄폐와 구분한다. Terrain 전용 쿼리는 식생/소유 소품을 제외하지만 이동 여유는 모든 월드 기하를 포함한다. 두 전투 틈과 내부 두 길은 반경1.2m/halfheight2m의 보스 후보 capsule을 검사한다. 실제 보스 형상/AI/스킬·다수인원 전투 합격은 아니다. 이 구분과 기존 서버의 실패/종료 소유권은 [게임플레이 통합](07_GAMEPLAY_SPATIAL_INTEGRATION.md)에 기록했다.

보스 prop의 선택적 `surface_mask_radius_m`는 수관의 넓은 배치 여유와 지면의 실제 제외 면적을 분리한다. 고목은 길 탐색의 보수적 렌더 경계 약23m를 유지하면서 지피 제외는3m로 저작했다. 공통 mask baker가 같은 Source 값을 소비하므로 수관 전체 바닥을 정사각형으로 비우지 않는다. 누락 시 기존 소품의 conservative_radius_m 계약을 유지한다. 새 Runtime writer나 전역 PCG 재생성 경로를 추가하지 않았다.


### 호반길과 선착장 지피 제외

S04의 ShoreWalk는 settlement_profiles의 streets 한 곳이 원천이다. 공통 표면 baker·Native 폭/지지 검사·실제 보행기가 같은 점열을 소비한다. 보행기의 `ROUTE=shorewalk`는 별도 이동 시스템이나 순간이동 없이 기존 플레이어 이동을 사용한다. 기존 부두 BankRamp는 경로 탐색용 평면 벽 투영에서만 제외하며 실제 바닥/이동 capsule에서는 그대로 검사한다.

선착장의 shaft/head 제외 면적은 공통 `load_surface_profiles`가 기존 dock 치수에서 파생한다. 별도 복제 프로필이나 흙길은 만들지 않는다. 현재 배치 마스크 해상도와 풀 군집 반경을 고려해4m 여유/1.5m 전이를 적용했다. Native 마스크 두 개를 저장한 후 `grass.FlushCache`로 생성된 Grass 캐시를 무효화한다. Source 마스크 갱신만으로 이미 생성된 풀이 없어졌다고 판단하지 않는다. PCG 숲 재생성이나 수목 제거는 이 경로에 포함되지 않는다.

### 물 표면 내부 단순화와 지형 읽기

`Starter-WetlandGeometry.coarsen_level_surface`는 수위가 일정한 기존 격자 clipping 결과만 받는다. 완전히 젖은16m 블록의 내부를 fan으로 합치고, 이웃이 fine이면 원래2m edge sample을 모두 유지한다. 두 coarse 블록 사이만 긴 edge를 공유한다. 경계 변화/면적 손실/새 내부 열린 edge 없이 정점을 줄이려는 제작 단계이며, 파도 변위·LOD·실제 성능은 Native 검증 대상이다. 기존 완성 물 메시를 자동 재생성하지 않는다.

현재 Landscape의 보호 증거는 Source hash만이 아니라 `landscape_export_heightmap_to_render_target`의 RG16 값을 렌더 프레임 이후 읽어 전체16,265,089점과 대조했다. 같은 프레임의 export 성공만으로 실제 데이터가 유효하다고 판단하지 않는다. 호안 후보는 Saved에만 있으며, 실제 적용은 원본 layer와 독립된 소유 Edit Layer/파생 데이터 및 부두/식생/하천 접속 검증 뒤 수행한다. base layer용 기존 C++ reimport를 독립 레이어 저작으로 오인하지 않는다.
