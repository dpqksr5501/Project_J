# Map_Resource 정리 및 단일 구조 통합 기록 (2026-11-22)

## 요약

`Content/Map_Resource` 내 미사용 에셋과 구버전 미사용 레벨(`Main_World`, `Main_World_WP`)을 정리하고, 실제 메인 월드(`L_MainWorld`) 및 프로젝트에서 참조하는 에셋만 남겨 `Content/Map_Resource/main_1` 단일 폴더 아래 타입별로 통합했다.

| 항목 | 정리 전 | 정리 후 | 변화 |
| --- | ---: | ---: | ---: |
| `Content/Map_Resource` 파일 수 | 8,650개 | 801개 | -7,849개 |
| `Content/Map_Resource` 용량 | 57.11 GB | 9.42 GB | **-47.69 GB (-83.5%)** |
| 최상위 팩 폴더 수 | 22개 | 1개 (`main_1`) | 단일화 |
| 미사용 데모/PLA `.umap` | 57개 | 0개 | 전량 제거 |
| 구버전 미사용 레벨 (`Content/Level/`) | 2개 + 외부 액터 72개 | 0개 | 전량 제거 |

## 최종 폴더 구조

- `Content/Map_Resource/main_1/Blueprints` — `BP_DoorFrame` (C++ 자동화 테스트 참조 포함)
- `Content/Map_Resource/main_1/Materials` — Material, MaterialInstance, MaterialFunction, MPC, LandscapeLayerInfo/GrassType
- `Content/Map_Resource/main_1/Meshes` — StaticMesh
- `Content/Map_Resource/main_1/Textures` — Texture2D, RuntimeVirtualTexture

*(동일 타입 폴더 내 이름 충돌이 발생한 에셋 26건은 `_원래팩이름` 접미사를 부여해 충돌 없이 이관했다.)*

## 함께 갱신한 참조

- **C++ 테스트**: `Source/Project_J/Private/Tests/Project_JPCStructureOwnershipTests.cpp`의 `BP_DoorFrame` 경로를 `/Game/Map_Resource/main_1/Blueprints/BP_DoorFrame.BP_DoorFrame_C`로 갱신
- **월드 매니페스트/팔레트**: `Content/World/MainWorld/Source/*.json` 13개 파일의 `Map_Resource` 경로 및 `resource_content_root`(`/Game/Map_Resource/main_1`) 갱신
- **콘텐츠 브라우저 컬렉션**: `Content/Collections/*.collection` 7개 파일의 이동 경로 반영 및 삭제 항목 제거
- **리디렉터 정리**: `UnrealEditor-Cmd.exe -run=ResavePackages -fixupredirects` 및 잔여 무참조 리디렉터 정리로 전체 `/Game` 잔여 `ObjectRedirector` 0건 달성

## 무결성 검증

- `UnrealEditor-Cmd.exe` `AssetRegistry` 재스캔 전수 검사 결과:
  - 검증 패키지 수: **6,367개** (오류 0건, 경고 0건)
  - `L_MainWorld` (`/Game/World/MainWorld/Maps/L_MainWorld`): `ExternalActors` 2,717개, `ExternalObjects` 77개, `main_1` 참조 에셋 782개 정상 유지
  - 깨진 `Map_Resource` 참조: **0건**
  - 잔여 `ObjectRedirector` (`Map_Resource` 및 전체 `/Game`): **0건**
