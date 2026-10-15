# PC BP·DA 잔여 구조 검토와 Door 정리 — 2026-10-15

[앞선 PC 정리](Project_J_PC_Structure_Cleanup_2026-10-15.md)에서 남긴 Door·BP·DA 연결을 후속 대조했다. **현재 확인한 PC 호출·상태 소유권 범위에서 열린 추가 정리 후보는 없다.** Door와 identity의 재현된 경계를 수정하고, 현재 DA19·GE2의 값과 기존 native 소비를 대조했다. 성능 조사와 전수 실행 품질 검증은 계속 보류한다. 모든 외부/동적 콘텐츠의 무결성을 보증하는 전수 감사 완료 선언은 아니다.

기준 HEAD는 `f8ebd8a5a33fb732f1b8c88dd4c23f75b22a95d1` 그대로다. 기존 작업 트리·사용자 인계 변경을 보존했고, 읽기 전용 담당 3명은 검토만 수행했다. 수정·공식 Unreal MCP·빌드·시험·저장은 주 작업자만 수행했다.

## 실제 수정

| 대상 | 원본 재현·문제 | 최종 변경과 보존 계약 |
|---|---|---|
| Door Timeline | 실제 BP의 AddDoor=false 및 첫 패널 소실에서 직접 Open/Close→Timeline Update가 각각 AccessViolation2회를 발생시켰다. 정상 단문·양문은 통과했다. | IsValid macro1개로 첫 패널만 보호한다. 없으면 기존 Door2 검사로 이어가 두 번째 패널을 독립 갱신한다. 기존 Timeline Play/Reverse·곡선·좌표·BeginPlay·CDO 전체값 유지 |
| Door Set Mesh | 동기인 component 생성→mesh→mobility와 그 뒤 선택 material을 Sequence의 두 출력으로 나눠 놓았다. | 불필요 Sequence1개를 제거하고 같은 순서로 직렬 연결. null material은 mesh 기본 재질을 유지하고, 서로 다른 frame5조각의 생성 호출은 보존 |
| Identity social 표시 | public SetIdentity가 invalid CharacterId를 수용했지만 이전 party/guild·leader 표시가 남았다. 이벤트 안/밖의 파생 상태2개 assertion 실패를 실제 GI·Social·PS로 재현했다. 현재 PC 로그인에서 발생한 증상으로 집계하지 않는다. | identity 대입 직후 이벤트 전에 기존 SetSocialState로 파생 표시4개만 비운다. 실제 membership/revision/tombstone과 정상 ID의 재연결, 계정만 invalid인 기존 계약, callback 뒤 현재 identity/퇴장 검사는 유지 |

Door는 prototype으로 보존한다. 신규 네트워크 기능·상호작용 프레임워크·timer/reset·특정 키/클립 예외를 만들지 않았다. 에셋 변경은 Door BP 하나이고, source 변경은 PS의 파생 상태 경계와 기존 시험 CPP 두 파일이다. 앞선 R17 정리·Class DA 변경은 반복 수정/새 성과로 집계하지 않는다.

## 검토 완료 범위와 유지 이유

