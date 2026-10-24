# 캐릭터·의상·장비 LOD 제작

기존 LOD를 보존하면서 **LOD가 없는 에셋만 공통 설정으로 생성·검사**하는 Editor 제작 경로다. 런타임에서 메시를 축소하거나 임포트 즉시 모든 의상에 같은 축소를 강제하지 않는다. 기존 body/의상/무기 표시·리타깃·장착 구조를 그대로 사용한다.

무기 텍스처의 2K/1K 해상도·압축·스트리밍 기준은 [무기 메시·텍스처 제작 규격](../../Combat/Authoring/Weapon_Asset_Standards.md)에서 관리한다. 이 문서는 지오메트리 LOD를 담당한다.

## 공통 데이터와 실행 책임

2026-10-24 대검 의상7개는 별도의 Nanite 시험을 적용했다. 몸체 LOD0–2와 망토 Cloth는 유지하며 기존 체인을 전역 교체하지 않는다. 적용 범위·성능과 투명 재질 제한은 [스켈레탈 Nanite 비교](../../Performance/Reports/ProjectJ_Character_Nanite_2026-10-24.md)를 따른다.

현재 기본 체인은 **LOD0–2**다. 2026-10-16 사용자 검토로 현재 GreatSword_Woman의 LOD1/2를 채택했다. LOD3–4는 추가하지 않으며, 이후 원거리 geometry가 실제 GPU 병목으로 남고 전환·외형·접촉 비교에서 이득이 확인될 때만 검토한다. 이 결정은 모든 새 캐릭터·의상·헤어의 자동 생성이나 품질 승인을 뜻하지 않는다.

- 공통 설정: `/Game/DataAssetSets/Editor/LOD/DA_MeshLOD_Default`. Editor 전용 `Project_JMeshLODSettings`가 **native USkeletalMeshLODSettings를 소유**하고 정적 장비용 native `FStaticMeshReductionOptions`도 함께 저장한다. native class의 비공개 export 제한 때문에 상속하지 않고 구성한다.
- SkeletalLODs는 native 설정, StaticMeshLODs는 정적 메시 설정이다. 기본 LOD1/2의 목표 삼각형은50%/25%, 화면 크기는0.35/0.18이다. 다른 제작 묶음은 설정 에셋을 복제해 선택할 수 있다. 설정 변경이 이미 제작된 메시를 소급 재생성하지 않는다.
- LOD0에는 공통 설정을 적용하지 않는다. skeletal influence 한도는 원본을 유지하며 뼈 제거·bake pose·특정 skeleton/section 우선순위가 있는 공통 설정을 거절한다. native per-level 설정만 새 LOD에 복사하고 MinLOD/streaming 정책이나 런타임 profile 참조를 새로 연결하지 않는다.
- 이미2개 이상 LOD가 있으면 그 전체 체인을 보존한다. 목표 개수가 더 많아도 자동 추가/재생성하지 않는다. 재임포트 후 기존 LOD의 품질·유효성을 전수 승인한다는 뜻은 아니다.
- skeletal은 엔진 `FLODUtilities`, static은 엔진 source model·reduction 설정과 메시 빌드를 사용한다. 별도 reducer·runtime manager·캐릭터별 생성 코드를 만들지 않는다.

## 추가할 때의 순서

1. 임포트된 에셋을 Content Browser에서 선택하고 **Project J → Inspect Mesh LODs**로 검사한다. 선택은최대32개이며 폴더 전체 생성은 하지 않는다.
2. 엔진 Data Validation도 공통 설정의 `ValidationRoots` 안에서 읽기 전용 진단을 제공한다. 기본은 `/Game/Characters/`, `/Game/Weapons/`, `/Game/Equipment/`와 현재 무기 폴더 `/Game/Anim_Assets/Great_Sword/Weapon/`다. 다른 제작 폴더는 공통 설정에 추가한다. 검증은 생성·저장을 실행하지 않으며 의도된 단일LOD도 자동 실패로 만들지 않는다.
3. 일반 몸체·일반 의상·정적 장비의 표면 메시인지 검수한 뒤 `Surface`로 명시해 Preview한다. hair/fur/card를 파일 이름으로 추측해 자동 분류하지 않는다. 애매하면 `Unclassified`나 `Hair`로 유지한다.
4. 에디터·빌드·검증이 정상 종료된 뒤 Apply한다. 설정 에셋을 지정하지 않으면 공통 기본값을 사용한다.
5. 저장된 에셋을 다시 열어 전환·재질·변형·접촉을 확인하고 기존 장착 경로에 연결한다. 생성 성공은 제작 품질 승인과 다르다.

