# BP·DA 구조 정리와 MMORPG 확장 계약 — 2026-10-15

최신 PC 잔여 범위는 [Door·현재 DA·GE 검토 마감](Project_J_PC_Asset_Review_Closure_2026-10-15.md)을 확인한다. 아래 모바일/품질·성능 미검증과 PC 구조 대조 결과를 구분한다.

후속 [PC 소스·BP·DA 정리](Project_J_PC_Structure_Cleanup_2026-10-15.md)에서 Class/Style의 실제 중복 grant를 추가 재현·수정하고, profile fallback·GA/GE 상태 소유권과 테스트 자산 유지 이유를 확인했다. 아래 결과는 그 이전 에셋 정리 기준점이다.

소스 정리에 이어 실제 에셋 연결을 대조한 후속 기록이다. **미참조라는 이유만으로 에셋을 삭제하지 않는다.** 게임·시험·샘플·제작 템플릿·확장 계약을 구분하고, 용도가 불명확한 자산과 사용자 변경은 보존한다. 이전 [소스 정리 기준점](Project_J_Source_Architecture_Cleanup_2026-10-15.md)과 이번 결과를 구분한다.

## 확인 범위와 판단 기준

- 시작 HEAD `855a1c8e111c94a5dffdd08ad05c68bdea2accec`, branch `codex/runtime-retarget-profiles`. Source/Config/Plugins/Content 3390개 파일의 시작 SHA와 incoming diff를 별도 보존했다. 추가 commit/push/merge/컴퓨터 종료는 하지 않는다.
- 설치된 UE5.8 공식 MCP HTTP 서버에 연결했다. 도구 목록·필요 스키마는 한 번 읽고 재사용했다. 주 작업자만 에디터·MCP·수정·검증을 담당하고 세 읽기 전용 담당이 다른 영역을 대조했다.
- BP 계열 33개의 부모·변수·참조·그래프 목록111개를 읽었다. DSL이 비어 반환된62개는 공식 노드/핀 API로342개 노드를 추가 수집했다. 수집 성공을 모든 핀·속성·실행·pose 품질의 상세 검토 완료로 해석하지 않는다.
- 새 프로세스 registry는 `/Game`2735개이며 지정 BP/프로젝트 정의77개의 hard/soft/management 참조를 분리했다. DA10개의 공개값, 기존 에셋 검토/해시와 Source/Config/Scripts의 실제 소비를 재사용했다. DA/GE/AnimGraph 전체 속성·동적 문자열·모든 환경의 전수 검증은 아니다.
- 핵심 원자료는 [이번 근거 장부](AssetArchitectureEvidence_2026-10-15/Summary.json)에 연결한다. 기존 전수 파일·registry 목록만으로 미사용이나 완료를 판단하지 않는다.

## 실제 정리

| 대상 | 발견과 최종 이유 | 보존한 계약 |
|---|---|---|
| `BP_GreatSword` 인터페이스3개 | `IsDead=false`, `GetCharacterLevel=0`, 소켓 인자를 무시한 원점 반환이 native 권위를 가렸다. 상수 반환 override graph 자체를 제거해 기존 부모의 Health·Progression·primary mesh socket/actor fallback 구현으로 귀속 | BP/직업/장비/프로필/ID 유지. CDO183개 값과 Construction/Event graph 동일. primary mesh 기준이며 retarget follower socket 구현으로 바꾸지 않음 |
| `ABP_Player` MM DB 작성 | native 설정 뒤 실제 pose-link 갱신에서 후보2개가1개로 사라지는 현상4회를 재현. 초기/관련성/update/state-updated 네 바인딩과 중복 `OnUpdate` setter를 제거하고 기존 native proxy를 단일 작성 권위로 사용 | 테스트 ABP·native parent·기존4개 pose/Event graph·노드/핀·DB getter pin 유지. 네 연결 None·컴파일/선택 저장 및 최종 집중 회귀 통과 |
| `Create-CompactHUDData.py` | HUD46/46/12, map0/5000, consumable cooldown10의 native와 같은 기본값 재지정을 제거. 신규 제작은 native CDO 기본값을 상속하고 script에는 콘텐츠 override만 남김 | 기존5개 에셋은 존재 검사로 보존. 제목·ID·회복량·보상·stack 같은 콘텐츠 값 유지. 실제 CDO 값과 script 실행 전후 기존 에셋 SHA를 확인 |

BP 상수 반환 제거는 단순 미사용 삭제가 아니라 실제 동작 교정이다. 수정 전 actual BP actor를 생성하고 PS ASC/Attributes/Progression·possession을 먼저 확인한 뒤 `Execute_*`로 호출해 레벨·사망·소켓 불일치를 재현했다. native `_Implementation` 직접 호출이나 native class만 생성하여 BP override를 우회하지 않았다.

## 검토 후 유지한 이유

