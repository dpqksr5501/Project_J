# v4 재사용 대응

기존 상세 근거는 [에셋 감사](../World/Build/01_AUDIT.md), [메시 목록](../World/Build/ASSET_INVENTORY.csv), [잔여 에셋 판단](../World/Build/03_ASSET_GAPS.md)을 따른다. 같은 목록을 새로 전수 조사하지 않는다.

| 기존 자원 | 실제 사용/유지 | v4 후속 작업 |
|---|---|---|
| WorldDefinition/WorldState/Environment | 기존 DA·단일 상태/환경 유지 | 기획 ID alias와 실제 PlaceId 연결; 날씨 추가 보류 |
| WorldAuthoringLibrary | Landscape/WP/검토 spline 도구 | 높이 재임포트 실행 금지; 기존 기능 범위 내 블록아웃 지원 |
| scanned_modular 건축 조립 | S00의 8채, 초가/회벽/목조 | 농업/산악 취락에서 데이터 기반 재사용; 처마 접합 결함 보정 필요 |
| MedCastle | S01 33개 필지·성문·성채 | 도시/관문 블록아웃; 닫힌 저층·출입 연결은 미검증 |
| MedievalVillage | 시장 노점·배럴·벤치 | 작업장/시장·소취락 소품 |
| Brushify/Elderboom/Megascans 지피 | 4팔레트·9 메시, 연결 저장 | 현재 지피 표시 부족 조사; 밀도 숫자로 완료 주장 금지 |
| PCG_StarterForest | 13 처리 노드/170타일, 수목 69,692·관목 218,369 | 새 거점/전투 공간 제외를 영향 타일에만 갱신 |
| S01 6아치 석교 | 실제 Saved Mesh/9개 충돌 표본 검증 | 석교·목교·산악 다리의 3종 구조로 확장 |
| 기존 GAS/AI/NPC/퀘스트/탈것 | 계약 보존 | 관련 실제 호출자/데이터를 필요한 범위에서 조사해 통합; 새 Spawn/보상 관리자 복제 금지 |

외부 GASP는 제외한다. 외부 공급자 프로젝트를 변환하거나 덮어쓰지 않는다. 실제 채택 전에 필요한 메시/재질/의존성만 MCP로 확인한다. 새 구매/라이선스 수락은 별도 사용자 승인 범위다.

H03은 현재 BlackAlder 3종 중 기존 나무6곳을 엄폐로 참조하고, 검증된 TreeStumpA와 Brushify 바위를 소량 재사용했다. Native simple collision 캡슐의 실제 반경/길이/회전과 인스턴스 위치를 읽어 숲길을 조사했으며 공급자 충돌/메시나 기존 식생을 변경하지 않았다. 스폰/이동 캡슐과 나무 차단 검사는 저작 검증이며 최종 AI/Projectile/시야 평가와 구분한다.

H02는 MCP 썸네일·Bounds/재질·simple collision을 확인한 Elderboom `SM_Modular_Fence_ufflaitdw_lod3_Var1`을 목책 기둥으로, 기존 SkyTown 천막04·수레/자루·Elderboom 식은 화덕·Brushify 바위와 PBR 목재를 재사용했다. 기둥은 높이2.7m/폭0.32m로 저작한 인스턴스이며 공급자 메시/충돌은 변경하지 않았다. `PalisadeStrutB`는 완제품 벽 패널이 아닌 단일 막대였고 `PalisadeSpike`의 메시 파일은 기존 공급 폴더에도 없어서 재질 이름만 보고 가정해 사용하지 않았다. 새 외부 에셋을 복사하거나 프로젝트를 변환할 필요는 없었다.

S02는 기존 회벽/목조/박공/초가와 우물·밀을 재사용했다. 기존 SkyTown 풍차 두 파츠는 목표의 전통 4날 풍차와 다른 형상이라 그대로 채택하지 않았으며, 현재는 기존 PBR 목재 재질과 92개 슬랫으로 구성한 명시적 시설 블록아웃이다. 완제품 풍차 아트/회전·기능은 미완료다. 실제 눈높이에서 초가 skin의 덮임 부족도 확인돼 공통 지붕의 닫힌 하부 구조와 coverage/LOD/재질 검증이 필요하다.

