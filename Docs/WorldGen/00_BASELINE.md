# v4 월드 제작 기준점 — 2026-10-18

기획 기준은 [v4 실행 지시서](../World/Project_J_8x8km_WorldGen_Completion_Expansion_v4.md)와 [목표 탑뷰](../World/초반%20지역%208×8km%20판타지%20월드맵.png)다. 최신 작업 상태는 [인수인계](../World/WORLD_HANDOFF_2026-10-18.md)를 따른다. 아래 내용은 초기 기반 조사이며 현재 아트·성능 합격을 뜻하지 않는다.

- 작업 브랜치 `main`, HEAD `a9df4f66952171bbbca516fb7d924db18dc020a3`. 미커밋 Source/Config/에셋과 신규 World 파일이 있다. reset/discard/commit/push하지 않았다.
- 설치 엔진 5.8.3, CL 58210709. 현재 native 엔진: `5.8.3-58210709+++UE5+Release-5.8`.
- 맵 `/Game/World/Starter/Maps/L_StarterRegion.L_StarterRegion`, 기존 DA `/Game/World/Starter/Data/DA_StarterRegion.DA_StarterRegion`. 런타임 해시 `WorldPartitionRuntimeHashSet`, Landscape 컴포넌트 1024개, 4033²/2m/8,064m.
- 보존 높이 SHA-256 `bc3a7957a44629beb807d4a453443e9d02e48981be0cfb93d5054432c077f8c8`. 현 기준 고도 -76.5~1400.75m. 목표 이미지의 600~800m 표기를 맞추려고 기존 산을 낮추지 않는다.
- Native DA 장소 18개와 마커를 재조회했다. 20개 목표와의 대응은 [CSV](01_EXISTING_TO_TARGET_MAPPING.csv)다. S/H/B 역할·기획 번호와 기존 안정 ID를 구분한다.
- Streaming Enable은 이전 저장/구현 기록이 있으나 이번 Python의 protected 필드 재조회는 차단됐다. 현재 값은 아직 독립 재확인하지 못했다. 값을 쓰는 setter로 검사를 대체하지 않는다.
- 현 Native 읽기 감사에서 dirty map은 없다. 원자료 `Saved/Validation/StarterWorld/Phase2/v4-native-baseline.json`.

보존/복구: 높이/Source와 이전 맵·External Actors 복구 사본은 `Saved/StarterWorld/BeforeCorridor`, `BeforeWorldBiome` 등 기존 단계 사본을 유지한다. 사본마다 보존 범위가 다르므로 현 월드 전체의 완전한 스냅샷이라고 주장하지 않는다. 신규 단계는 새 태그/정확한 Actor manifest로 분리하고 기존 출력의 무조건 재생성을 금지한다. 수정할 기존 패키지는 먼저 별도 보관한다. 현 높이 원본/공급자 원본/C++ 게임플레이는 변경 금지이며, 필요 시 비파괴 Edit Layer만 별도 검증한다.

도구: 공식 MCP의 Scene/Actor/Asset/Material/PCG/PIE/Capture와 native Editor Python이 실제 호출됐다. 전용 Landscape toolset은 현재 목록에 없다. GeometryScript와 기존 Editor C++ 도구를 재사용한다. C++는 Editor 등 관련 프로세스가 실행 중인 현재 새 빌드를 시작하지 않는다. 패키지/전역 Nav/HLOD 생성·확장 연결은 미검증이다.
