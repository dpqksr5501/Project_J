# 외부 에셋과 라이선스

조사 기준: **2026-11-01** · [프로젝트 소개](README.md) · [정확한 검토 경로](Docs/Reference/Asset_Review_Paths_2026-11-01.md) · [에셋 기여 안내](CONTRIBUTING.md#에셋-출처와-제출-조건)

Project J에는 자체 코드와 외부 콘텐츠가 함께 들어 있다. 이 문서는 원 제작자·취득 출처·이용 조건과 실제 공개 상태를 구분해 기록한다. 현재는 수익화를 목적으로 하지 않는 학습·기술 연구·포트폴리오 프로젝트이며, 일부 콘텐츠는 향후 적절한 권리를 확보한 에셋으로 교체할 계획이다.

**추출 캐릭터의 공개 Git/LFS 재배포 허락은 미확인이다.** 출처 표시·비상업적 목적·향후 교체 계획은 이용 허락을 대신하지 않는다. 아래 기록은 권리 확인 현황이며, 모든 외부 콘텐츠가 허가되었거나 침해에 해당한다는 판정이 아니다.

## 1. 자체 코드와 외부 콘텐츠

프로젝트 작성자가 권리를 보유한 자체 소스코드는 기존 [MIT LICENSE](LICENSE)를 따른다. 외부 모델·모션·텍스처·음향·폰트·샘플·엔진/플러그인 코드의 권리는 해당 권리자에게 있으며 각각의 유효한 조건이 적용된다. 이 안내는 기존 LICENSE나 과거 배포 조건을 변경하지 않는다. 개발자의 현재 비상업적 목적을 MIT 코드 이용자에 대한 새 제한으로 부과하지도 않는다.

리깅·리타기팅·LOD 제작·이름 변경·포맷 변환은 원본 콘텐츠의 출처와 권리를 바꾸지 않는다. 자체 제작으로 보이는 리그·무기·Data Asset도 포함된 외부 데이터와 제작 입력을 확인한 뒤 분류한다.

| 상태 | 의미 |
| --- | --- |
| 출처 확인 | 작성자 확인·제작 기록·공식 자료로 취득 출처를 식별함. 이용 허락 확인과는 별개 |
| `Unverified` | 원 제작자·상품·취득 조건·개별 파일 대응 등 필요한 근거가 부족함 |
| `Needs Review` | 출처나 적용 후보 약관은 있으나 실제 사용·수정·배포 방식에 대한 확인이 필요함 |

## 2. 캐릭터·구매 에셋·모션

폴더는 관련 파일의 검토 범위다. 같은 폴더의 모든 파일을 동일 출처·라이선스로 분류한 것은 아니다. 현존 파일과 이전 경로는 [경로 부록](Docs/Reference/Asset_Review_Paths_2026-11-01.md)에 있다.

| ID · 콘텐츠 | 경로·출처 | 권리 확인과 다음 조치 |
| --- | --- | --- |
| **A01 · Spellblade 캐릭터·의상·연관 무기** | `Content/Characters/Spellblade/`, `Content/Spellblade/`, `Content/Weapons/Spellblade/`, `SourceArt/Characters/Spellblade/UE5/`, `SourceArt/Weapons/Spellblade/`. 원 게임 **Black Desert**, 원 권리자 **Pearl Abyss**, 추출·업로드 **P3DM `:Dim0s`**. 작성자가 출처 대응 확인 | **`Needs Review`**. [Dark Knight 게시물](https://p3dm.ru/files/characters/human/19623-dark-knight.html)은 `Personal use only`로 표시. 원 권리자의 외부 게임 사용·public Git/LFS 재배포 허락은 미확인. 향후 배포용 콘텐츠의 교체 또는 별도 허락 우선 검토 |
| **A02 · Wyvern** | `Content/Mount_Assets/Wyvern/`의 모델·모션·텍스처. 작성자가 구매 사실 확인 | **`Unverified`**. 판매자·상품 URL·취득일과 약관 필요. 구매 사실만으로 원본 공개 권한을 확인할 수 없음 |
| **A03 · Epic 샘플 유래 모션·캐릭터·제작 자원** | `Content/Characters/Mannequins/`, `UEFN_Mannequin/`, `UE5_Mannequins/`, `Content/GASP58Native/`, `Content/Misc/`. [라이브러리 기록](Docs/Animation/Authoring/Player_Animation_Library.md)은 Epic Game Animation Sample과 기존 프로젝트 라이브러리를 출처로 설명. [공식 상품](https://www.fab.com/listings/880e319a-a59e-4ed2-b268-b32dac7fa016) | **`Needs Review`**. 취득 당시 조건과 개별 파일 출처 확인. 조회한 상품 페이지의 License terms는 `-`여서 적용 조건을 확정하지 못함. Examples·별도 Licensed Content·UE-Only 등의 적용 여부 구분 |
| **A04 · 비활성 모션 원본 ZIP** | `Archives/AnimationSources/UEFN_2026-10-21/Originals.zip`, `Archives/AnimationSources/Neutral_2026-10-22/Originals.zip`. [보관 안내](Archives/AnimationSources/README.md)와 각 manifest에 원본 package·지문 기록 | **`Needs Review`**. 복원용 원본의 공개 조건 확인. ZIP·LFS·Content 밖 보관도 같은 검토 대상 |
| **A05 · Foley·오디오** | `Content/GASP58Native/Audio/`, `Content/Audio/Foley/`, `Content/Audio/Mix/`. [연결 기록](Docs/Animation/Diagnostics/GASP_Foley_Investigation_2026-10-08.md)은 샘플 음원 사용 설명 | **`Needs Review`**. 개별 음원·MetaSound·mix의 원저작자와 원본/수정본·빌드·영상 조건 확인 |
| **A06 · 전투·탑승 모션** | `Content/Anim_Assets/Spellblade/`, `Content/Anim_Assets/Great_Sword/`, `Content/Anim_Assets/Riding/`, `Content/Spellblade/Animation/` | **`Unverified`**. 클립별 원본 라이브러리와 이용 조건 필요. 캐릭터와 같은 출처로 일괄 분류하지 않음 |
| **A07 · Basic 대검** | `SourceArt/Weapons/Greatsword/Basic/`, `Content/Weapons/Greatsword/Basic/`. [제작 기록](SourceArt/Weapons/Greatsword/Basic/README.md)은 제공된 GothicGreatsword_MMO FBX·텍스처를 입력으로 설명 | **`Unverified`**. 최초 제작·취득 경로와 권리 정보 필요. A01의 연관 무기와 별도 장비 |
| **A08 · SlashTrail** | `Content/SlashTrail_SoftTofu/` | **`Unverified`**. 원 상품·제작자·취득 조건 필요. Niagara뿐 아니라 재질·리본/노이즈 텍스처도 확인 |
| **A09 · 템플릿·맵·미디어** | `Content/ThirdPerson/`, `Content/Input/Touch/`, `Content/Level/`, `Content/World/MainWorld/`, `Content/__ExternalActors__/`, `Content/__ExternalObjects__/`, `Docs/Media/ProjectJ_Video_Thumbnail.jpg`와 비교 이미지 | **`Needs Review`**. 템플릿·프로젝트 저작·외부 파생 자원을 구분. 맵·blend의 포함 데이터, 영상·이미지의 외부 아트·상표·음원도 해당 조건 확인 |

### Black Desert 콘텐츠의 출처와 사용 범위

원 게임은 [Black Desert](https://www.kr.playblackdesert.com/)이며 관련 게임 콘텐츠의 권리는 Pearl Abyss에 있다. Project J는 Pearl Abyss가 제작·승인·후원한 공식 프로젝트가 아니다. 원본 모델·텍스처 제작과 프로젝트에서 수행한 리깅·IK·내보내기·리타기팅·런타임 통합을 구분한다.

취득 출처는 [P3DM `:Dim0s` 프로필](https://p3dm.ru/user/%3ADim0s/)과 [Dark Knight 게시물](https://p3dm.ru/files/characters/human/19623-dark-knight.html)이다. 프로필은 조사 도구에서 열리지 않았으나 개별 게시물의 원 게임·추출자·`Personal use only` 표시는 확인했다. 작성자도 캐릭터 출처를 확인했다. 업로더가 원 권리자를 대신해 현재 사용과 공개 배포를 허락할 권한을 가졌다는 근거는 확인되지 않았다.

[Pearl Abyss Fan Content Guidelines](https://www.pearlabyss.com/en-US/legal/detail?_policyNo=42)는 비상업적 팬 콘텐츠와 비공식 표시 조건 외에 제3자 제품에 IP를 포함하지 말라는 조건도 두고 있다. 면책 표시가 정책 밖의 사용을 허용하지 않으며 회신 부재는 승인이 아니라고 설명한다. 이 정책을 독립 MMORPG의 추출 모델·수정본 Git/LFS 공개 허락으로 해석하지 않는다.

| 용도 | 확인 상태 |
| --- | --- |
| 개인 학습·수정 | 개발 목적과 업로더 표시는 확인. 원 권리자의 해당 사용 허락은 미확인 |
| 원본·수정본 public Git/LFS 공개 | 허용 근거 미확인 |
| Cook된 게임 빌드 배포 | 허용 근거 미확인 |
| 영상·스크린샷·포트폴리오 | 팬 콘텐츠 정책과 실제 용도 대조 필요. 원본 파일 배포와 별도 검토 |
| 공모전·상금·상업적 활용 | 콘텐츠 라이선스와 실제 대회의 출품·홍보·배포 조건을 사전 검토 |

## 3. 환경 라이브러리

[제작 가이드](Docs/World/LEVEL_DESIGNER_GUIDE.md), [반입 기록](Docs/World/Build/01_AUDIT.md), [재사용 기록](Docs/WorldGen/02_ASSET_REUSE.md)에 외부 프로젝트에서 반입한 자원과 의존성이 정리되어 있다. 복사·이동·해시 확인 기록은 있으나 판매자·원 상품·취득 약관은 미확인이다. **아래 범위는 모두 `Unverified`**이며 상업적 이용·수정·원본/수정본 공개·빌드·공모전 조건을 개별 확인해야 한다.

| 실제 디렉터리 | 출처 단서·추가 확인 |
| --- | --- |
| `Content/Map_Resource/AncientTempleRuins/` | 제작 기록의 팩 식별자. 원 상품·권리자·취득 조건 |
| `Content/Map_Resource/Brushify/` | 기존 재사용 기록. 개별 팩·취득 조건 |
| `Content/Map_Resource/ElderboomVillage/` | ElderBoomHollowMassiveMed 프로젝트에서 반입. 원 상품과 구성 요소의 권리 |
| `Content/Map_Resource/Iceland_Environment/` | 환경 라이브러리 식별자. 원 상품·권리자 |
| `Content/Map_Resource/MWLandscapeAutoMaterial/` | 지형·식생과 포함 텍스처의 원 상품·조건 |
| `Content/Map_Resource/MedCastle/` | 메시·의존성 반입 기록. 원 상품·취득 조건 |
| `Content/Map_Resource/MedievalVillage/` | 외부 프로젝트 이름과 실제 제품을 구분해 확인 |
| `Content/Map_Resource/Megascans/` | 개별 콘텐츠의 취득 시점·경로·계정과 UE-Only/Fab 등 적용 조건 |
| `Content/Map_Resource/Nanite_Env_Bundle_1/` | 여러 하위 환경 팩의 개별 조건 |
| `Content/Map_Resource/ProceduralNaturePack/` | 자연·물 관련 원본과 의존 자원 |
| `Content/Map_Resource/SkyTown/` | 환경·소품 반입 기록. 원 상품·취득 조건 |
| `Content/Map_Resource/ThirdParty/` | PN_interactiveSpruceForest 등 하위 출처별 확인 |
| `Content/Map_Resource/UnrealNormandy/` | 메시·구름/VDB 등 구성 자원의 원본·조건 |
| `Content/Map_Resource/VillageScan/` | 스캔·텍스처·샘플 부속물의 원본·조건 |
| `Content/Map_Resource/Variants/` | 충돌 시 유지한 변형의 원본 출처 추적 |
| `Content/Map_Resource/Meshes/`, `Materials/`, `Effects/` | 여러 출처의 공통 의존 자원 가능성. 파일별 확인 |
| `Content/Map_Resource/LevelPrototyping/`, `ThirdPerson/`, `PhysMat/`, `MainWorld/` | 템플릿·프로젝트 제작·외부 파생 자원 구분 |
| `Content/Map_Resource/HM_Desert_Start.r16`, `Content/World/MainWorld/Source/` | 높이·생성 입력의 제작/데이터 출처 |

[Fab 라이선스 안내](https://www.fab.com/eula)와 [Epic Content License Agreement](https://www.unrealengine.com/eula/content)를 참고하되, 실제 취득 당시 조건·버전·개별 구성을 대조한다. 폴더명이나 현재 상품 페이지가 같다는 이유로 라이선스를 확정하지 않는다. 게임에 포함해 배포하는 권한과 원본 파일을 public 저장소에 배포하는 권한도 구분한다.

## 4. 엔진 플러그인 코드

| 경로 | 원본·수정 근거 | 상태 |
| --- | --- | --- |
| `Plugins/Mover/Source/` | [패치 기록](Plugins/Mover/PROJECT_J_PATCH.md)은 Epic UE 5.8.4 기반을 명시. `Mover/Public/MoverComponent.h`에 Epic 저작권 헤더 | `Needs Review`: 원본 포함 범위·배포 형태·수신자 조건 |
| `Plugins/NetworkPrediction/Source/` | [패치 기록](Plugins/NetworkPrediction/PROJECT_J_PATCH.md)은 UE 5.8.3 원본과 현재 5.8.4 사용을 구분. `NetworkPrediction/Public/NetworkPredictionWorldManager.h`에 Epic 저작권 헤더 | `Needs Review`: 자체 수정과 엔진 원본의 구분·배포 방식 |

[Unreal Engine EULA §5](https://www.unrealengine.com/eula/unreal)는 동일 버전의 별도 라이선스를 가진 수신자에 대한 Engine Code 배포, 공개 토론용 스니펫, Examples와 Engine Tools의 배포 조건을 구분한다. 플러그인 전체를 MIT 또는 Examples로 일괄 분류하지 않으며, 패치만 제공하는 방식도 포함된 엔진 코드와 배포 조건을 확인해야 한다.

## 5. GitHub 공개 상태와 과거 이력

2026-11-01 GitHub 조회에서 `dpqksr5501/Project_J`는 **public**, 기본 브랜치는 `main`이었다. 현재 로컬 이름은 Spellblade지만 원격에서 확인한 캐릭터 이름은 GreatSword다. 정확한 현존/이전 경로와 로컬 Git blob 표본은 [경로 부록](Docs/Reference/Asset_Review_Paths_2026-11-01.md#원격-공개-관측과-로컬-git-blob-표본)에 있다.

| 공개 경로 | 관측 내용 |
| --- | --- |
| [캐릭터](https://github.com/dpqksr5501/Project_J/tree/main/Content/Characters/GreatSword) | 메시·의상·텍스처 경로 존재. 로컬 origin/main의 `GreatSword_Woman.uasset`은 LFS 포인터 |
| [제작 원본](https://github.com/dpqksr5501/Project_J/tree/main/SourceArt/Characters/GreatSword/UE5) | blend·FBX·Comparison 경로 존재. `GreatSword_UE5_Rig.blend`는 로컬 origin/main에서 LFS 포인터. 제작 기록에는 텍스처 35개 패킹 명시 |
| [Wyvern](https://github.com/dpqksr5501/Project_J/tree/main/Content/Mount_Assets/Wyvern) | 모델·모션·텍스처 경로 존재. 로컬 origin/main의 `ABP_Wyvern.uasset`은 일반 Git blob |
| [모션 원본 ZIP](https://github.com/dpqksr5501/Project_J/tree/main/Archives/AnimationSources/UEFN_2026-10-21) | manifest와 `Originals.zip` 경로 존재. ZIP은 로컬 origin/main에서 LFS 포인터 |
| [환경 팩](https://github.com/dpqksr5501/Project_J/tree/main/Content/Map_Resource) | 위 환경 라이브러리 디렉터리 존재 |
| [Mover 코드](https://github.com/dpqksr5501/Project_J/blob/main/Plugins/Mover/Source/Mover/Public/MoverComponent.h) | Epic 헤더가 있는 소스 본문 조회 |

**경로·포인터 공개는 확인했으나 원격 LFS 실물의 익명 다운로드는 미검증이다.** 공개 경로 확인과 라이선스 승인은 별개다.

`.gitattributes`는 Unreal 패키지·제작 원본·미디어·지정 ZIP의 LFS 저장 규칙이다. 모든 과거 blob이 LFS로 변환됐거나 공개 권한이 있다는 뜻은 아니다. [GitHub LFS 설명](https://docs.github.com/en/repositories/working-with-files/managing-large-files/about-git-large-file-storage)처럼 Git 포인터와 실제 객체는 따로 관리된다.

관련 경로의 제한된 `git log --all` 조회에서 캐릭터·제작 원본·ZIP의 과거 커밋도 확인했다. 현재 파일 교체·제외 후에도 과거 커밋·태그·LFS·릴리스·첨부 등에 원본이나 수정본이 남을 수 있다. 전체 refs·도달 불가능 객체·원격 LFS·포크·배포 산출물은 전수 조사하지 않았다. 비공개 전환도 기존 공개 포크를 회수하지 않는다. [GitHub 공개 범위 안내](https://docs.github.com/en/repositories/managing-your-repositorys-settings-and-features/managing-repository-settings/setting-repository-visibility).

## 6. 단일 저장소와 PC 교체

저장소는 하나로 유지한다. 같은 public 저장소의 특정 폴더나 브랜치를 개인용 비공개 백업으로 취급할 수는 없으므로 다음 기준으로 보관 범위를 정한다.

- **공개 원본/수정본 배포 허용:** 같은 저장소와 LFS에 유지하고 필수 크레딧·조건을 기록한다.
- **프로젝트 사용은 허용하지만 원본 공개 제한:** public 저장소에는 허용 범위만 제공한다. 원본은 해당 약관이 허용하는 접근 제한 백업에 보관하고, 새 PC의 의존 버전·복원 경로를 기록한다.
- **취득·보관 권한도 불명확:** 비공개·암호화가 권한을 새로 부여한다고 보지 않고 허락 또는 대체 여부를 확인한다.

전체 프로젝트를 GitHub 하나에서 복원하는 것이 우선이면 저장소 전체 비공개도 선택지다. 이는 현재 권리 상태나 이미 공개된 이력을 해결하는 조치는 아니다. 저장소 공개 범위·파일·LFS 설정은 문서 정리와 별도로 결정한다.

백업에는 Git 이력, 필요한 LFS 실물, 제작 원본과 의존 버전을 포함한다. 외부 `lfs.storage`를 쓰면 `git rev-parse --git-common-dir`, `git rev-parse --git-path lfs`, `git lfs env`로 실제 위치를 확인한다. 외부 LFS 데이터를 임시 캐시로 삭제하거나 다른 저장소와 공유하는 전역 저장 경로로 바꾸지 않는다.

[GitHub 백업 안내](https://docs.github.com/en/repositories/archiving-a-github-repository/backing-up-a-repository)는 Git mirror와 LFS 객체 수신을 별도로 설명한다. 새 PC에서는 실제 파일·manifest 해시·엔진/플러그인 버전·프로젝트 로드·플레이·재시작을 확인해야 한다. 포인터만 든 ZIP은 완전 백업이 아니며, 현재 전체 복원·실행은 미검증이다. 구매 증빙·계정·토큰은 공개 문서와 분리한다.

<a id="replacement"></a>

## 7. 임시 콘텐츠 교체 계획

프로젝트가 발전해 공모전 출품·게임 배포·상업적 활용을 추진할 때는 권리 미확인 또는 용도 제한 콘텐츠를 **자체 제작하거나 해당 용도·배포 방식에 맞는 이용권을 확보한 콘텐츠로 교체할 계획**이다. 적절한 조건이 확인된 구매·외부 에셋까지 일괄 교체하는 것은 아니다. 일정과 예산은 미정이다.

1. **현재 공개 범위부터 확인한다.** A01 캐릭터, 엔진 원본 포함 코드, 구매 팩의 원본 공개 조건을 우선 검토한다. 향후 교체를 이유로 현재 권리 확인을 미루지 않는다.
2. **의존 묶음으로 교체한다.** 모델·스킨·의상·텍스처·모션·음향·리그·물리·파생 LOD, FBX/blend의 포함 데이터와 Blueprint·DA·맵·soft reference·Cook 연결을 함께 확인한다. 썸네일·문서 이미지·영상도 검토한다.
3. **새 콘텐츠의 권리를 확인한다.** 원본/수정본 public 공개, 빌드, 영상, 공모전·상업적 이용을 나누어 확인한다. 외주·자체 제작에서도 입력 텍스처·스캔·모션·폰트와 계약 범위를 확인한다. 추출 아트의 재가공만으로 새 권리가 확보되지는 않는다.
4. **실제 소비 경로를 점검한다.** 골격·소켓·접촉·타격·root motion·notify·복제·취소/교체/종료와 LOD/Cloth·성능을 확인한다. 아트 부하가 달라진 전후 결과를 같은 조건의 개선율로 비교하지 않는다.
5. **배포물과 이력을 구분한다.** 크레딧·라이선스·빌드 의존성을 갱신하고 대회의 심사·홍보·배포·상금 조건을 검토한다. 과거 Git/LFS·릴리스·미디어의 처리 여부는 정확한 경로·근거·영향을 정리한 뒤 별도로 결정한다.

## 8. 추가로 필요한 정보

- Wyvern·환경·VFX·모션 팩의 공식 상품 URL, 취득 시점과 적용 약관.
- Basic 대검 제작 입력의 최초 제작·취득 경로와 권리 정보.
- A01의 현재 사용·public Git/LFS 재배포에 대한 공식 적용 근거 또는 별도 허락.
- 근거 미확인 파일의 공개 범위, 허락 문의·교체 우선순위, 허용되는 독립 백업 위치.

개별 허락을 요청할 때는 원 권리자나 권한 있는 대리인에게 **외부 MMORPG에 통합한 원본·수정본을 public GitHub/Git LFS에서 다운로드·clone·fork할 수 있는 형태로 제공한다**는 점과 수정 범위·기간·배포 용도를 구체적으로 알린다. 서면 근거를 보관하고 미확인 항목을 갱신한다. 모든 작업에 별도 장부를 추가할 필요는 없으며 이 문서와 기존 제작 기록을 활용한다.

<details>
<summary>조사 범위</summary>

로컬 경로·Git 인덱스·제한된 과거 커밋의 메타데이터, 기존 제작 문서, GitHub의 public 메타데이터·일부 경로/소스, 공개 출처와 공식 약관을 확인했다. 원 배포본과 수정본의 바이트 동일성, AssetRegistry·전체 바이너리·LFS 원격 객체, 구매 계정·영수증·개별 허락, 전체 포크·태그·릴리스·Actions 산출물은 확인하지 않았다. 이 목록은 전체 콘텐츠의 라이선스 인증이나 빌드·실행 검증을 뜻하지 않는다.

</details>
