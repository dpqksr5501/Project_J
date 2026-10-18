# 월드 작업 인수인계 — 2026-10-18

**환경 아트 제작은 중단 상태이며, 이후 사용자 요청으로 메인 월드 이름 전환과 환경 에셋 통합 작업을 시작했다. 메인 월드 전환의 최종 검증은 아직 미완이며, 환경 에셋 이관은 아래 2026-10-19 기록대로 완료했다. 원거리 숲과 자연스러운 산, 도시·마을의 최종 품질도 미완이다.** 이 문서가 최신 재개 기준이다. 이전의 장문 세션 기록과 WorldQuality 문서는 커밋 전 정리하면서 여기에 통합했다.

## 2026-10-19 환경 에셋 이관 재개

환경 이관 커밋 `f8b697ca`와 검증 인계 커밋 `92a14f2e`를 origin/main에 push했다. Git LFS 5,789개 업로드가 정상 종료했고 원격 SHA와 로컬 `92a14f2e`가 일치함을 별도로 확인했다. 기존 WorldAuthoringLibrary C++ 변경 2개와 Build-StarterMountainHLOD.py는 이번 커밋에서 제외해 작업 사본에 보존했다. 아래 기록은 이번 이관 검증 범위이며 모든 구형 저작 도구·월드 아트의 완성을 뜻하지 않는다.

사용자 요청으로 완료 상태를 다시 점검했다. StaticMeshActor 상속 조립 Blueprint 2개와 환경 구름 Blueprint·재질·볼륨 6개의 선택 누락을 보완했다. 성채 부품 `SM_CastleLargeHouseAssambled_Cube_023`의 빈 convex 충돌체는 원본 백업 후 엔진 Auto Convex로 재생성했다. 렌더 삼각형 36개와 bounds는 유지했고 새 프로세스에서 유효 hull 1개를 확인했다. 식생의 Voxelize 형상 보존을 위해 `r.Nanite.Foliage=1`을 프로젝트에 명시했다. UE5.8의 실험 기능이며 실제 렌더링·성능 평가는 별도로 남는다.

최종 새 에디터 commandlet에서 자산 7,556개 등록·클래스·필수 hard 참조, 기존 경고 식생 메시 19개와 수정 충돌체, 추가 Blueprint 3개, 공유 컬렉션 6개를 확인했다. 최종 실행 로그는 오류 0·경고 0이다. 신규 패키지 5,838개(제작 자산 5,282 + 원본 레벨/외부 액터 507 + 로컬 격리 자료 49)의 Unreal 헤더가 정상이며, 추가 이관의 원본·공유 파일 51개 SHA도 일치했다. 원본 디스크의 환경 조사 범위에서 레지스트리에 없는 비외부 패키지는 없었다. 이 검사는 전체 재질 셰이더·PIE·모든 자산의 실제 사용 기능 합격을 의미하지 않는다.

커밋 전 제작 패키지 5,789개(31,741,594,478 bytes)의 실제 SHA256과 스테이징된 LFS 포인터가 모두 일치하며 로컬 LFS 객체의 존재·크기도 확인했다. 환경 자산과 관련 설정·문서만 총 5,794개 파일을 스테이징했다. 공유 컬렉션 6개는 아래 엔진 공급자의 기존 자동 커밋에 들어 있다.

저장소 전체 트리의 `git lfs fsck --pointers`는 기존 일반 Git 바이너리 690개를 LFS 미전환으로 보고한다. 반입 전 원격 main과 새 커밋의 경로 목록이 동일해 이번 작업의 신규 정책 위반은 0개다. 기존 캐릭터·애니메이션 등과 과거 이력을 일괄 변환하지 않았다. `git lfs fsck --objects f8b697ca^..f8b697ca`는 `Git LFS fsck OK`로 통과했다. 위 신규 패키지 전수 SHA/포인터 검사와 저장소 전체의 과거 LFS 정책 문제를 구분한다.

전체 환경 메시 2,213개와 필수 의존성은 채택했다. 소리·효과가 결합된 횃불/벽 Blueprint와 원본부터 필수 파일이 없는 도로 재질 등은 안전한 조립 자산 범위에서 제외했으며, 해당 건축·소품 메시 자체는 누락하지 않았다. 구형 PCG 12개는 제작용 호환성 보관 자료이고 정상 재생성 도구로 사용하지 않는다. 범위 밖 49개는 로컬 Developers/IntakeHold에 보존하며 이번 커밋에는 포함하지 않는다.

공유 컬렉션의 엔진 API 저장이 Git 공급자를 통해 컬렉션만 포함한 로컬 커밋 15개를 자동 생성한 사실을 확인했다. 이전의 “커밋하지 않았다” 안내는 정정한다. 실제 자산·설정·인계는 사용자 요청에 따라 별도 한글 커밋으로 정리했다. 원격 조회를 막던 공백 포함 중복 ref 파일은 유효 커밋을 확인하고 Saved의 GitRefBackup으로 원형 보관했으며, 유효한 로컬 백업 ref에도 연결했다. 기존 정상 브랜치·커밋은 삭제하지 않았다. fetch와 push가 정상 종료했고 원격 반영을 확인했다.