저장소 루트에서 실행 예:

```powershell
$meshes = @('/Game/Characters/NewBody/SK_NewBody.SK_NewBody',
            '/Game/Equipment/NewSet/SM_NewSword.SM_NewSword')
./Scripts/Authoring/Invoke-MeshLODAuthoring.ps1 -Action Inspect -Assets $meshes
./Scripts/Authoring/Invoke-MeshLODAuthoring.ps1 -Action Preview -Role Surface -Assets $meshes
./Scripts/Authoring/Invoke-MeshLODAuthoring.ps1 -Action Apply -Role Surface -Assets $meshes
```

프로젝트 EngineAssociation의 설치 경로를 찾지 못하면 `-EditorExecutable`에 해당 엔진의 `UnrealEditor-Cmd.exe`를 지정한다. 다른 설정은 `-Policy /Game/.../DA_Other.DA_Other`로 선택한다. 공통 설정은 저장소에 포함하므로 일반 사용자가 매번 생성할 필요가 없다. 누락된 새 환경에서만 `-Action CreatePolicy`를 사용하며 기존 설정은 덮어쓰지 않는다.

직접 호출 진입점은 `-run=Project_JMeshLOD -unattended`다. `Action`, `Assets`(쉼표 구분 명시 목록), `Role`, `Policy`, `Report`를 받는다. raw commandlet 종료0은 완료,2는 검수 대상 보존,1은 오류다. 스크립트는 검수 대상을 경고하고 실제 종료 값은 `process.json`에 남긴다. 보고서의 입력 수와 실제 처리 수가 맞지 않으면 스크립트도 실패한다.

## 자동 생성에서 제외하는 경우

| 경우 | 처리 |
| --- | --- |
| 기존 LOD 체인 | 보존. 별도 재임포트/전환 품질 검수 |
| 활성·미바인딩 Clothing asset, Cloth 바인딩 | 원본 보존·검수. native Clothing LOD 매핑·전환/시뮬레이션 검수 |
| Groom·Groom Binding | native Groom LOD·binding 제작 경로 사용 |
| 카드 헤어·퍼·WPO 의상 등 | `Hair` 또는 공통 설정의 `ManualReviewAssets`로 명시. 일반 표면 자동 축소에서 제외 |
| morph target | 보존·검수. native morph remap과 표정/극단 변형 검수가 필요하며 이 경로가 임의 삭제하거나 자동 remap하지 않음 |
| alternate skin weight profile·mesh deformer | 보존·검수. 해당 소비/LOD 매핑 확인 |
| Nanite·기존 skeletal LODSettings·static LODGroup | 기존 native 정책 보존·검수 |
| 단일LOD에 남은0이 아닌 플랫폼/품질 MinLOD | 생성 후 최소값이 새로 활성화되어 로컬 품질을 바꿀 수 있으므로 보존·검수 |
| 너무 작은 메시·실제 삼각형 감소가 없는 후보 | 생성 검증 실패. 단계 수만 늘린 결과를 저장하지 않음 |
| 미저장 변경·읽기 전용·분리 sidecar 패키지·redirector·중복/프로젝트 밖 경로 | 자동 저장 거절 |

새 에셋의 의미·WPO·custom deformer를 이름이나 형상만으로 완전히 알아낼 수 없다. `Surface` 선언은 제작자의 일반 표면 검수를 뜻하며 모든 예외의 자동 탐지 보장이 아니다. 검수된 의상과 헤어를 임의로 같은 그룹에 묶지 않는다.

`ManualReviewAssets`에 명시한 에셋은 이미 native 제작 경로를 선택한 대상이므로 저장 시 반복적인 일반 LOD 경고 대신 정보 메시지를 남긴다. 자동 축소 제외와 inspector/commandlet의 `Review` 판정은 유지하며 시각 품질 승인을 뜻하지 않는다. 명시하지 않은 단일 LOD/Cloth 메시의 검토 경고, 유효한 LOD0 누락 오류, 0이 아닌 플랫폼/품질 MinLOD 경고는 유지한다. 대검 `cloak`은 세 Cloth section을 직접 제작하므로 공통 설정에서 이 경로로 명시한다. 메시 이름을 하드코딩한 경고 예외나 전체 검증 경고 비활성화를 사용하지 않는다.

