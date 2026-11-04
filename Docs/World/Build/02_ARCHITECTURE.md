# 시작 권역 제작 구조

## 월드와 데이터

- 런타임 맵: `/Game/World/Starter/Maps/L_StarterRegion`. 단일 Persistent World + World Partition을 기준으로 한다.
- 지역 정의: `/Game/World/Starter/Data/DA_StarterRegion`, 기존 Project_JWorldDefinition을 사용한다.
- 공급자 자산: `/Game/Map_Resource/<Provider>/...`; 공급자 내부 경로와 참조를 유지하고 파생 제작물과 분리한다.
- 추가 공급자 자산: `/Game/ElderboomVillage`, `/Game/Meshes`, `/Game/Megascans`, `/Game/ThirdParty` 및 이들의 `/Game/Materials`, `/Game/Effects`, `/Game/PhysMat` 의존성은 원래 패키지 경로를 유지한다. 기존 자산과 충돌하지 않는 것을 확인했으며 바이너리 문자열 수정이나 중복 사본을 만들지 않았다. 이 경로들도 공급자 영역으로 보호한다. 새 파생 재질/마스크/제작물은 `/Game/World/Starter`에 둔다. 폴더 이름 통일만을 위해 공급자를 다시 대량 이동하지 않는다.
- 원본: `Content/World/MainWorld/Source/MainWorld.plan.json`, `.edges.json`, 높이맵과 바이옴 마스크. 생성 진입점은 `Scripts/World/Generate-StarterWorld.py`이며 런타임에 실행하지 않는다.
- 기존 v3 JSON의 S00–S06, H01–H09, B01–B02를 안정 ID로 유지한다. 현재 경계는 지형을 샘플링한 입지 후보이며 최종 Encounter/Nav 영역은 아니다.

## 지형과 좌표

