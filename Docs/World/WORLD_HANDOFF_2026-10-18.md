# 월드 작업 인수인계 — 2026-10-18

## 2026-10-19 Elderboom 원본 대조와 기존 환경 자산 재사용

사용자가 ElderBoomHollowMassiveMed의 남은 환경 에셋을 기존 반입분과 대조하여 Map_Resource로 이관하도록 요청했고, 선행 Starter 정리 채팅의 작업 완료를 확인한 뒤 수행했다. 원본 UE5.7 프로젝트에는 uasset 409개와 데모 맵 2개가 있으며, 실제 프로젝트의 `Content/Map_Resource/ElderboomVillage`에 411개 모두 대응 파일이 이미 존재했다. 앞서 채팅에서 일부만 반입됐거나 이번 범위에서 누락됐다고 설명한 것은 전체 대조 전의 부정확한 안내였다. 이번에는 신규 에셋을 복사하지 않았다.

환경 제작 자산은 380개(StaticMesh 56, Texture2D 192, Material 14, MaterialInstanceConstant 66, MaterialFunction 43, MaterialParameterCollection 3, LandscapeLayerInfoObject 4, FoliageType 2)이며 모두 기존 자산을 재사용했다. 신규 반입 0·신규 변형 0·환경 누락 0이다. 독립 효과 자원 29개와 데모 맵 2개도 이미 존재하며 그대로 보존했다. 효과 전용 29개를 환경 제작 자산 380개가 소비하는 hard 의존성은 없었다. 기존 Niagara 7개의 변경 식별자 차이를 효과 기능 합격으로 해석하지 않으며 Niagara 동작/성능 검증은 하지 않았다. 기존 7,556개는 앞선 세 원본의 반입 당시 집계이므로 현재 라이브러리 파일 수나 이번 380개의 신규 반입 수로 더하지 않는다.

원본과 실제 프로젝트 파일을 각각 SHA256으로 기록한 뒤 독립 복사한 UE5.8 content-only 프로젝트에서 읽기 전용 비교했다. 경로 `/Game/ElderboomVillage`와 `/Game/Map_Resource/ElderboomVillage` 차이만 정규화했으며 환경 380개의 실제 속성·재질/함수 그래프·hard 참조가 일치했다. 환경 범위 Game hard 의존성 누락은 0개다. 메시 56개는 렌더 및 원본 메시 FBX 내보내기의 LOD·충돌·형상을 대조해 모두 일치했고, 전체 Texture2D 205개(환경192+효과13)는 원본 ID·편집 속성 및 DDS 원본 픽셀 지문이 모두 일치했다. 원본/실제 프로젝트 411개 파일의 검사 전후 SHA256도 각각 유지했다. 실제 에셋 저장/월드 변경은 0개이며 전체 셰이더·PIE·Cook·시각/성능 검증은 수행하지 않았다.

기존 Env_All에는 Elderboom 데모 맵 2개만 카탈로그에 있었고 환경 자산은 빠져 있었다. 엔진 컬렉션 API로 환경380개를 추가해 기존7,647개를 모두 보존한8,027개가 됐으며 `Env_Elderboom`에는 환경380개만 등록했다. 소스 제어 자동 커밋을 피하려고 격리 프로젝트의 로컬 컬렉션 모드에서 네이티브 파일을 저장한 뒤 전체 경로 집합·기존 GUID 보존을 대조해 실제 공유 컬렉션에 반영했다. 에셋 자체는 복제하지 않았다. 실제 기존 컬렉션 파일은 하드링크여서 직접 쓰기 대신 새 파일의 원자적 교체로 다른 링크의 원형을 보호했다.

처음 텍스처 T3D 비교의 과도한 출력과 생성 JSON 잠금, HDR 피벗 텍스처에 PNG exporter를 적용한 검사기 오류는 실패 기록으로 남겼다. 진행 중이던 C++/UBT 빌드는 없었으며, 자체 읽기 전용 검사 실행만 정리하고 작은 속성 지문·DDS 방식으로 수정해 검증을 마쳤다. 이 실패들은 원본/실제 에셋 저장을 수행하지 않았다. 로컬 근거는 `Saved/AssetIntake/Elderboom`의 completion-summary.json·comparison.json·bulk-comparison.json·file-baseline.json과 컬렉션 영수증이다. 원본 삭제·커밋·push는 수행하지 않았다. 새 프로세스에서 공유 컬렉션의 환경380개 재로드를 확인했고 최종 DDS 비교·컬렉션 검증 로그는 각각 오류0·경고0이다. 격리 사본/임시 출력의 논리 크기 약12.75GiB를 정리했으며 실행기와 성공/실패 로그는 SHA256을 검증한 ExecutionScriptsAndLogs.zip에 보관했다.

**환경 아트 제작은 중단 상태이며, 이후 사용자 요청으로 메인 월드 이름 전환과 환경 에셋 통합 작업을 시작했다. 메인 월드 전환의 최종 검증은 아직 미완이며, 환경 에셋 이관은 아래 2026-10-19 기록대로 완료했다. 원거리 숲과 자연스러운 산, 도시·마을의 최종 품질도 미완이다.** 이 문서가 최신 재개 기준이다. 이전의 장문 세션 기록과 WorldQuality 문서는 커밋 전 정리하면서 여기에 통합했다.

## 2026-10-19 재질 작업 후 텍스처 DDC 용량 정리

사용자가 작업 후 늘어난 용량 정리를 다시 요청했다. 보고서/스크립트보다 Zen의 재생성 텍스처 변환 캐시가 큰 원인이었다. 복구 작업 시작 이후 생성된 Zen 캐시 파일의 논리 합계는 12.45GiB였으며 생성 시각 기준 크기를 정확한 증가량으로 단정하지 않았다. 조사 중 에디터와 기존 Zen이 정상 종료된 것을 확인했다. 공식 zen 관리 도구로 실제 사용자 Data 경로를 명시한 관리 서버를 시작하고, info가 그 경로를 가리키는지 확인한 뒤 ue.ddc/legacytexture 버킷만 drop했다. 관리 도구 기본 시작이 별도 빈 ProgramData 저장소를 가리킨 것은 변경 전에 확인해 그 관리 서버를 정상 종료하고 올바른 경로로 전환했다. 기존 프로세스를 강제 종료하거나 저장소 파일을 직접 삭제하지 않았다.