이 경로의 `Surface`는 표시용 geometry다. canonical 게임플레이 pose를 생성하는 메시의 LOD나, 실제 ABP/PP graph의 IK·RigidBody `LODThreshold`까지 에셋 단독으로 보장하지 않는다. 연결 후 실제 node의 LOD별 실행·physics 참여·소켓을 확인한다. 동일 mesh를 다른 graph나 독립적인 장비 액터에 사용하는 경우에도 해당 소비 계약을 검수한다.

## 저장·실패·품질 보존

Preview는 transient 복제본만 생성하고 원본을 바꾸지 않는다. Apply는 **격리된 unattended commandlet**에서만 실행한다. 복제본을 먼저 검증하고 원본을 백업한 뒤 같은 native 경로로 생성한다. 보호 값 비교와 임시 패키지 저장이 성공하고 원본 파일 지문도 같을 때만 선택한 패키지를 교체한다. 파일 교체 실패 시 백업 복구·지문 확인을 시도하고 보고서에 결과를 남긴다. 여러 에셋은 개별 commit이며 일괄 transaction이 아니다. 강제 종료/전원 장애나 복구 실패는 보고한 원본 백업으로 복구해야 한다. live editor의 Save All에 실패한 반쪽 결과를 남기지 않는다.

`Saved/MeshLODAuthoring`에 실행별 보고서·실제 process 종료 값·원본 백업이 남는다. 운영 원본·관련 없는 에셋·Config·공유 Cook 저장소를 이 도구가 일괄 수정하지 않는다. Cook 자체를 실행하지 않는다.

검사는 LOD0 정점/법선/UV/색/인덱스·가중치·section bone map·required/active bone 배열·reference skeleton·재질/소켓·물리 참조·원본 제작 설정을 보호하고, 실제 LOD 수·삼각형 감소·정확한 required-bone 배열을 확인한다. 엔진이 새 LOD를 반영해 다시 계산하는 **자동 UV streaming density**는 authored material/slot과 구분한다. 수동 density override는 보존한다. 수치 검사가 시각 품질을 대신하지 않는다.

공용 표시 몸체와 LeaderPose 의상은 기존 로컬 품질 경로를 사용한다. 2026-10-24 무기 예산 조정 후 공용 `Project_JWeaponPresentationActor`의 정적 무기는 **로컬·원격 모두 엔진의 화면 크기 기반 LOD**를 사용한다. 로컬 소유만으로 LOD0에 고정하던 대여 상태와 Controller 변경 delegate·등록/해제 경로를 제거했다. authored/다른 시스템의 명시 forced LOD는 유지하며 추가 actor tick·RPC·복제 상태는 없다. 이 변경은 몸체·LeaderPose 의상이나 canonical 게임플레이 pose/socket 정책을 바꾸지 않는다. 독자적인 장비 액터·Groom·LODSync·컴포넌트 override는 해당 표시 경로의 품질 계약도 별도 확인한다.

근접 로컬 품질, 원격 LOD 전환, 장비/망토 관통·손발 접촉, 뼈/소켓, 머티리얼/WPO, morph 극단값, 화면 복귀·재장착·avatar 교체를 실제 연결에서 검수한다. 서버 CMC·GAS·SSR 판정은 canonical pose/socket 권위를 유지하며 시각 LOD에 맞춰 판정 정확도를 낮추지 않는다.

## 에디터에서 LOD 비교와 적용 판단

검토 레벨은 `/Game/Developers/LODReview/L_MeshLODReview`다. Content Browser의 개발자 콘텐츠 표시를 켜고 `L_MeshLODReview`를 연다. 기존 게임 레벨·기본 시작 맵은 바꾸지 않았다.

- `BareFront`: 몸체만 같은 크기·기준 자세로 LOD0/1/2를 비교한다.
- `DressedFront`: 기존 의상8개를 붙인 정면 비교다. 의상 자체의 LOD 데이터는 그대로다.
- `DressedBack`: 같은 의상의 뒷면과 망토를 비교한다.
- World Outliner의 `Camera_BareFront`, `Camera_DressedFront`, `Camera_DressedBack`를 우클릭해 **Pilot**하면 해당 줄을 같은 거리에서 볼 수 있다. `Camera_Overview`는 전체 배치다. 몸체 액터를 선택하고 `F`로 집중하거나 View Mode의 Wireframe으로 윤곽·삼각형을 비교한다.

