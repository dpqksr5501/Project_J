# 오픈 월드 레벨 디자인과 최적화 조사 자료

정리일: 2026-10-16. 앞선 대화의 엔진·최적화·개발 사례·로컬 에셋 조사와 이번 레벨 디자인 자료를 한곳에 연결한다. Project J의 적용 기준은 [오픈 월드 레벨 설계와 확장 계획](Open_World_Level_Design.md)을 따른다.

출처의 설명과 프로젝트 설계 제안, 기술 데모와 출시 게임, 사용자 자가 보고와 프로젝트 실측을 구분한다. 실시간으로 갱신되는 문서는 도입 시 설치 엔진·플러그인·문서 버전을 다시 확인한다. 외부 페이지·프로젝트·영상 전체를 복제하지 않고 링크와 필요한 요점만 보관한다.

## Unreal Engine

| ID | 원문 | 읽을 내용과 적용 한계 |
| --- | --- | --- |
| T01 | [World Partition](https://dev.epicgames.com/documentation/en-us/unreal-engine/world-partition-in-unreal-engine) | 지속 레벨·셀·Streaming Source·텔레포트 준비·OFPA 연계. 월드 스트리밍과 서버 분산은 별도 책임 |
| T02 | [Landscape Technical Guide](https://dev.epicgames.com/documentation/en-us/unreal-engine/landscape-technical-guide-in-unreal-engine) | 정점·쿼드·컴포넌트·높이 범위·권장 규격. 해상도와 실제 길이를 구분 |
| T03 | [World Partition HLOD](https://dev.epicgames.com/documentation/en-us/unreal-engine/world-partition---hierarchical-level-of-detail-in-unreal-engine) | 원거리 셀의 정적 메시 대체 표현·레이어·빌드. 무조건 성능 개선을 보장하지 않음 |
| T04 | [Data Layers](https://dev.epicgames.com/documentation/en-us/unreal-engine/world-partition---data-layers-in-unreal-engine) | Editor/Runtime 레이어·게임 상태와 로딩. 개인 퀘스트 격리와 동일 기능으로 보지 않음 |
| T05 | [Level Instancing](https://dev.epicgames.com/documentation/en-us/unreal-engine/level-instancing-in-unreal-engine) | 건물·POI 저작 재사용·Embedded/Streaming 동작. 파티별 서버 인스턴싱과 구분 |
| T06 | [Water System](https://dev.epicgames.com/documentation/en-us/unreal-engine/water-system-in-unreal-engine) | 강·호수·바다·수면·물리 상호작용 기반. 수영·항해 규칙은 따로 구현 |
| T07 | [Water Body Actors](https://dev.epicgames.com/documentation/en-us/unreal-engine/water-body-actors-in-unreal-engine) | River·Lake·Ocean의 저작·지형과 수면 연결. 보유 강 스플라인과 역할 비교 |
| T08 | [Foliage Mode](https://dev.epicgames.com/documentation/en-us/unreal-engine/foliage-mode-in-unreal-engine) | Static Mesh/Actor Foliage·인스턴싱·컬링·페이드·별도 식생 그리드. 일반 거리 페이드와 Nanite의 지원 차이 확인 |
| T09 | [Impostor Baker](https://dev.epicgames.com/documentation/en-us/unreal-engine/impostor-baker-plugin-in-unreal-engine) | 방향별 캡처를 이용한 원거리 표현·프리셋·일괄 생성. 상하 관찰 각도와 실루엣 확인 |
| T10 | [Nanite Foliage](https://dev.epicgames.com/documentation/en-us/unreal-engine/nanite-foliage) | Assemblies·Voxels·Skinning·바람과 식생 제작. 확인한 UE 5.8 문서상 Experimental |
| T11 | [Lumen Technical Details](https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-technical-details-in-unreal-engine) | 빠른 카메라 이동 갱신·큰 월드·HWRT Far Field·HLOD1 등 조건. 조명 범위와 물체 가시 거리를 구분 |
| T12 | [Landscape Visibility Tool](https://dev.epicgames.com/documentation/en-us/unreal-engine/landscape-visibility-tool-in-unreal-engine) | Landscape 구멍과 정적 메시 동굴·터널 연결. 높이맵만으로 지하 전체를 제작하지 않음 |
| T13 | [FastGeo Streaming 플러그인](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/PluginIndex/FastGeoStreaming) | 파티션 월드 지오메트리의 스트리밍 최적화. 확인한 UE 5.8 문서상 Experimental |
| T14 | [World Partitioned Navigation Mesh](https://dev.epicgames.com/documentation/en-us/unreal-engine/world-partitioned-navigation-mesh) | Navmesh chunk·생성 모드·메모리·빌드. 확인한 UE 5.8 문서상 Experimental |
| T15 | [PCG Overview](https://dev.epicgames.com/documentation/en-us/unreal-engine/procedural-content-generation-overview) | 규칙 기반 포인트·에셋 배치와 그래프. 배치 도구의 사용 자체가 성능 검증을 대신하지 않음 |
| T16 | [Texture Streaming Overview](https://dev.epicgames.com/documentation/en-us/unreal-engine/texture-streaming-overview-for-unreal-engine) | 요구 해상도·밉·메모리 예산·우선순위. 텍스처 선명도 전환과 액터 팝인을 구분 |
| T17 | [Unreal Insights](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-insights-in-unreal-engine) | Timing·Memory·Asset Loading·Networking 분석. 실제 경로·조건을 붙여 측정 |
| T18 | [Virtual Shadow Maps](https://dev.epicgames.com/documentation/en-us/unreal-engine/virtual-shadow-maps-in-unreal-engine) | 캐시 무효화·식생 변형·그림자 비용·시각화. Static/Rigid 설정의 시각적 결과를 확인 |
| T19 | [3D 그리드 Runtime Hash 도입 설명](https://dev.epicgames.com/documentation/ko-kr/unreal-engine/unreal-engine-5.4-release-notes?application_version=5.4) | UE 5.4의 3D 런타임 해시 설명. 현재 프로젝트 맵의 실제 Runtime Hash·상하 셀 생성은 별도 확인 |
| T20 | [Working with Nanite-Enabled Content](https://dev.epicgames.com/documentation/en-us/unreal-engine/working-with-naniteenabled-content) | Nanite 콘텐츠 제작·식생의 겹침과 표현. 폴리곤 수만으로 비용 판단 금지 |
| T21 | [Programming Subsystems](https://dev.epicgames.com/documentation/en-us/unreal-engine/programming-subsystems-in-unreal-engine) · [UWorldSubsystem](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/UWorldSubsystem) | 엔진이 관리하는 수명과 확장 지점. 월드별 런타임과 GameInstance 수명을 구분하며 복제는 별도 설계 |
| T22 | [Game Mode and Game State](https://dev.epicgames.com/documentation/en-us/unreal-engine/game-mode-and-game-state-in-unreal-engine) | 서버 규칙·공개 복제 상태·GetServerWorldTimeSeconds. 프로세스 재시작·여러 서버의 영속 시각과는 별도 |
| T23 | [Sky Atmosphere](https://dev.epicgames.com/documentation/en-us/unreal-engine/sky-atmosphere-component-in-unreal-engine) · [Volumetric Cloud](https://dev.epicgames.com/documentation/en-us/unreal-engine/volumetric-cloud-component-in-unreal-engine) | 하늘·구름·조명 표현과 품질 조절. 날씨의 서버 상태·지역 전이·게임 규칙은 프로젝트 책임 |
| T24 | [Asset Management](https://dev.epicgames.com/documentation/en-us/unreal-engine/asset-management-in-unreal-engine) | Primary Asset·Soft Reference·Bundle·비동기 로드와 유지. 월드 셀 로딩과 자산 로딩의 책임 구분 |
| T25 | [PSO Precaching](https://dev.epicgames.com/documentation/en-us/unreal-engine/pso-precaching-for-unreal-engine) | 렌더 상태의 사전 수집·비동기 컴파일과 검증. 자산 로드 완료만으로 첫 표시 준비 완료를 가정하지 않음 |
| T26 | [Replication Graph](https://dev.epicgames.com/documentation/en-us/unreal-engine/replication-graph-in-unreal-engine) | 연결별 복제 목록·영속 노드·프로젝트별 분류. Iris와 무조건 겹쳐 켜는 최적화 묶음으로 취급하지 않음 |
| T27 | [Introduction to Iris](https://dev.epicgames.com/documentation/unreal-engine/introduction-to-iris-in-unreal-engine?lang=en-US) | opt-in 설정·filter·prioritization·등록. 확인한 UE 5.8 페이지상 Experimental이며 실제 프로젝트 런타임 연결은 별도 검증 |
| T28 | [Significance Manager](https://dev.epicgames.com/documentation/en-us/unreal-engine/significance-manager-in-unreal-engine) | 중요도 평가와 프로젝트별 소비 정책. 카메라 밖이라는 이유만으로 권위 전투·피해 판정을 중단하지 않음 |
| T29 | [Landscape Materials](https://dev.epicgames.com/documentation/en-us/unreal-engine/landscape-materials-in-unreal-engine) | 지형 레이어·텍스처 혼합과 재질 구성. 현재 패스는 원본 텍스처를 재사용하는 별도 지형 재질이며 vendor master를 수정하지 않음 |
| T30 | [Landscape Splines](https://dev.epicgames.com/documentation/en-us/unreal-engine/landscape-splines-in-unreal-engine) · [Edit Layers](https://dev.epicgames.com/documentation/en-us/unreal-engine/landscape-edit-layers-in-unreal-engine) | 지형 편집을 분리하고 도로·지형 연결을 제작하는 엔진 경로. 현재 저장형 SplineMesh ribbon과 Landscape Splines 전용 도구를 동일 기능으로 기록하지 않음 |
| T31 | [PCG in Electric Dreams](https://dev.epicgames.com/documentation/unreal-engine/procedural-content-generation-in-electric-dreams?lang=en-US) | 수작업 조합을 Point Data로 재사용하고 군집·변형·계층 규칙을 적용한 실제 샘플 구현. 현재 A 패스에 샘플 전체나 Soundscape가 도입된 것은 아님 |

엔진 5.8 공식 문서의 실험 상태와 예제 프로젝트가 처음 공개된 버전의 상태를 섞지 않는다. 특히 Electric Dreams의 오래된 소개 페이지에 적힌 PCG 상태를 현재 엔진 전체의 상태로 옮기지 않는다.

## 개발 사례와 샘플

| ID | 원문 | 참고할 구현과 한계 |
| --- | --- | --- |
| C01 | [CDPR 위쳐 4 기술 데모 개발 설명](https://www.cdprojektred.com/en/blog/149/working-with-epic-to-debut-the-witcher-4-unreal-engine-5-tech-demo-at-unreal-fest) · [공식 발표](https://www.cdprojekt.com/en/media/news/cd-projekt-red-and-epic-games-present-the-witcher-4-unreal-engine-5-tech-demo-at-the-state-of-unreal-2025/) | FastGeo·Nanite Foliage 등 조합. 완성 게임이나 MMORPG 부하 검증으로 해석하지 않음 |
| C02 | [Bringing Nanite to Fortnite Chapter 4](https://www.unrealengine.com/tech-blog/bringing-nanite-to-fortnite-battle-royale-in-chapter-4) | 출시 게임의 지형·식생·재질 제작 변경. 당시 엔진·아트 조건의 기록 |
| C03 | [Virtual Shadow Maps in Fortnite Chapter 4](https://www.unrealengine.com/tech-blog/virtual-shadow-maps-in-fortnite-battle-royale-chapter-4) | 낮밤·식생 바람·그림자·물·잔디의 실제 선택과 비교. 설정을 다른 게임에 일괄 복제하지 않음 |
| C04 | [Electric Dreams Environment](https://www.unrealengine.com/electric-dreams-environment?lang=en-US) | 다운로드 가능한 PCG·숲·절벽·개울 샘플. 전체 환경과 4km × 4km PCG 레벨·작은 예제 분리. 엔진 버전 차이 확인 |
| C05 | [SideFX Project Pegasus](https://www.sidefx.com/pegasus/) · [Roads, Tracks and Rivers](https://www.sidefx.com/tutorials/project-pegasus-roads-tracks-and-rivers/) | 4km 폭 섬·계곡·숲·흙길·강과 Unreal 편집 도구. Houdini 기반 제작 사례이며 필수 도구 결정은 아님 |
| C06 | [SideFX Project Titan](https://www.sidefx.com/titan/) · [Building Tool](https://www.sidefx.com/tutorials/project-titan-building-tool/) | 모듈 건물·인스턴싱·데이터 테이블을 연결하는 제작 강의와 프로젝트 자료 |
| C07 | [Ryan Brucks Octahedral Impostors](https://shaderbits.com/blog/octahedral-impostors) · [공개 구현](https://github.com/ictusbrucks/ImpostorBaker) | 제작자 직접 설명·UE4 시절 구현. 원리 학습용이며 현 엔진 내장 Baker와 비교 |
| C08 | [개발자 HLOD 설정 경험](https://www.reddit.com/r/unrealengine/comments/1gdkqwa) | 사용자 제작 경험과 설정 가이드 공유. 검색으로 확인한 범위이며 본문 도구 접근 실패로 전체 내용·수치는 미검증 |
| C09 | [대규모 식생 최적화 사용자 경험](https://www.reddit.com/r/unrealengine/comments/mutc6t) | 재질·임포스터·스트리밍 개선을 공유한 구버전 사례. 전체 본문 접근·수치 재현은 미검증. 제목의 식생 수를 프로젝트 성능 근거로 사용하지 않음 |
| C10 | [붉은사막 BlackSpace 개발 자료](https://crimsondesert.pearlabyss.com/en-US/News/Notice/Detail?_boardNo=40) | 개발사가 공개한 물 흐름·안개·대기·표현 기술. 자체 엔진의 소개이며 내부 스트리밍 전체를 공개한 자료가 아님 |
| C11 | [검은사막 공식 가이드](https://www.kr.playblackdesert.com/ko-KR/Wiki/Default/) · [바아마키아 공식 개발 이력](https://www.kr.playblackdesert.com/ko-KR/Adventure/History?_groupMasterNo=5696) | 월드·필드 우두머리·대양·던전의 경험 참고. 바아마키아의 공유 필드/보스 공간 설명은 2021년 도입 당시 이력이며 현재 전체 던전 정책으로 일반화하지 않음 |
| C12 | [Tom Looman UE 5.8 Performance Highlights](https://tomlooman.com/unreal-engine-5-8-performance-highlights/) | 개발자가 정리한 성능 변경 해설. 조사 진입점으로 사용하고 엔진 동작·옵션은 해당 버전의 공식 문서·소스로 확인 |
| C13 | [Guerrilla: GPU-Based Procedural Placement in Horizon Zero Dawn](https://www.guerrilla-games.com/read/gpu-based-procedural-placement-in-horizon-zero-dawn) | 개발사가 공개한 규칙 그래프·아티스트 저작·조밀한 환경의 절차 배치 사례. Project J에 동일 GPU 알고리즘이나 출시 게임 성능을 구현했다고 해석하지 않음 |

주요 학습 순서는 C01의 기술 조합 → C02·C03의 실제 비용과 제작 선택 → C04의 엔진 샘플 → C05의 지형·강·길 저작 → C07의 먼 식생 표현이다. 커뮤니티 글은 문제 후보를 찾는 자료로 사용하고 실제 개선 여부는 Project J에서 측정한다.

## UE 5.8 식생 추가 확인

- T32 — [Nanite Foliage](https://dev.epicgames.com/documentation/en-us/unreal-engine/nanite-foliage): Assemblies·Voxels·Skinning은 별도 제작 체계이며 공식 문서상 Experimental이다. 이번 정적 Nanite 메시/PCG 사용과 구분한다. 기존 나무를 가져왔다고 이 체계가 자동 적용되지는 않는다. 대표 수종 한 개에서 원경 부피·그림자·메모리·바람 비용을 비교한 뒤 도입 여부를 정한다.
- T33 — [Working with Nanite-Enabled Content](https://dev.epicgames.com/documentation/unreal-engine/working-with-naniteenabled-content): 잎과 풀처럼 조각난 기하의 과도한 겹침은 Nanite에서도 비용이 커질 수 있다. Preserve Area와 WPO 범위·거리, 그림자 비용을 실제 에셋에서 확인하며 Nanite 활성화 자체를 성능 검증으로 계산하지 않는다.

## 레벨 디자인

2026-10-17 지표·밀도·측정 추가 확인:

- T34 — [Grass Quick Start](https://dev.epicgames.com/documentation/en-us/unreal-engine/grass-quick-start-in-unreal-engine): Landscape Grass Type과 Grass Output으로 지형의 분포 신호를 실제 근거리 풀로 연결한다. A의 넓은 지피층과 PCG의 장소별 상세 군집은 제외 마스크로 소유 영역을 나눈다.
- T35 — [Landscape Materials](https://dev.epicgames.com/documentation/en-us/unreal-engine/landscape-materials-in-unreal-engine): 근거리 디테일과 원거리 매크로 텍스처를 함께 사용한다. 지면의 색·노멀과 실제 중간 크기 굴곡은 별도로 저작하며, 노멀만으로 실루엣을 바꿨다고 계산하지 않는다.
- T36 — [Unreal Insights](https://dev.epicgames.com/documentation/unreal-engine/unreal-insights-in-unreal-engine?lang=en-US): CPU/GPU와 비동기 작업의 시간 분석 근거. 에디터 콜백의 벽시계 간격을 패키지 게임의 GPU 프레임 시간으로 쓰지 않는다.
- T37 — [Virtual Shadow Maps](https://dev.epicgames.com/documentation/en-us/unreal-engine/virtual-shadow-maps-in-unreal-engine): WPO와 움직이는 오브젝트의 캐시 무효화 비용을 확인한다. A의 그림자 A/B 측정과 수목 WPO 거리 제한에 적용하며, 그림자를 잠시 껐던 진단 조건과 저장된 최종 표현을 구분한다.

| ID | 원문 | Project J에 적용할 지식 |
| --- | --- | --- |
| D01 | [The Level Design Book Pre-production](https://book.leveldesignbook.com/process/preproduction) | 메커니즘·경험 목표·핵심 기준·범위를 먼저 정하고 이동 시험 공간을 만든다 |
| D02 | [Layout](https://book.leveldesignbook.com/process/layout) | 큰 공간 관계·동선·랜드마크·목표를 읽을 수 있는 배치 초안 |
| D03 | [Blockout](https://book.leveldesignbook.com/process/blockout) | 단순한 3D 덩어리로 구조와 이동을 수정·시험하고 세부 아트 비용을 늦춘다 |
| D04 | [Flow](https://book.leveldesignbook.com/process/layout/flow) | 공간 사이를 이동하는 느낌을 배치 단계부터 설계하고 블록아웃·플레이로 확인 |
| D05 | [Metrics](https://book.leveldesignbook.com/process/blockout/metrics) | 실제 플레이어 물리 수치와 제작 치수·권장값을 구분. 탑승·비행·카메라 치수로 확장 적용 |
| D06 | [Wayfinding](https://book.leveldesignbook.com/process/blockout/wayfinding) | 위치와 목적지 이해·랜드마크·경로 정보. 자료 속 비율은 과학적 성공률로 쓰지 않음 |
| D07 | [Playtesting](https://book.leveldesignbook.com/process/blockout/playtesting) | 경험 목표와 실제 행동을 비교하는 반복 검증 |
| D08 | [World of Level Design 11단계 계획](https://worldofleveldesign.com/categories/level_design_tutorials/how-to-plan-level-designs-game-environments-workflow.php) | 주제·참고·목표·장소·배치·자산 계획을 정리하는 저자 작성 강의 |
| D09 | [Ubisoft The Wild World of Riders Republic](https://news.ubisoft.com/en-ca/article/38It3kpo4GJgTLP6S3SdNA/the-wild-world-of-riders-republic) | 개발진 인터뷰. 다양한 이동 방식과 지형·여러 진입·랜드마크·반복 조정을 함께 설계한 사례 |
| D10 | [Ubisoft Looking Back on 10 Years of Assassin's Creed](https://news.ubisoft.com/en-us/article/6P2OoMbx17TavKwwHev2eT/looking-back-on-10-years-of-assassins-creed) | 개발 책임자의 설명. Origins의 절차 월드와 수작업 도시·랜드마크·지형 조합, 경험과 시스템의 관계 |
| D11 | [USGS Rivers, Streams, and Creeks](https://www.usgs.gov/water-science-school/science/rivers-streams-and-creeks) | 강·하천의 기본 지리 참고. 게임 내 수치·유체 시뮬레이션 요구를 제공하는 자료는 아님 |

The Level Design Book은 저자들이 정리한 설계 자료다. 모든 항목이 완성된 표준·실험 결과인 것은 아니므로 원문 문맥과 예시의 출처를 함께 읽는다. Project J의 순환도로·수계·진입 설계는 위 자료를 참고한 제안이며 해당 게임들이 같은 구조를 썼다는 주장이 아니다.

## 2026-10-17 MMORPG 수직 탐험 추가 조사

| ID | 개발사 원문 | 확인한 내용과 Project J의 적용 판단 |
| --- | --- | --- |
| D12 | [ArenaNet: Making the Most of Maguuma](https://www.guildwars2.com/en/news/gameplay-features-making-the-most-of-maguuma/) | 수관·지상·뿌리층의 생태와 활동을 구분하고 보행로·다리로 장소를 연결한다. A의 숲 전망 동선, 지하 분기와 상층 복귀로를 서로 다른 경험으로 만드는 데 참고했다. GW2의 이동 숙련 시스템을 구현했다는 뜻은 아니다. |
| D13 | [ArenaNet: Flashpoint on Guild Chat](https://www.guildwars2.com/en-gb/news/flashpoint-on-guild-chat-a-summary/) | Draconis Mons 제작진이 동굴의 층·생태와 탐험, 이동 경로의 가독성을 설명한다. A에서는 주 동선과 아래층 막다른 탐험 가지를 구분하고 주 동선에 복귀하는 고리를 둔다. |
| D14 | [Blizzard: Zaralek Cavern](https://news.blizzard.com/en-gb/article/23934660/updated-april-27-zone-preview-uncover-the-mysteries-of-zaralek-cavern) | 넓은 지하의 비행, 지형 장애물, 거주지·교역·발견 활동이 함께 소개된다. A에서도 지하를 통로 한 개로 끝내지 않고 공동·상층 관찰·하층 가지로 구성한다. 공식 소개는 엔진 내부 스트리밍 방식을 설명하지 않는다. |
| D15 | [Blizzard: The War Within 지역 소개](https://worldofwarcraft.blizzard.com/en-us/news/24125258) | 지상에서 여러 지하 지역으로 이어지며 산업·거주·생태 배경이 달라진다. A의 폐광과 유적 동굴은 같은 기능의 지하 복사본에 머물지 않도록 이후 소품·활동의 지역 정체성을 발전시킬 기준으로 사용한다. |
| D16 | [Pearl Abyss: Kamasylvia Monster Zones](https://www.sa.playblackdesert.com/es-Mx/Wiki?wikiNo=180) | Gyfin Rhasia의 지상과 지하는 공간·전투 역할이 구분된다. A의 상층 순환로와 하층 탐험 가지의 목적을 구분하는 참고다. 공식 플레이 가이드에서 관찰한 구조를 설계에 해석해 적용했으며 내부 레벨 제작 문서나 최신 밸런스 수치의 복제는 아니다. |
| D17 | [Kristy Moret: The Ringing Deeps 제작 기록](https://kristytacular.artstation.com/projects/6LboRV) | 담당 Lead World Artist가 2D 지도·3D 배치와 생태별 아트 데모 제작을 직접 설명한다. 천창의 자연 생태와 채굴 산업의 대비가 핵심이다. A에서도 지형 높이만 늘리기보다 장소별 배경·구조·시각적 목표를 먼저 정하고 이동 시험 후 고유 아트를 발전시키는 기준으로 삼았다. |

자료에서 확인한 사례와 A의 구현 판단을 구분한다. 이번 수직 패스는 실제 지형 입구·동굴 충돌·보행 구조와 길 찾기 저작이며, 위 게임들의 전투·퀘스트·보상·비행 규칙 전체를 이식하지 않는다. 지하 면적을 추가한 것만으로 콘텐츠 밀도가 완성되었다고 평가하지 않는다.

## 2026-10-17 지형·도시 가로 추가 조사

| ID | 직접 자료 | 확인한 내용과 A의 적용 판단 |
| --- | --- | --- |
| D18 | [ArenaNet: Designing the New Lion’s Arch](https://www.guildwars2.com/en/news/hidden-arcana-designing-the-new-lions-arch/) | 환경 아트 책임자가 서비스의 기능별 묶음, 알아보기 쉬운 랜드마크, 모임 공간과 성능 부담을 함께 설명한다. A의 시장·작업·주거 가로 구분과 중앙 모임 공간에 참고했다. 현재 상점 앵커는 배경 저작이며 실제 거래 기능과 연결되지 않았다. |
| D19 | [Blizzard: Tiragarde Sound Visitor’s Guide](https://news.blizzard.com/en-gb/article/21952595/battle-for-azeroth-tiragarde-sound-visitors-guide) | Boralus의 얽힌 보행로·테라스·계단, 항구 시장과 주변 지역 연결을 소개한다. 도시를 동일 주택의 반복으로만 구성하지 않고 이동 폭·전면·역할을 구분하는 참고다. 공식 지역 소개이며 내부 생성 알고리즘이나 엔진 최적화 문서가 아니다. |
| D20 | [The Level Design Book: Circulation](https://book.leveldesignbook.com/process/layout/flow/circulation) | 저자가 주/보조, 공공/사적, 수평/수직 연결과 공간의 생활상·시스템 기능을 구분한다. A에서는 큰 시장길과 좁은 보행 골목·안뜰 접근을 나누었다. 책의 경쟁 멀티플레이 1–3 레인 설명을 MMORPG 전체 지역의 길 개수 법칙으로 적용하지 않는다. |
| D21 | [Epic 5.8: PCG with World Partition](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-pcg-with-world-partition-in-unreal-engine) · [Nanite](https://dev.epicgames.com/documentation/en-us/unreal-engine/nanite-virtualized-geometry-in-unreal-engine) | PCG의 WP/HLOD 연계와 Nanite의 스트리밍·LOD 기능을 확인했다. Nanite에도 인스턴스·재질·해상도·하드웨어별 한계가 있어 측정이 필요하다. 기능 사용을 전체 성능 합격이나 무팝인 보장으로 기록하지 않는다. |
| D22 | [Epic: Mesh Distance Fields Properties](https://dev.epicgames.com/documentation/unreal-engine/mesh-distance-fields-properties-in-unreal-engine) | 메시의 Distance Field Resolution Scale은 볼륨 텍스처 크기와 관련된다. A에서는 로그와 실제 생성 메시 설정을 확인하고 지형 위 길·광장·수면 오버레이의 개별 거리장을 제외하는 정책을 적용한다. 전역 거리장 기능을 끄거나 나무·건물 표현을 일괄 축소하는 설정이 아니다. |
| D23 | [Epic 5.8: Landscape Materials](https://dev.epicgames.com/documentation/unreal-engine/landscape-materials-in-unreal-engine) | 서로 다른 지표층과 혼합·높이 기반 경계의 제작 원리를 확인했다. A의 추가 패스는 기존 사용자 정의 재질 계약을 유지해 다진 흙·포장석·자갈·젖은 흙의 월드 좌표 마스크를 지형과 길이 함께 소비한다. `LandscapeLayerBlend`나 RVT를 새로 설치한 것으로 기록하지 않는다. |

사용자가 제공한 검은사막 전체/지역 지도는 능선 사이 생활권, 부분 산지, 강과 길의 연결을 읽는 시각 참고다. 미니맵 나무 아이콘을 실제 식생 좌표·간격으로 복사하지 않으며, 이미지로 실제 면적·이동 시간·게임 내부 생성 방법을 역산하지 않는다. A의 수치와 생성 방식은 프로젝트 자체 설계다.

## 대화에서 확인한 로컬 근거와 개념도

| ID | 근거 | 확인 범위 |
| --- | --- | --- |
| P01 | [프로젝트 파일](../../../Project_J.uproject) | EngineAssociation 5.8와 프로젝트 모듈·명시된 플러그인 목록 |
| P02 | [기본 맵·렌더링 설정](../../../Config/DefaultEngine.ini) | Lvl_ThirdPerson·GameMode, Lumen 계열 GI/반사·VSM·RayTracing 설정. 실제 실행 경로·성능은 별도 |
| P03 | [프로젝트 맥락](../../Overview/ProjectContext.md) · [MMO 보류 범위](../../Architecture/Planning/DeferredMMORPGSystems.md) | 상태 소유권과 실제 라이브 서버·handover의 구현 경계 |
| P04 | [로컬 환경 에셋 확인](Open_World_Level_Design.md#9-로컬-환경-에셋-확인-결과) | 사용자가 제공한 별도 assets.uproject의 환경 5묶음 경로·파일·예제 맵 확인. 자산 품질·권리·연결 미검증 |
| P05 | [초기 개념도](Assets/First_Region_Concept.html) · [문서용 그림](Assets/First_Region_Concept.png) | 이전 대화의 V1 배치·고도 가안. 북쪽 산 띠·항구 중심 배치를 현 설계로 사용하지 않으며 현재 기준은 설계 문서의 A 중앙 대도시와 유기적 권역 연결. 실제 UE 레벨·측량·비례 지도 아님 |
| P06 | [기존 성능 판단](../../Performance/README.md) · [프로파일링 수집](../../Performance/CaptureGuides/ProjectJ_Baseline_Capture_Guide_2026-09-03.md) | 실행 조건·권한·로컬 보호·인원·실측 한계. 새로운 월드의 성능 결과로 전용하지 않음 |
| P07 | [이전 광역 배치도](Assets/World_Regions_Overview.html) · [그림](Assets/World_Regions_Overview.png) · [편집 원본](Assets/world-regions-overview.fragment.html) | 도시·주변 마을·부분 산지·넓은 계곡·평야·강·간선·우회로 연결 예시. 모든 후속 지역에 중앙 대도시를 두는 규칙으로 사용하지 않음. 거리 비례·권역 면적·실제 UE 배치 미확정 |
| P08 | [A 지역 상세 배치도](Assets/A_Region_Top_View.html) · [그림](Assets/A_Region_Top_View.png) · [편집 원본](Assets/a-region-top-view.fragment.html) | A 내부의 중앙 강변 대도시와 여섯 작은 마을, 강·호수·폭포·고산·지하 던전·외곽 우회로·후속 지역 접속 가안. 고도·거리·규모는 블록아웃 검증 전 제안 |

로컬 에셋 라이브러리는 사용자가 알려준 `C:\Users\I\Documents\Unreal Projects\assets`를 읽었다. 이는 이번 확인 환경의 위치이며 새 PC의 필수 설치 경로나 런타임 경로가 아니다. 캐릭터·스킬 에셋을 포함한 원본 라이브러리는 보존했다.

## 확인 수준과 다음 조사

- Epic·개발사·SideFX·설계 저자의 직접 자료를 설명의 기준으로 사용했다. 각 표에는 원문이 지원하는 내용만 적었다.
- 앞선 대화에서 확인한 자료는 재사용했고, 이번에는 레벨 계획·동선·치수·길 찾기·플레이테스트·강 저작·자연 지형 사례를 추가 확인했다.
- 커뮤니티 C08·C09는 접근 한계가 있어 본문 전체나 수치를 검증했다고 기록하지 않는다. GDC 검색 결과·재인용 기사·영상 제목만으로 새로운 설계 법칙이나 성능 수치를 추가하지 않았다.
- 다음 구현 조사에서는 보유 자산의 실제 LOD·Nanite·충돌·플러그인·재질 연결, 현재 맵의 Runtime Hash, 캐릭터·탈것의 최종 수치와 목표 장비부터 확인한다.