텍스처 캐시 버킷 19,537,701,877바이트(18.20GiB)를 제거해 Zen cache의 논리 파일 크기는 22,553,535,930 → 3,015,834,053바이트가 됐다. fshaderjobcacheshaders 등 다른 캐시, CAS 6,552,811,285바이트/759파일 및 프로젝트 저장소는 유지했다. 프로젝트 저장소 9파일 SHA256과 보호한 Source/Config/Content/영구 Scripts/Binaries/Intermediate/Plugins/Build 20,762파일의 경로·크기·mtime 전후 일치를 확인했다. 에셋/월드/높이 원본·현재 빌드·복구 ZIP은 지우지 않았다. 다음 실행에서 필요한 텍스처 파생 데이터는 재생성된다.

최근 재질 검사/복구의 raw T3D·diff·로그 101개는 원문 6,904,152바이트를 해제 SHA256이 일치하는 647,358바이트 RetiredMaterialDiagnostics.zip으로 보관한 뒤 낱개 파일을 퇴역했다. 결과 JSON·복구 원형·이미 퇴역한 실행기 ZIP은 유지했다. 이후 과거 raw 경로를 직접 읽으려면 이 ZIP의 프로젝트 상대 경로를 먼저 복원해야 한다. 이번 정리의 관리 서버도 정상 종료했으며 용량을 다시 만드는 에디터 재로드/새 빌드는 수행하지 않았다.

캐시 정리 전후 드라이브 실제 여유 공간의 관측 증가는 약 18.53GiB이고, 완료 시 여유 공간은 약 166.23GiB였다. 논리 제거량과 실제 공간 관측값은 별개이며 같은 드라이브의 외부 활동 영향을 포함할 수 있다. 상세 before/after·버킷 도구 출력·보호 대조·지문은 Saved/StorageCleanup_20261019/MaterialWork/result.json 및 관련 영수증에 보존했다.

## 2026-10-19 보류 PPV 프리셋 추가 판정·복구

사용자가 보류 재질의 필요성 확인 후 복구/안전 삭제를 요청했다. `/Game/Map_Resource/AncientTempleRuins/Materials/Function/MI_PPV_Inst`와 정상 `Materials/Various/MI_PPV_Inst`의 원형/속성을 대조했다. 두 자산은 Strength 파라미터 GUID가 같고 정상본 부모 M_PPV의 실제 Strength 노드 GUID와도 일치하지만, 보류본은 Strength=2.0, 정상본은 0.5여서 강도가 다른 고유 프리셋이다.

전체 hard/soft/search/management 및 load-confirmed 참조, 옛 경로 참조는 0개였고 MainWorld의 후처리 설정에서도 사용하지 않았다. Config·영구 스크립트·제작 Source에는 사용 경로가 없으며 AssetManager/Cook 등록도 이를 지정하지 않는다. Env_All/Env_Assets의 항목은 제작 라이브러리 카탈로그다. 현재 실행에 필요하다는 근거는 없지만, 다른 튜닝의 제작 자산은 미참조만으로 삭제하지 않는 프로젝트 기준을 적용해 복구·보존했다. 현재 사용된다고 주장하거나 동명 정상본으로 치환하지 않는다.

원형을 Saved/Maintenance/PPVReview_20261019/BeforePPVRepair.zip에 SHA256 검증 후 보관하고, 실제 GUID가 일치하는 Various/M_PPV 부모만 연결해 MI 한 개를 엔진에서 저장했다. Strength=2.0과 기존 Scalar/Vector/Texture 값, 기본 속성 override 활성화/저작 수치는 유지했다. 부모 갱신으로 비override UsageFlags의 상속 캐시와 상태/Lighting GUID만 갱신됐다. D3D12 셰이더 결과는 pixel instructions 91/samplers 1이고 저장 패키지의 새 부모 의존성을 재스캔해 확인했다. 맵에 효과를 추가하거나 에셋을 삭제하지 않았으며 dirty map/content 패키지는 전후 모두 0개다. 이전 보류 상태도 복구 영수증으로 해소했다. 상세 비교·참조·백업·diff·저장 SHA는 PPVReview_20261019에 보존하고 일회성 실행기는 검증한 ExecutionScripts.zip으로 퇴역했다.

## 2026-10-19 환경 재질 내부 연결 확대 점검

후속 사용자 요청으로 실제 에디터의 MainWorld를 유지한 채 환경 라이브러리의 Material 364개·MaterialInstanceConstant 1,631개·MaterialFunction 328개, 총 2,323개를 읽기 전용으로 점검했다. 텍스트 경로 검색만 쓰지 않고 실제 로드된 부모·텍스처·함수 객체를 조사했으며, 출력에서 그래프 입력을 역추적해 사용 노드와 끊긴 실험 노드를 구분했다. 실제 메시·지형·후처리 설정에서 얻은 재질 인터페이스는 390개였다. 해당 직접 사용 범위의 빈 메시 재질 슬롯은 0개였다.

실제 사용 중인 부모 없는 MI 6개를 추가 발견했다. MainWorld SettlementInstancing의 담장·통·목재 빔 사본 3개는 원본 경로 해시·Authoring 태그·제작 함수의 재귀 부모 계약을 확인해 기존 instancing 대응 부모 M_MS_Default_Material_e0ab4854로 연결했다. MI_Bark_01은 보존한 AssetStaging 원형의 M_Asset 부모 기록을 확인했다. ProjectExisting의 Trim_Engraved_01/02는 정상 사본과 앞선 전체 속성 diff에 부모/정적 permutation 유무만 달랐다는 기록을 확인해 M_Asset으로 연결했다. 앞선 단계에서 보존했던 부모 없음은 현행에서 실제로 사용하는 오류였으므로 이번 사용 증거를 기준으로 복구했다. 별도 변형 에셋의 삭제·consolidate는 확대 수행하지 않았다.