| 에셋/묶음 | 현재 역할·유지 이유 |
|---|---|
| `BP_Player → ABP_Player → DA_Player_AnimProfiles` | `[ProjectJ.Tests].PlayerCharacterClassPath`를 Handover 시험이 실제 soft-load한다. 기본 게임 맵에 없다고 미사용이 아니다. 테스트 root를 유지하며 중복 실행 경로만 정리 |
| `DA_Player_AnimProfiles` / Greatsword AnimProfile | locomotion/combat DA를 이미 공유하지만 Greatsword의 Foley 연결이 다르다. 공통 기반과 직업 override를 합쳐 제작 의도를 잃지 않음 |
| Class/Equip의 같은 CombatStyle, AS_Humanoid_Core/AS_Greatsword_Base | 직업 기본과 장비 provider의 수명·해제/fallback이 다르다. grant lease가 공유 spec 소유권을 관리하므로 데이터 링크를 임의 제거하지 않음 |
| Master / weapon layer / runtime retarget ABP | 주 locomotion, 상체 overlay, visible follower 후처리가 다른 책임이다. 이름·부모 유사성만으로 병합하지 않음 |
| `ABP_Player_Mount` / `ABP_Rider_Dragon` | 노드 대조상 입력 pose 통과와 rider idle 재생으로 다르다. rider profile 우선과 player fallback 계약 유지 |
| Row/Tile·UI default/catalog·Potion/Quest | 목록/타일 두 제작 확장점, native fallback/PIE 시험 및 catalog 연결이 실제 소비한다. default UIProfile의 override=false/빈 배열은 native 상속이지 이중 상태가 아님 |
| 실제 아이템 DA3개 | `Greatsword.Starter`, `HealthPotion`, `ManaPotion`은 서로 다른 ItemId다. 표현·효과·사용 경로가 달라 병합하지 않음 |
| GA_CombatToggle/GA_Greatsword·BP_GE_CombatMode·GE_TestDamege | AbilitySet 및 공격 DA11개의 실제 소비가 있다. 특히 `Test` 이름 GE도 서버 hit→spec→ApplyToTarget에서 사용하므로 이름을 이유로 제거하지 않음 |
| GA_NPC_TestAttack·Door·template/controller/touch | 시험·제작/샘플 역할 또는 현재 설치 미확인. BP 이벤트 stub·참조0만으로 삭제하거나 샘플에 추정 서버 기능을 추가하지 않음 |
| `DA_Player_WeaponProfile`, `BP_GreatSword_Weapon1` | 전자는 실제 공개값이 비어 있는 유효 프로필이고 후자는 legacy 복사본 문자열에 흔적이 있다. 현재 목적이 확정되지 않아 보존. 전자를 제작 템플릿이라고 단정하지 않음 |
| `(1)` DA3개 | 정상 long package 이름에 맞지 않는 디스크 이름이며 내부 원래 package명/옛 경로와 정상본과 다른 SHA를 가진다. 미반영 제작 snapshot 가능성을 남기고 파일을 그대로 보존. 경로 우회·강제 로드·추정 병합하지 않음 |
| Brushify `MI_Landscape` redirector | 현재 참조0만으로 외부 제작·문자열 참조가 없다고 증명되지 않는다. 관계 없는 맵 저장·redirector 일괄 fix-up을 하지 않음 |

## MMORPG 구조와 데이터 소유권

1. **서버 gameplay 정의와 로컬 표시를 구분한다.** 지속 성장·ASC/장비 소유는 PS, inventory는 소유자 전용, equipment/public class는 관련 관전자에게 필요한 복제라는 기존 경계를 유지한다. BP 상수·로컬 UI 값이 사망/레벨 권위를 대신하지 않는다. 이번 변경으로 새 RPC·복제 필드·packet schema를 만들지 않았다.
2. **데이터 식별자와 경로를 구분한다.** ItemId/ClassId/QuestId/ProfileId 및 저장 quickslot/layout 키, asset path/PrimaryAssetId는 같은 식별자가 아니다. 병합·이름 변경 시 양쪽 server/client 정의 해석·기존 저장과 ID 이전을 함께 검토한다. 현재 경로/ID를 유지했다.
3. **Cook 발견과 서버 승인 roster를 구분한다.** PrimaryAsset scan/AlwaysCook 존재만으로 class/advancement runtime 등록·서버 승인을 넓히지 않는다. 테스트 콘텐츠를 숨기기 위해 전역 Cook 규칙을 임의 축소하지 않는다.
4. **기존 로딩 계약을 재사용한다.** 아이콘 등의 soft/비동기 lease는 유지한다. 모든 DA를 한 대형 hard-reference catalog로 합치거나 전부 soft로 바꾸는 식의 일괄 변경은 하지 않는다. 필요할 때 기존 AssetManager의 bundle/streamable handle 수명과 dedicated-server 표시 의존성을 측정·분리한다.
5. **관심 영역과 복제 비용은 데이터 통합과 별도 검증한다.** Iris filtering/prioritization·owner-only 상태·부하 조건을 구분한다. 에셋 수 감소나 이번 local 회귀를 서버 수용 인원·대역폭·CPU/GPU 개선 증명으로 표시하지 않는다. 기존 미해결 성능/운영 위험은 그대로 유지한다.

