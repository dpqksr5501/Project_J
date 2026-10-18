# 월드 작업 인수인계 — 2026-10-18

**환경 아트 제작은 중단 상태이며, 이후 사용자 요청으로 메인 월드 이름 전환과 환경 에셋 통합 작업을 시작했다. 이 전환은 아직 미완이다. 원거리 숲과 자연스러운 산, 도시·마을의 최종 품질도 미완이다.** 이 문서가 최신 재개 기준이다. 이전의 장문 세션 기록과 WorldQuality 문서는 커밋 전 정리하면서 여기에 통합했다.

## 후속 전환 상태와 디자이너 인계

[레벨 디자이너 가이드](LEVEL_DESIGNER_GUIDE.md)에 현재 맵 선택·C++ 역할·편집 주의사항·미해결 문제를 정리했다. 아래 본문의 Starter 경로·수치는 이전 제작 기준이며 새 맵 전환 완료를 뜻하지 않는다.

- 목표 맵 `/Game/World/MainWorld/Maps/L_MainWorld`는 WP 전용 엔진 복제 도구로 저장했다. 복구 후 로그는0 error/4 warning이며 이전·신규 외부 액터 패키지가 각각2,717개다. 관련 비맵 에셋45개의 이름 변경 영수증은 SAVED다. 새 맵 재로드·전체 참조·수목 보존·PIE 검증은 남아 있다.
- Source·도구는 아직 Starter 경로이며 DefaultEngine의 기본 맵은 ThirdPerson, DefaultGame의 WorldDefinition 검색/Cook은 Starter를 가리킨다. 사용자 삭제 승인된 `/Game/Level/Main_World`, `/Game/Level/Main_World_WP`와 이전 제작 맵도 남아 있다. 충돌 없이 전체 이행 완료로 보고하지 않는다.
- 이전 수목 cull 보정 스크립트의 출력 경로 오류로 외부 액터 `C/LL/S6WI6M7W1QA7CE59LOV348.uasset`에 JSON이 기록된 것을 발견해 유효 백업으로 복원했다. JSON은 실제 forest-streaming Source 경로로 분리했다. 당시 Content 패키지 헤더 검사에서 추가 비정상 파일은 없었다. 복구 액터의 변경 전 cull을0/0으로 다시 맞추고 새 맵의 수량·변환을 확인하는 작업이 남아 있다. 전체82,758그루가 새 맵에서 검증됐다고 주장하지 않는다.
- 에셋 통합 최신 범위는 사용자 지정4개 프로젝트의 환경 제작 자산·필수 의존성이다. GASP도 환경 에셋만 후보이며 캐릭터·애니메이션·효과/Niagara·샘플 UI/입력/GameMode·데모 맵은 제외한다. 아래의 과거 GASP 전면 제외 조건은 이 요청으로 대체됐다. 전체 반입·Map_Resource 통합·중복 분류는 아직 미완이다.
- 이번 디자이너 문서 작성은 소스·Config·manifest·저장 영수증 대조다. 원본 R16 해시는 기존값과 일치했다. 새 에디터/PIE·Cook·성능 시험은 실행하지 않았다.

## 기준과 보존 대상

- 현행 맵: `/Game/World/Starter/Maps/L_StarterRegion`. 약8×8km(8,064m), 4033² 높이 표본/간격2m. 원본 `Content/World/Starter/Source/HM_StarterRegion.r16` SHA256은 `bc3a7957a44629beb807d4a453443e9d02e48981be0cfb93d5054432c077f8c8`이다. 이번 비주얼 복구에서 높이를 수정하지 않았다.
- [v5 후속 지시서](Project_J_World_Visual_Quality_Recovery_Codex_v5.md), [v4 공간·확장 기획](Project_J_8x8km_WorldGen_Completion_Expansion_v4.md), [목표 월드맵](초반%20지역%208×8km%20판타지%20월드맵.png)을 참고한다. 사용자 기획문서·레퍼런스는 정리 대상에서 제외했다.
- 안정 장소20곳, 기존 C++ 시스템과 월드 정의, 기존 PCG 나무69,692그루를 유지한다. [ID 대응표](../WorldGen/01_EXISTING_TO_TARGET_MAPPING.csv), [월드 구조](../WorldGen/05_WORLD_ARCHITECTURE.md), [공간 기능 연결](../WorldGen/07_GAMEPLAY_SPATIAL_INTEGRATION.md)이 기존 계약이다. 첫 공간 배치20/20은 최종 아트·게임플레이·성능 합격을 뜻하지 않는다.
- 정착지9·사냥터9·보스2의 Source 배치는 `Content/World/Starter/Source/StarterWorld.locations.json`, `settlements.json`, `hunting.json`, `bosses.json`과 Native 저장본을 기준으로 한다. v4 기획 ID와 StableID를 임의로 통일하지 않는다.
- 에셋은 사용자 제공 Unreal Projects 라이브러리에서 채택하며 GASP는 제외한다. 필요하면 비5.8 에셋을 별도5.8 `assets` 프로젝트에서 조회한다. 공급자 원본과 실제 월드 참조 에셋을 미참조 검색만으로 삭제하지 않는다.

