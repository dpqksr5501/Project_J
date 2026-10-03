# Project J 콘텐츠 확장 구현 가이드

이 문서의 상세 본문은 [직업·전직·콘텐츠 데이터 제작 통합 가이드](Content_Authoring_System.md#expansion)에 통합했다. 설정·예외·수식·검증 내용을 유지하며, [이전 전체 원문](../../Archive/SourceDocuments/2026-10-03/Gameplay/Authoring/ContentExpansionGuide.md)도 보존한다.

## 기존 절 바로 찾기

<a id="project-j-콘텐츠-확장-구현-가이드"></a>
- [Project J 콘텐츠 확장 구현 가이드](Content_Authoring_System.md#expansion-project-j-콘텐츠-확장-구현-가이드)
<a id="1-현재-데이터-흐름"></a>
- [1. 현재 데이터 흐름](Content_Authoring_System.md#expansion-1-현재-데이터-흐름)
<a id="플레이어-런타임-소유권"></a>
- [플레이어 런타임 소유권](Content_Authoring_System.md#expansion-플레이어-런타임-소유권)
<a id="콘텐츠-조립-흐름"></a>
- [콘텐츠 조립 흐름](Content_Authoring_System.md#expansion-콘텐츠-조립-흐름)
<a id="2-캐릭터-기본-스탯-지정"></a>
- [2. 캐릭터 기본 스탯 지정](Content_Authoring_System.md#expansion-2-캐릭터-기본-스탯-지정)
<a id="에디터-설정-순서"></a>
- [에디터 설정 순서](Content_Authoring_System.md#expansion-에디터-설정-순서)
<a id="주의-사항"></a>
- [주의 사항](Content_Authoring_System.md#expansion-주의-사항)
<a id="레벨-변경-및-어트리뷰트-동기화-api"></a>
- [레벨 변경 및 어트리뷰트 동기화 API](Content_Authoring_System.md#expansion-레벨-변경-및-어트리뷰트-동기화-api)
<a id="3-새로운-스탯-추가"></a>
- [3. 새로운 스탯 추가](Content_Authoring_System.md#expansion-3-새로운-스탯-추가)
<a id="31-attributeset"></a>
- [3.1 AttributeSet](Content_Authoring_System.md#expansion-31-attributeset)
<a id="32-기본-스탯-dataasset"></a>
- [3.2 기본 스탯 DataAsset](Content_Authoring_System.md#expansion-32-기본-스탯-dataasset)
<a id="33-장비-보너스"></a>
- [3.3 장비 보너스](Content_Authoring_System.md#expansion-33-장비-보너스)
<a id="34-ui"></a>
- [3.4 UI](Content_Authoring_System.md#expansion-34-ui)
<a id="4-직업-추가"></a>
- [4. 직업 추가](Content_Authoring_System.md#expansion-4-직업-추가)
<a id="예-전사-직업"></a>
- [예: 전사 직업](Content_Authoring_System.md#expansion-예-전사-직업)
<a id="character에-연결"></a>
- [Character에 연결](Content_Authoring_System.md#expansion-character에-연결)
<a id="5-전직-추가"></a>
- [5. 전직 추가](Content_Authoring_System.md#expansion-5-전직-추가)
<a id="전직-실행"></a>
- [전직 실행](Content_Authoring_System.md#expansion-전직-실행)
<a id="권장-서버-흐름"></a>
- [권장 서버 흐름](Content_Authoring_System.md#expansion-권장-서버-흐름)
<a id="6-스킬-추가-및-직업전직장비에-연결"></a>
- [6. 스킬 추가 및 직업·전직·장비에 연결](Content_Authoring_System.md#expansion-6-스킬-추가-및-직업전직장비에-연결)
<a id="61-ability-생성"></a>
- [6.1 Ability 생성](Content_Authoring_System.md#expansion-61-ability-생성)
<a id="62-abilityset-또는-스타일-내부-작성"></a>
- [6.2 AbilitySet 또는 스타일 내부 작성](Content_Authoring_System.md#expansion-62-abilityset-또는-스타일-내부-작성)
<a id="63-연결-대상-선택"></a>
- [6.3 연결 대상 선택](Content_Authoring_System.md#expansion-63-연결-대상-선택)
<a id="7-장비-추가"></a>
- [7. 장비 추가](Content_Authoring_System.md#expansion-7-장비-추가)
<a id="예-철검"></a>
- [예: 철검](Content_Authoring_System.md#expansion-예-철검)
<a id="장비-스탯-적용-방식"></a>
- [장비 스탯 적용 방식](Content_Authoring_System.md#expansion-장비-스탯-적용-방식)
<a id="gameplayeffect-권장"></a>
- [GameplayEffect 권장](Content_Authoring_System.md#expansion-gameplayeffect-권장)
<a id="statmodifiers"></a>
- [StatModifiers](Content_Authoring_System.md#expansion-statmodifiers)
<a id="무기-애니메이션"></a>
- [무기 애니메이션](Content_Authoring_System.md#expansion-무기-애니메이션)
<a id="8-인벤토리-아이템-지급"></a>
- [8. 인벤토리 아이템 지급](Content_Authoring_System.md#expansion-8-인벤토리-아이템-지급)
<a id="현재-지원-연산"></a>
- [현재 지원 연산](Content_Authoring_System.md#expansion-현재-지원-연산)
<a id="9-장비-장착-ui-연결"></a>
- [9. 장비 장착 UI 연결](Content_Authoring_System.md#expansion-9-장비-장착-ui-연결)
<a id="ui-갱신-이벤트"></a>
- [UI 갱신 이벤트](Content_Authoring_System.md#expansion-ui-갱신-이벤트)
<a id="10-소비-아이템의-간단한-구현-방향"></a>
- [10. 소비 아이템의 간단한 구현 방향](Content_Authoring_System.md#expansion-10-소비-아이템의-간단한-구현-방향)
<a id="11-저장과-백엔드-연결-시-경계"></a>
- [11. 저장과 백엔드 연결 시 경계](Content_Authoring_System.md#expansion-11-저장과-백엔드-연결-시-경계)
<a id="12-기능별-완료-체크리스트"></a>
- [12. 기능별 완료 체크리스트](Content_Authoring_System.md#expansion-12-기능별-완료-체크리스트)
<a id="스탯"></a>
- [스탯](Content_Authoring_System.md#expansion-스탯)
<a id="직업과-전직"></a>
- [직업과 전직](Content_Authoring_System.md#expansion-직업과-전직)
<a id="장비"></a>
- [장비](Content_Authoring_System.md#expansion-장비)
<a id="인벤토리"></a>
- [인벤토리](Content_Authoring_System.md#expansion-인벤토리)
<a id="13-권장-구현-순서"></a>
- [13. 권장 구현 순서](Content_Authoring_System.md#expansion-13-권장-구현-순서)