6개 원본 139,749바이트를 BeforeParentRepair.zip으로 보관하고 압축 해제 SHA256을 검증한 후, 엔진에서 부모만 설정·업데이트하고 해당 6개만 저장했다. Scalar/Vector/TextureParameterValues, StaticParametersRuntime, BasePropertyOverrides는 저장 전후 유지됐으며 파생 GUID/참조 캐시만 재생성됐다. 실제 저장 파일에 새 부모 패키지 경로가 직렬화됐는지 재확인했다. 직전 공간 정리의 기준과 비교해 앞 절의 지형 재질 1개와 이번 MI 6개 외 실제 에셋·맵·높이·PNG 파일은 바뀌지 않았다.

실사용 인터페이스 390개의 전체 부모 체인과 D3D12 셰이더 통계를 다시 검사했다. 통계 API가 해당 MaterialResource의 컴파일 완료를 기다리는 엔진 구현을 확인했고, 390개 모두 셰이더 instruction 결과가 있으며 깨진 부모 체인 0개, 현재 로그의 재질 컴파일 실패 0개를 확인했다. HLOD 내부 재질을 일반 AssetRegistry 에셋 경로로 재로드하려던 검사기 1차 오류는 실제 로드 객체 참조를 사용하는 방식으로 고쳤다. 이전 실패 영수증도 보존했다. 맵·콘텐츠의 미저장 패키지는 전후 0개이며 액터 2,717개를 유지했다. 에디터에서 지형 표시도 유지됨을 확인했다.

154개 빈 텍스처 샘플 항목 중 실제 사용 노드는 TextureObject 입력으로 텍스처를 받았고, 활성 출력으로 이어지는데 입력도 없는 샘플은 0개였다. 87개 빈 MI 텍스처 override 항목은 현재 직접 사용 390개에 포함되지 않으므로 임의 채우기/삭제하지 않았다. MF_AdvancedWind_Level의 비어 있는 함수 호출 1개도 함수 출력으로 연결되지 않은 노드라 그대로 보존했다. Materials/Function/MI_PPV_Inst 1개는 원본부터 부모가 없고 별도 Various/MI_PPV_Inst와 역할이 다를 수 있으며, 현재 메시·지형·후처리 사용 범위에서 찾지 못했다. 미사용이라고 단정해 삭제하거나 다른 동명 자산으로 치환하지 않고 의도 확인 후보로 남겼다. 전체 비활성 permutation·PIE/스트리밍 상태·Cook 검증은 하지 않았다.

증거는 Saved/Maintenance/EnvironmentMaterialAudit_20261019의 completion-summary.json·audit.json·live-verification.json·파라미터/백업/저장 지문 영수증에 보존했다. 일회성 실행기는 검증한 ExecutionScripts.zip으로 퇴역했다.

## 2026-10-19 MainWorld 체크무늬 지형 재질 복구

사용자가 실제 에디터 화면의 체크무늬 지형을 제시했다. 로그에서 `M_MainWorldTerrain_Biome`의 TextureObject 입력이 비어 PCD3D_SM6 컴파일이 실패하고 기본 재질로 대체된 것을 확인했다. 재질 11개를 읽기 전용으로 검사해 해당 지형 재질의 TextureObject 13/14/22/23 네 개가 None으로 저장돼 있음을 확인했다. 앞선 옛 경로 AssetRegistry 검사와 액터의 재질 연결 일치 검사는 이미 None이 된 노드 및 실제 렌더링 문제를 포착하지 못했다. 따라서 앞 절의 경로 검사 통과를 시각 검증 완료로 해석하면 안 된다.

현재 재질 원형 42,409바이트를 `Saved/Maintenance/TerrainRepair_20261019/BeforeTerrainRepair.zip`에 보관하고 SHA256을 확인했다. 제작 스크립트의 입력 계약과 보존한 이동 전 자원 원형, 현재 그래프 Custom 노드의 Mask/Surface/Ecology/Placement 핀 연결을 대조해 각각 현재 Map_Resource/MainWorld의 `T_MainWorldBiomeMasks`, `T_MainWorldSurfaceMasks`, `T_MainWorldEcology`, `T_MainWorldPlacement`로 노드 네 개만 복구했다. Custom 코드·그래프 배선·노드 수 25개와 다른 제작 속성은 보존했고, 컴파일로 StateId/LightingGuid·ReferencedDefaultTextures/ReferencedTextureGuids 등 엔진 파생 데이터만 재생성됐다. 재질 한 패키지만 엔진에서 저장했다.

D3D12·SM6로 직접 재컴파일해 오류 배열 0을 확인했다. 그 뒤 임시 redirect 없이 실제 Unreal Editor 새 프로세스로 L_MainWorld를 열어 텍스처 노드 14개가 모두 유효하고 로드된 Landscape/Proxy 257개가 복구한 재질을 사용하는지 확인했다. 새 에디터 로그에는 재질 컴파일 실패/Requires valid texture 오류가 없으며, 셰이더 준비가 끝난 화면에서 체크무늬가 사라지고 풀·흙길·암반 표면이 표시되는 것을 computer-use로 직접 확인했다. 아웃라이너는 2,717개 액터 로드를 표시했다. 직전 공간 정리의 메타데이터 기준과 비교해 지형 재질 한 패키지 외의 실제 에셋/맵 패키지·높이/PNG 원본은 변경되지 않았다. 별도 Source JSON 9개의 메타데이터 차이가 관찰됐으나 이번 복구에서는 그 파일들을 쓰지 않았다. 지형 높이·월드/외부 액터 배치는 수정하지 않았다. 에디터는 복구한 MainWorld를 연 상태로 유지했다. 전체 PIE·Cook·최종 아트/성능 검증으로 확대하지 않는다.

