# PC 소스·BP·DA 구조 정리 — 2026-10-15 후속

이 문서는 R17 기준점이다. 여기서 남긴 Door·invalid identity와 데이터 근거 공백은 [R18 후속 구조 검토·수정](Project_J_PC_Asset_Review_Closure_2026-10-15.md)에서 판정했다. 아래 원자료와 당시 미완료 기록은 덮어쓰지 않는다.

기준 커밋은 실제 Git의 `f8ebd8a5a33fb732f1b8c88dd4c23f75b22a95d1`이다. 이전 소스·에셋 정리와 검토 근거를 재사용하고, 이번에는 반복 수정의 잔재와 실제 호출·상태 소유권이 어긋난 경로를 정리했다. 최신 Editor 빌드, 집중 회귀 90개, 실제 BP 전투 인터페이스 1개가 오류·경고 없이 통과했다. **모든 에셋 속성·동적 호출·환경의 전수 감사 완료를 의미하지 않는다.**

PC가 대상이다. CPU/GPU 원인 조사, 반복 성능 비교, 대규모 부하·원격·전수 시각 품질 검증은 요청에 따라 보류했다. 기존 CPU 증가·렌더 차이는 미해결로 유지한다. 새 시스템이나 추상화를 늘리는 대신 기존 공통 경로와 소유권을 재사용했다.

## 실제 정리한 8개 묶음

| 경로 | 변경 이유와 최종 계약 | 재현·집중 검증 |
|---|---|---|
| 공통 상호작용 | reflected `CanInteract`가 호출자를 파괴할 수 있다. 호출 수명 pin·퇴장/권위 재확인과 finite 위치 판정을 공통 query에 모았다. 개별 대상 예외를 추가하지 않았다. | 실제 overlap과 첫/마지막 승인 callback 파괴. 원본의 잘못된 Interact 2경로 재현 후 차단 |
| 미니맵 | 실패한 좌표 변환이 출력값을 덮거나 호출부가 실패를 무시했다. 성공한 finite 값만 게시하고 region/marker/player가 같은 결과를 소비한다. | 정상 축, finite 입력의 연산 overflow, 출력 sentinel 보존 |
| Identity·Social | identity delegate 후 퇴장한 PS에 social 상태를 게시할 수 있었다. 요청값 snapshot과 수명 보호, 현재 identity/권위 확인, 공통 typed admission을 사용한다. Unbind는 퇴장 정리를 계속 허용한다. | 실제 GI Social·PS delegate 파괴/GC 원본 재현, 퇴장 후 신규 가입 거절 |
| Gateway | 두 public adapter가 URL 미설정 판정을 중복했다. 기존 tracked dispatch 입구로 모아 URL→mutation→capacity/shutdown 순서와 typed 응답을 보존한다. | 미설정/잘못된 mutation/닫힘의 우선순위, correlation 1회, HTTP 미전송 |
| PC 스킬 입력·위젯 | 슬롯 번호만으로 release하면 마우스가 키보드 또는 callback이 만든 새 press를 소비했다. 기존 단일 pressed map에 receipt를 두고 원래 owner/index/receipt만 반환한다. 키 바인딩 세대로 과거 delegate도 차단한다. | 실제 ASC callback, 복사한 native key delegate, rebuild/destruct/capture loss, 재진입 press와 phantom capture |
| Weapon Motion 편집 | 중복 bool/index 상태를 scoped transaction 하나와 편집 키 receipt로 정리했다. Escape는 아직 자신이 소유한 delta만 복원한다. 외부 변경이면 값을 유지하고 undo 기록도 정상 종료한다. transaction을 기록할 수 없으면 편집하지 않는다. | 실제 mode의 Begin/Delta/Escape/End, 외부 key/array 변경, transactor 부재·undo queue 보존 |
| Retarget·시각 갱신 정책 | follower 초기화가 이미 외부 allocator/coordinator가 소유한 tick visibility 정책을 덮었다. 외부 갱신 제어 중에는 소유자의 정책을 보존하고 standalone 기본값만 적용한다. 새 allocator는 만들지 않았다. | 원본 정책 충돌 2개 재현. acquire→retarget 재초기화→disable에서 authored 복원, 외부 allocator·standalone 경계 |
| Class·CombatStyle DA | 실제 `DA_Class_Greatsword`와 기본 스타일이 같은 대검 AbilitySet을 다른 source key로 각각 부여했다. **Class DA의 중복 참조 1개만 제거**했다. 스타일의 7개 스킬과 Class의 Core 1개는 유지한다. | 변경 전 실제 총15 specs·7입력 각각2개 재현. 저장·freshload 후 총8·각1개, level 변경, style 공유 lease·최종 반환 통과 |