공식 기준: [Asset Management](https://dev.epicgames.com/documentation/unreal-engine/asset-management-in-unreal-engine)는 PrimaryAsset ID·bundle·load/unload 및 redirect 설정을 설명한다. [Reference Viewer](https://dev.epicgames.com/documentation/unreal-engine/reference-viewer-in-unreal-engine)는 management 참조를 포함한 조회 범위를 구분하며, [Asset Redirectors](https://dev.epicgames.com/documentation/unreal-engine/asset-redirectors-in-unreal-engine)는 이동/이름 변경 후 기존 참조 연결을 설명한다. [Iris 소개](https://dev.epicgames.com/documentation/en-us/unreal-engine/introduction-to-iris-in-unreal-engine)와 [Filtering](https://dev.epicgames.com/documentation/en-us/unreal-engine/iris-filtering-in-unreal-engine)은 연결별 복제 대상·우선순위의 별도 책임을 설명한다. 웹 설명과 설치 엔진 구현을 구분하며 현재 엔진 설정을 추측으로 바꾸지 않았다.

## 검증과 남은 범위

- 첫 Editor 빌드 direct UBT4actions/12.09초 성공. 실제 BP 수정 전 회귀1개는 의도한4 assertion 실패로 정상 종료했고, 수정 후 영향33개는 clean 통과했다. BP 저장/재로드/컴파일·native 상속·183 CDO 값·기존 제작 에셋 SHA를 확인했다.
- 첫 기본 맵 Cook-only는1361 cooked/0 incremental skip/7 platform skip·exit0이다. 최종 `Cook2`는 기본 맵과 수정한 테스트 ABP 폴더를 명시 root로 포함해1363 cooked/0 incremental skip/7 platform skip(합계1370)·exit0이며, 변경한 두 에셋이 실제 ReferencedSet에 있다. 기존 GameplayCue 경로 경고1개는 유지됐다. 실행 중 빌드/Cook/시험은 중단하거나 겹치지 않았다. Game 실행 파일·패키지·원격·전수 품질의 최신 검증이라고 표시하지 않는다.
- 최종 통합 `R16_AssetArchitectureFinalFocused1`은65개 clean·실패/미실행/진행0·exit0이다. 실제 곡선/단발 입력 재생260초를 포함한다. 새 MM 회귀는 AnimInstance 소유 proxy와 공개 pose-link를 사용해 실제 callback 실행 후 후보2/1·동일 ID 반복·companion 제거/복원·MM disable/re-enable을 확인한다. 의도적으로 격리한 노드의 missing IPoseHistory 진단5회는 예상 오류로 정확히 확인하며 pose 검색/품질 완료로 표시하지 않는다. disable 경계에서는 callback을 실행하지 않는다.
- 첫 시험 컴파일72actions/165.48초의 엔진 private 함수 접근 오류는 시험 코드 수정으로 분리했고, 공개 pose-link 경로로 잔여7actions/13.52초 빌드가 성공했다. 큰 공용 anim header에 기존 시험 friend1개를 추가한 의존 재컴파일이며 증분 실패로 단정하지 않는다. 초기 노드 선택 보호 검사의 조상 경로 오인과 추가 state-updated 연결은 저장 전 발견·교정했다. 미완성 ABP를 디스크에 저장하지 않았다.
- 최종 fresh `Reload2`는 저장된 두 에셋의 compile·native 함수 상속·네 바인딩 None 및 기존 제작 데이터 무변경을 확인했다. 시작3390개 중 기존 변경4개(에셋2·시험 본문/시험 friend2)와 신규 시험 cpp1개만 있으며 나머지3386개 SHA와 기존 사용자 인계 diff를 보존했다. Config/Plugins·에셋/파일 삭제·추가 에셋은0이다. Editor DLL/manifest9개 SHA와 관련 프로세스0, 정확한 원자료 범위를 Summary에 기록했다.
- 남은 검토/실행: 모든 DA/GE authored 값·모든 BP 노드 속성/동적 루트·모바일 touch/다중 LocalPlayer 실행, Door 제작 옵션/서버 권위, Wyvern 실제 generated Tick. `UI_TouchSimple`의 cursor/input mode와 native UI writer, thumbstick controller0/owning player 불일치는 미재현 후보로 보존한다.
- 보류: CPU/GPU 원인·반복 성능 비교·대규모 부하/원격·전수 시각 품질·운영 서비스 설치. OTM/Strafe·Run/Sprint·단발/MM·Land·측면 후보·전투/장비·retarget와 .35초/100cm/3도/85% 계약을 유지한다.

**전체 BP/DA/환경 정밀 감사 완료는 아니다.** 실제 수정·명시 범위 검토·수집만 된 자료·미확인/보류를 각각 유지한다.