감사 채팅 완료를 확인한 뒤 이관을 재개해 환경 라이브러리 반입·검증을 마쳤다. 범위는 `assets`, `MedievalVillage`, `MedievalVillageMegascansS` 세 원본의 환경 제작 자산과 필수 의존성이다. **GameAnimationSample/GASP는 전부 제외했다.** 기존 월드 배치·높이·StableID·C++ 및 감사 작업 변경은 보존했다. 아래 과거 전환 기록과 이번 자산 검증을 구분한다.

건물·나무·풀·바위·환경 소품, 조립 Blueprint와 필수 재질/텍스처/지형 의존성을 채택했다. 캐릭터·캐릭터 애니메이션·Niagara/VFX·오디오·샘플 UI/입력/GameMode·데모 맵은 제작 목록에서 제외했다. 깃발/천 소품의 필수 Skeleton/PhysicsAsset과 환경 재질에도 쓰이는 공유 노이즈는 보존했다. 효과 시스템만 소비하는 신규 자원48개와 샘플 EQS 표식1개는 `/Game/Developers/IntakeHold`에 별도 보존했다. 자동 승인 검토가 표식의 실제 삭제를 명시적 승인 부족으로 거부해, 삭제 대신 되돌릴 수 있는 보존·분류 방식을 사용했다.

원본은 읽기 전용으로 조회하고 `Saved/AssetIntake`의 격리 프로젝트에서 엔진 API로 경로 변경·의존성 갱신·마이그레이션한다. 기존 자산과 실제 내용/속성/의존성을 비교해 중복은 재사용하고 다른 튜닝은 `/Game/Map_Resource/Variants`에 보존한다. 기존 공유 경로의 대량 이동·월드 재저장·redirector 일괄 정리는 하지 않는다. 설치 엔진의 `Water`, `Volumetrics` 콘텐츠 의존성은 활성화했으며 실제 물 배치는 보류 상태다.

제작 대상7,556개 중 신규5,282개, 기존 재사용2,274개다. 원본별로 assets4,532개(신규4,049), MedievalVillage944개(신규829), Mega2,080개(신규404)다. StaticMesh2,213개와 FoliageType254개 등을 포함한다. 기존 튜닝과 다른543개는 Variants에 보존했다. 같은 FBX 원본 해시를 가진 교차 라이브러리 메시 후보는 발견되지 않았으며, 동일 이름·바이트 차이만으로 중복을 결정하지 않았다.

실제 프로젝트의 새 UE5.8 프로세스에서 클래스·필수 hard 참조·원본에 존재하던 유효 soft 연결을 검사했다. 전체 검사 후 구 경로가 남은 고해상도 팩25건을 격리 CoreRedirects로 재저장했고, 제작 범위에 남은 대상은 집중 재검증을 통과했다. 이 임시 설정은 실제 프로젝트로 복사하지 않았다. 지피 메시24개의 별도 재질 슬롯·임포트 이름·LOD별 바인딩을 원본 구조체로 복구하고 새 프로세스 검증 후 반영했다. 구름 재질의 플러그인·파라미터 컬렉션 연결도 복구했으며 관련 재질4개의 실제 그래프가 원본과 일치했다.

오래된 레지스트리 기록20건은 원본의 현재 객체·Blueprint 클래스/CDO·재질 그래프 또는 파생 텍스처 스트리밍 기록을 직접 대조해 구분했다. 원본부터 없던 soft 참조가 남은 패키지는 원본별4/6/46개이며, 이를 필수 hard 누락으로 오인하거나 누락 자산이 새로 해결됐다고 보고하지 않는다. 원본의 구형 MeshToPoints를 쓰는 PCG12개는 UE5.8에서 메시 입력이 남지 않는 호환성 제한이 있어 `Env_LegacyPCG`로 분리했다. 이 생성기의 현대 노드 전환·실행 검증은 보류다.

Packed Blueprint56개의 제작용 원본 레벨54개와 외부 액터453개를 네이티브 Save As로 보존했다. 실제 프로젝트에서54개 레벨의 메시·재질·변환·인스턴스 장면 해시가 격리 전후 기준과 일치했고,56개 Blueprint의 새 원본 레벨 연결과 신규 Blueprint126개의 컴파일을 확인했다. 복사 전 SHA가 남은 원본·공유 파일5,906개(약40.86GiB)를 재대조해 차이0을 확인했다. 기본 메시를 배치할 때의 충돌·시각 품질·월드 성능 합격을 뜻하지 않는다.