Class/Style의 모든 source를 전역 통합하지 않았다. 서로 다른 직업·장비가 의도적으로 소유하는 grant와, 동일 CombatStyle을 progression/equipment가 공유하는 lease는 다른 계약이다. `AS_Greatsword_Base` 자체의 7 entries·태그·레벨, `AS_Humanoid_Core`, Style DA와 테스트용 BP/DA는 수정·삭제하지 않았다.

## 시스템별 검토 범위와 유지 이유

기존 [소스 검토](Project_J_Source_Architecture_Cleanup_2026-10-15.md), [에셋 검토](Project_J_Asset_Architecture_Cleanup_2026-10-15.md), [본문 추가 대조 manifest](ContinuationEvidence_2026-10-14/AdditionalReadScopes.json)의 **명시된 본문/header/inline/caller 범위**를 재사용한다. 파일 수나 자동화 성공으로 읽지 않은 본문까지 완료 처리하지 않는다. 읽기 전용 담당 3명이 backend/상태, mount/animation/editor, quest/social/asset 연결을 나눠 보고했고, 수정·MCP·native 실행은 주 작업자만 수행했다.

| 시스템 묶음 | 이번 후속 대조·판정 | 남은 범위 |
|---|---|---|
| 이동·MM·Chooser·단발·애니메이션 | 기존 상태 controller/proxy/snapshot·selector 소유권 대조 재사용. retarget 재초기화와 editor 편집 경계 추가 수정. 실제 Wyvern의 빈 ReceiveTick은 script 구현으로 컴파일되지 않았고, grounded actor tick은 native 상태가 제어함을 시험 | 전체 클립/시각 품질·렌더/200명 부하 보류 |
| 전투·GAS·장비·성장 | 실제 Class/Style/AS·GA3/GE 연결을 native grant key·authority/prediction과 비교. Class 중복 1항목 수정. 실제 BP 전투 인터페이스 재검증 | 배포·대규모 원격/장비 조합 전수 미실행 |
| UI·입력·지도 | 기존 구독/async load/PS projection 계약 재사용. PC 키·마우스·callback release 소유권과 좌표 실패 경계 집중 대조·수정 | 다중 LocalPlayer/플랫폼별 UI 환경 전수 미검증. 모바일 기능 확대 안 함 |
| 상호작용·탈것 | 공통 reflected 승인 callback을 기준으로 caller retirement 정리. mount native 전환·상태 권위 기존 검토 재사용 | Door 제작 옵션의 아래 직접 호출 후보 미재현 |
| Identity·Social·Backend | 공통 typed admission/퇴장 cleanup·request tracker 경계 대조·수정. 서버 권위와 client projection, 논리 취소와 physical 완료 분리 유지 | invalid identity로 clear할 때 기존 social projection을 어떻게 표현할지 계약 미확정. 이번 수정이 해결했다고 하지 않음 |
| Quest·Inventory·저장·인계·NPC/Mass·Foley·제작 도구 | 기존 source/caller/owner 대조를 재사용하고 이번 변경과의 접점을 교차 검토. 별도 새 프레임워크 도입 근거 없음 | 외부 운영 서비스 설치·전체 장애 시나리오·대규모 성능 검증 보류 |