Play 없이 에디터 뷰포트에서 확인한다. 검토 맵에만 고정 노출·환경광·줄별 배경을 넣어 검은 의상도 비교할 수 있게 했다. View Mode의 Unlit도 재질/조명의 영향을 줄여 표면을 확인하는 데 쓸 수 있다. 각 줄은 원근 카메라이므로 가장자리 시점 차이는 고려하고, 미세한 차이는 같은 메시를 Skeletal Mesh Editor에서 열어 LOD를 차례로 전환하며 같은 시점에서도 확인한다.

각 몸체의 SkeletalMeshComponent에만 Forced LOD Model을1/2/3으로 설정했다. 각각 실제 LOD0/1/2이며 카메라가 가까워져도 비교 단계가 바뀌지 않는다. 소스 메시의 LOD·screen size·MinLOD나 전역 CVar를 바꾼 것이 아니다. 장면 표지의 REFERENCE는 기존LOD0, ADOPTED는2026-10-16 사용자가 채택한 현재 몸체LOD1/2를 뜻한다. 미래의 다른 에셋 품질 승인 표시는 아니다.

이는 **정적인 geometry 비교 장면**이다. 장면 컴포넌트의 Post Process ABP·물리 시뮬레이션은 꺼서 기준 자세를 맞췄다. 실제 게임플레이·리타깃/손발 IK·Cloth 전환 품질 승인을 대신하지 않는다. 무기는 원본 표시만 두었고 추가 LOD를 생성하지 않았다.

현재 몸체에는6차 작업에서 생성한LOD1/2가 이미 들어 있다. **2026-10-16 사용자 시각 검토와 실제 LOD/물리 근거 대조 후 이 몸체의 LOD1/2를 채택했다.** 기존 생산 메시의3LOD와 원격 자동 선택 경로를 그대로 사용하며 로컬은LOD0을 유지한다. 재생성·새 축소 정책·서버 판정 변경을 추가하지 않았다. 다른 캐릭터·의상·헤어의 새 LOD는 기존 예외와 에셋별 검수를 거친다.

그날 에디터에서 native 메시 LOD 배색을 확인했으며 화면 오른쪽LOD0은흰색, 가운데LOD1은빨강, 왼쪽LOD2는초록이었다. native 조회 정점 수는각각30,070/15,788/8,303개다. 모양이 비슷해도 같은LOD를 세 번 표시한 결과가 아니다. 배색은 확인 후 일반 라이팅 보기로 복원한다.

물리 판정은 이 정적 장면이 아니라 기존 `ProjectJ.Retarget.SecondaryPhysicsEquivalence`의 실제900표본을 사용했다. 실제LOD1/2에서도 동적 바디30개가 활성화됐고 전체 물리 actor91개·required bones338개·원래 PA/ABP를 유지했다. 위치 차이0cm, 회전 최대약0.000003415도, scale 차이0이었다. 근거 실행 이후 생산 메시·3개PA·ABP·Retargeter의 파일 지문도 일치했다. 이 결과는 시험 조건의 동등성이며 모든 체형/새 에셋/극단 동작의 시각 품질 보장은 아니다. **이번 LOD는 표시 geometry/GPU 비용을 줄이며 망토 물리 계산량 자체를 줄이지 않는다.**

레벨 재현 스크립트는 `Scripts/Authoring/Create-MeshLODReview.py`다. 별도 에디터의 `-ExecutePythonScript`와 명시 `-ProjectJCreateLODReview`로 생성하고, 기존 레벨이 있으면 덮어쓰지 않는다. `-ProjectJVerifyLODReview`는 저장본을 읽어9개 몸체의 강제LOD·48개 의상의 LeaderPose 연결과 원본 파일 보존을 검사한다. 생성/검증 전 미저장 작업을 정리하고 기존 에디터·빌드가 정상 종료된 뒤 실행한다. 검토 맵은 개발자 콘텐츠이며 출시용 맵/Cook root로 새 연결하지 않았다.