공유 컬렉션 `Env_All`, `Env_Assets`, `Env_Village`, `Env_Mega`, `Env_Prefabs`, `Env_LegacyPCG`를 엔진 API로 저장했다. Env_All은 제작 자산 중 구형 PCG12개를 제외한7,544개와 원본 레벨54개를 모은7,598개다. 기존 공유 경로1,710개는 물리적으로 Map_Resource로 이동하지 않았으며 컬렉션에서 함께 찾는다. 폴더별 사용법과 제한은 디자이너 가이드를 따른다.

실패한 commandlet의 PackageSaveHash·썸네일 참조 오류와 파일 공유 잠금은 실패 기록을 보존해 복구했고, 통과한 검사를 불필요하게 전부 반복하지 않았다. 크래시 감시 프로세스의 존재만으로 크래시를 판정한 초기 안내는 정정했다. 이관용 임시 프로젝트·파생 캐시는 정리했다. 일회성 실행 스크립트·로그200개는 무결성을 확인한 `Saved/AssetIntake/Execution_Archive.zip`으로 묶고 개별 파일을 정리했다. 최소 완료 장부·비교 근거는 로컬 Saved/AssetIntake에 남겼다. Saved는 Git 제외이므로 다른 PC 인계 근거는 이 문서와 가이드의 결과 요약이다.

감사 후 현재 `MapsToCook`과 WorldDefinition 검색은 MainWorld이며 기본 시작 맵 ThirdPerson은 유지한다. 이번에는 에디터 commandlet의 자산 로드·저장·재로드·참조·장면·Blueprint 컴파일을 검증했다. 새 월드 아트·PIE·Cook·전체 재질 셰이더·성능 시험은 수행하지 않았다. 커밋·push 상태는 위 재점검 기록과 실행 후 채팅 보고를 따른다. 이관 완료 후 자동 재개 예약은 PAUSED로 중지했다.

## 이전 전환 기록과 디자이너 인계

[레벨 디자이너 가이드](LEVEL_DESIGNER_GUIDE.md)에 현재 맵 선택·C++ 역할·편집 주의사항·미해결 문제를 정리했다. 아래 본문의 Starter 경로·수치는 이전 제작 기준이며 새 맵 전환 완료를 뜻하지 않는다.

- 목표 맵 `/Game/World/MainWorld/Maps/L_MainWorld`는 WP 전용 엔진 복제 도구로 저장했다. 복구 후 로그는0 error/4 warning이며 이전·신규 외부 액터 패키지가 각각2,717개다. 관련 비맵 에셋45개의 이름 변경 영수증은 SAVED다. 새 맵 재로드·전체 참조·수목 보존·PIE 검증은 남아 있다.
- Source·도구는 아직 Starter 경로이며 DefaultEngine의 기본 맵은 ThirdPerson, DefaultGame의 WorldDefinition 검색/Cook은 Starter를 가리킨다. 사용자 삭제 승인된 `/Game/Level/Main_World`, `/Game/Level/Main_World_WP`와 이전 제작 맵도 남아 있다. 충돌 없이 전체 이행 완료로 보고하지 않는다.
- 이전 수목 cull 보정 스크립트의 출력 경로 오류로 외부 액터 `C/LL/S6WI6M7W1QA7CE59LOV348.uasset`에 JSON이 기록된 것을 발견해 유효 백업으로 복원했다. JSON은 실제 forest-streaming Source 경로로 분리했다. 당시 Content 패키지 헤더 검사에서 추가 비정상 파일은 없었다. 복구 액터의 변경 전 cull을0/0으로 다시 맞추고 새 맵의 수량·변환을 확인하는 작업이 남아 있다. 전체82,758그루가 새 맵에서 검증됐다고 주장하지 않는다.
- 당시에는 4개 프로젝트를 후보로 조사했으나 최신 요청은 위 세 원본으로 좁혀졌으며 GameAnimationSample/GASP를 전부 제외한다. 당시 조사 자료는 이관 완료 기록이 아니다.
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

이전 월드 마감 정리 시점에는 커밋·push하지 않았으며 다른 기능의 사용자 변경·C++·프로젝트 에셋을 지우거나 원복하지 않았다. 이후 환경 라이브러리 이관의 커밋·push 결과는 위 2026-10-19 기록을 따른다.

커밋 전 정리에서는 적용 완료한 보정·퇴역 Region A 제작기와 일회성 검증 도구를 제거하고, 중복 진단·제작·재개 문서4개를 최신 인수인계·상태표로 통합했다. Source 생성 이력에 적힌 과거 report 파일명 중 일부 원자료도 삭제했다. 그 파일명을 현재 검증 결과로 재사용하지 않는다. 과거 native survey에 의존하던 B01 난간 전용 일회성 검증기도 함께 퇴역했다. 현재 Source·Native·기본 저장본 검증 도구는 보존했으며 삭제된 세션의 정밀 재분석/그대로의 재실행은 불가능하다.
