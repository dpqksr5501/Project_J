# 시작 권역 재제작: 확인한 기반

2026-10-17. 현재 사용자 요청은 기존 A 지역 결과물을 교체하고 v3 프롬프트의 첫 Lv.1~30 권역을 새로 제작하는 것이다. 이 폴더의 상태 기록은 이전 A 지역의 성공 수치를 새 월드에 전용하지 않는다.

## 확인한 자원

UE 5.8 에셋 프로젝트 `assets`와 `MedievalVillage`에서 Unreal MCP의 initialize → tools/list → list_toolsets → describe_toolset → call_tool을 실제 호출했다. AssetTools.find_assets의 StaticMesh 클래스 필터와 get_asset_tags로 확인했다.

| 팩/범위 | 실제 StaticMesh 수 | 대표 메시 메타데이터 표본 |
| --- | ---: | ---: |
| MedCastle | 301 | 18 |
| UnrealNormandy | 105 | 18 |
| SkyTown | 428 | 18 |
| Iceland_Environment | 14 | 12 |
| Nanite_Env_Bundle_1/StunningNaturalPlace | 36 | 13 |
| AncientTempleRuins | 191 | 18 |
| MedievalVillage/Meshes | 496 | 18 |

위 숫자는 폴리곤 예산·실내·게임플레이·시각 품질의 합격 수가 아니다. 표본에서 Triangles, LODs, CollisionComplexity, CollisionPrims, NaniteEnabled, ApproxSize, 재질 참조 등을 확인했다. 모든 메시를 실제 플레이에서 검사한 것은 아니다.

MedCastle 성문·탑·주택·교량 10종에서 출발해 MCP로 의존성 123개 패키지, 1,692,580,216바이트를 추적했다. 팩 외부의 미해결 `/Game` 의존성은 없었다. 원래 가상 패키지 경로를 유지한 복사와 SHA-256 대조 후, Project J 안에서 엔진 AssetTools로 Map_Resource 하위로 이동·저장했다. 원본 라이브러리의 자산은 저장하지 않았다. 이 팔레트가 실제 도시/마을에 배치되고 아트 품질을 승인받았다는 뜻은 아니다.

`ElderBoomHollowMassiveMed`는 EngineAssociation 5.7이다. 원본 버전을 변경하지 않고 `/Game/ElderboomVillage` 411개 패키지(6,843,648,523바이트)를 같은 패키지 경로로 복사·SHA-256 대조했다. Project J의 5.8 MCP에서 StaticMesh 56개를 확인했고, native Python으로 실제 벽·문·박공·나무·소품의 크기, 재질, LOD를 읽었다. 선택한 벽·문·활엽수·통은 S00에 배치했다. 팩 전체의 충돌·스타일·Cook 합격을 뜻하지 않는다.

추가로 `MedievalVillageMegascansS` 5.8 프로젝트를 MCP 8003으로 열어 Meshes 202개, Megascans 279개, ThirdParty 15개의 StaticMesh를 확인했다. MCP의 에디터 콘솔에서 native AssetRegistry 의존성 조회를 실행해 496개 메시에서 1,676개 패키지(14,338,649,270바이트)를 이관했다. 기존 파일 충돌을 사전 검사하고 원본/사본 지문을 대조했다. 샘플의 전체 오픈월드·외부 액터 약 7천 개를 가져오는 방식은 피했다. 원본부터 없는 의존성 39개가 기록됐으며, 선택 메시의 전이 참조에 해당 누락이 있으면 제작 스크립트가 배치를 거부한다. 예를 들어 누락 NormalLOD0를 쓰는 StoneWall은 첫 배치에서 제외했다.

`assets` 5.8은 다시 MCP로 열어 MedCastle 주택/탑 14개, SkyTown 메시 428개, ProceduralNaturePack 48개 범위를 확인하고 대표 크기·재질을 조회했다. 기존 MedievalVillage 5.8 MCP 조사 근거는 재사용했다. `GameAnimationSample` 외부 프로젝트는 이번 탐색·복사에서 제외했다. 기존 Project J 캐릭터/애니메이션의 사용 계약과는 별개다.

## 코드 선택

- 유지: 서버 기준 시간·강우 전이 WorldState, 클라이언트 환경 표현, 이벤트 ID 등록 수명, NPC 연결 계약, 기본 플레이어와 별도인 로컬 WP Streaming Source.
- 확장: 기존 WorldDefinition에 장소 종류·권장 레벨·경계 다각형, 전체 저작 범위, 생성 버전/시드/원본 지문과 외곽 연결 데이터를 더했다. 서버 ZoneId와 WP 셀 번호로 콘텐츠 ID를 대체하지 않는다.
- 재사용: 엔진 Landscape import/Edit Layer accessor와 공간 청크 spline 도구. 새 지형은 이전 A의 좌표·높이 패치·도시 필지·6종 단순 건물 조립 결과를 입력으로 사용하지 않는다.
- 이전 A의 검증 문서·테스트와 원본 공급자 자산은 제작 결과물 삭제와 구분한다. 기존 캐릭터·전투·탈것 코드는 이 재제작의 삭제 대상이 아니다.

## 도구와 안전 경계

MCP는 에디터 전용 ModelContextProtocol·AllToolsets로 켰다. 포트는 Project_J 8000, assets 8001, MedievalVillage 8002, MedievalVillageMegascansS 8003이고 로컬 기본 바인딩을 유지한다. 원격 Python·방화벽·인증·사용자 전역 Codex 설정은 변경하지 않는다. 원본 uproject와 기존 설정은 로컬 Saved/StarterWorld/ProjectDescriptorBackups에 보존한다.

원자료는 Saved/Validation/StarterWorld 아래 MCP inventory/schema/transfer 기록이다. 이는 로컬 재현 자료이며 공유 문서의 필수 절대 경로가 아니다. 메시 표본 메타데이터는 ASSET_INVENTORY.csv를 참고한다.