OTM/Strafe·Run/Sprint·단발/MM 복귀·Land·측면 후보·전투/장비·retarget와 지속 곡선 **0.35초·100cm·3도·85%** 설정은 변경하지 않았다. force reset/timer/특정 키·클립 예외를 추가하지 않았다. GT의 UObject/상태 소유와 worker의 값 계산, PS 권위/예측, GC pin, 취소/완료 계약을 유지한다.

## BP·DA 조회와 보존 판정

- 공식 Unreal MCP로 Door/Wyvern/Player/GreatSword BP, GA3/CombatMode GE의 선택 공개값·필수 exec 연결 8건을 읽었다. 이어 AbilitySet2와 Class/Style2를 최소 조회했다. 에셋 전체 검색·일괄 저장·삭제는 하지 않았다.
- BP_Player의 top profile과 direct locomotion/MM fallback은 현재 같은 값을 가리키지만 native의 top 우선·직렬화 호환 fallback 계약이 있다. 현재 값의 동치만으로 제거하지 않았다. GreatSword는 top profile을 사용하고 direct 둘은 비어 있다.
- CombatToggle·Greatsword의 LocalPredicted와 NPC의 ServerOnly는 native 실행 경계와 맞다. 짧은 toggle ability와 Infinite CombatMode GE는 실행과 지속 상태의 서로 다른 소유자다. ability classification tag와 activation-owned tag도 중복 상태로 삭제하지 않았다.
- 이전 DA32개의 현재 파일은 10/15 baseline과 모두 일치한다. 이 중 공격10개의 최종 교정/hash, RetargetFamily/Body2개의 조회 시점 hash는 직접 연결된다. 나머지20개는 **10/13 당시 값 근거 + 10/15 baseline 이후 파일 무변경**이며, 현재 모든 필드를 다시 읽었다는 뜻이 아니다. 공격은 원래 Root 값이 아니라 후속 DamageEffect 교정 영수증을 반영한다.
- Locomotion의 예전 heading/yaw-rate 3필드는 `DeprecatedProperty` 직렬화 호환 필드여서 공식 공개 속성 목록에 없다. 초기 이름 지정 조회 실패를 보존했다. 이를 새 기능 필드 누락으로 보고 접근을 우회하지 않았다.
- Door2 null 후보는 철회했다. 실제 BeginPlay/Update가 validated getter를 사용한다. Open/Close는 Timeline Play/Reverse로 역할이 다르다. 다만 첫 Door의 Timeline Update는 pure getter이며 AddDoor=false에서 직접 OpenDoor 호출하는 경계는 **미재현 후보**로 남긴다. 정상 옵션의 Box overlap 차단만 확인했으며 prototype을 삭제하거나 네트워크 기능을 새로 만들지 않았다.
- 테스트 ABP·`(1)` 제작 snapshot·빈 프로필·legacy 예제는 용도/호환 근거 없이 삭제하지 않았다. 미참조 registry 결과는 삭제 근거가 아니다.

## 빌드·재현·회귀 근거

영구 기록은 [검증 요약](PCStructureEvidence_2026-10-15/Summary.json), [실행별 로컬 원자료 목록·SHA](PCStructureEvidence_2026-10-15/ReceiptIndex.json), [당시 검증 파일·binary 해시](PCStructureEvidence_2026-10-15/CurrentHashes.json)에 연결한다. 커밋 요청에 따라 원시 요청·응답·로그 사본은 문서 트리에서 제외하고 ignored `Saved/Validation/CommitPreparation_20261015_PC/ExcludedReceipts/R17/`에 SHA 대조 후 보관했다. 원래 상세 로그·실패/crash 자료도 `Saved/Validation/Continuation_20261014/R17_*`에 보존한다. 세션 헤더·UserDir·dump·binary는 커밋하지 않는다.