상세 원형·노드/컴파일/새 에디터 검증·그래프 diff는 `Saved/Maintenance/TerrainRepair_20261019`에 보존했다. 일회성 실행기는 ExecutionScripts.zip의 원문 SHA256 검증 후 퇴역했다. 전체 캐시 삭제나 새 C++ 빌드는 하지 않았다.

## 2026-10-19 환경 작업 산출물 용량 정리

후속 사용자 요청으로 환경 작업 폴더와 종료된 격리 실행의 용량을 확인했다. Saved/Maintenance의 환경 정리 세 폴더는 2.69GiB, Saved/WorldMigration은 0.37GiB였다. 현재 root Binaries·Intermediate·DDC와 Source/Config/Content·영구 제작 스크립트·테스트, Saved의 개인 세이브/자동 저장 및 기존 별도 최적화 원자료는 이번 정리에서 유지했다.

종료된 IsolatedUser/RepairUser/ReviewMoveUser의 격리 Intermediate 캐시 27개(494,415,714바이트)만 삭제했다. 비교용 ActorProperties·WorldDiffs·WorldValueDiffs, 원형 Before*/BeforeSnapshot 및 이동 전 월드 백업과 대형 검사용 출력/로그 9,638개(2,477,757,870바이트)는 `Saved/StorageCleanup_20261019/AssetWork`의 ZIP 네 개(722,795,630바이트)로 압축했다. 각 항목을 압축 해제해 원문 SHA256과 일치함을 확인한 뒤 낱개 파일만 제거했다. 고유 원형을 폐기하지 않았다. LegacyRetired.zip·BeforePathRepair.zip과 기존 압축 스크립트/복구 백업은 그대로 유지했다. 완료한 저장 공간 정리 실행 스크립트 5개도 같은 위치의 ExecutionScripts.zip에 검증 후 퇴역했다.

논리 파일 크기 순감소는 2,249,377,954바이트(2.09GiB), 실행 전후 드라이브 여유 공간의 관측 증가는 862,097,408바이트(0.80GiB)다. 하드링크/공유 데이터 및 같은 드라이브의 외부 활동 때문에 두 값을 실제 회수량으로 혼동하지 않는다. 보호한 프로젝트 파일 20,762개의 경로·크기·수정 시간이 전후 동일하다. 실행 가능한 코드를 바꾸지 않아 새 빌드·PIE는 수행하지 않았다. 이전 검사 스크립트가 압축된 원래 경로를 직접 읽으려면 해당 ZIP의 프로젝트 상대 경로로 먼저 해제해야 한다. 상세 원문 지문·명세와 전후 공간은 `AssetWork/*-contents.json`, `plan.json`, `result.json`에 있다.

## 2026-10-19 후속 재검사·실제 참조 복구·전체 빈 폴더 정리

사용자의 재검사 요청으로 앞선 액터 메시·재질 연결 검사보다 내부 의존성 검사를 넓혔고, 환경 자산 160개에서 이동 전 패키지 경로를 발견했다. 이전 검사만으로 내부 재질 부모·텍스처까지 안전하다고 판단할 수 없었던 누락이다. 160개 원본 21,268,961바이트를 `Recheck/BeforePathRepair.zip`으로 보관하고 각 SHA256을 검증했다. 자동 승인 검토가 최초 일괄 수정을 차단한 뒤 사용자가 160개 복구·재검증·빈 폴더 삭제를 명시 승인했다.

이동 기록으로 만든 임시 엔진 Package/Object redirect 설정을 복구 프로세스에만 적용해 원래 직렬화된 부모·텍스처·함수·중첩 레벨 참조를 해석하고, 해당 160개 패키지만 엔진에서 저장했다. 부모가 로드되지 않던 재질 60개가 복구됐다. 존재하지 않는 텍스처를 가리키던 편집기 전용 TextureStreamingData의 TextureReference 43개만 비웠으며 SamplingScale·UVChannelIndex·TextureName·배열 순서는 유지했다. 별도로 재질 5개의 본체/썸네일 PreviewMesh는 같은 MedievalVillage 라이브러리의 실제 메시로 연결했다. 원본부터 부모 연결이 없던 `MI_Trim_Engraved_01`·`MI_Trim_Engraved_02` 보존 변형 및 `MI_Bark_01`은 추측으로 부모를 지정하거나 삭제하지 않았다. 재질 캐시 수정 중 엔진이 생성한 ParameterStateId/LightingGuid 변화와 레벨 저장 시 생긴 임시 기본 물리 볼륨/서브시스템·출력 순서 차이는 별도 확인했다. 레벨 5개의 저장 전후 diff에서 제거된 제작 속성은 없었다.

임시 Config 파일은 검증한 압축본 `Recheck/TemporaryRepairConfig.zip`에 보존한 뒤 제거했다. 보정 설정 없는 새 엔진 프로세스에서 전체 `/Game` 의존성과 복구한 재질 114개의 부모·텍스처 값을 다시 확인했다. 옛 환경 경로 참조 0개, 설명 없는 Map_Resource 외부 환경 자산 후보 0개이며 Map_Resource 자산 8,649개와 MainWorld 전용 환경 자산 41개를 확인했다. MainWorld 액터/고유 GUID는 각각 2,717개, 인스턴스 414,311개와 기존 배치가 일치하고 액터 메시·재질 연결 15,170개 차이도 0개다. 메시 추가 6개의 슬롯·LOD 연결을 새 프로세스에서 대조했으며 `grass_small_02`는 복구 저장 전후 전체 export 지문이 일치했다. 기존 17개 메시 슬롯 검사도 통과했다. Env_All 7,647개와 컬렉션·검토 맵 검사도 통과했다. 전체 PIE·WP 스트리밍·Cook·시각/성능 검증은 이번에 수행하지 않았다.