S03 숲 마을은 같은 건축 키트와 SkyTown 장작/수레, 의존성이 완전한 TreeStumpA, BlackAlder 3종을 사용한다. TreeLog·TreeStumpB·WoodStump는 누락 dependency가 있어 제외했다. 실제 선택/크기/거부 사유는 `forest-village-preflight.json`에 기록했다. 기존 PCG 전역 숲을 재생성하지 않고 예약 마을 경계 안에 길/집/작업장 제외를 적용한 수목 HISM을 추가했다.

S04 호반은 확인된 SkyTown 소형 배·말린 생선 2종·배럴/상자와 기존 목재/석재 재질을 재사용했다. 부두/보/기둥/난간은 PBR 목재 재질을 입힌 명시적 형상 블록아웃이다. Native 크기/재질과 기존 물 collision 상태는 `lake-village-preflight.json`에 기록했다. 최종 부두 키트/배 운항·물 표현 완료가 아니다.

S05 하구항은 Native `harbor-preflight.json`에서 MedCastle 감시탑의 크기/4개 재질과 SkyTown 배·화물 크기를 확인해 사용했다. 기본 박스+기존 PBR 목재는 약 120m 부두/지지/진입판의 블록아웃이다. 공급자 원본/강 표면은 수정하지 않았다.

S06 산악은 완전한 의존성을 확인한 GraniteRockAssembly/HovsBeachRock·BlacksmithAnvilA와 SkyTown 수레를 재사용했다. MossyRocksA는 누락 의존성이 있어 제외했다. 난간 전망대/지지/계단은 기존 PBR 목재/석재의 형상 블록아웃이다. 새 광물/광산 전용 완성 키트를 확보하거나 채집 기능을 구현한 상태가 아니다.

S07은 기존 건축·우물/배럴/상자와 PBR 목재/석재를 재사용했다. 110m 목교는 인스턴싱한 판재·말뚝·보/난간·진입판의 실제 저장 형상이며, 원본 공급자 메시를 수정하거나 별도 외부 프로젝트를 변환하지 않았다.

S09는 이전에 가져온 SkyTown 천막 01/03/04, Banner01, MedCastle MainTower2Doors/목재/석재, 기존 모듈 건축과 상자/배럴을 재사용했다. 공급자 원본은 수정하지 않았다. 천막 천은 시각용 NoCollision, 바닥/계단은 실제 충돌을 가진 개별 기초다.

S08은 기존 건축 키트와 SkyTown 천막04·탁자/벤치·수레/자루, MedCastle PBR 목재·공유 석재를 재사용한다. 망루는 실제 충돌 판재/계단과 기둥/보/난간/지붕을 가진 목조 구조 블록아웃이다. BellTower01은 Native bounds가 높이 약107m여서 이번 소형 감시탑에 채택하지 않았다.

H04의 육상 첫 배치는 이미 검증된 TreeStumpA·Brushify 바위6개를 재사용했다. 기존 식생659개를 그대로 두고 새 외부 에셋을 가져오지 않았다. 물 메시를 자산 이름만으로 수영 가능한 호수라고 간주하지 않고 Native Cylinder의 크기/회전/위치/충돌과 수면19곳을 확인했다. 호안 높이 결함과 불투명 물 표현은 미완료로 유지한다.

H05는 현재 프로젝트에 이미 있는 AncientTempleRuins의 `SM_Facade_Custom_01`(약4.73×2.39×3.60m)·`SM_Pillar_Cube_01_A`(약1.37×1.37×0.94m)를 MCP 썸네일·재질·Bounds/BodySetup으로 확인하고 파손 벽체2/낮은 석재 조각4로 사용했다. 기둥 원형은 낮은 블록이므로 완전한 높은 석주로 오인하지 않았다. 공급자 충돌은 각각 단일 box이며 실제 simple Visibility 차단을 따로 확인했다. 새 수목260개는 BlackAlder Forest01/Field04/Field02의174/38/48개 변형이고 서로 다른 생물학적 수종3개라는 뜻은 아니다. 기존 TreeStumpA/장작 더미도 재사용했다. 이 단계에서 새 외부 복사는 필요하지 않았으며 공급자 프로젝트/메시/충돌을 수정하지 않았다.

