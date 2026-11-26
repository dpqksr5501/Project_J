# 오픈월드 최적화 조사 자료와 대화 기록

2026-11-26에 확인한 작업 이력이다. 앞부분은 설계 전 수집한 자료이며, 뒷부분은 이 채팅에서 사용자가 요청한 내용과 각 작업의 완료 응답을 시간순으로 보존한다. 인용문의 ‘현재’, 수치, 남은 작업은 해당 응답 시점을 가리킨다. 오늘 이어갈 범위와 후속 수정 사항은 [통합 인계](../Project_J_OpenWorld_Project_Handoff.md)를 따른다. 도구 출력·중간 진행 메시지·이미지는 이 부록에 전재하지 않았다. 시험 로그와 캡처는 각 보고서에서 찾을 수 있다.

## 외부 자료와 연결할 판단

출처가 설명하는 공개 기술과 Project J의 적용 여부를 구분한다. 일부 Epic 최신 문서는 5.9로 표시된다. 우리 기존 패키지인 5.8.4와 기능·기본값·실험 여부를 대조하고 더 새로운 기능을 설치만 되어 있다고 가정하지 않는다.

### 제작사 발표·독립 실측·연구

- **펄어비스, 붉은사막**. 위치·시야 중심 스트리밍, 반복 배치 자동화와 랜드마크 수작업, 탐험 시야·길·전투 공간을 먼저 설계하는 방식의 사례다. G-CON 보도는 2만 개 이상 월드 단위와 GPU 식생 가시성/거리 처리를 소개한다. ‘50만 나무’ 등 발표 규모는 하드웨어·해상도·시야·그림자·지표 조건 없이 성능 보증이나 MMO 수용량으로 쓰지 않는다. 출시 시점 독립 측정에서는 일부 드로 거리·팝인·프레임 시간 문제도 관찰됐다. [펄어비스 Gamescom 개발 기술](https://www.pearlabyss.com/ko-kr/Board/Detail?_boardNo=15224) · [펄어비스 G-CON 발표 공지](https://www.pearlabyss.com/ko-kr/Board/Detail?_boardNo=15317) · [BlackSpace 개발 아카이브](https://crimsondesert.pearlabyss.com/en-us/News/Notice/Detail?_boardNo=40) · [G-CON 현장 보도](https://biz.chosun.com/en/en-it/2026/11/19/5POR37UDVZADNGLILQHPN37S2M/?outputType=amp) · [4Gamer 현장 보도](https://www.4gamer.net/games/484/G048495/20261119038/) · [GamersNexus 독립 실측](https://gamersnexus.net/gpus-game-benchmarks-graphics-guides/crimson-desert-gpu-benchmarks-bugs-simulation-error-tests) · [Digital Foundry PC 분석](https://www.youtube.com/watch?v=BDb5JfcArmc)
- **Epic, Fortnite Chapter 4**. Nanite 식생의 먼 수관이 얇아지는 문제, 메시·`Preserve Area`, Lumen/VSM과 시간대의 품질 비용을 UE 기반에서 비교한다. [UE 5.1 통합 사례](https://www.unrealengine.com/blog/battle-testing-unreal-engine-5-1-s-new-features-on-fortnite-battle-royale-chapter-4?lang=en-US) · [Fortnite Nanite 기술](https://www.unrealengine.com/tech-blog/bringing-nanite-to-fortnite-battle-royale-in-chapter-4?lang=en-US) · [Fortnite Lumen 기술](https://www.unrealengine.com/tech-blog/lumen-brings-real-time-global-illumination-to-fortnite-battle-royale-chapter-4?lang=en-US)
- **Guerrilla, Horizon Zero Dawn**의 규칙 기반 GPU 절차 배치와 아티스트 도구, **Insomniac, Sunset Overdrive**의 빠른 도시 블록 스트리밍·제작 제약, **Ubisoft Anvil, Assassin’s Creed Shadows**의 원거리 지형·랜드마크와 날씨 변화가 서로 다른 범위의 사례다. [Guerrilla GPU 배치](https://www.guerrilla-games.com/read/gpu-based-procedural-placement-in-horizon-zero-dawn) · [Insomniac GDC 발표](https://www.gdcvault.com/play/1022268/Streaming-in-Sunset-Overdrive-s) · [Ubisoft Anvil 기술](https://www.ubisoft.com/en-gb/game/assassins-creed/news/3aw71nNlR7kZJzoCATuNtm/inside-anvil-the-technology-powering-assassins-creed-shadows)
- **Hitman: Absolution / City Sample**은 배경 시민·차량 군중을 표현하는 비용을, **Assassin’s Creed Unity**의 발표는 군중 속 사건 제작을 비교한다. Project J의 실제 전투 NPC·파티원, 정확한 포즈·충돌·가시성·경고 예산은 해당 계약을 유지하면서 검증한다. [IO Interactive GDC](https://media.gdcvault.com/gdc2012/slides/Programming%20Track/Fauerby_Kasper_CrowdsInHitman.pdf) · [Epic City Sample](https://dev.epicgames.com/documentation/unreal-engine/city-sample-project-unreal-engine-demonstration) · [Ubisoft 군중 이벤트 GDC](https://gdcvault.com/play/1022212/Postmortem-Developing-Systemic-Crowd-Events)
- **ArenaNet Guild Wars 2**는 맵 복제본·월드 보스 스케줄링, **CCP EVE Online**은 특정 지역 과밀 시 시뮬레이션 시간 지연, **Riot VALORANT**는 틱 비용 계측의 사례다. 운영 도구, 채널 분산과 한 장소의 교전 지연을 구분한다. **USENIX Colyseus**는 분산 시뮬레이션의 연구 참고이고 UE의 배치 지시가 아니다. [ArenaNet 메가서버](https://www.guildwars2.com/en-gb/news/the-megaserver-system-world-bosses-and-events/) · [CCP TiDi](https://www.eveonline.com/news/view/introducing-time-dilation-tidi) · [Riot 서버 실측](https://playvalorant.com/en-gb/news/dev/how-we-got-to-the-best-performing-valorant-servers-since-launch/) · [Colyseus 논문](https://www.usenix.org/events/nsdi06/tech/full_papers/bharambe/bharambe.pdf)
- **GPU-driven rendering·DirectStorage·Snowdrop**는 CPU 제출/가시성, 데이터 I/O·압축 해제, 해상도·시간적 품질을 각각 검사할 때 쓸 수 있다. 동일 데이터에서 read/CPU decompression이 실제 병목이면 I/O 후보를 검토하고, 상세 메시 처리·픽셀 비용·present와 구분한다. [SIGGRAPH GPU-driven rendering](https://advances.realtimerendering.com/s2015/aaltonenhaar_siggraph2015_combined_final_footer_220dpi.pdf) · [DirectStorage GDC](https://gpuopen.com/download/GDC-2023-DirectStorage-optimizing-load-time-and-streaming.pdf) · [Snowdrop·FSR GDC](https://gpuopen.com/download/GDC2024_High_Performance_Rendering_in_Snowdrop_Using_AMD_FidelityFX_Super_Resolution_3.pdf)

### 엔진·오픈소스·계측 자료

| 범위 | 참고 자료와 사용할 때 |
| --- | --- |
| **월드 조립·HLOD·PCG** | [World Partition](https://dev.epicgames.com/documentation/en-us/unreal-engine/world-partition-in-unreal-engine), [HLOD 및 `WorldPartitionHLODsBuilder`](https://dev.epicgames.com/documentation/unreal-engine/world-partition-hierarchical-level-of-detail-in-unreal-engine), [Data Layers](https://dev.epicgames.com/documentation/en-us/unreal-engine/world-partition-data-layers-in-unreal-engine), [PCG GPU 처리](https://dev.epicgames.com/documentation/unreal-engine/using-pcg-with-gpu-processing-in-unreal-engine?lang=en-US), [FastGeo 5.8 플러그인 색인](https://dev.epicgames.com/documentation/unreal-engine/API/PluginIndex/FastGeoStreaming?lang=en-US). 프로젝트의 실제 그리드·HLOD·생성 대상을 대조한다. GPU PCG 배치와 결과 렌더링, 런타임 업데이트를 구분한다. |
| **화질·스트리밍·캐시** | [Nanite 기술 상세](https://dev.epicgames.com/documentation/en-us/unreal-engine/nanite-technical-details), [Virtual Texturing](https://dev.epicgames.com/documentation/en-us/unreal-engine/virtual-texturing-in-unreal-engine), [TSR](https://dev.epicgames.com/documentation/en-us/unreal-engine/temporal-super-resolution-in-unreal-engine), [Temporal Quality Guide](https://dev.epicgames.com/community/learning/tutorials/mjo7/unreal-engine-temporal-quality-guide), [PSO Precaching](https://dev.epicgames.com/documentation/en-us/unreal-engine/pso-precaching-for-unreal-engine), [Bundled PSO 캐시](https://dev.epicgames.com/documentation/unreal-engine/manually-creating-bundled-pso-caches-in-unreal-engine?lang=en-US), [비동기 에셋 로딩](https://dev.epicgames.com/documentation/unreal-engine/asynchronous-asset-loading-in-unreal-engine?lang=en-US). PSO missed/too late, 실제 실행의 first-use, cold/warm, 텍스처·메시 스트리밍의 필요/보유를 같이 읽는다. |
| **스킬·애니메이션·Niagara** | [Epic ABA](https://dev.epicgames.com/documentation/en-us/unreal-engine/animation-budget-allocator-in-unreal-engine), [Animation Sharing](https://dev.epicgames.com/documentation/unreal-engine/animation-sharing-plugin-in-unreal-engine?lang=en-US), [Animation Insights](https://dev.epicgames.com/documentation/unreal-engine/animation-insights-in-unreal-engine), [Lyra Abilities](https://dev.epicgames.com/documentation/unreal-engine/abilities-in-lyra-in-unreal-engine), [Niagara Effect Types](https://dev.epicgames.com/documentation/en-us/unreal-engine/performance-budgeting-using-effect-types-in-niagara-for-unreal-engine), [Niagara 측정](https://dev.epicgames.com/documentation/en-us/unreal-engine/measuring-performance-in-niagara). 표현이 줄어도 캐릭터의 입력·타격·경고 타이밍이 유지되는지 확인한다. |
| **Iris·실시간 운영** | [Iris 필터링](https://dev.epicgames.com/documentation/en-us/unreal-engine/iris-filtering-in-unreal-engine), [Iris 우선순위](https://dev.epicgames.com/documentation/unreal-engine/iris-prioritization-in-unreal-engine?lang=en-US), [UE Replication Graph](https://dev.epicgames.com/documentation/en-us/unreal-engine/replication-graph?application_version=4.27), [Agones](https://github.com/agones-dev/agones), [Agones FleetAutoscaler](https://agones.dev/site/docs/reference/fleetautoscaler/), [Nakama](https://github.com/heroiclabs/nakama), [TrinityCore](https://github.com/TrinityCore/TrinityCore). 엔진 교체를 정하기 전에 실제 NetDriver 경로, 동일 쟁점의 비용, 입장/드레인/재연결을 확인한다. |
| **계측과 오픈소스 비교** | [Unreal Insights](https://dev.epicgames.com/documentation/unreal-engine/unreal-insights-in-unreal-engine?lang=en-US), [PresentMon](https://github.com/GameTechDev/PresentMon), [Microsoft PIX](https://developer.microsoft.com/en-us/games/articles/2023/12/using-pix-to-optimize-your-pc-builds-and-diagnose-issues/), [Recast/Detour](https://github.com/recastnavigation/recastnavigation), [meshoptimizer](https://github.com/zeux/meshoptimizer), [FidelityFX SDK](https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK). 외부 오픈소스는 구현 개념·계측의 참고 자료다. 이 목록이 프로젝트 통합 결정을 의미하지 않는다. |

### 재점검에서 보강한 진단 자료

초기 대화에서 사용한 공식 성능 안내 주소도 원문과 함께 보존한다. [Epic 성능 프로파일링과 구성](https://dev.epicgames.com/documentation/unreal-engine/introduction-to-performance-profiling-and-configuration-in-unreal-engine)

- **40인 피해 수신 지연**은 기존 타격 타임스탬프에 Networking Insights의 패킷 전송·수신, 복제 객체/속성과 GT Apply를 같은 실험에서 맞춰볼 수 있다. `NetTrace`의 특정 지점이 무엇을 측정하는지 버전과 실행기로 검증한다. Networking Insights의 표시 패킷 크기는 압축 전일 수 있으므로, 실회선 바이트와 일치한다고 가정하지 않는다. 처리 비용을 바꿀 수 있는 추적은 기존 무추적 기준과 별도로 보고한다. [Epic Networking Insights](https://dev.epicgames.com/documentation/unreal-engine/networking-insights-in-unreal-engine?lang=en-US) · [Epic Trace Control](https://dev.epicgames.com/documentation/unreal-engine/using-the-trace-control-tab-in-unreal-insights-for-unreal-engine)
- **부하 장면의 CPU/GPU 블로킹**은 프레임과 Game/Render/RHI·worker 대기, 관련 RDG 패스와 실제 렌더 이벤트를 연결해 본다. 첫 진입의 PSO `Missed`와 `Too Late`를 구분하고, 로딩/컴포넌트 등록 비용 및 체감 히치는 별도 계측한다. [Epic Timing Insights](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-the-timing-panel-in-unreal-insights-for-unreal-engine) · [Epic Render Dependency Graph](https://dev.epicgames.com/documentation/en-us/unreal-engine/render-dependency-graph-in-unreal-engine) · [Epic PSO 검증](https://dev.epicgames.com/documentation/en-us/unreal-engine/pso-precaching-for-unreal-engine)
- **새 레벨의 참조·Nav 준비**는 Hard/Soft 참조, 실제 에셋 로드 및 Cook root, WP NavMesh의 셀과 Navigation Invoker 요구를 따로 검토한다. Project J의 현재 Nav 통합 검증은 작은 non-WP 검증 맵의 경로 사용까지 확인했으며, 대규모 WP 월드의 Nav freshness/패키지 대규모 이동은 별도다. [Project J Nav 검증 범위](../../../../Docs/World/README.md) · [Epic 에셋 참조](https://dev.epicgames.com/documentation/unreal-engine/referencing-assets-in-unreal-engine) · [Epic Navigation Invokers](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-navigation-invokers-in-unreal-engine?lang=en-US)
- **Sol 구현·디버깅 단계의 실행 증거**는 기존 목적별 실행기와 UE Automation의 규모를 비교해 고른다. 기존 `Measure-Network.ps1`은 소인수 지연/손실·애니메이션·전투 경로에, `Measure-RenderedCrowd.ps1`은 렌더 군중에, `Run-LoopbackCohort.ps1`은 실제 연결 실행에 참고할 수 있다. 새 테스트 하네스 도입은 기존 하네스가 못 잡는 회귀와 유지 비용이 확인될 때 검토한다. [네트워크 실행기](../../../../Scripts/Validation/Measure-Network.ps1) · [군중 렌더 실행기](../../../../Scripts/Validation/Measure-RenderedCrowd.ps1) · [Epic Automation Test Framework](https://dev.epicgames.com/documentation/unreal-engine/automation-test-framework-in-unreal-engine) · [Epic 네트워크 테스트·디버깅](https://dev.epicgames.com/documentation/en-us/unreal-engine/testing-and-debugging-networked-games-in-unreal-engine)

**추가 조사 우선순위는 외부 자료 수보다 우리 프로젝트에서 부족한 증거를 따르는 편이 좋다.** 현 단계에 추가로 필요한 핵심 근거는 실제 사용될 맵의 제작·패키지 경로, 목표 기기에서의 연속 영상 및 CPU/GPU/로딩 비용, 새 에셋의 검사 적용 범위, 40인 피해 수신 추적이다. 엔진 버전이 달라지거나 플랫폼 목표·서버 배치가 정해지면 관련 Epic 문서와 운영 사례를 그 조건에 맞춰 다시 대조한다.

## 1. 자료 조사와 초기 성능 근거

**사용자 요청**

> 우리 프로젝트 다양한 최적화, 퀄리티 높이는 기법들 적용되어있나? 붉은사막이나 뭐 방대한 오픈월드 게임 보면 엄청 많은 나무를 봐고 렉이 없고, 뭐 빠르게 이동해도 팝럽 현상도 없고, 유저 많아도(많으면 렉이 심한 게 당연) 최적화 잘되는 등 이런 최적화 기법이 적용되어있고 작동하는 게 맞나??
> 대규모 MMORPG, 오픈월드 등 여러 자료 찾아줘. 내가 말한 이런 예시에만 국한하지말고 뭐 다군중 처리, VFX 인스턴싱? 기타 등등 정말 다양한 AAA급 기법 기타 등등 찾아줘...
> 더 고려해야 할 점들 더 고려하면서 좋은 구조로.
> 내가 말하지 않은 것들/놓친 것들도 포함해서.
> 공식, 비공식, 오픈소스, 게임 제작사에서 발표한 자료 등 전부 다방면으로 포함해서 정보/자료 먼저 찾아보자.

**당시 완료 응답**

> **Project J에는 대규모 월드와 다인 전투를 위한 최적화가 이미 들어가 있고, 일부는 실제 패키지와 전용 서버에서 작동을 확인했습니다.** 가장 강한 증거는 MainWorld의 **578m 고속 비행 패키지 3회**와 **Iris 전용 서버 24인 필드 보스전**입니다. 아직 입증이 필요한 핵심은 **장거리 비행 중 나무·지형·텍스처의 시각적 연속성**, **100~200명이 같은 장소에서 반복 교전할 때의 서버와 클라이언트 성능**, **최소 사양에서 숲·군중·전투 이펙트가 동시에 보이는 장면**입니다. [Project J 최신 적용·실측 기록](C:/Users/I/Documents/GitHub/Project_J/Docs/Architecture/Planning/Project_J_PhaseAD_Codex_Audit_2026-11-25.md:130)
>
> 이번 조사는 현재 소스·설정, 기존 실측 원자료, 외부 기술 자료를 대조했습니다. 새 실행 측정이나 코드·에셋 변경은 없었습니다.
>
> ## 1. Project J에서 실제로 확인한 것
>
> | 영역 | 적용과 작동 근거 | 남은 검증 |
> |---|---|---|
> | **고속 이동·월드 스트리밍** | World Partition에 이동 방향과 상승·하강 속도를 반영하는 `Loaded` → `Activated` 예측 소스가 연결돼 있습니다. 이전 572m 비행에서 Landscape PSO 재등록 문제와 **최대 702.46ms** 프레임이 관측됐습니다. 프로젝트 측 호환 처리 후 약 33m/s로 578m씩 비행한 패키지 3회에서 해당 엔진 오류가 사라졌고, 프레임 **p99 11.08~11.48ms, 최대 13.71~16.94ms, 50ms 초과 0회**를 기록했습니다. | 한 경로의 D3D12 Development 결과입니다. 첫 반복에 스트리밍 미완료 표본 1개가 있었고, 감속은 세 번 모두 발동하지 않았습니다. 장거리 비행, 급선회, 지형·텍스처 팝인, 저사양 SSD는 별도 검증이 필요합니다. [패키지 반복 결과](C:/Users/I/Documents/GitHub/Project_J/Docs/Architecture/Planning/Project_J_PhaseAD_Codex_Audit_2026-11-25.md:149) · [Epic Landscape PSO 이슈](https://issues.unrealengine.com/issue/UE-404796) |
> | **숲·원거리 배경** | MainWorld에 인스턴싱 식생과 HLOD 에셋이 있습니다. 선택한 한 셀에서 원본이 내려가고 **502개 인스턴스가 포함된 HLOD 프록시**가 표시되는 경로를 확인했습니다. 최신 제작 검사에서는 가문비나무 컴포넌트가 Instancing HLOD를 쓰면서 바람 변형 중지 거리 값이 `0cm`인 사례도 찾았습니다. | 수종별 바람·그림자 예산, 전역 HLOD 재빌드 피크 메모리, 원경 숲의 실루엣과 전환 품질은 미확인입니다. BP02의 거리별 임포스터·RVT 설계에는 아직 적용을 확인할 항목이 있습니다. [선택 셀의 HLOD 전환](C:/Users/I/Documents/GitHub/Project_J/Docs/Architecture/Planning/Project_J_MMO_Architecture_Work_2026-10-26.md:749) · [실제 식생 검사](C:/Users/I/Documents/GitHub/Project_J/Docs/Architecture/Planning/Project_J_PhaseAD_Codex_Audit_2026-11-25.md:147) · [BP02 설계](C:/Users/I/Documents/GitHub/Project_J/Docs/Architecture/Blueprints/BP02_OpenWorld_HighSpeedStreaming_Foliage_Rendering.md:71) |
> | **다인 서버·복제** | Iris의 연결별 군중 필터, 전투 우선순위, 실측 틱 기반 부하 단계가 연결돼 있습니다. 같은 PC의 경량 클라이언트 **24인 전원이 접속·타격·체력 수신·종료**했고, 전투 집중 구간 서버 월드 처리 **p95 5.165ms**였습니다. 별도 최신 12인 혼합 파티에서는 필터가 **84개 연결–객체 쌍을 허용하고 72개를 제외**했습니다. 그 실행의 월드 처리 p95는 3.262ms, 실제 틱 시작 간격 p95는 34.008ms였습니다. | 최신 100~200인 분리 호스트 실전 교전은 미측정입니다. 이전 제작 보스 200인 시험은 확정 피해 0건이었고, Generic 200소켓 통과는 단순 합성 부하였습니다. 설치형 Iris에서 관측한 유효 슬롯 127개도 별도 제약입니다. [24인 실측](C:/Users/I/Documents/GitHub/Project_J/Docs/Architecture/Planning/Project_J_PhaseAD_Codex_Audit_2026-11-25.md:39) · [혼합 파티 필터 실측](C:/Users/I/Documents/GitHub/Project_J/Docs/Architecture/Planning/Project_J_PhaseAD_Codex_Audit_2026-11-25.md:162) · [200인 시험 이력](C:/Users/I/Documents/GitHub/Project_J/Docs/Architecture/Planning/Project_J_MMO_Audit_Followup_2026-11-24.md:31) |
> | **캐릭터·NPC 군중** | 원격 애니메이션 예산, 긴급 포즈 갱신, 몸체 LOD, 경량 망토가 실제 캐릭터 경로에 있습니다. 스펠블레이드 원격 망토는 화면용 LOD0을 유지하면서 **물리 메시 정점 2,534→678개**로 줄였습니다. Mass 이동·간격 계산도 구현돼 있고, 2,048개 합성 이동의 해당 계산 p95는 **0.634→0.241ms**였습니다. | 현재 Mover NPC는 Mass 강등 대상에서 제외됩니다. VAT의 일반 제작 콘텐츠 사용 범위도 미확인입니다. 실제 플레이어가 많은 장면에서는 애니메이션 외에 **Cloth 작업 완료 대기**, 그림자, 장비, 이름표까지 함께 봐야 합니다. [망토와 CPU 추적](C:/Users/I/Documents/GitHub/Project_J/Docs/Performance/README.md:7) · [Mass 벤치마크](C:/Users/I/Documents/GitHub/Project_J/Docs/Benchmarks/SystemsModernization.md:25) · [제작 적용 범위](C:/Users/I/Documents/GitHub/Project_J/Docs/Architecture/Planning/Project_J_PhaseAD_Codex_Audit_2026-11-25.md:136) |
> | **전투 VFX·그래픽 품질 단계** | Niagara 컴포넌트 풀링, 원격 생성 예산, 일부 저장된 트레일의 Effect Type 거리·활성 수 정책이 있습니다. 기존 비교에서 풀링 적용 시 **생성 CPU p95 4.57→3.35ms**였습니다. 그래픽 단계별로 식생 밀도, 텍스처 풀, Lumen, VSM, 원격 Cloth도 조절합니다. | 풀링 측정은 생성 비용에 해당합니다. 같은 순간 활성화된 시스템 수, 파티클 GPU 시간, 반투명 오버드로, 동적 조명·왜곡, 보스 경고 가독성은 각각 측정해야 합니다. 반복 충격을 공유 Niagara 시스템으로 배치 처리하는 방식은 이번 조사에서 실사용 근거를 확인하지 못했습니다. [VFX 구조](C:/Users/I/Documents/GitHub/Project_J/Docs/Combat/Architecture/CombatVFXArchitecture.md:36) · [풀링 측정](C:/Users/I/Documents/GitHub/Project_J/Docs/Benchmarks/SystemsModernization.md:42) · [그래픽 설정](C:/Users/I/Documents/GitHub/Project_J/Config/DefaultScalability.ini:3) |
> | **MMO 운영·혼잡 처리** | Windows 로컬 환경에서 실제 Nakama·PostgreSQL·UE 서버로 로그인, 대기열, 보상, 던전 왕복, 연결 드레인과 Prometheus 수집을 확인했습니다. MainWorld B01도 별도 단독 PIE에서 접근 시 생성, 경고 표시, 처치·개인 보상까지 연결됐습니다. | B01에서 다인 렌더링·반복 교전·재생성까지 이어지는 부하, Linux/Agones 실기동과 장애 복구, 여러 채널 동시 피크는 검증 범위에 남아 있습니다. [운영 검증](C:/Users/I/Documents/GitHub/Project_J/Docs/Architecture/Planning/Project_J_PhaseAD_Codex_Audit_2026-11-25.md:78) · [B01 제작 검증](C:/Users/I/Documents/GitHub/Project_J/Docs/Architecture/Planning/Project_J_PhaseAD_Codex_Audit_2026-11-25.md:145) |
>
> 기존 프로젝트 자료의 **200개 캐릭터 렌더링**, **200소켓 접속**, **200명분 보상 트랜잭션**은 각각 화면·네트워크·저장소에 대한 근거입니다. 목표가 **실제 200명이 같은 보스에게 달려들고, 모든 필수 전투 정보가 보이며, 프레임과 서버 틱이 유지되는 것**이라면 그 조건을 한 실행에서 검증해야 합니다. [렌더링 측정 범위](C:/Users/I/Documents/GitHub/Project_J/Docs/Performance/README.md:375) · [200인 접속 이력](C:/Users/I/Documents/GitHub/Project_J/Docs/Architecture/Planning/Project_J_MMO_Audit_Followup_2026-11-24.md:31) · [보상 트랜잭션 검증](C:/Users/I/Documents/GitHub/Project_J/Docs/Architecture/Planning/Project_J_PhaseAD_Codex_Audit_2026-11-25.md:105)
>
> ## 2. AAA 오픈월드·MMO 자료에서 가져올 기법
>
> ### 월드, 식생, 조명
>
> | 조사 주제 | 적용할 때 볼 것 | 참고 자료 |
> |---|---|---|
> | **붉은사막의 월드 스트리밍과 숲** | BlackSpace는 위치·시야에 따라 필요한 공간과 데이터를 불러오고, 반복 식생 배치는 규칙으로 생성하며 주요 랜드마크는 수작업으로 관리합니다. G-CON 현장 보도에는 2만 개 이상의 월드 단위와 GPU 식생 가시성·거리별 상세도 처리가 소개됩니다. 발표의 수목 규모를 비교할 때는 해상도, 사양, 카메라, 그림자, 실제 프레임 시간 조건이 필요합니다. 출시 시점 독립 실측에서는 일부 팝인과 프레임 스파이크도 관찰했습니다. | [펄어비스 개발 발표 요약](https://www.pearlabyss.com/ko-kr/Board/Detail?_boardNo=15224) · [G-CON 기술 발표 현장 보도](https://biz.chosun.com/en/en-it/2026/11/19/5POR37UDVZADNGLILQHPN37S2M/?outputType=amp) · [GamersNexus 독립 실측](https://gamersnexus.net/gpus-game-benchmarks-graphics-guides/crimson-desert-gpu-benchmarks-bugs-simulation-error-tests) |
> | **고속 이동과 팝인** | Insomniac의 *Sunset Overdrive*처럼 이동 속도와 레벨 구성을 스트리밍 예산과 함께 설계합니다. Project J에서는 패키지 읽기·압축 해제 → 셀 활성화·컴포넌트 등록 → 충돌·Nav → 텍스처·메시·PSO 준비를 따로 계측할 가치가 있습니다. 원거리 프록시가 먼저 보이고 근거리 원본으로 자연스럽게 바뀌는지는 영상으로 확인합니다. DirectStorage는 I/O나 압축 해제가 병목일 때 검토할 후보입니다. | [Insomniac GDC 발표](https://www.gdcvault.com/play/1022268/Streaming-in-Sunset-Overdrive-s) · [Epic World Partition](https://dev.epicgames.com/documentation/en-us/unreal-engine/world-partition-in-unreal-engine) · [Epic PSO Precaching](https://dev.epicgames.com/documentation/en-us/unreal-engine/pso-precaching-for-unreal-engine) · [DirectStorage GDC 발표](https://gpuopen.com/download/GDC-2023-DirectStorage-optimizing-load-time-and-streaming.pdf) |
> | **수십만 식생의 화면 비용** | ISM/HISM과 GPU 가시성 검사로 반복 배치 비용을 줄이고, 가까운 숲에는 Nanite 또는 메시 LOD, 먼 숲에는 HLOD·임포스터를 비교합니다. *Fortnite* 팀은 원거리에서 수관이 얇아지는 문제를 직접 다뤘습니다. *Horizon Zero Dawn*의 GPU 절차 배치는 아티스트 규칙을 넓은 지역에 적용하는 사례입니다. **배치 생성 시간, 런타임 렌더링, HLOD 제작 메모리**를 각각 측정해야 합니다. | [Epic Fortnite Nanite 기술 블로그](https://www.unrealengine.com/tech-blog/bringing-nanite-to-fortnite-battle-royale-in-chapter-4?lang=en-US) · [Guerrilla GPU 배치 발표](https://www.guerrilla-games.com/read/gpu-based-procedural-placement-in-horizon-zero-dawn) · [Epic PCG GPU 처리](https://dev.epicgames.com/documentation/unreal-engine/using-pcg-with-gpu-processing-in-unreal-engine?lang=en-US) · [GPU-driven 렌더링 SIGGRAPH 발표](https://advances.realtimerendering.com/s2015/aaltonenhaar_siggraph2015_combined_final_footer_220dpi.pdf) |
> | **화질을 유지하며 GPU 비용 줄이기** | 잎의 마스크 오버드로, 바람 WPO로 인한 VSM 캐시 무효화, 움직이는 태양의 그림자, 스켈레탈 RT 갱신, 물·안개·구름·반투명 효과를 각각 봅니다. 낮은 프리셋에서도 랜드마크 실루엣, 지면과 식생의 색 연결, 전투 경고는 유지할 가치가 큽니다. TSR이나 업스케일러는 그 뒤 GPU 시간과 고속 이동 시 잔상·깜빡임을 함께 비교합니다. | [Ubisoft Anvil·Shadows](https://www.ubisoft.com/en-gb/game/assassins-creed/news/3aw71nNlR7kZJzoCATuNtm/inside-anvil-the-technology-powering-assassins-creed-shadows) · [Epic Lumen 성능 가이드](https://dev.epicgames.com/documentation/unreal-engine/lumen-performance-guide-for-unreal-engine?application_version=5.8) · [Epic VSM](https://dev.epicgames.com/documentation/unreal-engine/virtual-shadow-maps-in-unreal-engine?lang=it) · [Epic TSR](https://dev.epicgames.com/documentation/en-us/unreal-engine/temporal-super-resolution-in-unreal-engine) |
>
> ### 군중, 전투 이펙트, MMORPG
>
> | 조사 주제 | 적용할 때 볼 것 | 참고 자료 |
> |---|---|---|
> | **군중을 표현하는 비용과 시뮬레이션하는 비용** | 도심 배경 시민, 전투 NPC, 원격 플레이어의 요구가 다릅니다. 배경 군중에는 Mass·저빈도 의사결정·VAT/인스턴스 표현이 후보이고, 가까운 전투원에는 정확한 포즈·장비·충돌이 중요합니다. 경로 요청 예산, 구역별 NavMesh, 밀집 회피와 관문 병목도 포함해야 합니다. 특히 좁은 문이나 보스 주변에서 NPC가 멈추고 경로를 재요청하는 상황을 따로 봐야 합니다. | [Epic City Sample](https://dev.epicgames.com/documentation/unreal-engine/city-sample-project-unreal-engine-demonstration) · [IO Interactive의 Hitman 군중 발표](https://media.gdcvault.com/gdc2012/slides/Programming%20Track/Fauerby_Kasper_CrowdsInHitman.pdf) · [Recast/Detour 오픈소스](https://github.com/recastnavigation/recastnavigation) |
> | **VFX 인스턴싱·배치 처리** | 풀링은 컴포넌트 생성·회수 비용을 줄입니다. 반복되는 먼지·피격 스파크처럼 비슷한 짧은 효과는 **Niagara Data Channels + 공유 시스템**으로 이벤트를 모아 활성 시스템 수도 줄일 수 있습니다. 다만 덩어리가 너무 크면 거리·시야 컬링이 거칠어집니다. 보스 경고와 로컬 피드백을 보호하면서 원격 장식 효과의 리본 포인트, 왜곡, 조명, 소프트 파티클, 화면 점유율을 조절하는 방식이 Project J에 맞습니다. | [Epic Niagara 최적화 가이드](https://dev.epicgames.com/documentation/unreal-engine/scalability-and-best-practices-for-niagara) · [Niagara Data Channels](https://dev.epicgames.com/documentation/unreal-engine/niagara-data-channels-overview) · [Niagara 성능 측정](https://dev.epicgames.com/documentation/en-us/unreal-engine/measuring-performance-in-niagara) |
> | **복제와 한 장소의 과밀** | 공간 관심 영역, 변경분 전송, 휴면, 갱신 빈도·우선순위를 조합합니다. 같은 보스 주변에 모두 모이면 거리 필터의 이점이 줄어듭니다. 이때는 **자기 캐릭터·파티·보스·실제 위협·필수 공격 신호**가 부하 상황에서도 도착하는지와, 연결별 송신량·상태 갱신 지연·필터 계산 비용을 함께 봐야 합니다. | [Epic Iris 필터링](https://dev.epicgames.com/documentation/en-us/unreal-engine/iris-filtering-in-unreal-engine) · [Epic Iris 우선순위](https://dev.epicgames.com/documentation/unreal-engine/iris-prioritization-in-unreal-engine?lang=en-US) · [분산 게임 시뮬레이션 논문 Colyseus](https://www.usenix.org/events/nsdi06/tech/full_papers/bharambe/bharambe.pdf) · [TrinityCore MMO 서버 소스](https://github.com/TrinityCore/TrinityCore) |
> | **서버가 감당할 수 있는 혼잡의 범위** | *Guild Wars 2*의 맵 복제본·월드 보스 운영, *EVE Online*의 과밀 시 시뮬레이션 감속, *VALORANT*의 틱 비용 계측은 서로 다른 문제에 답합니다. Project J에서도 **전체 동접, 채널 정원, 같은 지점의 활성 전투원, 한 화면의 렌더링 인원**을 따로 정의해야 합니다. 채널 증설은 전체 접속을 분산하고, 단일 보스의 교전 한도는 그 장면을 별도로 측정해서 정합니다. | [ArenaNet 메가서버와 월드 보스](https://www.guildwars2.com/en-gb/news/the-megaserver-system-world-bosses-and-events/) · [CCP Time Dilation](https://www.eveonline.com/news/view/introducing-time-dilation-tidi) · [Riot 서버 성능 분석](https://playvalorant.com/en-gb/news/dev/how-we-got-to-the-best-performing-valorant-servers-since-launch/) · [Agones](https://github.com/agones-dev/agones) · [Nakama](https://github.com/heroiclabs/nakama) |
> | **흔히 빠뜨리는 비용** | 이름표·데미지 숫자·채팅의 UI 갱신, 발소리·스킬 음성 수, 천·머리카락·장비의 스키닝과 물리, 순간이동 직후 리소스 동시 생성, 이탈 뒤 GC와 메모리 회수, 첫 실행·패치 후 PSO, 보스 처치 직후 보상·로그인 부하를 포함합니다. 실제 화면의 끊김은 PresentMon으로 잡고, 해당 시간대의 CPU·GPU·로딩·메모리·복제 원인을 Insights/PIX에서 좁힐 수 있습니다. | [Epic UMG 최적화](https://dev.epicgames.com/documentation/unreal-engine/optimization-guidelines-for-umg-in-unreal-engine) · [Epic 오디오 동시 재생 정책](https://dev.epicgames.com/documentation/unreal-engine/sound-concurrency-reference-guide?lang=en-US) · [Unreal Insights](https://dev.epicgames.com/documentation/unreal-engine/unreal-insights-in-unreal-engine?lang=en-US) · [PresentMon 오픈소스](https://github.com/GameTechDev/PresentMon) · [Microsoft PIX](https://developer.microsoft.com/en-us/games/articles/2023/12/using-pix-to-optimize-your-pc-builds-and-diagnose-issues/) |
>
> ## 3. Project J에 맞는 구조와 검증 순서
>
> **기존 시스템별 예산을 유지하면서, 같은 장면을 기준으로 결과를 연결하는 편이 좋겠습니다.** 월드 스트리밍은 지형·충돌과 배경 표현의 준비 상태를, 클라이언트는 캐릭터·VFX·오디오 품질을, 서버는 전투 판정·복제·부하를 각자 관리합니다. 공통으로 지켜야 할 플레이 경험은 **지면이 준비된 다음 이동할 수 있고, 교전 대상이 제때 보이며, 필수 경고와 명중 결과를 알아볼 수 있는 것**입니다.
>
> 1. **MainWorld 장거리 비행과 숲을 먼저 확인합니다.** 기존 578m 경로를 기준으로 숲 → 능선 → 정착지를 잇는 경로, 급선회·급강하·셀 재진입을 추가합니다. 패키지 첫 실행과 반복 실행에서 프레임 p95/p99/최대, 50ms 초과 횟수, I/O·셀 활성화·PSO, RAM/VRAM을 기록합니다. 영상에서는 **나무 팝인, 수관 두께 변화, 텍스처 선명도 회복, 그림자 튐**을 따로 봅니다. `Nature_5_11`의 WPO 경고가 수종별 공통 문제인지부터 확인할 수 있습니다.
> 2. **제작된 B01 전투에서 화면 병목을 분리합니다.** 동일 카메라와 공격 패턴으로 원격 Cloth, VFX, 그림자, 이름표를 한 항목씩 비교합니다. 화면에 보이는 인원과 필수 경고 가독성을 성능 수치 옆에 기록합니다. 활성 Niagara 시스템 수가 병목이면 공유 시스템을, GPU 반투명이 병목이면 화면을 덮는 이펙트와 왜곡·동적 조명을 먼저 비교하는 순서가 적절합니다.
> 3. **전용 서버·부하 생성기·렌더 관측을 분리합니다.** 기존 24인 흐름을 재현한 뒤 인원을 올리면서 **분산 이동, 같은 보스에 밀집, 반복 전투, 처치·보상, 재접속**을 구간별로 측정합니다. 서버 처리 시간과 실제 틱 간격, 접속별 송신량, 서버 권위 피해 확정·수신, 렌더 프레임, 장시간 운용 후 메모리·큐 회복을 함께 기록합니다. 100~200인 결과에는 배포 환경과 NetDriver의 입장 가능 여부도 포함합니다.
>
> 지금까지의 자료에서 가장 분명한 다음 질문은 두 가지입니다. **고속 이동 중 원경이 실제 화면에서 자연스럽게 이어지는가**, **실제 MainWorld 교전에서 인원이 늘 때 어떤 비용이 먼저 한계에 닿는가**입니다. 이 두 장면을 기준으로 삼으면 이미 잘 작동하는 최적화는 활용하고, 식생·Cloth·VFX·복제 중 어디에 제작과 구현 시간을 쓸지 실측으로 결정할 수 있습니다.

## 2. 언리얼과 프로젝트의 역할 및 한 PC 검증

**사용자 요청**

> 그러면 검증해줘 전체적으로.
> 그전에 우리 언리얼 기능 + 우리 자체 구현 시스템으로 한다는 거 맞지?
> 그리고 뭐 멀리있는 나무들은 보여지면서 최적화도 하고?? 이런 식으로 AAA급을 원하고 있고 찾아온 자료, 내 프젝의 자료들을 바탕으로 한 답변, 정보도 이와 같은 구성에 어울리는거지? 더 고려해야 할 점들 더 고려한거고? 놓친 부분 있는지 확인해줘. 자료 필요하면 찾고.

> 서버같은 경우는 남는 pc가 없어서 pc 1대로 진행중이야. 그래서 지금 뭐 최대 40인까지만 되는거로 알아서... 이점도 참고해줘.

> 필요하면 MCP(활성화해서 연동 등), 컴퓨터 유즈 스킬 사용해도 돼.

**당시 완료 응답**

> **네. Project J는 언리얼 엔진의 기능 위에 자체 최적화 정책을 얹는 구조입니다. 이번에 실제 패키지에서 먼 나무가 원거리 대체 메시로 계속 표시되는 것을 확인했고, PC 한 대에서 40인 Iris 전투도 전원 타격·체력 수신·정상 종료까지 다시 통과했습니다.** 현재 가장 큰 검증 공백은 장거리 이동 중 숲 전체의 팝인과, 실제 MainWorld 보스전에서 배경·군중·이펙트가 겹칠 때의 성능과 가독성입니다.
>
> ## 구조와 실제 검증 결과
>
> | 영역 | 언리얼과 Project J의 역할 | 이번 확인 결과 |
> |---|---|---|
> | **먼 나무·배경** | 언리얼의 World Partition, 인스턴싱, HLOD, Nanite를 사용합니다. Project J는 어떤 식생을 어느 레이어에 배치할지, 거리별 표현·바람·그림자 예산을 어떻게 정할지 맡습니다. | Win64 D3D12 패키지에서 원본 셀이 내려간 뒤 **27개 메시 컴포넌트, 502개 인스턴스**를 담은 HLOD가 표시됐습니다. 그중 **오리나무 36개**는 렌더링된 상태까지 확인했습니다. 관측한 화면 갱신 시점에서 원본과 대체 표현이 동시에 꺼진 경우는 없었습니다. [전환 기록](C:/Users/I/Documents/GitHub/Project_J/Saved/Validation/Continuation_20261014/AAAOverall_20261126_PackagedHLODRead/world-hlod-handover.json) · [도착 화면](C:/Users/I/Documents/GitHub/Project_J/Saved/Validation/Continuation_20261014/AAAOverall_20261126_PackagedHLODRead/hlod-destination.png) |
> | **고속 이동** | 언리얼이 셀을 로드하고 활성화합니다. Project J가 Mover의 방향과 상승·하강을 읽어 `Loaded` 사전 로드와 `Activated` 활성화 범위를 미리 요청합니다. | **578m, 최고 33.0m/s** 패키지 비행을 다시 통과했습니다. 3,229프레임에서 **p95 10.65ms, p99 10.99ms, 최대 13.22ms**였습니다. 두 스트리밍 소스가 실제로 소비됐고 Landscape PSO 재등록 보완도 작동했습니다. [비행 원자료](C:/Users/I/Documents/GitHub/Project_J/Saved/Validation/Continuation_20261014/AAAOverall_20261126_PackagedFlightNative/flight.json) |
> | **40인 전용 서버** | 언리얼 Iris가 상태를 복제합니다. Project J가 연결별 관심 대상, 전투 우선순위, 과부하 단계와 서버 권위 판정을 관리합니다. | **40/40 접속·타격·체력 수신·종료**, 서버와 생성기 모두 정상 종료했습니다. 전투 집중 구간의 **월드 처리 p95는 7.27ms**, 실제 **틱 시작 간격 p95는 34.01ms**였습니다. 같은 PC의 경량 클라이언트 시험으로, 이전에 메모리 보호선에 걸렸던 규모를 이번에는 완주했습니다. [40인 원자료](C:/Users/I/Documents/GitHub/Project_J/Saved/Validation/Continuation_20261014/AAAOverall_20261126_Field40/verification.json) |
> | **군중 렌더링·전투 VFX** | 언리얼의 애니메이션 예산, Cloth, Niagara를 사용합니다. Project J가 원격 경량 망토, 포즈 갱신 우선순위, 이펙트 풀링과 원격 생성 예산을 적용합니다. | **16인 Iris 필드 보스**를 렌더링 클라이언트 1개와 함께 통과했습니다. D3D12 720p에서 화면 인원 중앙값은 16명, 공격 구간 프레임 **p95 14.69ms, p99 16.72ms, 최대 46.39ms**였습니다. 원격 경량 망토도 확인했습니다. 최장 프레임에서 게임 스레드는 **33.52ms**, GPU는 **2.26ms**로 기록돼, 그 프레임의 원인을 좁히려면 CPU 추적이 우선입니다. [16인 원자료](C:/Users/I/Documents/GitHub/Project_J/Saved/Validation/Continuation_20261014/AAAOverall_20261126_Field16RenderedRetry/verification.json) · [프레임별 측정](C:/Users/I/Documents/GitHub/Project_J/Saved/Validation/Continuation_20261014/AAAOverall_20261126_Field16RenderedRetry/Rendered/frames.csv) |
>
> ## AAA 목표에 맞춰 더 봐야 할 부분
>
> 1. **먼 숲을 보여주는 구조는 작동합니다. 이제 숲의 비용과 전환 품질을 확인해야 합니다.** 저장된 식생 액터 4개는 실제로 `HLOD_MainWorldForest`의 Instancing 레이어를 사용했습니다. 표본의 **10개 컴포넌트 모두 실제 바람 변형을 쓰면서 WPO 중지 거리 값이 `0cm`**였습니다. 멀리서 형태가 충분히 작은 나무까지 바람 변형이 남으면 VSM 그림자 캐시 비용이 커질 수 있습니다. 먼저 수종별로 수관 실루엣·색·그림자를 보존하는 바람 범위를 비교하고, 근거리 원본 → 중거리 LOD → 원거리 HLOD의 전환을 같은 카메라에서 연속으로 확인할 가치가 있습니다. 문서에 있는 전용 임포스터 구성을 숲 전역에서 사용 중이라는 근거는 아직 없습니다. [식생 표본](C:/Users/I/Documents/GitHub/Project_J/Saved/Validation/Continuation_20261014/AAAOverall_20261126_FoliageScan/foliage.json) · [검사 로그](C:/Users/I/Documents/GitHub/Project_J/Saved/Validation/Continuation_20261014/AAAOverall_20261126_FoliageScan/Run.log:2398) · [Epic VSM 가이드](https://dev.epicgames.com/documentation/unreal-engine/virtual-shadow-maps-in-unreal-engine?lang=it)
>
> 2. **비행이 매끄러운 것과 첫 진입·팝인이 해결된 것은 각각 확인해야 합니다.** 이번 비행에서 PSO 누락 **68건이 모두 이륙 전**에 기록됐습니다. 비행 감속은 코드 기본값이 꺼져 있고, 이번 시험에서는 켰지만 발동은 0회였습니다. 다음 월드 검증은 숲·능선·정착지를 이어 달리면서 급선회와 재진입을 포함하고, 프레임 시간과 별도로 **지형·충돌 준비, 텍스처 밉 도착, 원거리 숲 전환**을 봐야 합니다. 기존 BP02의 검증 조건도 정정할 지점이 있습니다. **30m/s로 3분이면 5.4km**이고, 8×8km 정사각형의 대각선은 약 **11.3km**입니다. [비행 로그](C:/Users/I/Documents/GitHub/Project_J/Saved/Validation/Continuation_20261014/AAAOverall_20261126_PackagedFlightNative/Run.log) · [감속 기본값](C:/Users/I/Documents/GitHub/Project_J/Source/Project_J/World/Project_JWorldStreamingComponent.h:128) · [BP02 검증 조건](C:/Users/I/Documents/GitHub/Project_J/Docs/Architecture/Blueprints/BP02_OpenWorld_HighSpeedStreaming_Foliage_Rendering.md:135)
>
> 3. **40인에서는 틱이 여유로워도 피해 결과 수신이 밀릴 수 있습니다.** 이번 40인 시험에서 서버 피해 확정 뒤 클라이언트가 체력 변화를 확인하기까지 **p95 318ms**였습니다. 같은 PC에서 잰 해당 구간의 지연이며, 복제 스케줄링과 생성기 처리 중 어느 쪽의 영향인지는 아직 분리하지 못했습니다. 코드상 같은 길드와 파티는 군중 상한 예외이므로, 혼합 파티뿐 아니라 **같은 길드가 한 보스에 몰리는 경우**도 따로 볼 필요가 있습니다. 당분간 **40인은 한 PC에서 확인한 경량 전투 규모**, **16인은 한 화면까지 확인한 규모**로 쓰는 게 정확합니다. [40인 지연 원자료](C:/Users/I/Documents/GitHub/Project_J/Saved/Validation/Continuation_20261014/AAAOverall_20261126_Field40/verification.json) · [군중 필터 예외](C:/Users/I/Documents/GitHub/Project_J/Source/Project_JCharacter/Private/Network/Project_JCrowdDensityNetObjectFilter.cpp:383)
>
> 4. **실제 제작 장면에서는 성능과 가독성을 같이 봐야 합니다.** 이번 16인 화면은 붉은 공격 트레일이 크게 겹쳐, 그 한 장으로 보스 경고의 가독성을 판정하기 어려웠습니다. 시험 장소도 MainWorld B01과 별개인 필드 샌드박스였습니다. **B01에서 숲·지형·그림자·망토·이펙트·이름표·오디오가 겹치는 장면**이 다음 비교에 더 적합합니다. 반복 스파크와 먼지처럼 비슷한 효과의 활성 시스템 수가 병목으로 확인되면 Niagara Data Channels 기반 배치를 검토할 수 있습니다. 필수 경고는 그 장면에서 식별 가능해야 합니다. [16인 공격 화면](C:/Users/I/Documents/GitHub/Project_J/Saved/Validation/Continuation_20261014/AAAOverall_20261126_Field16RenderedRetry/Rendered/Strike.png) · [기존 B01 검증](C:/Users/I/Documents/GitHub/Project_J/Docs/Architecture/Planning/Project_J_PhaseAD_Codex_Audit_2026-11-25.md:145) · [Epic Niagara 최적화](https://dev.epicgames.com/documentation/unreal-engine/scalability-and-best-practices-for-niagara)
>
> **앞서 찾은 자료는 이 구성에 맞습니다.** 붉은사막에서는 원경과 랜드마크를 살리는 월드 제작·스트리밍 판단을, Fortnite에서는 Project J에도 직접 적용되는 **원거리 수관 보존과 나무 그림자 비용**을, 대규모 MMO 사례에서는 **같은 지점에 몰릴 때 어떤 정보와 표현을 우선할지**를 가져올 수 있습니다. Project J에서 우선순위가 높은 것은 **숲의 전환 품질과 WPO·그림자 비용**, **40인 피해 수신 지연**, **MainWorld B01의 복합 장면**입니다. [펄어비스 개발 발표](https://www.pearlabyss.com/ko-kr/Board/Detail?_boardNo=15224) · [Epic Fortnite 식생 기술](https://www.unrealengine.com/tech-blog/bringing-nanite-to-fortnite-battle-royale-in-chapter-4?lang=en-US) · [ArenaNet 월드 보스 운영](https://www.guildwars2.com/en-gb/news/the-megaserver-system-world-bosses-and-events/)
>
> 이번 검증은 기존 Development 패키지와 저장된 맵을 사용했습니다. 초기 패키지 실행 오류, HLOD 진단 입력 누락, 16인 실행기의 서버 준비 시간 초과는 재실행으로 분리했고, 표에는 최종 결과 파일이 생성된 실행만 반영했습니다. 프로젝트 소스·설정·에셋 변경 없이 원자료 33개를 기존 중앙 보관소에 등록했습니다. [검증 원자료 목록](C:/Users/I/Documents/GitHub/Project_J/Saved/StorageCleanup_20261104/ArchiveStore/INDEX.csv)

## 3. 원경·탈것·캐릭터의 범용성

**사용자 요청**

> 자녀랑 뭐 검증도 많이 했구나. 오케이 근데 그 전에 궁금한 게 있어. 어 지금 막 원숭이를 보여주는 구조, 이것도 지금 뭐 메인 월드에서만 한정되는 게 아닌가? 약간 과적합 같은 거? 예를 들어서 지금 메인 월드를 하나를 기준으로 엄청 큰 오픈월드 MMORPG를 만들려고 해. 근데 거기에 뭐 추가할 때, 추가된 부분에는 뭐 적용이 안 되는 뭐 이런 과적합이나 좀 안 좋은 구조로 하는 건 아니지? 뭐 그 뭐 범용적으로 뭐 적용도 되면서 뭐 얼리어 엔진에 뭐 HLO인가? 뭐 그것도 사용하면서 뭐 그렇게 하는 건가? 약간 구현을 어떤 식으로 하려고 하는 거지? 설계 같은 거? 그리고 뭐 지금도 막 비행 막 이렇게 하는데 비행에만 집중된 거 아니지? 아 지상 탈것도 빠른 게 있는데 빠른 지상 탈것이 나중에 추가된다고 하면은 뭐 그것도 적용되는 거지? 막 비행에만 신경 쓴 게 아니고. 어 그리고 지금도, 지금 있는 캐릭터에 스킬에만 한정된 게 아니고 범용적으로 전부 다 되는 거 맞지? 일단 이런 거 답변해줘. 만약 아니라면은 필요한 자료나 뭐 여러 정보들도 더 찾아주고. 내 말 이해했지? 내 말에서 이해 안 되는 부분 있으면 좀 더 말해줄래?

**당시 완료 응답**

> 네, 이해했어요. 말씀하신 “원숭이”는 문맥상 **원경, 특히 멀리 보이는 숲**으로 이해했습니다. 질문의 핵심은 **새 지역·탈것·캐릭터·스킬을 추가할 때마다 최적화를 다시 구현해야 하느냐**는 거죠.
>
> 코드를 다시 확인해 보니, **월드 스트리밍·이동 예측·전투 표현의 핵심은 공용입니다.** 다만 새 콘텐츠가 공용 경로를 타도록 제작할 때 연결해야 할 설정이 있고, **식생 자동 검사와 Landscape PSO 보완에는 실제로 MainWorld 한정 코드가 남아 있습니다.**
>
> ### 1. MainWorld를 넓히면 먼 나무도 같은 방식으로 최적화되나요?
>
> **네. 새 구역의 나무가 같은 World Partition과 식생 HLOD 규칙을 사용하면, 기존 구역처럼 원본 셀이 내려간 뒤 먼 거리에서 가벼운 대체 표현을 표시할 수 있습니다.** HLOD를 만들고 전환하는 역할은 언리얼이 맡습니다. 앞서 패키지에서 확인한 502개 인스턴스 전환도 언리얼의 실제 HLOD 경로였습니다. 언리얼은 액터, Data Layer, World Settings에서 HLOD 레이어를 지정할 수 있고, PCG로 생성한 액터에도 설정된 레이어를 이어줄 수 있습니다. 새 구역에서 그 배치 규칙을 재사용하고 HLOD를 생성해 패키지에 포함하면 됩니다. [Epic World Partition HLOD](https://dev.epicgames.com/documentation/unreal-engine/world-partition-hierarchical-level-of-detail-in-unreal-engine) · [Epic PCG와 World Partition](https://dev.epicgames.com/documentation/unreal-engine/using-pcg-with-world-partition-in-unreal-engine)
>
> **현재 확장할 때 가장 조심해야 할 부분은 자동 검사입니다.** 식생 검증기는 `L_MainWorld`와 기존 제작 태그 두 개를 조건으로 사용합니다. 같은 MainWorld라도 새 나무를 수동 배치하거나 새 생성 도구가 다른 태그를 쓰면, HLOD나 바람 설정이 빠져도 기존 검사가 잡지 못할 수 있습니다. Landscape PSO 재등록 보완도 현재 `L_MainWorld`에 한정되어 있어, 같은 맵 안의 확장 구역에는 적용 범위가 이어지지만 별도 대륙 맵에는 그대로 넘어가지 않습니다. [식생 검증기](C:/Users/I/Documents/GitHub/Project_J/Source/Project_JCharacterEditor/Private/Validation/Project_JCrossAssetEditorValidator.cpp:23) · [Landscape PSO 보완](C:/Users/I/Documents/GitHub/Project_J/Source/Project_J/Project_J.cpp:22)
>
> 설계는 **숲·랜드마크·근거리 소품처럼 콘텐츠 역할별로 재사용할 제작 규칙을 두는 방향**이 맞습니다. 예를 들어 새 바이옴은 수종과 밀도를 바꾸면서도 숲용 HLOD 레이어를 재사용할 수 있습니다. 이후 식생 검증도 맵 경로나 기존 태그 대신 **해당 월드가 선택한 정책과 액터의 제작 설정**을 읽도록 확장하면, 새 구역을 추가해도 같은 기준으로 확인할 수 있습니다.
>
> ### 2. 나중에 빠른 지상 탈것을 추가해도 되나요?
>
> **네. 두 단계 예측 스트리밍은 현재도 지상 이동을 처리합니다.** 컨트롤러가 현재 조종 중인 Pawn의 실제 속도를 읽고, 지상에서는 수평 속도로 전방의 로드·활성화 범위를 계산합니다. 비행에서는 상승·하강 속도까지 포함합니다. 따라서 새 지상 탈것이 기존 `AProject_JMountMoverPawn` 경로를 사용하고 속도를 정상 제공하면, 말이나 탈것마다 스트리밍 코드를 추가할 필요가 없습니다. 기본 고속 진입 기준은 **6m/s**이고, 기존 지상 질주 프로필은 **14m/s**입니다. 공통 스트리밍이 적용되려면 해당 월드에 Project J 컨트롤러와 월드 정의도 연결돼 있어야 합니다. [속도 기반 예측](C:/Users/I/Documents/GitHub/Project_J/Source/Project_J/World/Project_JWorldStreamingComponent.cpp:172) · [지상 탈것 프로필](C:/Users/I/Documents/GitHub/Project_J/Source/Project_JMount/Public/Mount/Project_JMountMoverTypes.h:154)
>
> 다만 **앞길 셀이 준비되지 않았을 때 속도를 낮추는 보호 기능은 현재 비행 전용**입니다. 그리고 지난번 패키지에서 실측한 경로도 비행이었습니다. 빠른 지상 탈것을 제작하면 울타리·다리·경사면·좁은 길에서 **지면과 충돌이 제때 준비되는지**를 먼저 확인하는 게 맞습니다. 기존 예측으로 부족하다는 근거가 나오면, 그때 공통 준비 상태를 지상 이동에도 연결하는 방향이 좋습니다. [비행 준비 상태 및 감속 코드](C:/Users/I/Documents/GitHub/Project_J/Source/Project_J/World/Project_JWorldStreamingComponent.cpp:299) · [기존 검증 범위](C:/Users/I/Documents/GitHub/Project_J/Source/Project_J/Private/Tests/Project_JPhase1SystemTests.cpp:82)
>
> ### 3. 새 캐릭터와 스킬도 공통 최적화를 받나요?
>
> **공용 부모와 데이터 연결을 사용하면 받습니다.** 캐릭터의 애니메이션 예산은 공용 기본 메시 컴포넌트에 들어 있고, 전투 VFX는 `AttackTag`와 연출 프로필을 통해 공용 컴포넌트로 들어갑니다. 새 직업이 별도 검술과 이펙트를 써도, 이 경로를 사용하면 풀링·원격 생성 예산·취소 시 정리를 재사용할 수 있습니다. 근접·장판·투사체에도 각각 공용 제작 경로가 있습니다. [캐릭터 공용 메시](C:/Users/I/Documents/GitHub/Project_J/Source/Project_JCharacter/Private/Project_JBaseCharacter.cpp:35) · [전투 VFX 구조](C:/Users/I/Documents/GitHub/Project_J/Docs/Combat/Architecture/CombatVFXArchitecture.md:5) · [새 스킬 제작 가이드](C:/Users/I/Documents/GitHub/Project_J/Docs/Architecture/Blueprints/Content_Authoring_Integration_Guide.md:57)
>
> **새 콘텐츠마다 정해야 하는 건 품질과 비용이 달라지는 부분**입니다. 새 스켈레톤의 메시 LOD와 소켓, 새 스킬의 타격 창, Niagara Effect Type과 컬링, 보스 경고의 보호 등은 해당 에셋에 맞춰 설정하고 확인해야 합니다. 예를 들어 공용 경로를 써도 새 망토에 스펠블레이드의 경량 물리 메시가 생기거나, 새 스킬이 스펠블레이드의 트레일 예산을 자동으로 이어받지는 않습니다. [Niagara Effect Type 예산](https://dev.epicgames.com/documentation/en-us/unreal-engine/performance-budgeting-using-effect-types-in-niagara-for-unreal-engine)
>
> 그래서 확장성을 확인하는 가장 좋은 기준은 **두 번째 콘텐츠를 추가해 보는 것**입니다. 새 바이옴 한 구역, 빠른 지상 탈것 한 종, 다른 캐릭터의 새 스킬 한 세트가 **기존 데이터와 제작 규칙만으로 공용 경로에 연결되는지** 보면 됩니다. 이번 코드 확인에서 먼저 손볼 근거가 가장 분명한 곳은 **식생 검사의 MainWorld·기존 태그 의존성**이고, 다음으로 확인할 실제 플레이는 **빠른 지상 탈것의 셀 경계 통과**입니다.

## 4. 원거리 표현 정정

**사용자 요청**

> 아니야, 내가 말한 게 저 원숭이가 아니고 원거리야. 원거리

**당시 완료 응답**

> 네, **원거리**였군요. 새 지역이 추가돼도 멀리 있는 나무·건물·랜드마크가 계속 보이면서, 거리에 따라 더 가벼운 표현으로 전환되는 구조인지 물으신 거예요.
>
> 그 원거리 표현은 **언리얼의 공용 HLOD 기능**이라 새 지역에도 재사용할 수 있습니다. 새 콘텐츠에 역할에 맞는 HLOD 레이어를 지정하고 대체 메시를 빌드하면 됩니다. 현재 MainWorld와 기존 태그에 묶여 있는 부분은 **Project J의 식생 자동 검사**입니다. 그래서 확장할 때는 새 지역도 같은 제작 규칙을 따르고, 설정이 빠졌을 때 자동으로 잡히게 하는 것이 핵심입니다.

## 5. 제작 누락과 예외

**사용자 요청**

> 아무튼 뭐 나중에 뭐 얼리어레 뭐 HLOD나 우리가 구현한 뭐 시스템 아키텍처 이런 걸 두 가지 방식 또는 더 나은 게 있으면 그걸 활용한다는 거지. 그러면 약간 궁금한 게 있어. 나중에 월드, 메인 월드나 이런 레벨 작업자들이 모르고 HLOD를 빠트렸어. 혹은 뭐 우리가 만든 시스템 적용을, 우리가 만든 시스템을 적용 안 했어. 이럴 때 대비하는 뭐 예외 처리나 그런 게 있나? 지금 내가 말한 이런 예외 처리에만, 뭐 월드 예외 처리에만 집중하지 말고 뭐 VFX 인스턴싱이나 뭐 다른 월드의 인스턴싱 이런 게 또 부족한지. 또 그리고 탈것, 몇몇 탈것에서만 이게 팝업이 안 나타나게 하는 등 뭐 이런 뭐 하드코딩 이런 방식은 아닌 거지? 뭐 게임 캐릭터 같은 경우는 뭐 데이터 에셋 만들어서 거기에서 뭐 인스턴싱 뭐 이런 거 적용한다거나 약간 이런 구조인가? 더 고려해야 할 점이나 이렇게 예외 처리할 게 더 있나? 뭐 전투나 애니메이션, 서버, 월드, 기타 등등 이런 식으로 뭐 내가 말한 것에만 아니고 여러 분야가 있을 거잖아. 뭐 이런 것들도 다 나중에 추가 확장할 때도 다 범용적으로 더 좋은 구조로 적용이 되는 건가? 뭐 이런 예외 처리나 이런 거 다 포함해서 애매한 부분이 있으면 뭐 더 자료나 정보 더 찾아와서 적용, 뭐 적용 및 설계해도 돼. 그래서 지금 당장 작업하자는 건 아니고. 그래서 내가 말하지 않은 것들이나 더 놓친 것들, 더 고려해서 좋은 정보나 자료 부족하면 찾아줘

**당시 완료 응답**

> 네. **언리얼이 HLOD·인스턴싱·스트리밍·복제를 실행하고, Project J가 어떤 콘텐츠에 어떤 정책을 적용할지 정하는 구조**입니다. 이 방식이면 새 지역이나 직업을 만들 때 기존 시스템을 재사용하면서, 실제 병목이 확인된 부분만 다른 기법으로 바꿀 수 있습니다.
>
> 다만 확인해 보니 **누락 방어는 분야마다 수준이 다릅니다.** 전투 경고에는 실제 대체 표시가 있고, 서버는 필수 준비가 빠지면 입장을 막습니다. 반면 **새 월드의 HLOD와 새 VFX의 비용 정책은 제작자가 설정을 빠뜨려도 현재 검사에서 놓칠 수 있습니다.** 이번에는 소스·제작 문서와 언리얼 공식 자료를 대조했습니다. 구현이나 설정 변경은 하지 않았습니다.
>
> ## 지금 무엇을 빠뜨리면 어떻게 되나
>
> | 분야 | 현재 있는 방어 | 현재 남은 구멍 |
> |---|---|---|
> | **월드·원거리 HLOD** | 언리얼은 액터에 지정된 레이어와 Data Layer·World Settings 기본값을 사용할 수 있습니다. Project J도 특정 식생의 Instancing HLOD와 바람 변형 거리 설정을 검사합니다. | 기본 레이어까지 빠졌거나 대체 메시를 빌드·Cook하지 않으면, 원본 셀이 내려갈 때 원경에서 사라질 수 있습니다. 현재 식생 검사는 **MainWorld와 기존 제작 태그**에 묶여 있어, 같은 맵의 새 배치도 태그에 따라 놓칠 수 있습니다. 제작 문서에 적힌 레이어와 실제 표본이 사용한 `HLOD_MainWorldForest`도 다릅니다. 제작자가 따라야 할 기준을 먼저 일치시킬 필요가 있습니다. [Epic HLOD 레이어와 빌드](https://dev.epicgames.com/documentation/unreal-engine/world-partition-hierarchical-level-of-detail-in-unreal-engine) · [식생 검사 코드](C:/Users/I/Documents/GitHub/Project_J/Source/Project_JCharacterEditor/Private/Validation/Project_JCrossAssetEditorValidator.cpp:23) · [실제 식생 표본](C:/Users/I/Documents/GitHub/Project_J/Saved/Validation/Continuation_20261014/AAAOverall_20261126_FoliageScan/foliage.json) |
> | **월드 인스턴싱** | 식생 외에도 마을의 계단·울타리와 광산 구조물 같은 반복 배치에 HISM을 사용하는 제작 경로가 있습니다. | 레벨 작업자가 같은 소품을 개별 액터로 대량 배치했을 때 일괄 검출하거나 변환한다는 근거는 확인하지 못했습니다. 반대로 상호작용·파괴·복제·저장 상태가 있는 물체까지 한꺼번에 합치면 게임플레이가 달라질 수 있습니다. **반복 장식은 인스턴싱, 상태를 가진 오브젝트는 기존 게임플레이 경로**를 쓰고, 셀별 배치 밀도와 충돌 비용을 확인하는 방향이 맞습니다. [정착지 제작 경로](C:/Users/I/Documents/GitHub/Project_J/Scripts/World/Starter-SettlementAuthoring.py:54) · [Epic ISM/HISM](https://dev.epicgames.com/documentation/unreal-engine/instanced-static-mesh-component-in-unreal-engine) |
> | **탈것·팝인** | 두 단계 예측은 **현재 조종 중인 Pawn의 속도**를 씁니다. 빠른 지상 이동은 수평 속도, 비행은 수직 속도까지 사용합니다. 특정 말이나 용의 에셋 이름으로 팝인을 처리하는 코드는 아닙니다. | 비행 여부는 현재 지원하는 탈것 부모 클래스에서 읽고, 미준비 셀에 대한 감속은 **기존 Mover 비행 전용이며 기본값은 꺼져 있습니다.** 새 이동 방식이나 별도 컨트롤러가 공통 경로를 우회하면 같은 보장을 받지 못합니다. 월드 정의가 빠지면 Project J의 `Activated` 예측 소스도 제공되지 않습니다. [공통 이동 예측](C:/Users/I/Documents/GitHub/Project_J/Source/Project_J/World/Project_JWorldStreamingComponent.cpp:337) · [활성화 소스 조건](C:/Users/I/Documents/GitHub/Project_J/Source/Project_J/World/Project_JWorldStreamingComponent.cpp:417) |
> | **VFX·보스 경고** | 공용 Cue 경로는 Niagara 풀링, 원격 생성 예산, 취소 시 정리를 제공합니다. **이미 활성화된 필수 경고**는 Niagara 프로필을 찾지 못해도 기본 경고 마커를 표시할 수 있습니다. 등록된 경고 Notify의 서버 실행·필터 설정도 검사합니다. | **필수 경고 Notify 자체를 빼먹으면** 현재 경고 검사기는 그 Notify가 필요했던 공격이라는 사실을 알아내지 못합니다. 또한 Effect Type이 없는 새 Cue에 공용 비용 정책을 자동으로 달아주는 구조는 아닙니다. 반복 스파크 등을 공유 시스템으로 묶는 Data Channels 배치도 현재 공용 적용 근거를 확인하지 못했습니다. [경고 대체 표시](C:/Users/I/Documents/GitHub/Project_J/Source/Project_JCharacter/Private/Components/Project_JCombatPresentationComponent.cpp:391) · [경고 검사 조건](C:/Users/I/Documents/GitHub/Project_J/Source/Project_JCharacterEditor/Private/Validation/Project_JCombatWarningAssetValidator.cpp:6) · [Epic Niagara 최적화](https://dev.epicgames.com/documentation/unreal-engine/scalability-and-best-practices-for-niagara) |
> | **캐릭터·전투·애니메이션** | 직업 Data Asset이 능력 세트와 전투 스타일을 연결합니다. 공용 캐릭터의 기본 메시에는 애니메이션 예산 경로가 있고, 공격 정의·몽타주·베이크 타격 궤적에 검사와 실행 중 방어가 있습니다. 예를 들어 베이크 궤적이 현재 스켈레톤과 맞지 않으면 해당 공격을 중단합니다. | 새 메시의 LOD, Cloth 물리 메시, 소켓, AnimBP의 실제 전환, 스킬의 타격·경고 타이밍은 제작해서 연결해야 합니다. **Data Asset은 어떤 정책과 에셋을 쓸지 연결하는 곳**이고, 실제 메시·VFX 최적화는 그 데이터를 소비하는 컴포넌트에서 실행됩니다. 배경 군중의 Mass·VAT와 실제 플레이어의 애니메이션 예산도 별도로 봐야 합니다. [직업 Data Asset](C:/Users/I/Documents/GitHub/Project_J/Source/Project_JCharacter/Public/CharacterClass/Project_JCharacterClassDefinition.h:21) · [공용 메시](C:/Users/I/Documents/GitHub/Project_J/Source/Project_JCharacter/Private/Project_JBaseCharacter.cpp:35) · [공격 실행 방어](C:/Users/I/Documents/GitHub/Project_J/Source/Project_JCharacter/Private/Combat/Project_JGameplayAbility_Melee.cpp:453) |
> | **서버·복제·보상** | 온라인 서버는 월드·NetDriver·저장소 복구·채널 등록이 준비되어야 입장을 허용합니다. 과부하 제어와 보상 재시도 경로도 있습니다. | 서버가 입장 준비를 마쳤다는 결과만으로 **신규 NPC에 복제 우선순위가 실제로 붙었는지**, **새 보스의 경고와 보상이 재접속 후에도 맞는지**까지 알 수는 없습니다. 새 직업·펫·보스처럼 복제 대상이 늘어나는 변경은 실제 객체 등록, 필수 상태 수신, 종료·재접속을 확인해야 합니다. 지난 40인 시험도 서버 틱과 피해 수신 지연이 별개라는 걸 보여줬습니다. [서버 준비 조건](C:/Users/I/Documents/GitHub/Project_J/Source/Project_J/Backend/Project_JServerDrainSubsystem.cpp:127) · [40인 실측](C:/Users/I/Documents/GitHub/Project_J/Saved/Validation/Continuation_20261014/AAAOverall_20261126_Field40/verification.json) |
>
> 참고로 같은 **인스턴싱**이라는 말도 역할이 다릅니다. 월드의 ISM/HISM은 반복 메시를 묶어 그리고, Niagara 풀링은 컴포넌트를 재사용하며, Data Channels는 여러 효과 이벤트를 공유 시스템으로 모을 수 있습니다. GAS의 `InstancingPolicy`는 능력 실행 상태의 소유 방식을 정합니다. 새 직업 Data Asset 하나로 이 네 가지가 동시에 켜지는 구조는 아닙니다. [Epic ISM/HISM](https://dev.epicgames.com/documentation/unreal-engine/instanced-static-mesh-component-in-unreal-engine) · [Epic Niagara Data Channels](https://dev.epicgames.com/documentation/unreal-engine/niagara-data-channels-overview) · [Project J 스킬 제작 규칙](C:/Users/I/Documents/GitHub/Project_J/Docs/Architecture/Blueprints/Content_Authoring_Integration_Guide.md:57)
>
> ## 확장할 때 적용할 설계
>
> **기본값으로 누락을 줄이고, 필수 연결은 저장·Cook에서 잡고, 실행 중에는 영향에 따라 처리하는 방식**이 Project J에 맞습니다.
>
> 1. **제작 기본값은 콘텐츠 역할에 붙입니다.** 새 숲은 숲용 Data Layer·PCG·HLOD 레이어를, 새 탈것은 공통 Mover와 이동 프로필을, 새 스킬은 공용 공격·연출 프로필을 사용합니다. 특별한 경우에는 에셋별로 명시적으로 조정합니다. 여러 맵으로 확장할 때는 식생 검사 대상을 `L_MainWorld`나 기존 태그 두 개에서 **월드가 선택한 정책과 콘텐츠 역할**로 옮기면 됩니다. [Epic PCG와 World Partition](https://dev.epicgames.com/documentation/unreal-engine/using-pcg-with-world-partition-in-unreal-engine)
> 2. **검사 대상은 에셋 하나에서 실제 연결까지 넓힙니다.** 숲 액터의 레이어가 유효한지 외에 HLOD가 생성되고 패키지에 들어가는지, 스킬의 Cue가 유효한지 외에 **그 공격이 요구하는 경고가 몽타주에 존재하는지** 확인해야 합니다. 기존 선택 에셋 검증과 Cook 게이트를 재사용할 수 있습니다. 현재 게이트는 선언한 입력의 검증을 보장하며, 월드 외부 액터 전체나 Cook 의존성 전체에 대한 검증은 별도입니다. [기존 검증·Cook 기준](C:/Users/I/Documents/GitHub/Project_J/Docs/Maintenance/Change_Completion_Policy.md:59) · [Epic Asset Manager](https://dev.epicgames.com/documentation/unreal-engine/asset-management-in-unreal-engine)
> 3. **실행 중 대체 정책은 기능의 중요도에 맞춥니다.** 장식 트레일이 빠지면 안전하게 생략할 수 있습니다. 필수 공격 경고는 기본 표시가 남아야 합니다. 지면·충돌이 필요한 이동은 준비를 기다리거나, 검증된 경우 감속할 수 있습니다. 피해 판정이나 보상 저장의 필수 데이터가 빠졌으면 실행을 중단하거나 안전하게 재시도해야 합니다. 특히 **HLOD가 화면에 보이는 상태와 실제 지형·충돌·Nav가 준비된 상태**는 따로 확인해야 합니다.
>
> 추가로 놓치기 쉬운 건 **셀을 가로지르는 액터 참조, 패키지에서 빠진 Soft Reference, 종료 뒤 늦게 오는 비동기 콜백, 이름표·오디오·Cloth가 한 장면에 몰리는 비용**입니다. 언리얼은 서로 참조하는 월드 액터를 함께 로드할 수 있어서, 새 정착지가 겉으로는 잘 나뉘어 보여도 참조 하나로 멀리 있는 셀까지 같이 로드될 수 있습니다. 그리고 새 콘텐츠를 한 번 보이는 것만 확인하면, 반복 진입·이탈 후 메모리와 작업 큐가 돌아오는지는 놓치게 됩니다. [Epic World Partition의 액터 참조](https://dev.epicgames.com/documentation/en-us/unreal-engine/world-partition-in-unreal-engine) · [Epic 성능 프로파일링](https://dev.epicgames.com/documentation/unreal-engine/introduction-to-performance-profiling-and-configuration-in-unreal-engine)
>
> **지금 가장 가치 있는 후속 설계는 세 가지입니다.** 새 바이옴의 **HLOD 지정·빌드·Cook 누락**을 기존 검사에서 잡도록 하고, 새 지상 탈것의 **셀 경계와 충돌 준비**를 실제로 확인하고, 새 보스 공격의 **필수 경고 생산자가 통째로 빠진 경우**를 검출하는 것입니다. VFX 공유 배치나 군중 표현 교체는 그다음에 실측으로 도입 여부를 판단하면 됩니다. 이렇게 하면 기존에 잘 작동하는 엔진 기능과 Project J 공용 경로를 유지하면서, 콘텐츠가 늘어날수록 생기는 누락을 제작 단계에서 줄일 수 있습니다.

## 6. 설계 전 인계 범위

**사용자 요청**

> 좋다, 그러면 여러 자료 정보 구현 뭐 이런 거 설계 이런 거 찾아왔잖아. 그래서 이제 아스트라, 6.2 아스트라 모델로 할 거거든? 근데 채팅창 도중에 모델 변경하면은 뭐 우리 컨테스트 맥락 좀 잃거나 그런 경우가 있잖아. 혹은 약간 우리가 찾은 정보를 다 모를 수도 있고. 그래서 우리가 찾은 정보와 우리가 앞으로 이 프로젝트에서 원하는 게 뭔지 이런 거를 마크다운 문서로 해서 바탕화면에 저장하려고 하거든. 너의 생각은 어때? 더 고려해야 할 점은 뭐고 내가 놓친 부분이나 말하지 않는 것들도 더 고려해서 답변 먼저 해줘. 그리고 우리가 찾은 이 모든 정보를 이 채팅창에서 찾은 모든 정보와 목표 이런 것들을 다 담으려고 하는데 내가 뭐 Ctrl+C, Ctrl+V 해서 이 채팅창에 있는 모든 내용을 너에게 전달해줄까? 아니면 뭐 너 다 전부 기억하고 있는 거 맞아? 확실해?

**당시 완료 응답**

> **좋은 방법입니다. GPT‑6.2 Astra로 바꾸기 전에, 이 채팅에서 정리한 내용을 바탕화면에 마크다운 인계 문서 한 개로 남기는 걸 추천합니다. 그리고 지금은 채팅 전체를 복사해서 보내주실 필요 없습니다.**
>
> **확실한 범위는 이렇습니다.** 저는 현재 이 대화에서 Project J의 목표, 조사한 자료, 실제 테스트 결과, 남은 설계 쟁점을 확인할 수 있습니다. 이 범위에서 인계 문서를 작성할 수 있습니다. 다만 **화면에 표시되는 과거 대화와 도구 출력이 한 글자도 빠짐없이 다음 모델에 전달된다고 보장할 수는 없습니다.** Codex 공식 문서는 같은 스레드의 다음 턴에서 모델을 바꿀 수 있다고 설명하며, 대화 기록을 압축하는 기능도 문서화합니다. 대화는 참고 자료로 이어가고, 중요한 판단과 증거는 파일로 고정해 두는 편이 가장 확실합니다. [Codex 스레드·모델 설정과 대화 압축](https://learn.chatgpt.com/docs/app-server)
>
> ## 문서에 꼭 담을 내용
>
> - **프로젝트가 원하는 결과.** 큰 오픈월드 MMORPG에서 멀리 있는 숲·건물·랜드마크가 계속 보이고, 고속 지상·비행 이동 중에 자연스럽게 전환되며, 다인 전투에서도 필수 경고와 피해 결과가 잘 전달되는 것. 언리얼의 HLOD·World Partition·ISM/HISM·Niagara·Iris를 활용하고, Project J는 제작 규칙과 이동 예측·전투 우선순위·부하 대응을 맡는 방향.
> - **검증 결과와 그 조건.** 패키지 HLOD 전환에서 확인한 **502개 인스턴스와 오리나무 36개**, **578m 비행**, **한 PC의 40인 경량 전투 완주**, **16인 렌더링 시험**, 그리고 **40인 피해 수신 p95 318ms**. 장면·빌드·실행 방식과 원자료 경로를 같이 넣어, 다음 모델이 40인 서버 결과를 40인 렌더링이나 운영 정원으로 해석하지 않게 합니다. 첫 진입 PSO 누락과 식생 WPO 검사 결과도 포함합니다.
> - **확장하면서 빠뜨리기 쉬운 연결.** 새 월드의 HLOD 지정·빌드·Cook, 새 식생을 놓칠 수 있는 기존 태그 검사, 빠른 지상 탈것의 셀·충돌 준비, 새 스킬에서 필수 경고를 만드는 Notify가 통째로 빠진 경우, VFX Effect Type과 배치 적용 여부입니다. 캐릭터·애니메이션·UI·오디오·Nav·저장·복제까지 포함하고, 각 항목에 **현재 있는 방어와 아직 입증되지 않은 범위**를 기록합니다.
> - **조사 자료와 판단 근거.** 붉은사막·Fortnite·Horizon·MMO 제작사 발표, 언리얼 공식 문서, 오픈소스와 독립 측정 자료를 주제별로 연결합니다. 각 링크에는 **Project J에서 어떤 결정을 검토할 때 참고하는지** 한 줄을 붙입니다. 개발사 발표 수치와 우리 프로젝트의 실측 조건도 함께 적습니다.
> - **다음 모델의 작업 경계.** 현재는 **조사·검증·설계 인계 단계**입니다. 문서에 적힌 개선 후보는 이후 작업 지시로 자동 실행되지 않도록 명시합니다. PC 한 대라는 조건, 저장소 `AGENTS.md`, Unreal 빌드 규칙, 기존 원자료 위치도 넣습니다. 다음 모델이 작업을 시작할 때는 문서 날짜와 현재 작업 트리를 먼저 대조할 수 있게 합니다.
>
> 제가 권하는 형식은 **맨 앞에 한 페이지 분량의 핵심 판단, 뒤에 상세 검증·자료·재현 경로**가 있는 단일 `.md`입니다. 특히 **왜 이 방향을 택했는지**, **어떤 조건에서 결과가 달라졌는지**, **무엇을 아직 검증하지 않았는지**를 남기는 게 중요합니다. 그래야 다음 모델이 이미 끝난 조사를 반복하거나, 설계 후보를 적용 완료로 읽는 일을 줄일 수 있습니다. 파일명은 `Project_J_오픈월드_MMO_최적화_인계_2026-11-26.md` 정도가 적당합니다.
>
> **지금 필요한 복사·붙여넣기는 없습니다.** 다른 채팅에만 있는 결정, 여기서 확인할 수 없는 첨부물, 또는 문장 그대로 보존하고 싶은 발언이 있다면 그 부분만 추가해 주세요. 문서 작성 전에 전체 채팅을 다시 보낼 필요는 없습니다. 요청하신 대로 이번에는 먼저 의견만 정리했고, 파일은 아직 저장하지 않았습니다.

## 7. 원문 보존과 모델 역할

**사용자 요청**

> 오케이, 그럼 우리가 원하는 목표, 설계 방향과 남은 과제까지 빠짐없이 담아서 GPT-6.2 아스트라가 바로 이어갈 수 있게 마크다운 인계 문서로 바탕화면에 저장해줘. 아직 구현은 하지 말고. 모\~든 정보를 빠짐 없이 담고 그리고 정리하면서 부족한 부분, 더 찾아야 할 것 같은/찾아야 하는 자료/정보 등 이 있으면 더 찾아줘.
> 그리고 설계는 아스트라가 할 거니까 너의 의견에 과적합되지 않게 해줘. 뭔지 알지?? 더 좋은 구현 구조, 설계가 있을 수 있잖아. 그 부분들이나 기타 등등 더 고려해야 할점있으면 더 고려해서 좋은 구조로 작성해줘. 내가 놓친 부분/말하지 않은 부분들도 더 고려해서 좋은 구조로 해주고.
> "C:\Users\I\Desktop\설계전 모든 내용을 담은 문서.txt" 혹시몰라서 모든 내용을 담은 txt파일도 만들었어.

> 아스트라 모델로 설계를 할거야. 설계 이후 구현은 6.2sol로 할거고. 알고있지 이미?

**당시 완료 응답**

> 바탕화면에 [Project J 오픈월드·MMO 최적화 인계 문서](C:/Users/I/Desktop/Project_J_오픈월드_MMO_최적화_인계_2026-11-26.md)를 저장했습니다.
>
> 문서 첫머리에 **GPT‑6.2 Astra가 설계하고, 설계 이후 사용자가 구현을 요청하면 GPT‑6.2 Sol이 구현한다**고 명시했습니다. 기존 제안은 설계 후보로 남겼고, Astra가 대안을 비교해 더 나은 구조를 선택할 수 있도록 각 후보의 판단 근거와 미검증 항목을 적었습니다.
>
> 사용자 목표, 현재 구현과 검증 원자료, 확장 시 빠뜨리기 쉬운 연결, 남은 과제를 담았습니다. HLOD 생성·Cook, 지상 탈것의 충돌·Nav 준비, VFX 배치, 피해 수신 지연을 검토하는 데 필요한 자료도 보강했습니다. 제공하신 **TXT 전문을 부록에 그대로 포함**했고, 저장소 링크 54개와 바탕화면에 저장된 파일을 검증했습니다.
>
> 이번에는 문서만 작성했습니다. 프로젝트 코드·설정·에셋 변경이나 빌드는 수행하지 않았습니다.

## 8. 원문·코드 재대조

**사용자 요청**

> 어 작성이 다 끝났으면 다시 한 번 더 점검해줘. 그 전체 대화 내용이 담긴 문서.txt 저거랑 저거에서 빠진 내용이 있는지 사사히 비교해주고, 또한 우리 프로젝트 문서 소스코드 보면서 틀린 부분이 있는지, 또한 내가 말하지 않은 부분, 더 놓친 부분들이 있는지 더 점검해서, 더 고려해서 좋은 구조로 해줘. 전부 담아야 돼, 전부. 또한 또 지금 또 다시 재차 점검하면서 필요한 정보나 자료 있으면 찾아서 해줘

**당시 완료 응답**

> 재점검을 마쳤고, 바탕화면의 [Project J 오픈월드·MMO 최적화 인계 문서](C:/Users/I/Desktop/Project_J_오픈월드_MMO_최적화_인계_2026-11-26.md)를 갱신했습니다.
>
> **제공하신 TXT는 290줄, 55,545바이트 전체를 부록과 대조했고 누락이 없었습니다.** TXT 작성 이후의 요청도 추가했습니다. **GPT‑6.2 Astra가 설계하고, 설계 이후 사용자가 요청하면 GPT‑6.2 Sol이 구현**한다는 역할을 첫머리에 유지했습니다.
>
> 프로젝트 소스와 원자료를 다시 보면서 다음을 바로잡거나 보강했습니다.
>
> - **40인 지연의 측정 범위를 정정했습니다.** 첫 구간은 플레이어 입력이 아니라 서버의 시험 요청 발송부터입니다. 40개 기록을 요청별로 짝지어 다시 계산한 결과, 서버 요청부터 클라이언트 체력 변화 관측까지 **p95 916.300ms**였습니다. 기존 **318.047ms**는 그중 서버 피해 확정 이후의 구간입니다.
> - **기본 보스 경고의 범위를 명확히 했습니다.** 현재 대체 마커는 액터에 붙는 주의 표시입니다. 실제 장판 범위·방향·시간 표시와, 필수 경고를 만드는 Notify 자체가 빠진 경우는 따로 검증해야 합니다.
> - **후속 설계에서 놓치기 쉬운 조건을 추가했습니다.** 원경 HLOD의 실제 생성·Cook, 오프스크린·무음 렌더 시험의 한계, 셀을 나갔다 돌아왔을 때 남아야 할 게임 상태, 새 콘텐츠의 정상·누락·의도적 예외를 비교하는 검증 사례를 정리했습니다. 복제 지연, 렌더 병목, Nav 및 에셋 참조를 추적할 공식 자료도 보강했습니다.
>
> 문서에 **TXT 줄 범위별 대조표, 실제 실행 로그와 원자료 진입점, 저장소 링크 65개**를 확인해 연결했습니다. 이번 작업은 문서 갱신과 기존 자료 재집계까지였으며, 프로젝트 구현 변경이나 새 Unreal 실행은 하지 않았습니다.

## 9. 설계와 구현 인계 보강

**사용자 요청**

> 어 다시 한번 점검해줘. 일단 GPT 6.2 버전 아스트라로 설계를 할 거고 설계와 관련된 작업들을 할 거고 이후에 구현, 디버깅, 버그 탐지, 세부 구현 등은 6.2 솔 모델로 할 거야. 이 점 전부 다 반영된 거지? 그래서 점검해주고 또한 놓친 부분들, 놓친 정보들 있는지 다시 한번 점검해줘. 더 고려해야 할 점들, 더 고려해서 좋은 구조로 해주고 놓친 부분이나 내가 말하지 않은 부분들 더 있는지 좀 더 파악해서 좋은 구조로 해줘

**당시 완료 응답**

> 다시 점검하고 [바탕화면의 Project J 인계 문서](C:/Users/I/Desktop/Project_J_오픈월드_MMO_최적화_인계_2026-11-26.md)를 갱신했습니다. 기존 문서에는 Sol의 역할이 **구현**으로만 적혀 있었고, 이번에 담당 범위를 모두 명시했습니다.
>
> - **GPT‑6.2 Astra**는 설계와 설계 관련 작업을 맡습니다. 현재 구조 검토, 대안 비교, 제작 누락 대응, 수용 기준과 검증 계획까지 포함합니다. 인계 문서의 제안은 후보이며, Astra가 더 적합한 구조를 선택할 수 있습니다.
> - **GPT‑6.2 Sol**은 설계가 끝나고 사용자가 실행을 요청한 뒤 **구현·세부 구현·버그 탐지·재현·디버깅·수정·회귀 검증**을 맡습니다.
>
> 추가 점검에서는 **인접 월드의 지형·원거리 표현·실제 통행이 이어지는지**, **별도 월드로 이동하거나 재접속할 때 좌표와 안정 ID가 유지되는지**, **복제 변경이 기존 클라이언트와 저장 데이터에 어떤 영향을 주는지**를 보강했습니다. Sol이 디버깅할 때 재사용할 네트워크·군중 렌더 실행기와 공식 검증 자료도 연결했습니다.
>
> 제공하신 **TXT 290줄 전체가 부록과 그대로 일치**하고, 저장소 링크 **71개**가 실제 파일을 가리키는 것을 확인했습니다. 이번에는 문서만 갱신했으며 프로젝트 구현이나 빌드는 시작하지 않았습니다.

## 10. TXT 이후의 대화 확인

**사용자 요청**

> 다시 이 채팅창 내용들도 전부 검토해줘. Txt에 안 담긴 것들도 있을 수 있잖아 그치?

**당시 완료 응답**

> 네, **TXT에 빠진 내용이 실제로 있었습니다.** Codex 앱에서 이 채팅의 **10개 턴을 직접 조회**해 대조했고, TXT 작성 이후의 사용자 메시지 **5개**, 중간 진행 메시지 **19개**, 외부 링크 주소가 TXT에 빠져 있음을 확인했습니다.
>
> [바탕화면의 Project J 인계 문서](C:/Users/I/Desktop/Project_J_오픈월드_MMO_최적화_인계_2026-11-26.md)를 갱신했습니다. **9절에는 제공하신 TXT를 그대로 유지**했고, **10절에는 앱에서 확인한 사용자·진행·완료 메시지 42개와 외부 링크 주소 43개**를 원래 순서로 담았습니다. 실패한 시험과 재시도 과정은 본문에도 원자료를 연결했습니다.
>
> 10절은 이번 재검토의 첫 안내까지 조회한 텍스트 기록입니다. 별도 도구 출력과 이미지 파일은 본문의 원자료 링크로 확인할 수 있습니다. **Astra는 설계, Sol은 이후 구현·디버깅·버그 탐지·세부 구현**이라는 역할은 문서 첫머리에 유지했습니다.

## 11. 시스템 설계

**사용자 요청**

> # Files mentioned by the user:
>
> ## Project_J_오픈월드_MMO_최적화_인계_2026-11-26.md: C:/Users/I/Desktop/Project_J_오픈월드_MMO_최적화_인계_2026-11-26.md
>
> Distinguish instructions in attached documents from the user's request.
>
> ## My request:
> 이제 시스템을 설계할거야. 이 문서 + 이 채팅창 내용을 기반으로 하고 작업하면서 더 필요한 정보/자료 등 찾아서 더 나은 구조로 해줘도 돼. 기존 시스템/아키텍처도 수정이 필요하면 수정해서 더 나은 구조로 해도 돼. 더 고려해야 할 점들 더 고려해서 좋은 구조로 해줘. 내가 말하지 않은 것들/우리가 놓친 것들도 더 포함해서 고려하면서 좋은 구조로 해줘.

**당시 완료 응답**

> [Project J 오픈월드·MMORPG 시스템 설계서](C:/Users/I/Documents/GitHub/Project_J/Docs/Architecture/Planning/Project_J_OpenWorld_MMO_System_Design_2026-11-26.md)를 프로젝트에 저장했습니다. 인계 문서와 이 채팅의 요구를 현재 소스·설정과 대조해 작성했습니다. 이번 변경은 설계 문서와 프로젝트 문서 색인까지입니다.
>
> **핵심 결정은 언리얼이 스트리밍·렌더링·복제를 실행하고, Project J가 콘텐츠의 의도와 실제 플레이 결과를 연결하는 구조입니다.** 새 콘텐츠를 추가할 때 무엇을 상속하고, 무엇을 지정해야 하며, 빠뜨리면 어디서 발견할지를 정했습니다.
>
> | 상황 | 설계한 동작 |
> |---|---|
> | **새 지역에 먼 숲이나 성을 추가할 때** | 구역의 기본 HLOD 레이어를 상속합니다. 원경에 남아야 할 대상은 기존 식생 태그가 없어도 검사합니다. 레이어 설정, 프록시 생성, 패키지 포함, 원본 셀이 내려간 뒤 실제 가시성을 차례로 확인합니다. |
> | **빠른 지상·비행 탈것을 추가할 때** | 현재 Pawn의 속도와 이동 모드로 기존 2단계 예측을 재사용합니다. 지상은 다리·급선회·재진입, 비행은 상승·강하·착륙에서 검증합니다. 먼 숲이 보이는지와 발밑 충돌이 준비됐는지는 각각 판정합니다. |
> | **새 직업·스킬·보스를 추가할 때** | 공격 정의에 플레이어가 읽어야 할 경고를 지정합니다. 이 정의에서 실제 공격 구간을 따라가므로 필수 경고 Notify 자체를 빠뜨린 경우도 잡을 수 있습니다. 선택 VFX의 품질을 낮춰도 필요한 방향·범위·시간은 기본 표시로 전달합니다. |
> | **군중과 전투 효과가 몰릴 때** | 로컬 캐릭터, 직접 교전 대상, 먼 플레이어, 배경 NPC의 요구를 나눴습니다. Niagara 풀링·공유 시스템·재질 단순화는 각각 생성 비용·활성 인스턴스 수·픽셀 비용 중 실제 병목에 맞춰 선택합니다. |
> | **PC 한 대에서 검증할 때** | 패키지 단독 주행, 서버+경량 클라이언트, 서버+경량 다수+렌더 관측자 1개로 나눕니다. 기존 40인 성공은 로컬 기준으로 활용합니다. 기본 군중 제한 K가 60이므로, 필터 포화는 소수 인원에서 K를 낮춘 별도 대조로 검사하도록 했습니다. |
>
> **Sol의 첫 구현 단위는 월드 제작 누락 탐지와 필수 전투 경고 누락 탐지입니다.** 현재 코드는 각각 ‘MainWorld의 지정 태그가 있는 액터’와 ‘이미 경고 Notify가 있는 애니메이션’에서 검사를 시작합니다. 작업자가 설정 자체를 빠뜨리면 검사 대상에서도 사라질 수 있어, 두 부분을 먼저 고치는 편이 이후 콘텐츠 전체에 효과가 큽니다.
>
> 설계서 [7절의 검증 기준](C:/Users/I/Documents/GitHub/Project_J/Docs/Architecture/Planning/Project_J_OpenWorld_MMO_System_Design_2026-11-26.md:104)과 [8절의 Sol 구현 순서](C:/Users/I/Documents/GitHub/Project_J/Docs/Architecture/Planning/Project_J_OpenWorld_MMO_System_Design_2026-11-26.md:120)에 단계별 완료·롤백 조건을 담았습니다. 출시 최소 사양, 목표 해상도·FPS, 최고 이동 속도, 공격별 최소 반응 시간은 제품 결정으로 열어 두었습니다. 새 지상 탈것의 연속 주행과 제작 B01의 다인 교전은 구현 후 실제로 검증해야 할 항목입니다.

## 12. 설계 재검토

**사용자 요청**

> 좋다. 설계문서 잘 작성했네 더 놓친 부분이나 더 고려해야 할 점이나 기타 등등 더 고려해서 점검해줘. 혹은 추가/수정하면 좋은 아키텍처 등등 좋은 구조로.

**당시 완료 응답**

> [오픈월드·MMORPG 시스템 설계서](C:/Users/I/Documents/GitHub/Project_J/Docs/Architecture/Planning/Project_J_OpenWorld_MMO_System_Design_2026-11-26.md)를 **설계안 2**로 갱신했습니다. 현재 소스와 대조하면서, 초안대로 구현하면 놓칠 수 있는 네 가지를 보강했습니다.
>
> | 분야 | 설계에 반영한 내용 |
> |---|---|
> | **원거리 숲·도시** | HLOD 프록시가 보여도 원본 숲까지 계속 메모리에 남으면 최적화 효과가 작습니다. 원본이 실제로 내려가는지, 재진입하면 돌아오는지 확인하도록 했습니다. 성문 개폐나 축제처럼 월드 상태가 바뀔 때는 근경과 원경이 같은 상태를 보여야 합니다. |
> | **지상 탈것·포털·부활** | 초안에서 함께 다룬 같은 월드 포털, 던전 입장, 부활은 실제 승인 주체가 달랐습니다. 각 시스템이 기존 권한과 취소 처리를 유지하도록 수정했습니다. 빠른 탈것으로 다리를 건널 때는 클라이언트가 다리를 그린 시점과 서버에서 실제로 밟을 수 있는 시점을 함께 확인합니다. |
> | **새 스킬·필수 경고** | NPC는 기본 공격을 다른 공격 정의로 교체할 수 있고, 일부 투사체는 공격 데이터 에셋을 거치지 않고 생성됩니다. 실제로 선택된 스킬까지 검사를 따라가도록 했습니다. 투사체 풀이 가득 찼을 때 비용·쿨다운·경고만 남는 경우도 검증 대상에 넣었습니다. |
> | **군중·서버 부하** | Iris의 군중 한도는 관찰자마다 처리하는 비면제 후보 액터 수에 적용됩니다. 따라서 40인 시험에서도 NPC 수와 서버 부하 단계에 따라 한도에 걸릴 수 있습니다. 같은 인원이 보스 앞에 모일 때와 여러 지역에 흩어질 때를 비교해 전송량·후보 탐색 비용·서버 상주 메모리를 각각 보도록 했습니다. |
>
> 특히 **개별 기능은 통과하지만 한 장면에서 함께 실패하는 경우**를 추가했습니다. 파티가 포털과 지상 탈것으로 동시에 정착지에 도착해 첫 스킬을 쓰는 장면이 대표적입니다. 여기서는 지면 준비, 첫 프레임의 멈춤, 필수 경고를 실제로 읽을 수 있었던 시간을 함께 봅니다. [복합 상황 인수 기준](C:/Users/I/Documents/GitHub/Project_J/Docs/Architecture/Planning/Project_J_OpenWorld_MMO_System_Design_2026-11-26.md:130)
>
> **구현 우선순위는 월드 제작 누락 탐지와 필수 경고 누락 탐지를 먼저 두는 것이 여전히 적절합니다.** 둘 다 기존 제작 경로에서 해결할 수 있고 이후 추가되는 콘텐츠에도 적용됩니다. 이번 점검에서는 설계서만 수정했고 로컬 근거 링크 34개를 확인했습니다. 추가한 시나리오의 실제 플레이·성능 결과는 Sol 구현 단계에서 검증할 항목입니다.

## 13. 핵심 기반 구현

**사용자 요청**

> 좋다. 그러면 저 문서 기반 + 너의 추가적인 고려 사항 + 더 좋은 구조 + 놓친 부분 등 더 고려해야 할점들 더 고려해서 기반 구조, 아키텍처 등 구현하자. 내가 말한 예시에만 국한하지말고 더 고려해서 좋은 구조로 해줘. 자료/정보 더 필요하면 찾아주고.
> 아키텍처와 핵심 설계, 기반 코드 구현해줘 작업하면서 지금 현재 아키텍처 적으로 수정해야 할 부분/수정하면 좋은 부분/추가하면 좋은 부분 등 더 고려하면서 좋은 구조로 해줘. 세부적인 것들은 더 고려해서 인수인계 문서남겨서 진행하자. . 더 고려해야 할 점들 더 고려해서 좋은 구조로 해줘. 내가 말하지 않은 것들/우리가 놓친 것들도 더 포함해서 고려하면서 좋은 구조로 해줘.
> (아직 프로젝트 초반, 1\~2인 개발로 시작하고 있으니 유로 서비스나 뭐 돈 드는건 하지말고 - 이건 이미 알고있지?)

**당시 완료 응답**

> 설계안을 바탕으로 **새 월드·탈것·공격이 기존 최적화 경로를 재사용하고, 제작 누락을 발견할 수 있는 기반 코드**를 구현했습니다. 기존 언리얼 기능과 Project J의 실행 경로를 확장했고, 유료 서비스나 추가 서버는 사용하지 않았습니다.
>
> | 영역 | 구현한 내용 |
> |---|---|
> | **월드·원거리 표현** | 메시마다 원거리 수관·랜드마크·근경 전용 의도를 지정할 수 있습니다. 다른 WP 맵에서도 월드 기본 HLOD 레이어를 상속하고, 원거리 필수 메시의 HLOD 참여를 끄면 검사가 실패합니다. 새 지역의 월드 정의 등록 범위도 `/Game/World`로 넓혔습니다. |
> | **빠른 지상·비행 이동** | 이동 중인 Pawn이 현재 공중 주행 여부를 알려 주는 공통 인터페이스를 추가했습니다. 지상은 기존 속도 기반 XY 예측을 사용하고, 기존 두 공중 탈것 부모와 새 공중 이동 Pawn은 같은 3차원 예측 경로를 사용할 수 있습니다. |
> | **전투 경고** | 공격 정의에서 필수 몽타주 경고를 지정하면, Notify 전체 누락·중복·잘못된 실행 조건·첫 근접 타격보다 늦은 시작을 검출합니다. 실제 B01 보스의 클리브 공격 에셋에도 적용했습니다. |
> | **군중·서버** | 기존 Iris 메트릭에서 면제된 관찰자–액터 쌍과, 거리 안에 있지만 인원 한도로 제외된 쌍을 구분할 수 있습니다. 한 대의 PC에서도 군중이 사라진 원인을 좁힐 수 있습니다. |
>
> **검증 결과:** UE 5.8.4 Win64 에디터·게임 빌드가 모두 성공했고, 집중 회귀 **12개가 통과**했습니다. MainWorld와 이름이 다른 임시 WP 맵에서 원경 설정 상속과 누락 검출을 확인했습니다. 실제 MainWorld B01의 D3D12 단독 플레이에서는 경고 생성·해제, 공격 10회로 처치, 개인 우편 **1,625골드** 지급까지 확인했습니다.
>
> 이번 결과가 입증하는 범위는 기반 연결과 위 장면까지입니다. 새 지상 탈것의 장거리 패키지 주행과 연속 팝인, 방향·범위까지 전달하는 공간 경고, 변경 후 40인 교전 성능은 후속 검증으로 남겼습니다. 새 콘텐츠를 연결하는 방법과 다음 구현의 인수 기준은 [기반 구현 인계 문서](C:/Users/I/Documents/GitHub/Project_J/Docs/Architecture/Planning/Project_J_OpenWorld_Foundation_Handoff_2026-11-26.md)에 정리했습니다.

## 14. 설계안과 기반 대조

**사용자 요청**

> 좋다. 그러면 이번에 구현한 기반 코드랑 인계 문서를 설계안하고 다시 한번 대조해서 점검해줘. 새 월드·탈것·스킬을 추가할 때도 범용적으로 적용되는지, 누락이나 하드코딩, 기존 시스템과 중복된 부분은 없는지 봐줘. 기반에서 고칠 건 지금 수정·검증하고, 세부 구현과 남은 과제는 인계 문서에 반영해줘. 더 고려해야 할 점들 더 고려해서 좋은 구조로 해줘. 내가 말하지 않은 것들/우리가 놓친 것들도 더 포함해서 고려하면서 좋은 구조로 해줘.

**당시 완료 응답**

> 설계안과 실제 호출 경로를 대조해 **새 콘텐츠를 추가할 때 검사가 빠지거나 이전 상태가 남을 수 있는 부분**을 수정했습니다. 새 월드·탈것·스킬은 기존 UE 기능과 프로젝트 공용 코드를 통해 확장할 수 있습니다. 제작자가 연결해야 하는 데이터와 실제 플레이로 확인해야 하는 품질 기준도 인계 문서에 구분해 두었습니다. [갱신한 기반 구현 인계 문서](C:/Users/I/Documents/GitHub/Project_J/Docs/Architecture/Planning/Project_J_OpenWorld_Foundation_Handoff_2026-11-26.md)
>
> | 영역 | 수정하고 확인한 내용 |
> |---|---|
> | **새 월드·원거리 나무** | 지정한 콘텐츠 폴더에서 저장된 지도를 먼저 발견할 수 있게 했습니다. 야외로 지정한 맵은 실제 WP 스트리밍 활성 여부, 상시 로드되는 WorldState, 등록된 월드 정의의 일치까지 검사합니다. 수관과 랜드마크는 실제 배치 검사를 통해 레이어 상속·누락·잘못된 빌더·근경 예외를 확인하고, 시험에만 있던 중복 판정을 제거했습니다. |
> | **새 탈것·고속 이동** | 기본 컨트롤러에서 월드 정의가 없거나 활성화 제공자를 껐을 때 선행 로드만 계속되는 문제를 수정했습니다. **탈것 전용 코드가 없는 일반 Character**도 속도만으로 고속 지상 예측을 사용했습니다. 같은 프레임에 비행 탈것에서 지상 Pawn으로 바꿔도 이전 고도·방향·후방 앵커·준비도 캐시가 섞이지 않았습니다. |
> | **새 스킬·전투 경고** | 몽타주에서 마지막 필수 경고를 지워도 해당 몽타주를 참조하는 공격 정의를 따라 누락을 검출합니다. 같은 몽타주를 재사용하는 플레이어 콤보가 이미 지나간 경고 뒤에서 계속 재생되는 문제도 수정했습니다. 기존 B01과 별도의 플레이어 공격 표본에서 확인했습니다. |
>
> **검증 결과는 UE 5.8.4 Win64 Development 에디터·게임 빌드 성공, D3D12 집중 회귀 14/14 통과**입니다. 실패·경고 동반 성공·미실행은 모두 0건입니다. 제작 MainWorld의 B01 독립 PIE에서도 경고 표식 생성·해제, 공격 입력 10회, HP 100→0, 개인 보상 우편 1,881골드를 확인했습니다. [자동화 결과](C:/Users/I/Documents/GitHub/Project_J/Saved/Validation/OpenWorldFoundation_Reaudit_20261126/Verified/Automation/index.json) · [B01 실행 결과](C:/Users/I/Documents/GitHub/Project_J/Saved/Validation/OpenWorldFoundation_Reaudit_20261126/B01PIE/verification.json)
>
> **남은 최우선 검증은 두 번째 지역을 패키징해 빠른 지상 탈것으로 실제 주행하는 것입니다.** 현재 기반은 선언된 수관의 HLOD 설정 누락을 잡지만, 역할 표시가 없는 임의의 수동 메시를 나무로 식별하거나 Cook된 프록시가 원하는 프레임에 그려졌음을 보장하지는 않습니다. 인계 문서에 원경 전환·다리 충돌·급선회·재진입의 판정 기준을 남겼습니다. 독립 장판·투사체의 경고 수명, 비용 있는 콤보의 GAS 확정 시점, Niagara·애니메이션·서버 병목도 후속 과제로 정리했습니다. 이번에는 한 대에서 40인 부하를 다시 측정하지 않았습니다.

## 15. 세부 구현 인계

**사용자 요청**

> 확장성도 다 고려한거고 예외처리나 뭐 기존 아키텍처듀 수정하는 등 기타 기반 설계 등 구현된거지? 이후 sol로 세부 구현 및 테스트 하면 되지?
> 다시 한 번 전체적으로 점검 후 이후 세부 구현 문서도 추가/갱신해줘. 내가 말한 것에만 국한하지말고 너가 더 고려해야 할 점들 더 고려해서 좋은 구조로해줘. 놓친부분/말하지 않은 부분들 더 고려해서 좋은 구조로해줘.

**당시 완료 응답**

> 네. **GPT-6.2 Sol이 기존 기반에서 세부 구현·디버깅·테스트를 이어갈 수 있는 상태입니다.** 설계안과 소스를 다시 대조해 보니, 월드 제작 검사, 지상·비행 스트리밍 예측, GAS 공격 경고, Iris 군중 집계는 기존 아키텍처의 실제 실행 경로에 연결돼 있습니다.
>
> 다만 새 콘텐츠에는 제작자가 선언해야 하는 의도가 있습니다. 예를 들어 새 지상 탈것은 Pawn 속도를 제공하면 공용 예측을 사용하지만, 수관의 원경 역할과 스킬의 필수 경고는 제작 데이터에 지정해야 합니다. 이번 점검에서는 특히 다음 경계를 확인했습니다.
>
> - **월드** — 저장 맵 검사는 WorldState 중복을 검출하지만 런타임은 첫 유효 배치를 사용합니다. 새 배포 맵을 검사 대상에 넣고 일반 입구에서 실제로 들어가 보는 절차가 중요합니다.
> - **탈것·원경** — 이동 예측은 범용이지만 기존 비행 측정기는 MainWorld와 시작 탈것을 직접 지정합니다. 두 번째 맵과 지상 탈것 검증에는 시험 입력도 바꿔야 합니다. WPO 거리 정책의 편집값과 실제 검사 상한이 따로 있다는 점도 기록했습니다.
> - **전투·군중** — 플레이어 콤보는 경고가 잘못된 노드를 거절하기 전에 비용이 확정될 수 있습니다. 독립 투사체·장판의 경고 수명과 기본 캐릭터를 상속하지 않는 액터의 Iris 등록도 해당 경로에서 검증해야 합니다.
>
> **첫 작업은 작은 두 번째 야외 지역을 패키지의 일반 입구로 열고, 빠른 지상 탈것으로 다리·급선회·재진입을 시험하는 것**으로 정리했습니다. 소스 셀이 내려간 뒤 원경이 실제로 보이는 프레임과 지면 충돌이 준비된 시각을 따로 확인하면, 문제가 생겼을 때 HLOD 제작·Cook·스트리밍·렌더 중 어디를 고칠지 좁힐 수 있습니다. 이후 신규 공격 한 개의 공간 경고와 비용을 검증하고, 단일 PC 군중 시험에서 병목이 확인된 항목을 최적화하는 순서입니다. [Sol 세부 구현·시험 문서](C:/Users/I/Documents/GitHub/Project_J/Docs/Architecture/Planning/Project_J_OpenWorld_Sol_Implementation.md)
>
> 이번 작업에서는 문서를 추가·갱신하고 코드 및 보관 원자료를 재대조했습니다. **D3D12 회귀 14/14와 B01 경고 생성·해제는 직전 실행 기록**이며, 이번에 새 빌드나 40인 부하 시험을 실행한 결과는 아닙니다. [갱신한 기반 인계 문서](C:/Users/I/Documents/GitHub/Project_J/Docs/Architecture/Planning/Project_J_OpenWorld_Foundation_Handoff_2026-11-26.md)

## 16. 두 번째 지역 구현·시험

**사용자 요청**

> /C:/Users/I/Documents/GitHub/Project_J/Docs/Architecture/Planning/Project_J_OpenWorld_Sol_Implementation.md 좋다, 그러면 이 세부 분석 이제 설계와 기본 구현과 어느 정도 핵심 설계, 기반 마련했으니까 여기에 있는 마크다운 문서 읽고 작업 진행하자. 또한/C:/Users/I/Documents/GitHub/Project_J/Docs/Architecture/Planning/Project_J_OpenWorld_Foundation_Handoff_2026-11-26.md 이 경로에 있는 마크다운 문서도 읽고 작업 진행하자. 작업 진행하기 전에 고려해야 할 점들 더 고려하고 그리고 또 필요한 문서나 자료 정보들도 필요하면 찾아서 해줘. 그리고 내가 말하지 않은 것들이나 놓친 것들도 포함해서 더 고려해서 좋은 구조로 해줘

**당시 완료 응답**

> 두 인계 문서를 바탕으로 **두 번째 World Partition 맵에서 원경 HLOD, 고속 지상·비행, 패키지 입장까지 구현하고 검증했습니다.** 검증용 `RidgeRoute03`은 프로젝트의 기존 게임 모드·탈것·스트리밍 경로를 사용합니다. [세부 구현과 검증 기록](C:/Users/I/Documents/GitHub/Project_J/Docs/Architecture/Planning/Project_J_OpenWorld_Sol_Implementation.md)
>
> 새 맵을 제작하면서 **HLOD 설정 누락을 실제로 재현했습니다.** 에디터에서 수관 레이어가 지정돼 있어도 그 레이어가 런타임 파티션에 등록되지 않으면, UE 빌더가 종료 코드 0으로 프록시를 0개 만들 수 있었습니다. 에디터 검사가 엔진의 등록 여부를 확인하도록 보강했고, 저장 맵 검사에는 예상 프록시 수를 확인하는 옵션을 추가했습니다. [에디터 검사 코드](C:/Users/I/Documents/GitHub/Project_J/Source/Project_JCharacterEditor/Private/Validation/Project_JCrossAssetEditorValidator.cpp)
>
> | 검증 | 결과 |
> |---|---|
> | **원거리 나무** | HLOD 프록시 2개 생성. D3D12 PIE에서 약 420m 떨어져 원본 수관이 언로드된 뒤에도 프록시가 화면에 남았고, 가까이 돌아오자 원본이 다시 렌더됐습니다. |
> | **고속 지상·비행** | 지상은 약 26m/s로 좁은 다리를 왕복하고 180도 선회했습니다. 최종 약 559m 주행에서 노면 트레이스 누락은 0프레임이었고, 엔진 로그에서 `Loaded`와 `Activated` 요청을 모두 확인했습니다. 같은 맵에서 약 579m 비행하며 상승·선회·급강하도 검증했습니다. |
> | **패키지·회귀** | Win64 Development Cook·Stage 성공. 패키지의 Entry에서 `open` 명령으로 새 맵에 들어가 정상 종료했습니다. 직접 에디터 빌드와 집중 회귀 14개도 통과했습니다. |
>
> 이번 원경과 주행 관찰은 PIE에서 했고, 패키지에서는 개발 콘솔을 통한 맵 입장을 확인했습니다. **실제 게임 입구에서의 이동, 패키지 안에서의 연속 주행과 팝인 측정, 신규 스킬의 공간 경고·비용, 16·40인 혼잡 시험**은 남은 작업입니다. 재현 입력과 판정 기준을 인계 문서에 갱신했습니다. [기반 구현 인계](C:/Users/I/Documents/GitHub/Project_J/Docs/Architecture/Planning/Project_J_OpenWorld_Foundation_Handoff_2026-11-26.md)

## 17. 두 인계 문서의 진행 범위

**사용자 요청**

> 저 내가 준 인수인계 문서 저 두 개 전부 다 한 거 맞아?

**당시 완료 응답**

> **두 문서 모두 읽고 작업에 반영했습니다. 문서에 적힌 전체 후속 과제는 아직 남아 있습니다.** 이번에 완료한 범위는 두 번째 월드의 적용과 검증입니다.
>
> - **완료** — 검증용 WP 맵 제작, HLOD 레이어 등록 누락 검사 보강, 원본이 언로드된 뒤 원거리 수관 프록시가 표시되는지 확인, 고속 지상·비행 PIE 시험, Cook·Stage와 패키지에서 개발 콘솔로 맵 입장, 집중 회귀 14개 통과.
> - **남음** — 실제 게임 입구를 통한 지역 이동, 패키지에서 연속 주행하며 팝인과 충돌 준비 시점 측정, 새 탈것 BP 적용 검증, 신규 스킬의 공간 경고·비용 처리, 16·40인 혼잡 시험과 병목별 최적화.
>
> 이전 답변에서 완료라고 한 범위는 **두 번째 월드 검증 단계**입니다. 각 과제의 결과와 남은 조건은 [Sol 세부 구현 문서](C:/Users/I/Documents/GitHub/Project_J/Docs/Architecture/Planning/Project_J_OpenWorld_Sol_Implementation.md)와 [기반 인계 문서](C:/Users/I/Documents/GitHub/Project_J/Docs/Architecture/Planning/Project_J_OpenWorld_Foundation_Handoff_2026-11-26.md)에 반영했습니다.

## 18. 패키지 용량과 남은 작업

**사용자 요청**

> 패키지 한다고 하면은 용량 좀 많이 증가하려나? 그러니까 저 남은 것들만 하면 되는 거지? 아 이미 좀 기반이 좀 잘 돼 있어서 별로 없는 건가? 뭐지?

**당시 완료 응답**

> **지금 남은 작업을 위해 작은 검증용 패키지를 한 번 더 만드는 정도라면, 이번 실측 기준으로 용량 부담은 크지 않습니다.** C: 여유 공간은 약 **155GiB**이고, `RidgeRoute03`을 포함한 Win64 Development 패키지는 **1.27GiB**였습니다. 그중 약 **488MiB는 디버그 심볼**입니다. 별도 Cook 폴더는 **0.41GiB**였고, 공유 Zen 캐시와 임시 작업 공간까지 포함한 최대 사용량은 따로 측정하지 않았습니다.
>
> 검증 맵의 원본 에셋은 약 **4MiB**였습니다. 패키지 전체 크기에는 실행 파일, 공용 에셋, 셰이더와 플러그인도 들어갑니다. MainWorld와 여러 지역을 포함한 실제 배포판의 크기는 그 구성으로 Cook해야 알 수 있습니다. 동일한 시험을 여러 번 할 때는 맵·콘텐츠·실행 파일이 그대로라면 기존 패키지를 재사용할 수 있습니다.
>
> **두 인계 문서의 이번 작업 범위는 남은 항목을 이어서 하면 됩니다.** 기반이 갖춰져 있어서 새 스트리머나 별도 군중 시스템부터 만들 필요가 없습니다. 남은 일은 기존 경로를 **실제 콘텐츠에 연결하고, 플레이와 부하에서 드러난 부분을 수정하는 것**입니다.
>
> 1. **월드·탈것** — 실제 게임 입구로 두 번째 지역에 들어가고, 패키지에서 새 탈것으로 연속 주행하며 원경 전환과 충돌 준비를 측정합니다.
> 2. **전투** — 새 공격 하나로 공간 경고의 표시·취소·재진입과 GAS 비용 처리를 검증합니다. 이 부분은 실제 세부 구현이 남아 있습니다.
> 3. **군중** — 한 PC에서 16인부터 확인하고 여유가 있으면 40 경량 연결로 올립니다. 측정에서 확인된 병목부터 수정합니다.
>
> 즉, 목록이 짧아진 이유는 **공용 기반을 이미 연결했고 두 번째 맵에서 실제 동작도 확인했기 때문**입니다. 남은 과제는 항목 수는 적어도 각각 패키지·전투·다인 환경에서 확인해야 할 범위가 있습니다. [세부 구현과 시험 기준](C:/Users/I/Documents/GitHub/Project_J/Docs/Architecture/Planning/Project_J_OpenWorld_Sol_Implementation.md)

## 19. 일반 입구와 전투·부하 검증

**사용자 요청**

> 그러면 남은 작업들 전부 해줘, 남김없이. 고려해야 할 점인데도 더 고려하고 그리고 내가 말하지 않은 것이나 놓친 것들 있는지 더 점검이나 고려하면서 더 좋은 구조로 해줘, 알겠지? 이해했지? 전부 다 하는 거야, 전부

**당시 완료 응답**

> 현재 프로젝트에서 구현하고 검증할 수 있는 후속 작업을 마쳤습니다. **새 지역·탈것·스킬은 기존 UE 기능과 프로젝트 시스템에 연결하는 방식으로 구현했습니다.** 인계 문서 두 개에 실제 결과와 남은 검증을 반영했습니다.
>
> - **월드·탈것** — 최종 Win64 Development 패키지에서 일반 포털 상호작용으로 두 번째 WP 지역에 들어갔습니다. 새 고속 지상 탈것으로 다리를 왕복하는 동안 노면 누락은 **5,971샘플 중 0건**이었습니다. 같은 패키지에서 비행 탈것의 상승·급강하·225도 선회와 원거리 HLOD 프록시, 복귀 시 원본 복구도 확인했습니다.
> - **전투·연출** — 첫 유효 콤보 노드에서만 서버가 GAS 비용을 확정하도록 수정했습니다. 데이터로 설정하는 원형 공격은 경고와 피해 판정이 위치·반경·시간을 공유합니다. 원격 D3D12 화면에서 경고 원을 확인했고, 자연 만료와 서버 취소 때 각각 사라졌습니다. Niagara 풀·애니메이션 예산·던전 복귀를 포함한 **D3D12 회귀 27개가 통과**했습니다. 이 중 1개에서 PoseSearch 인덱스 준비 경고가 있었습니다.
> - **단일 PC 다인 시험** — 렌더 관찰자 1대를 포함한 **16/16**, 경량 **40/40**에서 타격·체력 수신·정상 종료를 확인했습니다. 40인 기본 시험의 교전 서버 월드 틱 p95는 **8.14ms**였습니다. 군중 한도 시험도 40/40을 통과했고, 파티 면제를 유지하면서 한도 초과 복제 쌍 720개를 제외했습니다. 제작 MainWorld의 B01은 독립 경량 클라이언트 **2/2**와 일반 플레이의 처치·보상 우편을 각각 확인했습니다.
> - **패키지 용량** — 최종 검증 패키지는 **1.27GiB**로, 이전 같은 지역 검증 패키지보다 **약 0.51MiB** 늘었습니다.
>
> **아직 확인이 필요한 범위도 있습니다.** 원경 전환에서 원본과 프록시 모두 최근 렌더를 보고하지 않은 3샘플이 반복됐습니다. HLOD 준비 시간을 늘린 비교에서도 같았으므로, 실제 화면에서 팝인이 없는지는 프레임 캡처로 판별해야 합니다. 제작 MainWorld의 16·40인 실측은 이 PC의 메모리 한계로 완료하지 못했습니다. 온라인 지역 이동·영속 데이터 이관과 신규 원형 공격의 출시용 스킬 부여·늦은 진입 검증도 인계에 남겼습니다.
>
> [Sol 구현·검증 인계](C:/Users/I/Documents/GitHub/Project_J/Docs/Architecture/Planning/Project_J_OpenWorld_Sol_Implementation.md) · [기반 인계](C:/Users/I/Documents/GitHub/Project_J/Docs/Architecture/Planning/Project_J_OpenWorld_Foundation_Handoff_2026-11-26.md) · [최종 패키지 실행 결과](C:/Users/I/Documents/GitHub/Project_J/Saved/Validation/OpenWorldCompletion/PackagedFinal/result.json)

## 20. 두 인계 문서 재감사

**사용자 요청**

> 결론적으로 말하자면 일단은 저 "오픈월드 솔 임플리멘테이션 마크다운" 문서랑 "오픈월드 파운데이션 핸드오프" 저 두 문서, 진짜 저 마크다운 문서에 있는 내용 전부 다 한 거 맞지? 저 내가 너한테 준 인수인계 문서랑 저 구현 문서 전부 다 해야 돼. 한 거 맞지? 다시 한번 점검 후 다 했는지 해주고 그리고 전체적으로 버그가 있는지 실제로 잘 적용되고 있는지도 확인해줘. 알겠지? 전부 다 해야 돼. 빠진 게 있는지 이런 것도 다 체크하고 다 해야 돼. 더 고려해야 할 사항들은 더 고려해서 좋은 구조로 해주고 내가 말하지 않은 것들이나 놓친 것들도 있는지 더 확인해서 좋은 구조로 해줘.

**당시 완료 응답**

> 두 인계 문서를 항목별로 다시 대조했습니다. **현재 기반에서 재현된 결함은 수정했고, 제작·운영 단계에서 검증할 항목은 남아 있습니다.** 두 문서에 항목별 구현 상태와 실행 증거를 갱신했습니다.
>
> - **월드·포털** — 없거나 지도 타입이 아닌 목적지에 상호작용할 수 있던 버그를 수정했습니다. MainWorld와 검증 지역의 저장 데이터를 다시 열어 확인했고, 최종 패키지에서 일반 포털 입장과 고속 지상·비행 주행을 통과했습니다. 지상 노면 누락은 **6,116샘플 중 0건**이었습니다.
> - **원거리 HLOD** — 최근 렌더 이력이 비었던 두 순간을 실제 게임 화면으로 캡처했습니다. **두 화면 모두 수관이 보였습니다.** 수관 WPO 거리 상한도 공통 값으로 통합했습니다.
> - **전투·연출** — 캐릭터 연결이 바뀐 뒤 이전 원형 경고가 남던 버그를 수정했습니다. 사망·연결 변경·캐릭터 종료 시 경고와 타이머가 정리되는지 확인했습니다. 원격 경고도 재검증했고, Niagara 풀·애니메이션·이관을 포함한 **D3D12 회귀 36개가 통과**했습니다. 1개 시험에는 PoseSearch 인덱스 준비 경고가 있었습니다.
> - **다인·서버** — 최신 **40인 경량 군중 시험은 40/40**을 통과했습니다. 제작 B01에서는 최초 2인 시험에서 한 타격이 측정창을 넘겼고, 상세 로그를 켠 두 반복은 모두 2/2를 통과했습니다. 경량 클라이언트만 60FPS로 제한한 두 반복에서는 서버 호출부터 피해까지 최대 **약 603ms**로 재현됐습니다. 이 설정은 단일 PC 시험 조건이며, 운영 환경에서 지연이 생기는 원인은 추가 확인이 필요합니다.
>
> **남은 항목**은 제작 MainWorld의 16·40인 실측, 출시 맵의 여러 바이옴에서 원경 전환 품질 확인, 신규 원형 스킬의 실제 캐릭터 부여·비용·늦은 진입 검증, 온라인 지역 이동과 영속 배치 이관입니다. 제작 맵 16인 시험은 독립 클라이언트 방식의 메모리 사전 검사에서 차단됐습니다.
>
> [Sol 구현·재감사 결과](C:/Users/I/Documents/GitHub/Project_J/Docs/Architecture/Planning/Project_J_OpenWorld_Sol_Implementation.md) · [기반 인계 문서](C:/Users/I/Documents/GitHub/Project_J/Docs/Architecture/Planning/Project_J_OpenWorld_Foundation_Handoff_2026-11-26.md) · [최종 패키지 실행 증거](C:/Users/I/Documents/GitHub/Project_J/Saved/Validation/OpenWorldReaudit/PackagedAudit/result.json)

## 21. 독립 검토 인계

**사용자 요청**

> 이렇게 전체적으로 점검하고 뭐 버그나 기타 뭐 구현 안 하는 것들 있는지 더 체크해서 구현 안 한 거 없으면은 더 한 거지. 그러면은 이거를 다시 아스트라 모델로 검증할 거거든. 그래서 일단은 내가 너한테 전달한 두 개 인수인계 문서 있지? 그거와 그거의 경로와 너가 구현하면서 추가, 갱신한 문서 경로도 포함하는 등 아스트라 모델로 돌렸을 때 이해할 수 있도록 어떤 걸 이해해야 하는지, 뭘 검증해야 되는지, 부족한 게 있는지, 뭐 구현 문서, 검증 문서 등 그런 거 점검하기 위한 문서 하나 작성해줘. 이해했지? 내가 말한 예시에만 국한하지말고 전체적으로 점검도 해야 해. 더 고려해야 할 점들 더 고려하고 내가 말하지 않은 것들이나 놓친 것들 등 더 포함해서, 더 고려해서 좋은 구조로 해줘.

**당시 완료 응답**

> [Astra 독립 검증 인계 문서](C:/Users/I/Documents/GitHub/Project_J/Docs/Architecture/Planning/Project_J_OpenWorld_Astra_Independent_Review_Handoff_2026-11-26.md)를 저장했습니다.
>
> 두 원본 인계 문서와 갱신한 구현·설계 문서의 경로, 핵심 코드, 재현 명령, 패키지·회귀·다인 시험 증거를 한곳에 모았습니다. **GPT‑6.2 Astra는 독립 검증과 설계 판단을 맡고, GPT‑6.2 Sol은 후속 구현·디버깅·회귀를 맡도록** 명시했습니다. 기존 설계보다 나은 대안이 있다면 책임, 호환성, 제작 비용을 비교해 판단할 수 있게 했습니다.
>
> 제작 MainWorld의 16·40인 실측, 출시 맵의 원경 품질, 신규 스킬의 실제 부여·비용·늦은 진입, 온라인 지역 이동과 영속 이관은 남은 검증으로 기록했습니다. 혼합 목적지 포털의 초기 등록처럼 소스에서 발견한 추가 확인 후보도 넣었습니다.
>
> 파일 링크 **63개**의 접근 가능 여부와 보관 증거 **34개**의 SHA-256 일치를 확인했습니다.

## 22. 독립 검증과 잔여 항목

**사용자 요청**

> /C:/Users/I/Documents/GitHub/Project_J/Docs/Architecture/Planning/Project_J_OpenWorld_Astra_Independent_Review_Handoff_2026-11-26.md 이제 어 우리가 원래 아스트라 모델로 설계한 기본 구현하고 이제 솔 모델로 구현했잖아. 세부 구현과 뭐 테스트 검증 및 버그 수정. 이런 걸 했어. 그래서 이제 관련 구현 문서, 또 검증 문서, 테스트 결과 이런 문서들도 생겼어. 그래서 이제 아스트라 모델로 검증을 하려고 해. 그래서 처음에 우리가 검증하기 위해서 인수인계 문서 두 개 했잖아. 그것과 이제 구현과 검증, 또는 테스트 이러면서 이제 구현하면서 나온 새로운 문서들을 바탕으로 점검 및 검증해줘. 버그나 이런 것 있으면 수정하고. 또 작업하면서 수정하면 좋은 점들, 추가하면 좋은 것들, 그런 것들 있으면 더 고려하면서 좋은 구조로 해줘. 내가 말한 예시에만 국한하지 말고 전체적으로 점검해줘. 또한 내가 말하지 않은 것들, 놓친 것들도 포함해서 더 좋은 구조로 고려하면서 작업해줘. 이해했지?

> 그리고 또한 지금 보니까 남은 항목이 있더라고. 메인 월드에 열여섯 명에서 마흔 명인 7층. 그리고 여러 바이옴에서 원경 전환 품질 확인. 그리고 신규 원형 스킬의 실제 캐릭터 부여. 비용 늦은 진입 검증. 그리고 온라인 지역 이동과 영속 배치 이관. 이렇다는데 이것도 한번 점검해서 작업해줄 수 있어? 그리고 또한 이거 말고도 남은 것들이 있으면 전부 다 해줘, 전부 다. 하면서 이제 검증도 하면서 뭐 여러 고려해야 할 점들이 있으면 또 고려하고 필요한 자료나 정보 있으면 찾아서 적용 같은 것도 좋게 해줘도 돼. 내가 말한 것들만 극한하지 말고, 내가 말한 것들에만 국한하지 말고 더 고려해야 할 점 고려하면서 해주고 말하지 않은 것들, 놓친 것들도 포함해서 더 좋은 구조로 고려하면서 해줘

**당시 완료 응답**

> 원형 스킬의 실제 부여·비용·늦은 진입, 세 바이옴의 원경 전환, 로컬 온라인 지역 이동과 영속 배치 이관을 검증했습니다. **제작 MainWorld의 16·40인 동시 전투는 아직 미실측입니다.** 현재 PC에서 확인한 범위는 제작 MainWorld 2접속과 별도 전투 맵 40접속입니다.
>
> | 항목 | 확인한 결과 |
> |---|---|
> | **MainWorld와 40인 전투** | 제작 MainWorld B01에서 독립 클라이언트 2명이 새 스킬로 2/2 타격했습니다. 별도 전투 맵에서는 40명이 동시에 시전해 **40/40 타격, 총 400 피해, 40/40 체력 복제 확인·연결 해제**를 기록했습니다. 이때 서버 월드 틱 p95는 **5.873ms**였습니다. MainWorld 16·40인 실행은 가용 메모리 약 19GiB에서 사전 검사로 중단됐습니다. 실행기의 예정량은 각각 74GiB와 170GiB입니다. [제작 B01](</C:/Users/I/Documents/GitHub/Project_J/Saved/Validation/Continuation_20261014/AstraMainGroundPulse2/verification.json>) · [40접속 실측](</C:/Users/I/Documents/GitHub/Project_J/Saved/Validation/Continuation_20261014/AstraGroundPulseCrowd40/verification.json>) |
> | **원경·고속 이동** | MainWorld 초지·산지·호수 계곡에서 800m 이탈·복귀한 15개 장면을 비교했습니다. 산지에서는 멀어지면서 수목 밀도가 낮아졌고 능선은 유지됐습니다. 최종 패키지의 별도 지상·비행 경로에서는 **노면 5,937샘플 중 누락 0건**이었습니다. 원본과 HLOD의 최근 렌더 이력이 동시에 비었던 두 시점에도 캡처에는 수관이 보였습니다. [바이옴 비교 화면](</C:/Users/I/Documents/GitHub/Project_J/Saved/Validation/OpenWorldAstraReview/BiomeViews/contact.jpg>) · [최종 패키지 실행](</C:/Users/I/Documents/GitHub/Project_J/Saved/Validation/OpenWorldAstraReview/FinalPackaged/result.json>) |
> | **제작 스킬·원격 경고** | 패키지의 일반 MainWorld에서 Spellblade가 `Ground Pulse`를 실제로 부여받았습니다. 입력은 **CapsLock을 누른 채 T**, 비용은 마나 10, 쿨다운은 4초입니다. 마나 부족·취소·재시전·서버 피해를 확인했습니다. 4접속 최저 이펙트 품질에서 늦게 들어온 클라이언트는 남은 시간만큼 경고를 보았고, 만료 후 진입한 경우에는 경고가 뜨지 않았습니다. [원격 경고 검증](</C:/Users/I/Documents/GitHub/Project_J/Saved/Validation/Continuation_20261014/AstraSlopeLate4/verification.json>) |
> | **온라인 이동·영속 배치** | 기존 로컬 Nakama·PostgreSQL과 UE 서버로 제작 샌드박스→던전→원래 위치 복귀를 확인했습니다. 별도 3서버 시험에서는 던전에서 연결을 끊고 새 클라이언트로 재접속한 뒤 원래 지역의 다른 서버로 복귀했습니다. 보스 배치의 맵·ID 변경을 위한 오프라인 이관 도구도 추가했고, **대기 중인 보상·퀘스트 201건**과 처치 기록·쿨다운 만료 시각이 보존됐습니다. [실제 백엔드 왕복](</C:/Users/I/Documents/GitHub/Project_J/Saved/Validation/OpenWorldAstraReview/NativeBackend.json>) · [재접속](</C:/Users/I/Documents/GitHub/Project_J/Saved/Validation/Continuation_20261014/AstraDistinctMapsReconnect/verification.json>) · [이관 검증](</C:/Users/I/Documents/GitHub/Project_J/Saved/Validation/OpenWorldAstraReview/Migration.json>) |
>
> 검증 중 **혼합 목적지 포털이 정상 던전 정의를 덮어쓰는 문제**, **원형 전조가 렌더 경계에서 잘리는 문제**, **경사면에서 전조 한쪽이 지면에 묻히는 문제**를 재현해 수정했습니다. 경사 수정은 공통 원형 공격 경로에 적용했습니다. 18도 경사에서 수평 피해 반경 350cm를 유지했고, 늦게 진입한 원격 클라이언트도 같은 기울기로 표시했습니다.
>
> 최종 Win64 Development Editor·Game 빌드, 선택 에셋 9개·선언 입력 47개 검증, 네 지도 Cook·Stage와 패키지 실행을 마쳤습니다. **네이티브 회귀는 51건 통과, 실패·미실행 0건**입니다. 애니메이션 관찰 시험 1건에는 PoseSearch 인덱스 준비 경고가 있었습니다.
>
> 남은 출시 검증은 **실제 MainWorld 16·40인 밀집·분산 전투**, **최소 사양에서 바이옴 전환 중의 연속 프레임과 끊김**, **절벽·계단에 걸친 전조**, **전용 HUD·사운드**, **두 공개 야외 지역의 운영 라우팅과 Shipping 구성**입니다. 두 원래 인계 문서에도 최신 결과를 연결했고, 항목별 근거와 남은 합격 조건은 [Astra 독립 검증 인계 문서](</C:/Users/I/Documents/GitHub/Project_J/Docs/Architecture/Planning/Project_J_OpenWorld_Astra_Independent_Review_Handoff_2026-11-26.md>)에 정리했습니다.

## 23. 패키지·원경·실제 서비스 검증

**사용자 요청**

> 다시 한번 점검해줘. 점검하고 실제로 적용되고 있는지 또는 이제 작업이 안 된 게 있는지, 남은 것들이 있는지, 구현 안 된 게 있는지 점검해주고, 안 된 것들이 있으면 너가 마무리해줘, 전부 다. 그리고 뭐 바이옴 온라인 이관 이런 것처럼 지금 로컬의 하나의 PC로 할 수 없는 것들도 있을 거야, 아마.(바이옴 온라인 이관은 됐나..? 내가 잘 몰라서..) 그런 것들은 안 해도 돼. 나중에 하면 되고. 그리고 전체적으로 다 점검 후에 좀 부족한 구조나 좀 더 좋은 구조로 할 수 있는 구조가 보일 수도 있어. 그러면은 그때 필요한 자료 정보, 오픈소스나 논문 이런 걸 찾아, 또는 게임 회사의 구현 방식 같은 걸 공개한 자료, 이런 여러 다방면의 자료 찾아서 좋은 구조로 구현하고. 고려해야 할 점들 고려해서 적용도 해줘. 테스트도 해주고. 내가 말한 예시들에만 국한하지 말고 너가 더 고려해야 할 점들 더 고려해서 좋은 구조로 해주고, 그리고 내가 말하지 않은 것들, 놓친 부분도 있을 거야. 그런 것들도 너가 더 고려해서 좋은 구조로 해줘

**당시 완료 응답**

> 한 PC에서 메인 월드, 전투, 온라인 지역 이동, 영속 데이터 이관, Shipping 패키지까지 다시 검증했습니다. **메인 월드의 원경 프록시 참조 오류 2개와 Shipping 빌드 오류를 수정했고, 관련 자동화 테스트는 52건 모두 통과했습니다.** 애니메이션 관찰자 테스트 1건에는 PoseSearch 인덱스 준비 경고가 남았습니다. [전체 검증·인계 문서](C:/Users/I/Documents/GitHub/Project_J/Docs/Architecture/Planning/Project_J_OpenWorld_Astra_Independent_Review_Handoff_2026-11-26.md)
>
> | 영역 | 실제로 확인한 결과 |
> |---|---|
> | **원거리 나무·바이옴** | 메인 월드에서 존재하지 않는 셀을 가리켜 엔진이 숨기던 HLOD 2개를 찾았습니다. 셀 구성을 갱신하고 프록시 666개를 재생성했습니다. 초지·산지·호수 계곡의 800m 왕복을 두 번 실행한 결과, 30개 관측점 모두 오래된 참조가 0개였습니다. [이동 중·정착 화면 27장](C:/Users/I/Documents/GitHub/Project_J/Saved/Validation/OpenWorldLocalReview/FreshMotion/Contact.jpg) |
> | **새 월드로의 확장** | 별도 월드에 나무 3개가 든 하위 레벨을 배치했습니다. 가까이서는 원본 3개, 중간 거리에서는 인스턴스 프록시, 800m에서는 병합 프록시로 수관이 보였고 복귀하면 원본 3개가 다시 표시됐습니다. 새 월드도 HLOD 생성이 필요하며, 이제 원경 검사에서 오래된 셀 참조와 예상 프록시 누락을 잡습니다. [중첩 레벨 검증](C:/Users/I/Documents/GitHub/Project_J/Saved/Validation/OpenWorldLocalReview/NestedCapture/transitions.json) |
> | **스킬·HUD·전투 경고** | 기존 퀵슬롯 순서와 저장 배치를 보존하면서 직업별 스킬을 추가하도록 수정했습니다. 제작 Spellblade의 키 조합과 HUD 슬롯으로 지면 파동을 각각 사용해 마나 **100→90→80**, 아이콘과 효과음 재생을 확인했습니다. 계단·절벽 표시를 회귀했고, 저옵션 4접속에서는 늦게 들어온 유효 경고만 남은 시간 동안 표시됐습니다. [실제 HUD](C:/Users/I/Documents/GitHub/Project_J/Saved/Validation/OpenWorldLocalReview/InputHUD/result.png) |
> | **온라인 지역 이동·영속 이관** | **한 PC에서도 실제 지역 서버 왕복이 됐습니다.** UE 서버 2개와 Nakama·PostgreSQL을 띄워 같은 클라이언트가 샌드박스→릿지→샌드박스로 이동했습니다. 매번 캐릭터 제어권을 받았고 서버 세션은 **1→2→3**으로 전진했습니다. 던전 완료 후 원위치 복귀도 통과했습니다. 별도 보스 배치 DB 이관에서는 보상·퀘스트 201행 보존과 잘못된 이관 5종 거절을 확인했습니다. [온라인 왕복](C:/Users/I/Documents/GitHub/Project_J/Saved/Validation/OpenWorldLocalReview/NativeBackend2.json) · [DB 이관](C:/Users/I/Documents/GitHub/Project_J/Saved/Validation/OpenWorldLocalReview/Migration.json) |
> | **다인 전투·배포** | 제작 메인 월드 B01은 독립 클라이언트 **2/2**, 격리 전투 맵은 **40/40**이 타격·체력 복제·연결 해제까지 통과했습니다. Shipping 패키지는 약 **2.78GiB**였고, 실제 컨테이너에서 메인 월드와 스킬 리소스 포함, 검증 전용 월드 제외를 확인했습니다. 같은 Shipping 실행 파일을 두 번 시작해 게임 창 응답과 정상 종료도 확인했습니다. [40접속 결과](C:/Users/I/Documents/GitHub/Project_J/Saved/Validation/Continuation_20261014/LocalWorldCrowd40/verification.json) · [패키지 구성](C:/Users/I/Documents/GitHub/Project_J/Saved/Validation/OpenWorldLocalReview/ShippingClosure.json) |
>
> **아직 남은 성능 문제는 초지의 순간 지연입니다.** 이동 중 캡처를 끈 에디터 시험에서 초지 프레임 간격은 p95 **21.81ms**, 최대 **89.55ms**였습니다. 반복해서 지목된 셀에는 지형 렌더 컴포넌트 4개와 충돌 컴포넌트 4개가 있었습니다. 등록 묶음을 10에서 2로 줄여도 p95가 개선되지 않아 기본값 10을 유지했습니다. 실제 패키지에서 해당 셀을 로드한 시험은 시작 직후를 포함해 p95 **14.03ms**, 최대 **125.59ms**였습니다. 두 시험은 이동 방식이 다르며 최대 지연의 원인은 아직 확정되지 않았습니다. [바이옴별 측정](C:/Users/I/Documents/GitHub/Project_J/Saved/Validation/OpenWorldLocalReview/MotionSummary.json) · [패키지 실측](C:/Users/I/Documents/GitHub/Project_J/Saved/Validation/OpenWorldLocalReview/CookedMeadow.json)
>
> **추후 인수 항목**은 제작 메인 월드의 독립 클라이언트 **16·40인 실측**, 출시 최소 사양에서의 팝인·프레임 판정, 실제 출시 지역의 출입·정원·복귀 정책입니다. 현재 PC에서는 메인 월드 16·40인 시험이 메모리 사전 검사에서 보류됐습니다. 또한 Shipping의 기본 시작 맵은 현재 `Lvl_ThirdPerson`입니다. 메인 월드는 패키지에 포함됐고, 메인 월드를 지정한 Development 패키지에서 실행까지 확인했습니다. [메인 월드 인원별 메모리 근거](C:/Users/I/Documents/GitHub/Project_J/Saved/Validation/OpenWorldAstraReview/MainWorldCapacity.json)

## 24. 예외·병렬 처리·에셋 검사

**사용자 요청**

> 어 점검 끝났어? 다시 한번 버그나 그런 거 있는지 전체적으로 점검해줘. 알겠지? 뭐 구현 안 한 거 있는지, 또는 이제 우리가 애초에 설계에서 부족한 게 있는지. 개선 안된것들도 개선하고(남아있는 문제/사안들 등이 없도록) 그리고 또 뭐 예외 처리나 그런 것도 잘하고 있는지. 또 뭐 멀티스레딩이나 병렬 처리도 이렇게 포함되고 또 미래를 위한 확장성이 좋은지, 기타 등등 고려해야 할 정도가 더 고려해서 해주고 그리고 내가 말하지 않은 것들. 그리고 놓친 것들도 포함해서 또 고려해줘. 알지? 그리고 뭐 예외 처리 검증 뭐 그런 도구도 있는 건가? 예를 들어서 뭐 우리가 뭐 맵에 HLOD를 배치해야 하는데 그런 거 안 돼 있거나, 뭐 어떤 것들이 연결 안 돼 있어서 어떤 기능이 안 된다든가. 약간 이런 것들도 뭐 간단하게 있나? 체크만 할 수 있는 것들? 너무 무겁지 않으면서 체크할 수 있는 용도. 이런 거 있으면 좋아? 아니면 오히려 안 좋을 수도 있잖아, 그치? 이런 것들도 한번 더 봐줘, 이런 것들. 너가 더 고려해서 좋은 구조 있으면 적용하고. 오히려 안 좋으면 하지 말고 어 꼭 H LOD에만 국한하지 말고 전체적으로 뭐 이런 것들을 넣으면 좋은 것들 있는지 전체적으로 봐줘. 실제로도 뭐 적용되고 있는지 이런 거 점검 도구도 있으면 좋은데 너무 무겁진 않게. 실용성도 좋고. 더 고려해줘. 필요 없으면 하지 말고. 아니 뭐 자동적으로 되는 구조면은 불필요하잖아. 그러니까 너가 더 고려해서 내가 놓친 것들이나 말하지 않은 것들도 더 포함해서 좋은 구조로 봐줘. 봐주고 작업 이어서 해줘

**당시 완료 응답**

> 이번 로컬 재점검은 끝났습니다. **월드가 바뀔 때 투사체 상태가 섞이는 버그와 애니메이션 검사가 에디터를 종료시키는 버그를 수정했습니다.** 새 콘텐츠의 연결 누락은 기존 언리얼 선택 검증에서 잡도록 보강했습니다.
>
> | 영역 | 수정하고 확인한 내용 |
> |---|---|
> | **전투·병렬 처리** | 다른 월드에서 생성한 투사체 1발이 현재 월드의 80발에 섞여 81발이 되는 문제를 재현했습니다. 이제 생성과 다단 적중 재사용 대기시간 등록에서 월드 소유권을 확인합니다. 적중 콜백 도중 월드가 종료되면 남은 콜백도 중단합니다. 80발 병렬 탄도 계산, 동기 충돌, 비동기 충돌, 취소와 종료를 시험했습니다. |
> | **스킬·HUD·NPC** | 새 직업·전직·장비·전투 스타일에 원형 스킬을 넣고 기본 공격 정의를 빠뜨리면 제작 단계에서 알려줍니다. GAS가 공격 정의를 직접 전달하는 방식도 지원합니다. HUD는 중복 ID, 빈 프로필, 같은 직업의 입력 충돌, 잘못 연결된 게이지를 잡습니다. NPC 설정은 무한대나 유효하지 않은 추격·귀환 수치도 검사합니다. |
> | **원거리 나무·HLOD** | 메인 월드의 저장 프록시 **666개**와 월드 정의 연결을 확인했습니다. 초지·산지·호숫가 왕복 **9,787회**, 별도 중첩 레벨 **3,360회** 관측에서 엔진이 오래됐다고 판정한 HLOD는 **0개**였습니다. 중첩 레벨에서는 같은 경로에서 근경 나무 원본 3개가 중간 거리 프록시 1개로 바뀌고 복귀 시 원본 3개가 다시 표시됐습니다. [초지 원경 화면](/C:/Users/I/Documents/GitHub/Project_J/Saved/Validation/OpenWorldLocalReview/ExtensionField/Main/meadow_eye-far.png) |
> | **애니메이션** | 아직 시작되지 않은 PoseSearch 인덱스를 검사가 동기적으로 기다리면서 에디터가 종료됐습니다. 엔진이 비동기 빌드를 마친 뒤 포즈를 읽도록 수정했습니다. 전환 테이블 **40개·활성 행 312개**를 확인했고, 새 에디터에서 관련 검사만 반복해 **3/3, 경고 0건**이었습니다. |
>
> **평소에는 변경한 BP·데이터 에셋을 선택하고 `Asset Actions → Validate Assets`를 실행하면 됩니다.** 원경 배치를 바꿨다면 해당 맵을 열고 변경한 액터에서 `Validate`를 실행하세요. 생성된 프록시의 존재는 저장 월드 검사로, 이동 중 전환은 왕복 검사로 확인할 수 있습니다. 이동 중 추가한 검사는 촬영할 때만 엔진의 기존 카운터를 읽으므로 플레이 중 매 프레임 월드 전체를 훑는 비용이 없습니다. [프로젝트별 검사 절차와 인계 문서](/C:/Users/I/Documents/GitHub/Project_J/Docs/Architecture/Planning/Project_J_OpenWorld_Astra_Independent_Review_Handoff_2026-11-26.md) · [Epic 선택 에셋 검증 API](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/DataValidation/IDataValidationModule/ValidateAssets)
>
> **최종 결과는 Editor·Development·Shipping 빌드 성공, 자동화 66건 통과, 저장 에셋 15/15 통과**입니다. 자동화 중 1건에서 캐릭터 초기화 시 PoseSearch 준비 전 검색 두 번을 건너뛴 경고가 기록됐습니다. 실패·미실행은 0건입니다. [자동화 결과](/C:/Users/I/Documents/GitHub/Project_J/Saved/Validation/OpenWorldLocalReview/ExtensionAuditClosure/Automation/index.json) · [에셋 검증 로그](/C:/Users/I/Documents/GitHub/Project_J/Saved/Validation/OpenWorldLocalReview/ExtensionAuditClosure/Selected.log)
>
> **성능 판단이 남은 부분은 있습니다.** 기존 Development 패키지를 두 번 새로 실행해 기동과 지역 이동을 분리했을 때, 여섯 번의 이동 직후 총 360프레임에서 최대 **28.51ms**, 33.33ms 초과는 **0회**였습니다. 반면 에디터 초지 연속 이동에서는 최대 **87.43ms** 간격이 남았습니다. 이번 결과로 전역 스트리밍 설정을 바꾸지는 않았습니다. 제작 메인 월드의 16~40인 실접속, 최소 사양 장시간 주행, 이번 코드로 새로 Cook한 패키지 실측은 여전히 별도 검증 대상입니다. [패키지 측정 1](/C:/Users/I/Documents/GitHub/Project_J/Saved/Validation/OpenWorldLocalReview/ExtensionField/Trip1/windows.json) · [패키지 측정 2](/C:/Users/I/Documents/GitHub/Project_J/Saved/Validation/OpenWorldLocalReview/ExtensionField/Trip2/windows.json)

## 25. 메인 월드 교체 토의

**사용자 요청**

> 그리고 어 궁금한 게 있는데 이거 메인월드 레벨에서 작업했잖아. 근데 이걸 아예 레벨 디자이너분이 아예 갈아엎고 다른 맵으로도 만들 수 있거든? 아예 다 삭제하고? 아니면 애초에 그냥 다른 레벨을 만들어서 시작하거나? 이럴 때는 어떻게 해야 돼? 그냥 답변만 해줘. 그런 범용 구조도 뭐 적용된 건가? 모르겠네. 뭐 메인 월드라는 이름을 가진 지금 이 레벨에만 적합이 된 건지, 뭐 어떻게 아니면 또 다른 레벨 만들었을 때 그냥 뭐 HLOD만 배치해서 적용하면 되는 건지, 이걸 잘 모르겠어서 일단은 토이 해보자. 토의

**당시 완료 응답**

> **레벨 디자이너가 `L_MainWorld`를 전면 재제작하거나, 다른 이름의 맵을 새 메인 월드로 만들어도 됩니다.** 월드 스트리밍, 고속 지상·비행 이동, 전투·VFX·애니메이션 예산의 공통 코드는 재사용할 수 있습니다. 실제로 `RidgeRoute03`이라는 별도 맵에서 지상 주행·비행·원경 전환을 검증했고, 중첩 레벨로 만든 다른 맵에서도 나무 원본과 원경 프록시가 전환되는 것을 확인했습니다. [별도 맵 검증 기록](/C:/Users/I/Documents/GitHub/Project_J/Docs/Architecture/Planning/Project_J_OpenWorld_Sol_Implementation.md) · [중첩 레벨 검증 결과](/C:/Users/I/Documents/GitHub/Project_J/Saved/Validation/OpenWorldLocalReview/ExtensionField/Nested/transitions.json)
>
> 다만 **새 맵에 HLOD만 배치하면 완성되는 구조는 아닙니다.** HLOD는 멀리 있는 나무·건물·산을 어떻게 보여줄지 담당합니다. 플레이어가 어디서 시작하고, 빠르게 이동할 때 어느 범위를 미리 준비하고, 포털이 어디로 연결되는지는 다른 설정이 담당합니다.
>
> | 디자이너가 하는 작업 | 다시 챙길 부분 |
> |---|---|
> | **기존 맵에서 지형·숲·도시를 전부 새로 제작** | 기존 `WorldState`와 월드 정의 연결을 유지합니다. 전부 삭제했다면 `WorldState` 하나를 항상 로드되게 복원하고 정의를 다시 연결합니다. 새 지형에 맞춰 시작점·포털 도착점·보스 배치·내비게이션을 점검하고 HLOD를 다시 빌드합니다. |
> | **완전히 다른 맵을 새 메인 월드로 제작** | 스트리밍할 야외 맵이면 World Partition 스트리밍을 켜고, 그 지역에 맞는 월드 정의를 `WorldState`에 연결합니다. 프로젝트의 플레이어 컨트롤러가 사용되는지 확인하고, 원경이 필요한 콘텐츠에 HLOD 레이어를 지정하거나 월드 기본값을 상속시킨 뒤 프록시를 빌드합니다. 출시할 때는 패키징 목록과 실제 입장·포털 경로도 새 맵으로 연결합니다. |
>
> 나무마다 디자이너가 원경용 나무를 하나씩 수동 배치할 필요는 없습니다. **나무 메시와 배치가 원경 생성에 참여하도록 설정하고, 해당 맵에서 프록시를 생성**하는 방식입니다. 반대로 실내 던전이나 가까이서만 보일 소품까지 일괄적으로 HLOD를 요구할 이유도 없습니다. [프로젝트 제작 검사 절차](/C:/Users/I/Documents/GitHub/Project_J/Docs/Architecture/Planning/Project_J_OpenWorld_Astra_Independent_Review_Handoff_2026-11-26.md) · [Epic의 World Partition HLOD 설명](https://dev.epicgames.com/documentation/unreal-engine/world-partition-hierarchical-level-of-detail-in-unreal-engine)
>
> **현재 맵 이름에 묶인 예외도 실제로 있습니다.** 패키징 목록에는 `L_MainWorld` 경로가 명시돼 있고, 패키지의 D3D12 환경에서 지형 셀이 다시 보일 때 렌더링 준비를 보완하는 코드도 그 경로를 직접 비교합니다. 새 이름의 맵에서는 이 보완이 자동 적용되지 않습니다. 또 현재 설정상 게임과 에디터의 기본 시작 맵은 `Lvl_ThirdPerson`입니다. 새 맵을 만들었다고 접속 목적지까지 자동으로 바뀌지는 않습니다. [지형 렌더링 보완 코드](/C:/Users/I/Documents/GitHub/Project_J/Source/Project_J/Project_J.cpp:24) · [패키징 목록](/C:/Users/I/Documents/GitHub/Project_J/Config/DefaultGame.ini:115) · [시작 맵 설정](/C:/Users/I/Documents/GitHub/Project_J/Config/DefaultEngine.ini:31)
>
> 제 의견은, **디자이너가 크게 갈아엎을 계획이면 새 맵에서 자유롭게 제작하고, 준비됐을 때 메인 월드 역할을 넘기는 방식**이 지금 프로젝트에 잘 맞습니다. 기존 맵을 비교 기준으로 남겨 둔 채 새 맵에서 입장 → 빠른 지상 이동 → 비행 → 원경 왕복 → 포털 복귀를 확인할 수 있습니다. 이미 저장 캐릭터나 영속 배치가 생겼다면 기존 위치를 새 지형의 안전한 지점으로 옮기는 것도 이 전환에 포함하면 됩니다. 즉, 디자이너에게 필요한 것은 기존 맵의 지형이나 이름을 보존하는 일이 아니라 **새 맵을 플레이 가능한 지역으로 연결하는 짧고 명확한 제작 절차**입니다.

## 26. 문서 갱신 범위와 표현 정정

**사용자 요청**

> 좋다, 그러면은 어 이제 문서 갱신하자. 문서 갱신해야 할 것들 더 고려해서 하고 더 고려해야 할 점들은 너가 더 고려해서 좋은 구조로 해줘. 내가 말하지 않았던 것들이나 놓친 것들도 다 해줘, 문서 갱신. 알겠지? 뭐 저렇게 디자이너, 레벨 디자이너를 위한 문서라든가 기타 등등

> 랩은 디자이너를 위한 문서만 등장하면 안 돼? 우리가 지금까지 전부 이 채팅창에서 작업한 것들 모든 내용을 담아야 돼, 알겠지? 이미 그렇게 하고 있는 거지? 더 고려해야 할 점들도 고려해서 문서 갱신에서 빠지는 내용 있으면 안 돼.

> "랩은" == "레벨"이야. 오타야 저거 알지?