| 묶음 | 실제 대조 범위·판정 |
|---|---|
| Door 전체 로직 | Construction99·Set Mesh10·EventGraph39개 원본 노드의 input/output 핀·기본값·연결을 읽었다. 최종99/9/40개다. Frame 옵션과 panel/Box 옵션은 독립이고 frame5조각·단문/분할 패널은 서로 다른 mesh/위치/회전/scale을 생성한다. 중복 호출로 합치지 않는다. signed 크기와 frame scale을 모델링 의도 없이 절댓값/강제 clamp로 바꾸지 않는다. |
| 기존 BP 본문 | 기존33 BP·111개 고유 graph의 DSL과 빈 DSL 보강 node 근거를 재사용했다. 이번에는 Door의 미대조 핀과 public identity/social writer 접점을 추가 확인했다. GameMode/Player/GreatSword/UMG에 social 권위를 덮는 추가 writer는 확인하지 못했다. signature·빈 stub를 미사용으로 삭제하지 않는다. |
| 현재 DA19·GE2 | 현재 공개 reflected 값 전체를 공식 MCP로 읽고 dirty=false를 확인했다. 필드 목록만 수집한 것으로 끝내지 않고 아래 소비 경로와 대조했다. CombatMode의 실제 TargetTags component도 별도 읽어 State.CombatMode 부여를 확인했다. |
| 기존 DA25 재사용 | 최근 실제값13개와 공격10·retarget2의 최종 hash 연결을 재사용해 선택 native DA44개의 값을 연결했다. Class는 이전 Query3 원본이 아닌 R17 수정 후 receipt가 최종값이다. 공격10개도 최초 Root 값이 아니라 DamageEffect 교정 후의 최종 근거를 사용한다. |
| PC 비활성·테스트 경로 | touch geometry/다중 LocalPlayer 후보는 기본 bForceTouchControls=false인 PC 비설치 경로로 유지한다. 현재 PC 입력을 대체하거나 모바일 지원을 삭제하지 않는다. 테스트 ABP·빈 Player WeaponProfile·제작 `(1)` snapshot·legacy 예제를 미참조/공백이라는 이유로 삭제하지 않는다. |

현재 데이터와 실제 소비 대조의 결론은 다음과 같다.

- **입력→공격→피해:** InputMapping의 LMB/RMB·chord·Q/R/T→SkillInputExecution→CommandSet/Combo/AttackSet→Melee/HitValidation을 대조했다. AttackSet과 Combo가 같은 공격 정의를 역할별로 참조하는 것은 이중 실행이 아니다. 빈 CommandSet은 raw-input passthrough이자 제작 연결점이므로 참조를 유지한다. 피해 GE는 Instant Health AddBase -10 하나이며 추가 execution/conditional/component 피해 경로가 없다. HitSpec.BaseDamage는 이미 명시된 직렬화 legacy placeholder다.
- **전투 상태·표현:** CombatToggle의 즉시 완료와 Infinite GE의 State.CombatMode 소유를 구분한다. 실제 TargetTags component1개와 modifier/execution/conditional0을 확인했다. CPS의 공격별11매핑→공유 AP1은 cosmetic 선택이고, 서버 피해 권위와 합치지 않는다.
- **애니메이션·손·무기:** OTM/Strafe 및 Run/Sprint의 실제 DB와 전환 튜닝 역할이 다르다. Default=Idle 같은 참조는 fallback 역할이다. top profile/direct 호환, nested/legacy budget 우선순위, weapon attachment와 retarget 손 보정 역할을 유지한다. 지속 곡선 **0.35초·100cm·3도·85%**는 그대로다.
- **Foley·rider:** sound가 있는 event를 먼저 반환하는 ResolveEvent 계약이므로 fallback은 동시 재생 중복이 아니다. Rider layer/tags/handIK/blend는 mount binding과 GT snapshot→worker 값 소비에 맞다.
- **HUD·map·quest:** native 문자열 로딩 root가 있어 registry 참조0은 삭제 근거가 아니다. Map의 빈 texture/marker는 fallback region과 runtime projection 역할이고, FirstSteps의20m/XP100은 objective/claim 소비와 일치한다. native theme 기본값과 같아도 제작용 DA를 제거하지 않는다.

[범위·값 근거](PCStructureEvidence_2026-10-15/Remaining/ReviewCoverage.json)에는 DA44개 각각의 조회/재사용 근거와 현재 SHA를 연결했다. 이44개는 프로젝트의 모든 imported asset/DT/Chooser/IA/IMC를 뜻하지 않는다. 종전의 native/body/caller 대조 범위도 파일 수로 확대하지 않는다.

## 검증