H06 물가 지피는 기존 Elderboom GrassClumps Var1/Var3(439/415개)와 Megascans FernB00(146개)을 사용한다. Native 크기/재질·MCP 썸네일을 확인하고 FernB의 기존 공급자 dependency closure36개에 누락이 없는지 대조했다. 두 얕은 물 표면만 소유 StaticMesh476/1199삼각형으로 생성하고 기존 임시 물 재질을 재사용했다. 공급자 프로젝트/메시를 복제하거나 수정할 필요는 없었다. 고사리/짧은 풀을 갈대·부들로 오인해 기록하지 않으며, 젖은 흙/하층 식생 다양성과 최종 물 셰이더는 후속이다.

H07은 MCP 썸네일/Native bounds·재질·ComplexAsSimple 충돌을 확인한 Brushify S_Cliff_Large_01을 능선 아래 암벽으로 사용했다. 변형 scale4×5×4와 상단43.25m 정렬의 명시적 첫 배치이며 공급자 메시/충돌을 변경하지 않았다. 실제 콜라이더에서 상단37.77m→주변 지면14.47m의 급격한 낙차를 확인했다. 기존 SkyTown 천막04·과일상자/배럴과 Brushify 작은 바위도 재사용했다. 최종 절벽군/암석-지형 접합·야영 아트/생활 세부는 후속이다.

H08 광산은 기존 Elderboom 스캔 목재 SM_Worn_Wooden_Beam을 지지틀6조/18개와 산길 난간106개에 재사용했다. 광산 외피/바닥/벽/천장은 원본 지형을 건드리지 않는 소유 StaticMesh이고, 타일 가능한 Brushify T_CliffSurface_D/N으로 기존 아틀라스 UV 오용을 수정했다. 계단은 기존 석재 PBR의 HISM701개로 만든 초기 구조이며 무늬 반복/옹벽/암석 접합과 최종 재질을 보완해야 한다. 작업장에는 기존 SkyTown SM_Cart01·FruitCrate01·Barrel03과 작은 Brushify Rock02를 사용했다. 공급자 원본/충돌은 수정하지 않았고 새 외부 복사는 필요하지 않았다. 미확인 에셋을 가져왔다고 기록하지 않는다.

H09는 공급자 assets.uproject(UE5.8)의 기존 MCP 플러그인을 사용해 보조 에디터/포트8001로6개 유적 메시의 실제 dependency closure를 확인했다. 누락0/외부 Game 의존성0의40패키지(약728MB)를 원래 /Game/AncientTempleRuins 경로로 복사했고 모든 파일의 바이트 지문을 대조했다. 원본 프로젝트/기존 H05의 Map_Resource 경로는 덮어쓰지 않았다. 현재 프로젝트 MCP에서는6개 메시의 bounds/재질/BodySetup을 확인했다. 실제 높이4.86m 석주·2.61m 파손 석주·5.12m 벽·프레임형 문·6.02m 부조 바닥을13개 유적 부재로 배치했다. 8.03×5.01×2.18m 계단은 향후 유적 제작용으로 보존했으며 이번에 연결/배치했다고 세지 않는다. 낮은 기존 기둥을 늘려 높은 석주로 대신하지 않았다. 현재 단순한 기둥/벽 구도와 풀-부재 접합은 최종 유적 아트 전이다.


B02는 같은 UE5.8 공급자 MCP8001에서 Ceiba Tree01 A와 Roots01 B/Roots05의3메시·32개 의존 패키지(새 파일18개/213,315,129byte)를 확인·복사했다. 누락/외부 Game 의존성은0이고 원본 파일은 변경하지 않았다. Native bounds/충돌/재질과 MCP 썸네일을 확인했다. 고목 원형은 약27.0×32.4×28.7m/convex2개이며1.1배로 사용했다. 첫 회색 썸네일은 이후 녹색 잎·수피 재질로 렌더링됨을 다시 확인했으며 재질 원본을 바꾸지 않았다. 두 뿌리 원형은 높이 약14.3/15.5m의 아래로 뻗는 부재여서 전부 지상에 실물 크기로 세우지 않고0.25/0.28배의 독립 노출 뿌리 엄폐로 배치했다. Native convex26/31개를 유지했다. H09 때 가져온 프레임형 문·벽·낮은 파손 석주는 재사용했다. 현재6개 환경 부재의 첫 구도이며 고목/뿌리-지형 접합과 유적 아트는 후속이다. 근거: `B02-forest-{seeds,transfer,asset-preflight}.json`, `B02-boss-build.json`.