실제 Content 파일의 추가·삭제 없이 승인된 160개 패키지만 변경됐음을 확인한 후, 파일이 전혀 없는 Content 디렉터리 6,961개를 하위부터 제거했다. reparse point를 따라가지 않고, 경계를 확인한 각 빈 폴더에만 비재귀 rmdir를 사용했다. 폴더 정리 전후 Content 파일 14,408개의 경로·크기·수정 시간이 모두 같고 남은 빈 Content 폴더는 0개다. 실제 레벨·월드 설정·제작 원본과 캐릭터·무기·탑승물·전투 전용 자원 및 기존 보류 자료는 유지한다. 이번 일회성 실행기는 `Recheck/ExecutionScripts.zip`에 원형/SHA256 검증 후 퇴역했다. 상세 결과는 `Saved/Maintenance/AssetCleanup_20261019/Recheck`와 `completion-summary.json`에 있다. 커밋·push는 수행하지 않았다.

## 2026-10-19 최종 환경 폴더·중복·Legacy 정리

사용자의 최신 요청은 Content에 흩어진 환경 자원을 `/Game/Map_Resource`로 모으는 것이다. 앞선 2,195개 이동에 이어 남은 상호작용 자원·환경 공유 노이즈·Elderboom 비-WP 원본 맵 2개를 통합했고 MainWorld 전용 환경 자원 41개도 `/Game/Map_Resource/MainWorld`로 이동했다. 이 41개는 StaticMesh 13·Material 11·MI 7·Texture2D 6·LandscapeGrassType 4개다. 마지막 ThirdPerson 테스트 맵 색상 재질도 `Map_Resource/ThirdPerson`에 모았다. 실제 맵·DA·HLOD 설정·PCG 그래프·제작 원천은 `World/MainWorld`에 유지한다. 캐릭터·무기·탑승물·전투 VFX와 기능 전용 자료, 호환성 보류 `Developers/IntakeHold`는 환경 라이브러리로 합치지 않는다. 이하 이전 절의 경로 보존 설명보다 이 절이 최신이다.

최초 `Variants/ProjectExisting` 충돌 59개는 정규화한 전체 편집 속성, 텍스처 42개의 실제 PNG 픽셀, 의존 자원 연결을 비교했다. 48개는 동일 자산으로 엔진에서 consolidate했고, 8개의 직접 속성 차이와 이들에 연결된 3개는 별도 유지했다. 추가 충돌 3개는 재사용했으며 상호작용 `BP_DoorFrame`은 기존 공용본과 동작이 달라 `Map_Resource/Variants/ProjectExisting/LevelPrototyping/Interactable/Door`에 보존했다. C++ `ProjectJ.PCStructure.AuthoredDoorOptions`의 실제 Blueprint 경로도 이 변형으로 갱신했다. 동일 이름이나 미참조만으로 삭제하지 않았다.

Legacy는 현재 맵과 액터 이름·클래스·변환·인스턴스 414,311개가 같고, 외부에서 Legacy 패키지로 들어오는 실제 참조가 없음을 확인했다. 2,795개 파일을 SHA256 재검증한 `Saved/Maintenance/AssetCleanup_20261019/LegacyRetired.zip`에 보존했다. 최초 active-world 강제 삭제가 맵 1개만 제거한 것은 사용자 승인으로 동일 바이트 복구한 뒤, 엔진에서 액터 2,717개·폴더 외부 오브젝트 77개를 삭제·저장하는 방식으로 마무리했다. 빈 월드 alias와 기존 Starter/환경 alias는 ResavePackages FixupRedirects로 참조를 갱신해 제거했다. `World/Starter`와 `World/MainWorld/Legacy` 및 Legacy 외부 패키지에는 실제 파일이 남지 않는다. 이번 요청과 다른 `Level/Main_World*` 검토 맵을 자동으로 추가 삭제하지 않았다.

제작 원본 37개의 파일명을 `MainWorld.*`·`HM_MainWorld.r16`·`T_MainWorld*.png`로 맞췄다. Source JSON 33개의 좌표·수치·안정 ID와 높이/PNG 원본 지문을 보존했고, manifest에 환경 자원용 `resource_content_root`를 명시해 월드·DA·설정 루트와 구분했다. 관련 명시 경로와 계획 도구의 동적 생성 경로, 텍스처 6개의 재임포트 원본 연결을 갱신했다. 텍스처 픽셀은 변경하지 않았다. MainWorld 액터 표시 이름 675개도 바꿨고 참조 레지스트리에 나타나지 않는 텍스처 스트리밍 제작 캐시의 문자열 경로 6,035개를 갱신했다. 캐시 GUID·배열 순서·수치는 보존했다.

자동 생성 HLOD의 내부 UObject 이름·GUID, 액터 폴더의 기존 계층/식별자와 `Starter.Lv1_30`, 기존 소유 태그는 유지한다. HLOD 내부 이름을 직접 바꾸는 시험에서 이전 외부 패키지가 남아 중복 GUID가 되는 엔진 동작을 확인해 시험 패키지는 엔진에서 퇴역했다. 최종 새 프로세스에서 MainWorld 액터 2,717개·고유 GUID 2,717개와 인스턴스 414,311개의 실제 변환이 기존 저장본과 일치했다. 표시 이름 이외의 씬 배치는 바꾸지 않았다. 메시 17개의 재질 슬롯·LOD 연결도 대조했고, 재저장 중 비워진 광산 메시 재질 1개는 이동 직전 기록의 동일 재질로 복구했다. 환경 목적지 2,250개 등록·클래스, 컬렉션 6개와 추가/검토 월드 6개 재로드 검증의 미해결 오류는 0개다. `Env_All`은 7,647개 경로가 모두 유효하다. 기존 정착지 instancing 재질 복사본 6개의 원본 경로 기반 이름도 새 위치에 맞춰, 실제 공용 제작 함수가 새 복사본 없이 재사용함을 확인했다.