## 저장된 현재 상태

- 수도는 기존33필지·6도로에 주택10채·기초·계단과 약312m 가로를 추가한43필지·7도로 상태다. 거대한 성곽 대도시를 제작 완료한 상태가 아니다. 기존 마을의 배치 어색함도 남아 있다.
- 기존 PCG 숲170타일의 높이 경계를 지형 범위로 줄이고 Nature 루트를 실제 배치 높이로 옮겼다. 기존 인스턴스의 월드 변환·수량을 보존했다. PCG는 수동 GenerateOnDemand 제작 흐름이며 자동 에디터 재생성은 꺼져 있다.
- Nature 최초 추가 나무2,701그루 중 Spruce2,097그루를 PN full01/03으로 교체했다. 프로젝트 소유 잎·원경 MI4개로 조색했고 공급자 원본은 유지했다. 이후 북동부 Source `[4992,5120,7552,7552]m`에10,365그루를 추가했다. 현재 Nature 나무13,066그루, PCG와 합계82,758그루다. 개별 장소의 수동 나무는 이 합계 밖이다. 추가한 작은 바위61개는 유지했고 시각적으로 실패한 반복 암벽은 제거했다.
- 이 식재는 북동부 샘플 보강이다. 사용자가 보고 있던 앞쪽 산을 포함해 전체 산림이 처리된 것은 아니다. 수목이 점처럼 흩어진 곳, 비어 있는 중턱, 둥글고 단순한 산 윤곽이 남아 있다.
- `M_StarterTerrain_Biome`의 지표 표현, 암석 거리별 샘플링과 법선, 풀·양치류 팔레트를 수정했다. 마지막 수정은 고도620m부터 높아지던 암석 비중을 조정하고, 기존 Placement 마스크 밖의 산림 가능 완경사에도 개선 표현을 적용했다. 원본 높이와 PCG 생성 마스크는 바꾸지 않았다. Grass 중원경 전환은 아직 품질 미달이다.
- Grass variety 거리 범위는180~300m로 조정했다. `Config/DefaultScalability.ini`에는 Low/Medium 밀도 바닥값과 텍스처 풀 단계값을 작성했다. 설정 파일의 재시작 적용은 미검증이다. 마지막 실제 에디터는 Epic3/ScreenPercentage100/StreamingPool2560이었다.

## 원거리 수목·HLOD의 실제 문제

`S00_Tree_25_28_34`는 별도 HLOD layer=None이고 최대 표시 거리0이다. 반면 숲 인스턴스에는4km component cull이 있어 전체 로드된 에디터 원경에서 차이가 생길 수 있었다. 마지막에 큰 나무82,758그루의 component cull을0/0으로 저장하고 기존 PCG descriptor3개도 맞췄다. Source `StarterWorld.forest-streaming.json`의 `tree_instance_cull_cm`을 제작 소비자들이 사용한다. Grass·관목 거리와 원본 WP 로딩768m는 유지했다. 실제 게임에서 원본 언로드와 HLOD 전환이 검증됐다는 뜻은 아니다.

월드는 `WorldPartitionRuntimeHashSet`/MainGrid/셀252m/원본 로딩768m다. 새 `/Game/World/Starter/HLOD/HLOD_StarterForest`를 액터에 지정했으나 처음에는 RuntimePartitions→HLODSetups→HLODLayers 등록이 빠졌다. 로그의 invalid HLOD layer 때문에 숲 입력이 제외됐으며, 첫512m 시험의 native_result=true/7개 빌드만으로 수목 프록시 성공을 주장하면 안 된다. 이후 기존 persistent HLOD 파티션의 허용 목록에 등록하고 맵을 저장했다. 새 유한거리 파티션은 만들지 않았다. HLODLayer에 기록된 legacy loading_range=5km는 HashSet의 실제 로딩 범위 근거가 아니다.

기존 템플릿 지형 HLOD64개와 일부 지역 수목 시험이 섞여 있다. 수목/재질 교체와 암벽 제거 이후 프록시가 모두 갱신된 상태도 아니다. 등록 수정 후의 native 프록시 빌드·PIE 전환·구역별 포함 여부는 미검증이다. 기존 프록시 개수나 메시 LOD 수만으로 해결 판정하지 않는다.