[당시 마감 대조](PCStructureEvidence_2026-10-15/FinalVerification.json)는3392개 파일·Editor binary9개·원자료 사본67개의 해시 불일치0, 보고서 로컬 링크 누락0, 관련 native 프로세스0과 사용자 인계 SHA 보존을 확인한다. 원자료 사본은 위 로컬 보관소에 유지한다. 이 숫자는 파일 보존 근거이며 전수 검토율이 아니다.

| 최종 기준점 | 결과 |
|---|---|
| `R17_PCGrantAndEditorBuild1` | direct UBT full Editor/Win64 Development 성공. 실제7 actions 중 CPP compile2, 나머지는 link/metadata. 16.45초 |
| `R17_PCStructureFinalFocused2` | 90 clean, warning/fail/notRun/inProcess 모두0. 기존77 + 새 PC 경계7 + 성장/공유 grant6. NullRHI 기능 시험이며 성능 측정이 아님 |
| `R17_PCAuthoredCombatFocused1` | 실제 BP·PS ASC·level/health/socket 인터페이스 1 clean |
| Class DA MCP | 변경 전 dirty=false 확인, 한 필드/한 참조만 수정, 지정 에셋 하나만 저장, 저장 후 dirty=false, 다음 프로세스에서 재로드·검증 |

앞선 빌드도 실제 4–22 actions 수준으로 기록했다. adaptive 제외 파일 목록은 실제 재컴파일 개수가 아니다. 이번 마지막 빌드는 소스 2개를 컴파일했으며 프로젝트 전체를 다시 컴파일한 것이 아니다. 앞선 `Exchange` API 컴파일 오류, fixture의 위젯 construct 순서 오류와 `GetDelegateForManualSet()` 오용 crash는 시험 제작 오류로 분리했다. 제품 원본 실패·수정 후 성공과 섞지 않았다. 실행 중 빌드·검증은 중단하지 않았고 두 MCP 호스트 모두 QuitEditor로 정상 종료했다.

이번에는 reflected 이름/packet schema/Config/모듈을 바꾸지 않았으며, DA 변경은 기존 기본 스타일이 이미 제공하는 중복 참조 제거다. Editor+해당 grant·실제 BP freshload를 검증 기준으로 삼았다. 새 Game/Cook/패키지·대규모 원격 검증은 실행하지 않았다. 과거 Game/Cook 결과를 최신 변경의 검증으로 승격하지 않는다.

## 남은 문제와 경계

1. Door 직접 호출 후보와 invalid identity clear의 social 표현 계약은 미재현/미확정으로 남는다. 확인되지 않은 용도의 테스트 콘텐츠도 보존한다.
2. 모든 BP/DA/DT/Chooser/IA/IMC의 모든 속성·동적 참조를 현재 프로세스에서 전수 확인한 것은 아니다. 선택 metadata77개에 DT가 없다는 결과를 프로젝트 전체 DT=0 또는 전수 완료로 확대하지 않는다. 기존 source 읽기 범위 밖의 새 기능/미기록 경로도 자동 완료 처리하지 않는다.
3. CPU/GPU·렌더 차이, 반복 성능 비교·200표시 캐릭터·실제 socket 부하·품질·운영 환경은 보류 상태다. 이번 수정에 성능 개선율을 붙이지 않는다.
4. MMORPG 확장 시에는 기존 PS/ASC·source lease·authoritative service port·snapshot 경계를 먼저 사용한다. 새 서비스/모듈 분리·추상화·전역 dedup은 실제 요구와 장애/부하 근거가 있을 때 별도 작업으로 판단한다.

사용자가 삭제한 인계 끝의 요청문 블록과 그 파일 SHA를 보존했다. R17 마감 당시에는 변경을 작업 트리에 남겼으며, 이후 사용자가 승인한 커밋 범위는 [최종 커밋 준비 기록](Project_J_PC_Commit_Preparation_2026-10-15.md)을 따른다. 전수 감사 완료라고 선언하지 않는다.