메시·재질 연결 속성 15,170개를 액터 2,717개 전체에서 이동 전 기록과 대조해 미해결 차이 0을 확인했다. 최초 388개 차이는 기존 월드 비교 기록이 HLOD 내부 `L_StarterRegion` 이름을 `L_MainWorld`로 정규화했던 기준 차이였으며, 같은 비교 기준으로 새 엔진 프로세스를 재실행해 종료 코드 0을 확인했다. 에셋을 바꾸어 비교를 통과시키지 않았다. 마지막 Niagara 점프 패드 자산의 독립 재로드와 WorldState→DA 연결·지형 원본 경로/지문도 확인했다. 이전 한 번의 복합 검사에는 기존 GameplayCue 검색 경로·RecastNavMesh 초기화 경고가 있었으며 해당 시스템을 이번 폴더 정리에서 변경하지 않았다. 전체 WP 스트리밍·PIE·Cook·시각/성능 합격을 뜻하지 않는다.

설치 엔진의 direct UnrealBuildTool.exe로 `Project_JEditor Win64 Development` 빌드에 성공했다. 기존 성공 옵션을 재사용한 전체 재컴파일에서 드러난 테스트 코드 3곳의 누락 include 5줄만 보완했으며 게임 동작은 바꾸지 않았다. 이동한 실제 Door Blueprint의 `ProjectJ.PCStructure.AuthoredDoorOptions` 집중 회귀는 성공 1·실패 0·경고 0으로 네 가지 옵션과 타임라인 열기/닫기·스크립트 예외 부재를 확인했다. 기존 Python 저장/원천 검증 8개도 통과했다.

완료된 `Build-StarterWorld.py` bootstrap과 이번 일회성 이행 실행기는 원형·SHA256을 `RetiredExecutionScripts.zip`에 보관한 뒤 퇴역했다. 공용 제작 함수·현행 검증기·영구 테스트는 유지한다. 실제 엔진 파일 제거 후 비어 있는 옛 폴더만 제거했으며 Unreal 패키지를 파일 탐색기로 이동·삭제하지 않는다. 실행 기록·Legacy와 자원 원형 백업은 로컬 `Saved/Maintenance/AssetCleanup_20261019`에 보존한다. 커밋·push는 수행하지 않았다.

## 2026-10-19 MainWorld 폴더 통합 (직전 이력)

후속 사용자 요청으로 이름 변경 뒤 Starter에 남은 실제 자료를 MainWorld로 통합했다. 아래 직전 환경 정리 당시의 Starter 보존 설명보다 이 항목이 최신이다. 현행 맵 `/Game/World/MainWorld/Maps/L_MainWorld`는 그대로 유지한다. 이전 월드는 `/Game/World/MainWorld/Legacy/Maps/L_StarterRegion`, 검토용 맵 4개는 `/Game/World/MainWorld/Review/Phase2`로 이동했다. 이전 월드는 현행 맵과 병행 저작하는 맵이 아니라 보존본이다.

일반 제작 원본 41개는 `Content/World/MainWorld/Source`에 위치한다. 관련 스크립트·테스트·문서 91개의 물리 경로를 갱신했고, 명시된 에셋/이전 맵 경로도 26개 도구와 Source JSON 10개에서 갱신했다. Source JSON 33개 전체의 구조·좌표·수치·안정 ID는 보존하면서 경로만 변경했음을 대조했다. 높이·PNG 8개 원본의 SHA256은 이동 전과 동일하다. `StarterWorld.*`, `HM_MainWorld.r16` 파일명, `Starter.Lv1_30`와 기존 제작 태그는 안정 계약이므로 그대로 유지한다. 초기 생성기의 지원/소유 저장 제한을 없애거나 MainWorld 전체 재생성기로 전환하지 않았다.

World Partition 전용 `WorldPartitionRenameDuplicateBuilder -Rename`으로 이전 월드의 외부 액터 2,717개와 외부 오브젝트 77개를 함께 이동했다. 목적지는 새 Legacy 경로로 사전 확인했고 현행 맵을 덮어쓰지 않았다. 검토용 비-WP 맵은 AssetTools로 이동했다. 파일 탐색기 이동이나 바이너리 치환은 사용하지 않았다. 편집기 최근 맵/뷰포트 설정의 경로도 기존 값을 보존하며 갱신했다. 프로젝트 C++·Config는 변경하지 않았다.

텍스처 6개의 재임포트 원본 경로를 에디터 API로 갱신했고 변경 전후 실제 픽셀 지문이 일치했다. 두 월드 지형의 높이 원본 경로는 표준 에디터 SET 명령으로 해당 액터의 파일 경로 필드만 갱신·저장했다. 지형 재임포트나 높이/레이어 재생성은 하지 않았다. 새 프로세스에서 두 경로가 현재 `MainWorld/Source/HM_MainWorld.r16`의 실제 파일로 해석됨을 확인했다. 높이 원본 지문은 `bc3a7957a44629beb807d4a453443e9d02e48981be0cfb93d5054432c077f8c8`로 동일하다.

최종 재로드에서 이전/현행 월드의 액터 이름·클래스·라벨·그리드·공간 로딩·데이터 레이어 목록과 액터 수 각각 2,717개를 확인했다. 이전 월드의 편집기 bounds 캐시가 갱신된 433개는 이동 전 Git 저장본과 실제 액터/컴포넌트 변환·태그·메시·재질 및 인스턴스 4,046개의 변환을 별도로 대조해 차이 0을 확인했다. 초기 검사기의 `str(Guid)`는 GUID 값이 아니라 파이썬 래퍼 주소이므로 비교에서 제외했다. 검사용 export task 참조를 남겨 월드 전환 시 검사 프로세스가 종료된 것은 참조 수명을 고쳐 재검증했으며 저장된 에셋 복구나 원복이 필요한 문제가 아니었다. 초기 실패 기록도 로컬에 보존한다.