| 실행 | 결과와 의미 |
|---|---|
| `R18_DoorIdentityRepro3` | 원본2개 시험 실패. Door Mode0/3 AccessViolation각2, identity 파생 표시2 assertion 재현. 정상 Door Mode1/2와 실제 membership/rebind는 원본부터 통과한 보호 계약 |
| `R18_IdentityProjectionFixBuild1` | direct UBT full Editor Win64 Development 성공. 실제4 actions 중 PS CPP compile1. 7.61초 |
| `R18_DoorIdentityFocused1` | freshload Door 옵션·직접 호출·패널 소실·identity detach/멤버십·퇴장·Social4개 clean |
| `R18_RemainingFinalFocused1` | 기존 UI/애니메이션/전투/성장/소유권을 포함한93개 clean. 마지막 Set Mesh 직렬화 전 통합 기준점 |
| `R18_DoorLinearFinalFocused1` | 마지막 BP 변경 후 Door·identity·실제 BP 전투 연결3개 clean 재확인. 앞선93개와 중복되는 시험이며 합산한 고유96개가 아님 |
| `R18_DoorCook2` | 최종 저장본 Windows Cook 성공. 1365 cooked·incremental skip0·platform skip7(총1372), 19.09초. 기본맵+Door 폴더를 명시 root로 포함하고 Door·Class·Style이 실제 ReferencedSet에 존재. Game 실행 파일/패키지/원격·성능 검증은 아님 |

첫 Door fixture의 함수명 공백(`Open Door`/`Close Door`) 오인과 tracepoint를 exception으로 센 오류는 시험 제작 오류로 보존했다. `R18_DoorIdentityRepro3`부터 실제 AccessViolation만 세므로 그 이전 실패 수를 제품 결함 수로 사용하지 않는다. 새 identity 시험의 membership/rebind는 기존 보존 계약이고, GC·중첩 callback은 기존 IdentityRetirement 및 source 교차 검토의 범위로 구분한다.

MCP는 최종 graph를 warnings-as-errors로 컴파일하고 CDO 전체 동일·dirty=false를 확인한 뒤 지정 Door 하나만 저장했다. 임시로 만든 Branch는 저장 전에 삭제했다. 전용 호스트3개는 모두 QuitEditor로 정상 종료했다. 빌드·시험·Cook은 겹치거나 중단하지 않았다. Cook의 기존 GameplayCue 경로 warning은 별도로 보존한다.

[로컬 실행 원자료 목록·SHA](PCStructureEvidence_2026-10-15/Remaining/ReceiptIndex.json) · [변경·최종 검증 요약](PCStructureEvidence_2026-10-15/Remaining/Summary.json). R17 기록을 덮지 않고 후속 `Remaining`에 요약·값 대조·해시를 분리했다. 커밋 요청에 따라 원시 요청·응답·로그 사본은 ignored `Saved/Validation/CommitPreparation_20261015_PC/ExcludedReceipts/R18/`에 보관하고 커밋에서 제외한다. 원래 상세 로그도 Saved에 유지한다.

[당시 마감 대조](PCStructureEvidence_2026-10-15/Remaining/FinalVerification.json): 파일·binary·원자료 사본 SHA 불일치0, 관련 native 프로세스0, 문서 링크 오류0. 원자료 사본은 로컬 보관소에 유지한다. 별도로 기존 로컬 보관소 부재 링크12개는 유지한다. 이 수치는 파일 보존·기록 확인이며 검토율이 아니다.

## 남은 범위

이번에 식별한 PC 불필요 로직·연결 후보는 수정 또는 유지 이유로 판정했다. 추가로 구현해야 할 큰 구조/새 서비스는 만들지 않았다. 실제 socket 존재·최종 pose/audio/map 현장 품질, 모바일/다중 LocalPlayer 실행, 임의 제작값의 형상 품질·새 동적/외부 콘텐츠의 전체 속성 대조는 이번 구조 정리의 실행 완료 범위가 아니다. 미확인 테스트 snapshot의 제작 의도도 그대로 보존한다.

CPU/GPU 원인·반복 비교·200표시 캐릭터·실제 socket 부하·대규모 원격·운영 검증은 계속 보류한다. 기존 CPU 증가와 렌더 차이가 해결됐다고 하지 않는다. R18 마감 이후 사용자가 승인한 커밋은 [최종 준비 기록](Project_J_PC_Commit_Preparation_2026-10-15.md)에 연결하며 push/merge/컴퓨터 종료는 하지 않는다.