초기 전체 지형은 4033² 정점 × 200cm XY 간격, 한 변 8,064m이다. 2×2개의 63쿼드 subsection을 가진 32² 컴포넌트를 사용한다. 이는 컴포넌트 1,024개의 비용을 수반하므로 실제 에디터·패키지 프레임/메모리와 외곽 확장 이음을 평가해야 한다. [Epic Landscape Technical Guide](https://dev.epicgames.com/documentation/en-us/unreal-engine/landscape-technical-guide-in-unreal-engine)의 치수·컴포넌트 비용을 기준으로 했다.

입력 JSON의 km를 미터로 바꾸고, 남서 원점·동쪽 +X·북쪽 +Y를 유지한다. Unreal은 중앙 원점 cm로 `((x-4032)*100, (y-4032)*100, z*100)`을 사용한다. 높이맵 행은 +Y이며 문서용 북쪽 위 그림만 행을 뒤집는다. Z Scale 800의 표현 범위는 약 ±2048m이며 16비트 양자화는 약 6.25cm다. 범위 초과를 잘라내지 않고 생성 실패로 처리한다.

산체·분기 능선 계획 → 배수 누적에 따른 제한적 침식 근사·퇴적 사면 완화 → 해안·유역 → 공유 수면 고도의 하천/호수 → 입지와 연결 후보 순서다. 실제 수리 침식·유체 시뮬레이션으로 주장하지 않는다. 강의 수면 메시, 강바닥과 내비게이션·수영·부력은 각기 검증한다.

외곽 4방향의 2m 간격 높이 표본과 도로·강·해상 연결의 위치·방향·폭을 저장한다. 인접 타일의 법선·재질·충돌·수계·PCG 이음 시험이 통과하기 전에는 확장이 검증됐다고 보고하지 않는다. 실험 Mesh Terrain과 Nanite Foliage는 초기 필수 의존성으로 추가하지 않는다.

## 제작과 런타임 책임

정적 배치/PCG는 에디터에서 생성·저장한다. 랜드마크와 주요 도로·도시 구도는 수동 아트 결정을 먼저 한다. 수백 주택을 늘려 시각 품질을 대신하지 않는다. 초기 route spline은 도로 후보 시각화이며 최종 grading·교량·보행/탈것 통과 합격이 아니다.

S00의 필지는 `MainWorld.corridor.json`이 입력이다. `Refine-StarterCorridor.py`는 최초 지형 완화·생활 분지·주도로 grading을 하고, `Compose-StarterHamlet.py`는 높이맵을 건드리지 않고 8개 필지·앞마당 동선·농지 표면 마스크를 조정한다. `Build-StarterCorridor.py`는 명시적으로 실행할 때만 지형 재임포트, 재질, 건물, 소품, 식생, S01 성문 진입 구도를 저장한다. 실행별 단계 상태는 로컬 Saved에 기록하므로 MCP 응답 시간 초과를 취소로 보고 중복 실행하지 않는다. 일반 편집·게임 시작에는 생성 스크립트가 실행되지 않는다.

건물은 개별 Actor와 직렬화된 편집용 컴포넌트다. 공급자 모듈의 실제 Bounds·오프셋 pivot을 적용하며 회전은 축 이름을 명시한다. 식생은 메시 종류·128m 공간 구간별 HISM Actor로 나눈다. 나무/바위의 기존 LOD를 보존하고 풀/밀만 제한된 거리에서 페이드한다. 전체 숲을 하나의 상시 로딩 Actor에 넣지 않는다. 기존 후보 spline은 편집용으로 유지하되 낡은 도로 ribbon은 숨기고 Actor를 editor-only로 전환했다. 실제 첫 두 도로와 마을길은 높이맵 grading과 Landscape 표면 마스크가 소비한다. 전체 후보 도로가 통행 가능한 것은 아니다.

RGB 표면 마스크의 alpha는 불투명으로 저장한다. 투명 PNG의 RGB가 임포트/압축에서 소실되어 길이 나타나지 않은 문제를 실제 화면에서 확인했다. 광장 포석도 같은 Landscape 재질에 혼합하며 반복 평면 수백 개를 겹치는 경로는 제거했다. 공급자 마스터의 변경은 실제 HISM에 필요한 instancing usage 저장에 한정하고 원본 외부 프로젝트를 수정하지 않는다.

S01 접근부는 수로 교차 좌표에서 출발해 북쪽 강둑의 성문 terrace와 실제 석교를 연결한다. `Grade-StarterEntrance.py`는 제한된 건조 필지/진입로 원본을 한 번 수정하며 수로 바닥과 외곽을 보존한다. `Starter-BridgeGeometry.py`는 강둑 높이·camber·6개 아치·난간/기초와 미터 단위 UV를 가진 별도 StaticMesh 및 PBR 재질을 만든다. 기존 공급자 자산을 덮어쓰지 않는다. 보행면의 복잡 충돌을 저장하고 설계 윗면과 실제 trace를 비교한다. HLOD·Nav·탈것 통과는 별도 승인 단계다.

몬스터·보스는 기존 서버 NPC/GAS/AI를 소비하는 별도 런타임 계층이다. WP 로딩과 네트워크 관련성·서버 활성화·보상·영속 상태는 합치지 않는다. 최초 월드에 B01/B02 후보가 있어도 두 보스의 실제 전투가 구현됐다는 뜻은 아니다.

날씨는 기존 WorldState가 시간·강우 전이를 소유한다. 클라이언트가 하늘·구름·강수·바람·재질 젖음·음향을 소비해야 하며 입자 상태를 매 프레임 복제하지 않는다. 기존 Environment의 ClearSunIntensity/RainSunIntensityMultiplier를 사용하고, 새 맵에는 60,000lux와 강우 시 0.25배, 비공간 로딩 PostProcessVolume의 검토용 고정 EV14를 저장했다. 고정 노출은 초기 조명 기준이며 최종 실내외/밤 적응 정책이 아니다. 구름/강수 아트, 실내 차단·지역 전이·늦은 접속·원격 멀티플레이 검증은 후속 작업이다. 두 번째 날씨 관리자를 만들지 않는다.

## Phase 2 바이옴의 실제 소비 경로

평탄화는 제외한다. `Generate-StarterBiomeRules.py`는 현재 높이와 기존 수목 위치를 읽고 schema 2 `MainWorld.biomes.json`의 영역/Seed/제외 규칙을 소비한다. 높이맵을 쓰지 않는다. `T_StarterEcology`의 RGB는 숲 바닥·암반/자갈 성향·수변 습윤이고, `T_StarterPlacement`의 RGB는 적용 영역·식생 허용·기존 수관이다. `T_StarterForestPlacement`는 새 수목·관목 밀도와 암반 성향이다. PNG alpha는 255다. 현재 적용 마스크는 해면을 제외한 전체 높이장 약 91.03%다. 도로·필지·농지·수면·성문 진입부·정착지 중심과 기존 식생을 배제한다. 후속 확장도 입력 범위와 프리셋에서 규칙을 소비하며 기존 월드를 자동 재생성하지 않는다.

`M_StarterTerrain_Biome`는 기존 재질을 보존하고 그 밖의 표면을 유지하는 파생 재질이다. 적용 영역에서 초지/낙엽/암반/습윤 흙의 색·노멀·거칠기와 크기가 다른 텍스처 샘플을 혼합한다. 현재 Landscape의 실제 소비자는 이 재질이며, 전신 `M_StarterTerrain`은 영역 밖 표현의 기준 및 비교용으로 유지한다. 전체 재질 노드 23개, Grass Output 1개의 4개 팔레트 슬롯을 저장했다. Custom HLSL의 텍스처 fetch는 편집기 통계에 모두 반영되지 않을 수 있으므로 노드/명령 수만으로 비용 합격을 판단하지 않는다.

지피 소유자는 Landscape Grass다. Meadow·Forest·Riparian·Alpine의 4종 GrassType에 14 variety/9종 메시를 연결했다. Meadow 주 풀 density는 115이며 PerPlatform과 PerQuality를 함께 저장했다. 예전 수동 풀 30 Actor/21,425개는 정확한 변환을 로컬에 보관한 뒤 퇴역해 중복 생성하지 않는다. 기존 수목 680개·바위 367개·밀 3,765개와 별도 시험 관목 700개는 보존한다.

실제 `PCG_StarterForest`는 13개 native node로 마스크 샘플링→마스크 밀도와 Seed 기반 확률 비교→Landscape 투영→upright yaw/scale 변형→HISM 생성을 수행한다. 약 512m 공간 단위 PCGVolume 170개(전역 대상 169개와 보존한 시험 타일 1개)가 에디터 생성 결과를 소유한다. OnDemand로 저장해 게임 시작 시 전역 재생성에 의존하지 않는다. 수목 3종은 단순 충돌과 QueryAndPhysics를 보존하고 관목 3종은 비충돌이다. 수목 근거리 표현은 1.2~1.8km, 관목은 240~360m 거리 설정이며 별도 원경/HLOD는 아직 제작·검증 대상이다. 입력 제외 마스크와 수동 landmark 소유권을 공유한다.

명시적 제작은 `Build-StarterBiome.py`가 수행한다. 재진입 가드로 import/save 도중 Slate tick이 중첩 실행되지 않게 한다. 빈 Actor는 지속 루트 생성 후 위치를 지정하고 월드 인스턴스를 추가한다. 저장본에서 입력 XY/피벗/회전을 검사한다. 완료된 출력의 무조건 재생성을 막고, 동일 작업의 확인된 실패 단계만 재개한다. 생성 관목의 좌표 수정은 해당 소유 태그의 확인된 출력에 한정한다. 일반 편집/게임 시작에 제작 스크립트를 자동 실행하지 않는다.

`Verify-StarterBiome.py`는 독립 재로딩 후 저장 연결/배치와 높이 지문을 확인한다. `Verify-StarterCorridor.py`도 보관된 전신 재질이 아니라 현재 Landscape의 실제 소비 재질을 확인한다. Grass의 CPU instance count가 0인 것과 GPU 렌더 인스턴스 부재는 동일하지 않다. 현재 반영 API에서 Grass 렌더 개수를 직접 읽지 못했으므로 연결 검사와 실제 PIE 화면 확인을 구분하며 정량 밀도 합격은 보류한다.

## 원경과 성능 검증 조건

사용자의 최신 결정으로 날씨 아트·확장은 보류하고 지형·정착지·자연·원경 제작을 우선한다. 위 환경 기반과 과거 강우 기능 검사 기록은 보존하되 이번 S00 제작의 날씨 완료로 표시하지 않는다.

설치된 5.8의 `PostProcessEyeAdaptation.cpp`는 캐시 조명 기본 EV 범위가 약 -8~12이며 `r.EyeAdaptation.CachedLightingPreExposure=8`일 때 약 -4~16을 지원한다고 명시한다. 새 물리 조명에서 실제 노출 약 12.8EV의 Lumen/실시간 Sky 캐시 클리핑 경고를 확인해 프로젝트 SystemSettings에 8을 설정했다. 경고를 숨기거나 해상도·가시거리·품질 단계를 낮추는 설정이 아니다. 최종 밤/실내 노출 정책은 별도 검증한다.

큰 산·도시·숲 실루엣의 원경 표현과 근거리 메시/충돌/텍스처의 준비를 따로 설계한다. 기본 Streaming Source 위에 기존 속도 방향 준비 소스를 재사용하고, 가속·급회전·상승/하강·순간이동은 실제 측정으로 확장한다. 전역 가시거리 배수 증가·전 액터 상주로 해결하지 않는다.

HLOD 레이어의 설정, 실제 생성, 고속 전환 검증을 구분한다. [Epic PCG/WP 문서](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-pcg-with-world-partition-in-unreal-engine)의 Data Layer/HLOD 연결은 콘텐츠 소유에 사용하며 동적 보스의 상태 대체에 사용하지 않는다.

목표는 사용자가 제시한 큰 월드의 긴 시야·고속 이동 연속성·AAA 지향 품질이다. 특정 상용 게임과 같은 성능을 보장하지 않는다. 현재 PC의 60FPS는 초기 후보이며 최종 사양·해상도·품질 단계별 예산은 패키지 측정 뒤 확정한다.