최종 엔진 검사는 종료 코드 0, 미해결 오류 0, 로그 오류/경고 0이다. 검토 맵 4개·기존 자산 redirector 35개·이전 월드 redirector와 재임포트 경로를 확인했고, 관련 순수 Python 회귀 8개와 변경 스크립트 84개의 구문 검사도 통과했다. 전체 WP 셀 스트리밍·PIE·Cook·성능/최종 아트 검증은 하지 않았다.

`Content/World/Starter`에는 실제 제작 원본이나 실제 맵 없이 호환 redirector 36개만 남아 있다. 기존 소비자의 옛 경로를 보호하므로 폴더째 삭제하지 않는다. 이 36개를 없애려면 해당 소비자들의 별도 참조 재저장·fixup 검증이 필요하다. 이번 요청에서는 전체 프로젝트 redirector 정리를 확장하지 않았다. 실행/대조 근거는 로컬 `Saved/Maintenance/MainWorldConsolidation_20261019`이며 Git 제외다. 커밋·push는 하지 않았다.

## 2026-10-19 환경 폴더 통합과 Starter 보존

후속 사용자 요청으로 `AncientTempleRuins`, `ElderboomVillage`, `Megascans`, `Meshes`, `Materials`, `ThirdParty`, `PhysMat`, `ProceduralNaturePack`의 환경 자산과 `LevelPrototyping`의 일반 환경 자산을 합쳐 2,195개 이동했다. 파일 탐색기 이동이나 바이너리 문자열 치환 없이 설치 엔진의 `AssetTools.rename_assets`와 에디터 저장 API를 사용했다. 공급자/하위 구조를 유지했고 목적지가 겹치는 59개는 `Map_Resource/Variants/ProjectExisting`에 별도로 보존했다. 이름이나 파일 차이만으로 합치거나 덮어쓰지 않았다.

기존 소비자 228개 패키지도 엔진이 참조를 갱신·저장했다. 제작/검증 Python 19개, Source JSON 7개, 컬렉션 3개의 경로를 갱신했다. JSON의 장소 ID·좌표·기타 수치와 컬렉션의 구성원 수는 유지했다. 보존된 H09 사전 조사 자료의 구 경로 키도 새 제작 경로와 대응하도록 했다. C++·Config·실제 맵·외부 액터 파일은 변경하지 않았다. 월드 전용 자산, 상호작용 기능 자산, 효과 공유 노이즈는 각 소유 위치를 유지한다. 반입 당시 공유 자산 1,710개 중 1,707개가 이동했고 기능별 위치를 유지한 3개는 컬렉션에서 계속 찾는다.

`Content/World/Starter`는 삭제하지 않는다. 높이맵·배치/제작 입력을 포함하는 Source 41개 파일, 이전 맵 5개, redirector 35개가 남아 있다. 일부 redirector는 MainWorld 외부 액터의 현재 참조 경로다. 높이 원본 SHA256은 `bc3a7957a44629beb807d4a453443e9d02e48981be0cfb93d5054432c077f8c8`로 동일하다. 이번 이동의 옛 경로에도 필요한 redirector 1,807개를 보존했으며 일괄 삭제하지 않았다.

독립된 새 엔진 프로세스에서 목적지 2,195개의 등록·클래스·기존 소비자 연결과 컬렉션 6개를 검증했다. 최초 엄격한 직접 의존성 비교의 382개 차이는 재질 인스턴스 38개의 실제 속성이 이전 Git 저장본과 모두 일치하고 부모 재질을 통한 연결도 전부 존재함을 확인해 상속 캐시 차이로 분류했다. 마스터 재질 4개의 정규화 속성 지문도 기존 검증값과 일치했다. 추가 13개 비교 중 6개는 속성이 동일했고, 나머지는 엔진 재저장/빌드 메타데이터 차이를 별도로 확인했다. 텍스처 3개는 실제 PNG 픽셀 지문과 임포트 파일·노드·파이프라인 정보가 동일했고, 메시 8개는 원본 형상·LOD·충돌을 포함한 FBX 비교가 일치했다.

엔진 빌드 중 지피 메시 4개의 비사용 재질 슬롯이 자동 정리된 것은 기존 이관에서 검증한 `StaticMeshEditorSubsystem` 방식으로 원래 슬롯명·임포트 이름·재질과 LOD별 슬롯 연결을 복구했다. 주 프로젝트의 새 프로세스에서 4개 모두 재로드 확인했다. 최종 검증은 종료 코드 0, 미해결 참조 오류 0, 로그 오류/경고 0이다. 대표/경고 대상 자산 50개와 MainWorld·Starter·ThirdPerson 월드 에셋 3개의 로드를 확인했으며, 전체 WP 셀 스트리밍·PIE·Cook·성능 검증은 하지 않았다. 기존 메인 월드 전환의 나머지 보류 조건을 완료 처리하지 않는다.

실행 영수증·초기 엄격 비교와 차이 해명·이전 Git 저장본 대조는 로컬 `Saved/Maintenance/EnvConsolidation_20261019`에 보존한다. 이는 Git 제외 경로이며 영구 인계 요약은 이 문서와 디자이너 가이드다. 커밋·push는 수행하지 않았다.

## 2026-10-19 후속 스크립트 정리

사용자가 남은 변경점과 이미 커밋된 불필요한 실행기를 정리하도록 요청했다. WorldAuthoringLibrary C++ 두 파일은 HEAD와 코드가 같고 LF/CRLF만 달라, 원형 보관 후 HEAD 바이트로 맞췄다. 코드 기능 변경은 없다. 미커밋 `Build-StarterMountainHLOD.py`는 Starter 전용 산악 시험 파일로 퇴역했으며 현재 HLOD 해결 도구로 실행하지 않는다.