전체 영역 HLOD 시험은 MeshUtilities/Embree 메모리 고갈로 크래시했다. 당시 주 에디터 used virtual91.92GiB, available physical0.50GiB/virtual0.06GiB였고 Windows 이벤트2004로 확인했다. 마지막 저장에 도달하지 않았으며 HLOD 관련138패키지는 빌드 전 백업과 디스크 SHA가 동일했다. 전역 빌드를 다시 강행하지 않는다. 이후 등록 수정 시험도 physical/commit 각각8GiB 기준을 충족하지 않아 시작하지 않았다(마지막 physical11.18GiB/commit7.55GiB).

## 물·도시·검증의 남은 범위

- 물은 나중에 실제 Water 플러그인으로 구현하기로 사용자와 정리했다. SingleLayerWater 시험은 철회했고 기존 공유 재질을 복원했다. 복원 SHA256은 `0040f9a850f3c9ea8190f9b863f47f4c8b1a408ede3069475e9078ba37fa4229`이다. 현재 강·호수·바다의 임시 수면은 실제 물 완성이 아니다. 호안 후보도 기본 높이에 적용하지 않았다.
- 큰 성곽 도시, 도로 전면·마당·생활 기능을 갖춘 자연스러운 마을, 전역 산림·지표·산악 조형, 실제 물, 최종 성능은 미완이다. 도보 시험 통과를 이 품질의 대체 근거로 쓰지 않는다.
- 초기 수도 변경 때의 SP71 벤치마크는 후속 환경 변경과 조건이 달라 재사용할 수 없다. 마지막 환경 수정 후 성능 CSV·PIE 재검증은 하지 못했다. 재개 시 사용자 원경 카메라에서 실제 보이는 결과를 먼저 판정한다.
- 캡처 하네스는 백그라운드 CPU throttle을 촬영 중에만 해제하고 복원한다. unfocused 렌더 정지로 잘린 사진을 지형 손상으로 오판한 적이 있다. `CaptureViewport`는 Editor 화면이며 PIE 증거로 사용하지 않는다.

## 중단·저장·복구 상태

마지막 공식 MCP IsPIERunning=false, 촬영 세션 RESTORED였다. 사용자 승인으로 조회용 `assets` 에디터만 dirty0 확인 후 정상 종료했고 주 에디터를 임의 종료하지 않았다. 중단 후 제작을 재개하지 않았다.

마지막 native 센서의 dirty content는0이다. dirty map에는 직전 HLOD 시험의 saved_packages와 일치하는 외부 액터 패키지9개가 남았다. 저장 뒤에도 다시 dirty로 보였으며 종료/삭제 상태를 확정하지 못했다. 강제 clear·일괄 폐기는 하지 않았다. 재개 시 이9개부터 확인하고, 새 전체 HLOD 빌드로 덮지 않는다.

로컬 최소 증거는 `Saved/Validation/StarterWorld/VisualHarness/recovery08/`의 `user_far.png`, `montane.png`, 카메라·native/session JSON이다. **원경 사진에도 앞쪽 산은 크게 비어 있다.** 이 사진들은 시각 품질 합격 자료가 아니다. Saved는 Git 제외이므로 다른 PC 인계에 자동 포함되지 않는다. 이전 반복 사진·도구 스키마·원시 로그·일회성 실행 파일은 정리했고 해당 원자료를 다시 분석할 수는 없다.

핵심 영수증은 `Saved/Validation/StarterWorld/Structure/`의 `forest-registration.json`, `forest-hlod-pilot.json`, `world-hlod-refresh.json`, `world-hlod-crash-disk-check.json`, `montane-canopy-native.json`, `tree-wp-visibility.json`, `mountain-surface-transition.json`에 남겼다. 제작기가 입력·중복 실행 방지에 사용하는 기존 체크포인트도 유지한다. 복구가 필요한 `Saved/StarterWorld` 백업은 보존했다. 동일 경로·파일 SHA가 전부 일치한 `Before_forest-hlod-pilot` 사본만 정리했으며 대응 원본은 `Before_world-hlod-refresh`에 있다.

커밋에는 Source·Config·실제 에셋과 현재 제작 도구·기준 문서를 검토해서 포함한다. 이번 정리는 커밋·push를 실행한 것이 아니며, 다른 기능의 사용자 변경·C++·프로젝트 에셋을 지우거나 원복하지 않았다.

커밋 전 정리에서는 적용 완료한 보정·퇴역 Region A 제작기와 일회성 검증 도구를 제거하고, 중복 진단·제작·재개 문서4개를 최신 인수인계·상태표로 통합했다. Source 생성 이력에 적힌 과거 report 파일명 중 일부 원자료도 삭제했다. 그 파일명을 현재 검증 결과로 재사용하지 않는다. 과거 native survey에 의존하던 B01 난간 전용 일회성 검증기도 함께 퇴역했다. 현재 Source·Native·기본 저장본 검증 도구는 보존했으며 삭제된 세션의 정밀 재분석/그대로의 재실행은 불가능하다.