고정 세션 감사 기록 생성기 9개와 완료된 Foley/EarlyTransition 이행 래퍼 4개를 선별 퇴역했다. 실제 실행 호출·에디터 시작 설정·CI 연결 및 보존된 결과를 확인했고, native API·현행 제작/검증 도구·영구 테스트·과거 검증 결과와 지문은 유지했다. 상세 구분은 [감사 도구 안내](../../Scripts/Audit/README.md)를 따른다. 원형과 SHA는 로컬 `Saved/Maintenance/ScriptRetirement_2026-10-19`에 보존하며, 기존 커밋 파일은 Git `e8fe6d0d`에서도 복구 가능하다. 이번 정리는 월드·높이·C++ 동작·에셋을 변경하지 않는다.

정리 검증은 기존 Python 16개·래퍼 모의 11조건·provenance 6조건 통과다. 퇴역 파일의 실행 코드 참조와 새로 생긴 문서 링크 오류는 0개이며 Source·Config·Content diff는 비어 있다. 과거 JSON 지문과 기존 문서 문제는 변경하지 않았고, 에디터/UBT는 실행하지 않았다.

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

- 현행 맵: `/Game/World/Starter/Maps/L_StarterRegion`. 약8×8km(8,064m), 4033² 높이 표본/간격2m. 원본 `Content/World/MainWorld/Source/HM_MainWorld.r16` SHA256은 `bc3a7957a44629beb807d4a453443e9d02e48981be0cfb93d5054432c077f8c8`이다. 이번 비주얼 복구에서 높이를 수정하지 않았다.
- [v5 후속 지시서](Project_J_World_Visual_Quality_Recovery_Codex_v5.md), [v4 공간·확장 기획](Project_J_8x8km_WorldGen_Completion_Expansion_v4.md), [목표 월드맵](초반%20지역%208×8km%20판타지%20월드맵.png)을 참고한다. 사용자 기획문서·레퍼런스는 정리 대상에서 제외했다.
- 안정 장소20곳, 기존 C++ 시스템과 월드 정의, 기존 PCG 나무69,692그루를 유지한다. [ID 대응표](../WorldGen/01_EXISTING_TO_TARGET_MAPPING.csv), [월드 구조](../WorldGen/05_WORLD_ARCHITECTURE.md), [공간 기능 연결](../WorldGen/07_GAMEPLAY_SPATIAL_INTEGRATION.md)이 기존 계약이다. 첫 공간 배치20/20은 최종 아트·게임플레이·성능 합격을 뜻하지 않는다.
- 정착지9·사냥터9·보스2의 Source 배치는 `Content/World/MainWorld/Source/MainWorld.locations.json`, `settlements.json`, `hunting.json`, `bosses.json`과 Native 저장본을 기준으로 한다. v4 기획 ID와 StableID를 임의로 통일하지 않는다.
- 에셋은 사용자 제공 Unreal Projects 라이브러리에서 채택하며 GASP는 제외한다. 필요하면 비5.8 에셋을 별도5.8 `assets` 프로젝트에서 조회한다. 공급자 원본과 실제 월드 참조 에셋을 미참조 검색만으로 삭제하지 않는다.

## 저장된 현재 상태

- 수도는 기존33필지·6도로에 주택10채·기초·계단과 약312m 가로를 추가한43필지·7도로 상태다. 거대한 성곽 대도시를 제작 완료한 상태가 아니다. 기존 마을의 배치 어색함도 남아 있다.
- 기존 PCG 숲170타일의 높이 경계를 지형 범위로 줄이고 Nature 루트를 실제 배치 높이로 옮겼다. 기존 인스턴스의 월드 변환·수량을 보존했다. PCG는 수동 GenerateOnDemand 제작 흐름이며 자동 에디터 재생성은 꺼져 있다.
- Nature 최초 추가 나무2,701그루 중 Spruce2,097그루를 PN full01/03으로 교체했다. 프로젝트 소유 잎·원경 MI4개로 조색했고 공급자 원본은 유지했다. 이후 북동부 Source `[4992,5120,7552,7552]m`에10,365그루를 추가했다. 현재 Nature 나무13,066그루, PCG와 합계82,758그루다. 개별 장소의 수동 나무는 이 합계 밖이다. 추가한 작은 바위61개는 유지했고 시각적으로 실패한 반복 암벽은 제거했다.
- 이 식재는 북동부 샘플 보강이다. 사용자가 보고 있던 앞쪽 산을 포함해 전체 산림이 처리된 것은 아니다. 수목이 점처럼 흩어진 곳, 비어 있는 중턱, 둥글고 단순한 산 윤곽이 남아 있다.
- `M_StarterTerrain_Biome`의 지표 표현, 암석 거리별 샘플링과 법선, 풀·양치류 팔레트를 수정했다. 마지막 수정은 고도620m부터 높아지던 암석 비중을 조정하고, 기존 Placement 마스크 밖의 산림 가능 완경사에도 개선 표현을 적용했다. 원본 높이와 PCG 생성 마스크는 바꾸지 않았다. Grass 중원경 전환은 아직 품질 미달이다.
- Grass variety 거리 범위는180~300m로 조정했다. `Config/DefaultScalability.ini`에는 Low/Medium 밀도 바닥값과 텍스처 풀 단계값을 작성했다. 설정 파일의 재시작 적용은 미검증이다. 마지막 실제 에디터는 Epic3/ScreenPercentage100/StreamingPool2560이었다.

## 원거리 수목·HLOD의 실제 문제

`S00_Tree_25_28_34`는 별도 HLOD layer=None이고 최대 표시 거리0이다. 반면 숲 인스턴스에는4km component cull이 있어 전체 로드된 에디터 원경에서 차이가 생길 수 있었다. 마지막에 큰 나무82,758그루의 component cull을0/0으로 저장하고 기존 PCG descriptor3개도 맞췄다. Source `MainWorld.forest-streaming.json`의 `tree_instance_cull_cm`을 제작 소비자들이 사용한다. Grass·관목 거리와 원본 WP 로딩768m는 유지했다. 실제 게임에서 원본 언로드와 HLOD 전환이 검증됐다는 뜻은 아니다.

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
