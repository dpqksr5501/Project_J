# 잔여 전체 소스 아키텍처 고도화 감사

기준: 2026-10-03, `main`, `33b574a1d6775306a26357f67bc3d79311e7189b`, UE 5.8.

이 문서는 [캐릭터 컴포넌트·DA 감사](Project_J_Character_Architecture_Maturity_Audit_2026-10-03.md)의 후속이다. 이전 A01–A10의 개선안을 반복하지 않고 NPC, Mass, GAS 공통 계층, 비행 탑승, Notify, IK 내부, 백엔드, 소셜, 에디터, 렌더링, 진단·테스트와 실험 플러그인까지 확장했다. 코드 수정은 하지 않았다.

## 1. 판단과 실제 점검 범위

현재 구조의 강점은 **값 스냅샷과 UObject 작업의 분리, 비동기 요청의 수용량 제한, 소유권·revision 검사, 모듈 의존성 방향, 명시적인 실험 진입점**이다. 다음 고도화는 프레임워크 교체보다 **실패한 전환의 복구, 실행별 소유권, 적용 성공의 의미, 제작 시 검증**에 집중하는 것이 효과적이다.

| 범위 | 파일 / 줄 | 이번 검토 방법 |
| --- | ---: | --- |
| 기존 상세 감사 영역 | 144 / 33,448 | 이전 보고서를 기준으로 범위 제외. 새 발견의 호출 경계에 필요한 일부는 재확인 |
| 나머지 `Source` 구현·헤더·빌드 정의 | 184 / 14,401 | 본문, 공개 계약, 호출 연결, 수명·권한·실패 경로 검토 |
| `Source` 진단·테스트 | 72 / 11,824 | 테스트명·assertion·컴파일 조건을 추출하여 계약 검토. 주요 fixture의 준비·정리 및 실패 관련 본문 추가 확인 |
| `Plugins/ProjectJExperiments` | 13 / 1,223 | C++·헤더·Build.cs 12개와 compute shader 1개 본문 검토 |
| 검증·측정 스크립트 | 18 / 886 | `Scripts/Validation` 17개와 `Docs/Benchmarks/recompute.py` 본문 검토 |
| 합계 | **431 / 61,782** | **기존 영역 144개를 제외한 287개를 위 방법으로 검토** |

파일별 분류·검토 깊이·관련 항목·SHA-256은 [감사 범위 CSV](Project_J_Remaining_Source_Coverage_2026-10-03.csv)에 기록했다. `Source` 자체는 400파일·59,673줄, 8모듈과 3 Target 정의다. 줄 수에는 공백·주석이 포함된다. CSV의 해시는 해당 파일의 기준 상태를 식별하며 테스트 통과나 전 행 정독의 증거는 아니다.

**모든 테스트 파일의 모든 줄을 같은 깊이로 정독했다는 의미는 아니다.** 런타임 본문 검토와 테스트 계약 검토를 구분했다. 생성물, 엔진·외부 라이브러리, 모든 `.uasset` 내부, 실제 서비스 서버, Shipping·패키지 실행, RHI 시각 품질과 신규 성능 측정은 이번 범위 밖이다. Config 및 프로젝트·플러그인 descriptor는 연결 근거로 확인했다. Unreal MCP와 마이그레이션 commandlet은 실행하지 않았고 에셋을 저장하지 않았다.

이하 **확인**은 소스 경로 확인, **실행 재현**은 실제 테스트 재현, **조건부**는 특정 사용 조건의 위험, **확장**은 기능 확대 시의 설계 제안이다. P1은 정확성·복구·검증 신뢰성, P2는 확장 안정성·제작 품질, P3는 측정 후 도입할 항목이다. P1 중 서비스 연결 전제인 항목을 현재 운영 장애로 해석하면 안 된다.

## 2. 개선 목록

| ID | 우선순위·근거 | 개선 대상 | 적용 시점 |
| --- | --- | --- | --- |
| R01 | P1 · 확인 | 공통 AttributeSet의 자원·최댓값 불변식 | 현재 GAS 정확성 개선 |
| R02 | P1 · 조건부 | Mass 전환 중단·행동 재개 실패 복구 | Mass 사용 확대 전 |
| R03 | P1 · 조건부 | 비행 착륙의 막힘·실패 탈출 경로 | 비행 콘텐츠 확대 전 |
| R04 | P1 · 조건부 | WeaponMotion·MeleeHit Notify 실행 소유권 | 몽타주 중첩·교체 회귀와 함께 |
| R05 | P1 · 확인 / 서비스 연결 전 | Handover 적용 결과·재전송 영수증 | 실제 서버 간 전송 연결 전 |
| R06 | P2 · 확장 | Gateway 명령 결과·handover 수용량 | 상태 변경 백엔드 연결 전 |
| R07 | P2 · 확인 / 복원 전제 | 소셜 그룹 복원·영속 identity | 재접속·서버 이동 구현 전 |
| R08 | P2 · 조건부 | LocalPlayer 입력·UI 소유권 | Controller 교체·travel 검증 시 |
| R09 | P2 · 확인 | 무기 모션 편집기의 소켓 좌표계·세션 | 콘텐츠 제작 정확성 개선 |
| R10 | P2 · 확장 | Bundle 검색 비용·리그 제작 검증 | DA·리그 종류 확대 시 |
| R11 | P2 · 확장 | NPC 조립·활성화 계약 | 실험 NPC의 정식 콘텐츠화 시 |
| R12 | P3 · 측정 필요 | 전체 GT 비용·대기 지연·Mass 적용률 | 부하 측정 후 선택 |
| R13 | P1 · 실행 재현 | 장비 수명 테스트의 준비 조건 불일치 | 다음 구현의 검증 기반부터 |
| R14 | P2 · 도구 재사용 전 | 에셋 마이그레이션의 재실행·부분 저장 복구 | commandlet 재사용·범용화 시 |

### R01. Health/MaxHealth, Mana/MaxMana의 불변식을 모든 변경 경로에 적용

근거: `Source/Project_JGAS/Private/Project_JAttributeSet.cpp:23,41`, 비교 구현 `Source/Project_JMount/Private/Mount/Project_JMountAttributeSet.cpp:19–59`.

현재 `PreAttributeChange`는 Health/Mana가 바뀔 때만 현재 최댓값으로 제한한다. 최댓값 변경에 따른 현재 자원 보정은 `PostGameplayEffectExecute`에 있다. 하지만 duration/infinite GE의 적용·제거에 따른 최댓값 재계산은 execute 전용 콜백으로 모두 처리되지 않는다. 예를 들어 MaxHealth 150 상태에서 Health 140인 캐릭터의 최대 체력 버프가 만료되어 MaxHealth가 100이 되면, 이 계층에는 Health를 다시 100 이하로 제한하는 경로가 없다. 최댓값의 음수·비유한 값 방어도 Mount AttributeSet보다 약하다.

권장 구조는 작은 공통 자원 정책 함수와 `PreAttributeBaseChange`·`PreAttributeChange`·권한 측 `PostAttributeChange`의 역할 분리다. 최댓값 하락 시 절대값 제한인지 비율 유지인지 명시한다. Mount와 Player를 하나의 거대한 AttributeSet 상속 구조로 합칠 필요는 없다. UE 5.8 로컬 `AttributeSet.h:203–223`의 execute/attribute-change 계약도 대조했다.

완료 조건: 임시·무기한 최대치 GE 적용/제거, 직접 base 변경, 0·음수·비유한 값, 클라이언트 복제 후 UI 값 일관성. 사망 이벤트는 값 보정과 분리하여 중복 발생하지 않아야 한다. 새 사례의 실행 재현은 아직 하지 않았다.

### R02. Mass Demote/Promote를 복구 가능한 전환으로 구성

근거: `Source/Project_JCharacter/Private/Mass/Project_JMassRepresentationSubsystem.cpp:146–194`, `Private/Components/Project_JNPCActionComponent.cpp:42–70`.

`Demote`는 활성 행동 정보를 저장하고 `StopActions()`를 호출한 뒤 callback으로 상태가 변했는지 재검사한다. 그 재검사에서 실패하면 그냥 반환한다. 살아 있는 NPC가 새 전투 조건 때문에 Mass 진입만 거절된 경우에도 기존 행동이 Disabled로 남을 수 있다. 뒤의 재시도는 이미 꺼진 상태를 새 기준으로 저장할 수도 있다. 반대쪽 `Promote`는 `StartActions()`의 실패를 소비하지 않으므로, 스케줄러 용량 초과나 scoring 등록 해제 시 표현·이동은 돌아오지만 행동은 재개되지 않을 수 있다.

`TrySuspend → 이동 소유권 Commit → Resume/Abort`를 실행 토큰으로 묶는 편이 좋다. 복구는 원래 행동 소유자가 여전히 유효하고 다른 시스템이 새 행동을 잡지 않았을 때만 수행한다. world teardown이나 외부 소유권 변경에서는 무조건 재시작하지 않는다. Promote에는 `Resumed / Pending / Superseded` 결과와 제한된 재시도·진단을 둔다.

완료 조건: Stop callback 중 전투 진입, 소유권 교체, scoring 소멸, action admission 포화, world teardown. 기존 `MassActionHandoff` 성공 테스트와 별도로 실패 후 NPC가 멈춰 남지 않는지 검증한다.

### R03. 착륙 상태에는 진행 실패와 입력 복귀 규칙이 필요

근거: `Source/Project_JMount/Private/Mount/Project_JFlyingMountCharacter.cpp:173,242,261,308`.

`Landing`은 조종 입력을 잠그고, 허용 경사도의 바닥을 가까이 찾을 때까지 하강 속도를 계속 설정한다. 지형 충돌로 더 내려가지 못하거나 걸을 수 없는 표면 위에 걸렸을 때 진행 시간·위치 변화·중단 요청으로 탈출하는 경로가 없다. 정상 바닥에서의 cue fallback은 있지만 공간적 실패의 fallback은 별개다.

서버가 착륙 시도 ID, 시작 시점, 최근 진행량, 실패 이유를 소유하게 하고 `Landed / RetryFlying / Aborted` 전이를 명시한다. 막힘에서는 안전하게 Flying으로 돌리거나 다른 착륙을 요청하도록 한다. 무조건 Walking으로 바꾸거나 충돌을 통과시켜 종료하면 안 된다. 필요해지면 속도·경사·timeout 등 조정값만 작은 flight profile로 분리한다.

완료 조건: 정상 지면, 급경사, 턱·돌출 구조물, 바닥 제거, 탑승 해제·사망 중 착륙. 기존 감사 A02/A03의 탑승 세션·강제 하차 문제와 별도 항목이다.

### R04. Notify의 Begin과 End를 같은 실행에 귀속

근거: `Source/Project_JCharacter/Private/Animation/Project_JAnimNotifyState_WeaponMotion.cpp:14,47`, `Project_JAnimNotifyState_MeleeHit.cpp:14,142`. 비교: `Project_JAnimNotifyState_TwoHandIK.cpp`의 `GetNotifyInstanceID()`.

WeaponMotion의 runtime 상태 키는 mesh이고 종료는 현재 presentation의 `EndNotifyIndependentMotion()`를 호출한다. MeleeHit도 bool로 hit window를 열고 닫는다. 이전 Notify A가 끝나기 전에 B가 시작하면 A의 늦은 End가 B의 모션이나 hit window를 닫을 수 있다. Begin이 거절된 Notify도 End에서 현재 실행을 종료할 수 있는 구조다. 이는 실제 콘텐츠의 중첩·블렌드·재시작 조건에서 발생하는 위험이며 모든 공격에서 재현되는 문제라고 단정하지 않는다.

mesh + notify instance + attack/montage generation으로 발급한 핸들에 Begin/Tick/End를 묶는다. 종료는 같은 핸들의 상태만 해제한다. 전투 창에는 ref-count만 더하지 말고 공격 실행 identity까지 연결한다. 중첩 자체가 금지된 콘텐츠라면 제작 validator도 함께 금지 이유를 보여줘야 한다. 이미 실행 ID를 사용하는 TwoHandIK 패턴을 확장할 수 있다.

완료 조건: A Begin → B Begin → A End, 동일 몽타주 재시작, Begin 거절 후 End, 중단·장비 교체. Leader 판정 포즈와 follower 표현 포즈의 기존 분리는 유지한다.

### R05. Handover의 '호출했다'와 '적용했다'를 분리

근거: `Source/Project_J/Backend/Project_JHandoverManager.cpp:234–263`, `Source/Project_JCharacter/Public/Network/Project_JHandoverSerializable.h:33`, `Private/Project_JPlayerCharacter.cpp:1580`.

`ApplyEnvelope`는 void `DeserializeFromHandover` 호출 뒤 TransferId를 적용 완료 집합에 넣고 true를 반환한다. 수신 구현은 버전 불일치나 reader 오류에서 조용히 반환할 수 있다. 따라서 envelope의 CRC가 맞더라도 내부 snapshot을 거절했는데 적용 성공으로 기록될 수 있다. 동일 TransferId의 재전송도 false로만 처리되어, ACK 유실 후 재시도를 이미 적용된 성공과 구분할 수 없다.

`Decode/Validate → 준비된 snapshot → Commit 결과`를 반환하도록 바꾸고, 결과를 `Applied / AlreadyAppliedSamePayload / Rejected`로 구분한다. 위치·회전의 유한성, 버전, 권한 대상도 commit 전에 검증한다. 영수증에는 TransferId와 payload digest·대상 identity를 묶어 다른 payload의 같은 ID 재사용을 거절한다. 실제 authority 이전 완료는 이 영수증과 소스 측 상태 전환에 연결한다.

완료 조건: CRC는 맞지만 내부 버전·크기가 틀린 데이터, 비유한 위치, ACK 유실, 같은/다른 payload의 같은 ID, commit 실패. 현재 loopback/계약 구현을 실제 분산 서비스 검증으로 간주하지 않는다. 이전 A10의 통신 계층 구분보다 구체적인 수신 적용 계약 개선이다.

### R06. Gateway는 명령 의미를, Handover는 총 수용량을 명시

근거: `Source/Project_J/Backend/Project_JBackendConnection.h`, `Project_JGatewaySubsystem.cpp:48–90`, `Project_JHandoverManager.cpp:76`, `.h:187–202`.

Gateway의 request tracker, timeout, shutdown 후 callback 억제, HTTP 분류는 유지할 만하다. 다만 `bRetryable`은 전송 실패 분류이며 상태 변경 명령의 재실행 안전성을 뜻하지 않는다. 서버가 변경을 완료한 뒤 응답이 유실된 경우는 단순 실패와 다른 `UnknownOutcome`이다. 모든 2xx를 transport 성공으로 처리하는 것과 endpoint별 업무 성공 판정도 나눠야 한다. legacy `SendAsyncRequestWithContext` 기본 구현은 context를 버리는 fallback이므로 mutation adapter에는 허용하지 않는 편이 좋다.

기존 `Project_JMMO` service port 위에 조회/명령별 typed adapter를 두고, 변경 명령은 idempotency key와 결과 조회·조정 절차를 필수화한다. 메모리 repository의 원자성·CAS·tombstone·재실행 영수증은 좋은 기반이지만 DB 장애 후에도 남는 트랜잭션·영수증 보존을 대신하지 않는다.

Handover는 개별 payload 1 MiB 제한과 terminal TTL이 있지만 전체 활성 전송 수·보관 byte·시간창 내 영수증 수를 제한하는 admission은 없다. 실제 transport를 붙이기 전에 검증 후 등록, 활성/terminal 별 cap, 총 byte budget과 overload 결과를 추가한다. 현재 원격 telemetry는 꺼져 있고 Gateway URL도 비어 있으므로 운영 중 중복 지급이나 외부 공격이 확인되었다는 뜻은 아니다.

완료 조건: 성공 응답 유실 후 동일 key 재시도, 불명확한 mutation timeout, 서버 재시작 후 replay, 다수 actor의 전송 폭주. 기존 bounded tracker를 재사용하고 모든 비동기 서비스를 하나의 관리자에 합치지는 않는다.

### R07. 소셜 복원의 단위는 멤버 ID 쌍보다 그룹 snapshot

근거: `Source/Project_J/Social/Project_JSocialSubsystem.cpp:68–114,289–340`, `Source/Project_J/Game/Project_JGameMode.cpp:25–37,54`.

현재 복원 API는 CharacterId·PartyId·GuildId를 받아 로컬 그룹을 만들며, 리더가 없으면 먼저 복원된 캐릭터를 리더로 지정한다. 그룹 저장 상태가 없는 서버에서 일반 멤버가 먼저 복원되면 원래 리더 정보가 보존되지 않는다. prototype identity 또한 새 로그인에서 생성하는 GUID이며 계정의 영속 캐릭터 조회와는 다르다.

그룹 ID, 명시적 LeaderCharacterId, 멤버십·revision을 가진 snapshot을 저장 서비스가 제공하고 로컬 subsystem은 online projection을 관리하게 한다. party의 세션 수명과 guild의 영속 수명을 구분한다. 조회·복원 순서로 리더가 바뀌지 않도록 하고 오래된 복원은 revision으로 거절한다. 현재 API는 서버 내부 호출이므로 이를 곧바로 클라이언트 권한 취약점이라고 평가하지 않는다.

완료 조건: 멤버 우선 재접속, 리더 지연 접속, 오래된 membership 복원, 그룹 탈퇴와 동시 복원, 서버 간 이동. 계정 identity adapter가 들어올 때 함께 검증한다.

### R08. Controller보다 오래 사는 LocalPlayer 자원에 명시적 소유권 부여

근거: `Source/Project_J/Game/Project_JPlayerController.cpp:108,131,139` 및 헤더의 lifecycle 선언.

Controller는 LocalPlayer Enhanced Input subsystem에 mapping context를 추가하고 모바일 위젯을 생성한다. 프로젝트 코드에는 이 추가분을 추적·해제하는 대응 경로가 없다. 같은 Controller의 Pawn 교체는 그대로 유지되어야 하지만, 같은 LocalPlayer 아래 다른 Controller가 들어오는 경로에서는 이전 context가 남을 가능성을 따로 검증해야 한다. 위젯은 엔진 travel 정리와 소유 관계를 확인해야 하므로 누수를 확정하지 않는다.

자기 입력 context 목록과 자신이 만든 widget을 소유 기록으로 관리한다. 공유 context에는 등록 주체별 lease 또는 소유 정책을 두고, 전체 `ClearAllMappings`로 다른 시스템 설정을 지우지 않는다. 게임 중 입력과 editor 전용 진단 코드의 현재 컴파일 경계도 유지한다.

완료 조건: Controller 교체, seamless/non-seamless travel, 로컬 분할 화면, 같은 context를 쓰는 다른 기능과의 공존.

### R09. 무기 모션 편집기는 실제 attachment socket 좌표계를 사용

근거: `Source/Project_JCharacterEditor/Private/Project_JCharacterEditorModule.cpp:117,166,324–340`.

편집기 주석과 데이터는 socket 상대 Transform인데, gizmo delta 변환에는 attach parent의 component transform만 사용한다. 소켓이 component와 다르게 회전하면 world drag/rotation을 잘못된 로컬 축으로 변환한다. 실제 `GetAttachSocketName()`의 socket world transform을 기준으로 편집 변환을 계산하고 scale 지원 범위도 명시해야 한다.

또한 전역 `SelectedMotionKey`와 전체 preview mesh 검색은 같은 몽타주를 여러 Persona 창에서 열 때 선택한 창을 명확히 식별하지 못한다. 편집 세션을 preview scene/viewport에 귀속시키고 transaction은 정상 종료·취소·창 종료 모두에서 닫는다.

완료 조건: 90도 회전한 소켓, 회전된 character mesh, 두 Persona 창, drag 중 취소, undo/redo, runtime과 preview Transform 일치. 실제 UI 조작으로 재현한 결과는 아니며 좌표 변환 경로 확인에 근거한다.

### R10. DA 제작 비용과 리그 오류를 제작 단계에서 관리

근거: `Source/Project_JCharacterEditor/Private/Authoring/Project_JContentBundleAuthoring.cpp:131–178`, `Source/Project_JAnimationNodes/Private/Animation/Project_JAnimGraphNode_GuidedHandIK.cpp`.

Bundle 제작의 plan → transient draft → validate → publish, 기존 asset 덮어쓰기 금지, 자동 저장 금지는 유지한다. 다만 ID 중복 확인은 같은 정의 class의 AssetRegistry 결과마다 `GetAsset()`을 호출하므로 콘텐츠가 늘면 preview/create에서 동기 로드가 늘어난다. ClassId/AdvancementId를 검색 가능한 registry metadata로 노출하고, 로드된 미저장 asset overlay와 생성 직전 최종 재검사를 결합한다. 기존 asset의 metadata가 없는 과도기에는 제한된 fallback 검증이 필요하다.

GuidedHandIK의 runtime 방어와 값 기반 solver는 좋지만 editor node는 주로 표시 정보만 제공한다. 정적으로 알 수 있는 bone 역할·chain 연결·effector가 자기 출력에 의존하는 구성·지원하지 않는 scale을 compile/asset validation에서 설명할 수 있다. 동적으로 선택되는 profile은 실제 해석된 리그 조합 검증과 연결한다. 이전 A06/A07의 조합 DA 검증기를 재사용하고 같은 규칙을 editor/runtime에 복제하지 않는다.

완료 조건: 저장·미저장 중복 ID, 오래된 registry metadata, 수백 definition의 preview 비용, 잘못된 rig에 대한 제작 오류 메시지. 새 추상화 계층 수보다 제작자가 실패 원인을 즉시 알 수 있는지가 목표다.

### R11. NPC 구성은 검증된 하나의 활성화 계약으로 제공

근거: `Source/Project_JCharacter/Private/Project_JNPCCharacter.cpp`, `Private/Components/Project_JNPCActionComponent.cpp:42`, `Private/System/Project_JNPCDecisionSubsystem.cpp`, `Private/Combat/Project_JGameplayAbility_NPCAttack.cpp`.

현재 scoring 등록, action 활성화, 공격 ability handle, team, controller/path 준비는 명시적으로 조립하는 구조다. 이는 실험을 opt-in으로 유지하는 장점이 있다. 정식 NPC 콘텐츠가 많아질 때 각각의 순서를 BP·spawner마다 반복하면 일부만 활성화된 NPC를 만들기 쉽다.

NPC definition의 데이터와 runtime activation 결과를 분리한다. definition은 전투/공격 capability, team 공급자, decision/action 설정을 참조하고, 활성화 함수는 준비 검증 → 등록/grant → action 시작을 수행하며 실패 시 자신이 만든 것만 역순 해제한다. revision 변경 시 decision/path 결과를 무효화하는 기존 경계를 연결한다. 실제 faction adapter가 team 변경을 전달하게 한다.

완료 조건: 잘못된 공격 capability, controller 미준비, 중복 활성화, faction 변경, 일부 등록 실패, respawn. 현재 행동 계층을 StateTree 등으로 전면 교체할 근거는 찾지 못했다.

### R12. 다음 최적화는 전체 GT 비용·대기 시간·적용률을 함께 측정

근거: `Source/Project_JCharacter/Private/System/Project_JNPCDecisionSubsystem.cpp:384`, `Private/Mass/Project_JMassRepresentationSubsystem.cpp:121,211`, `Private/System/Project_JPresentationBudgetSubsystem.cpp`, `Source/Project_JCore/Private/System/Project_JVisualAssetSubsystem.cpp`.

| 관찰 | 개선 방향 | 도입 조건 |
| --- | --- | --- |
| Decision의 최대 2,048 target snapshot 생성은 호출 도중 쪼개지지 않음. `IsDead` 등 GT interface 호출도 포함 | snapshot·query·apply의 전체 GT P95/P99와 최대 단일 작업 측정. 필요하면 공유 observer snapshot/공간 인덱스 도입 | budget 초과가 실제 관찰될 때 |
| Mass는 전환 수를 제한해도 매 tick 전체 entry × observer 거리 검사와 eligibility 검사를 수행 | 전환 시간과 전체 tick·worker join 시간을 별도 집계 | NPC·관전자 규모 확대 시 |
| 모든 active GE를 demotion 금지 조건으로 삼음 | 상시 장비 GE가 있는 실제 NPC의 Mass 적용률부터 측정. 이후 명시적 demotion capability와 combat pin 사용 | 적용률이 낮을 때. 임의로 GE 검사 삭제 금지 |
| Mass는 Character/ASC를 유지한 채 이동 실행만 전환 | actor 메모리 절감으로 홍보하지 말고 CPU·표현 비용과 메모리를 따로 측정 | 기존 설계 유지 |
| Presentation queue 최대 4,096, tick당 최대 4 적용 | visibility 우선순위와 aging, enqueue→apply 지연·취소 비율 측정 | 긴 cosmetic 대기가 관찰될 때 |
| Visual/Path의 admission·timeout·cancel 결과 표현이 서로 다름 | 공통 결과 어휘와 지표만 정렬하고 실행 관리자는 각 수명 경계에 유지 | 운영 진단 필요 시 |

예컨대 presentation queue가 가득 차면 count 제한만으로도 1,024 tick이 필요하다. 60 Hz 가정에서 약 17초이며 시간 budget 때문에 더 길어질 수도 있다. 이는 최악 적체 계산이지 이번에 측정한 지연 수치가 아니다. 모든 frame에 hard time bound가 있다고 표현해서는 안 된다.

### R13. 기존 장비 로딩 수명 테스트의 준비 조건을 현재 계약에 맞추기

**실행 재현:** `ProjectJ.Modernization.EquipmentLoadLifecycle`가 추가 suite와 단독 실행에서 모두 실패했다.

근거: `Source/Project_JCharacter/Private/Tests/Project_JSystemsModernizationTests.cpp:82–126`, `Private/Components/Project_JEquipmentRuntimeComponent.cpp:275–291`.

테스트는 bare `ACharacter`와 `SkeletalCube` 장비를 만들고, 캐릭터 메시의 호환 pose source 또는 유효 부착 socket을 준비하지 않은 상태에서 visual 생성을 기대한다. runtime은 이 조건을 거절하며 경고를 남긴다. 실패 assertion은 `Completed asset creates visual`(107행), `Visual before BeginPlay`(120행)이다. **현재 근거는 fixture와 runtime 계약의 불일치를 가리키며 실제 장비 로딩 기능의 회귀를 입증하지 않는다.**

수명 테스트에는 호환 mesh/socket fixture를 제공하고, 호환되지 않는 조합은 별도의 거절 테스트로 유지한다. assertion을 삭제하거나 runtime 검사를 느슨하게 만들어 통과시키면 안 된다. teardown·lease 반환 검증도 그대로 유지한다.

두 실행 모두 프로세스 종료 코드는 0이었지만 report에는 실패가 있었다. 기존 `Measure-Mass`, `Measure-Navigation`, `Measure-CrowdE`, `Measure-Experiments`는 이미 결과 JSON을 검사한다. 이를 결함으로 중복 제안하지 않고, 이 테스트도 빠지지 않는 명시적인 회귀 suite/필수 테스트 목록을 유지하도록 권장한다.

### R14. 일회성 migration 도구를 재사용하려면 복구 단위를 명시

근거: `Plugins/ProjectJExperiments/Source/ProjectJExperiments/Private/AnimationMigrationTests.cpp:109–168`.

commandlet은 `-Apply`를 요구하고 세 asset을 backup한 뒤 변환·컴파일 후 저장한다. 자동화 preflight는 transient 복제본만 수정한다. 이 분리는 유지할 가치가 있다. 다만 실제 적용은 asset을 순서대로 저장하므로 뒤의 SavePackage가 실패하면 앞의 asset은 이미 저장되어 있다. backup은 있지만 자동 rollback이나 단계별 receipt는 없다. 이미 migration된 그래프는 preflight에서는 허용하지만 apply에서는 예상 교체 수 검사에 걸려 재실행을 거절한다.

일회성 도구로 남긴다면 이 제한과 정확한 복구 방법을 문서화하면 된다. 반복 도구로 만들 때는 dry-run plan과 원본 hash, 이미 적용됨/미적용/부분 적용 상태, asset별 저장 receipt, 복구 절차를 추가한다. 세 파일의 저장이 원자적이라고 표현하지 않는다. 이번 감사에서는 도구 실행이나 asset 변경을 하지 않았다.

## 3. 유지할 구조와 시스템별 판단

| 시스템 | 유지할 점 / 판정 | 다음 경계 |
| --- | --- | --- |
| 모듈 | Core·GAS·Mount·Character·composition root 의존성 방향, MMO의 Core-only 계약 | 역방향 include·dependency를 현재 validator로 계속 차단 |
| TargetScoring | 불변 값 입력, 직렬 기준과 병렬 결과 동등성, epoch·취소·물리 작업 수 추적, 종료 drain | UObject를 worker로 옮기지 않기 |
| NPC Decision | 공유 target snapshot, agent/context revision, bounded admission, stale 결과 거절 | R11/R12 |
| NPC Path | UE navigation worker 사용, GT 수용·전달, 취소 후 실제 작업 slot 유지, urgency·aging | 별도 navigation thread를 다시 만들 근거 없음 |
| NPC Action / GA | 자신이 시작한 move·attack만 취소, 거리·시야·생존 재검사, non-cancelable ability 소유권 유지 | R02/R11, 미지원 attack capability는 명시적 거절 유지 |
| Mass | 값 전용 fragment/processor, generation, promote 대기 중 freeze, 보수적 전투 보호 | R02/R12. 실험 spawner/trait와 production bridge를 동일 구현으로 오인하지 않기 |
| GuidedArm/HandContact | 손바닥→wrist 보정 1회, pre-IK guidance, helper bone 보존, alpha 1회, LOD·teleport reset | solver 재작성보다 R10 제작 검증 |
| Retarget/mesh resolver | pose source와 시각 follower 구분, 호환 skeleton·socket 검사 | NPC rig는 visual override 등 실제 공급 계약으로 검증 |
| ABA / update scheduling | 전투 중요 구간 즉시 복귀, subsystem이 얻은 allocator 소유권만 해제 | 전체 actor를 임의로 AnyThread tick으로 이동시키지 않기 |
| Visual asset / Presentation | 공유 load group, owner lease, revision, stale callback 거절, 수용·적용 제한 | R12 결과·지연 가시성 |
| Message router | callback 중 listener 변경을 견디는 snapshot dispatch, 로컬 이벤트 역할 | 사용처 증가 시 owner별 subscription token. RPC 대체로 사용하지 않기 |
| MVVM | event/FieldNotify 기반 UI, 불필요한 polling 없음 | 복합 자원 snapshot의 UI 일관성은 R01과 연결 |
| AssetManager / GameData | 얇은 시작 계층 | 현재 readiness 표시는 모든 콘텐츠 준비 검증이 아님. 실제 의존성이 생길 때 계약 확장 |
| ObjectPoolRegistry / GameFeatureReceiver | 작은 등록·인터페이스 경계 | 실제 pool 구현·feature activation orchestration 완료로 세지 않기 |
| Gateway / Social / Handover | 서버·세션 경계와 실패 분류 기반 | R05–R07, durable/live adapter의 별도 검증 |
| MMO Foundation | CAS·tombstone·bounded memory repository, multi-owner gate, request lifetime, catalog DAG | 205개 catalog 항목은 구현 완료 기능 수가 아님 |
| 복제·Iris diagnostics | owner-only 데이터와 실제 socket fixture, callback count 구분 | 기존 A10. callback 횟수를 전송 byte로 해석하지 않기 |
| PSO / warmup | UE 버전 조건·collector layout 방어, 명시적 precache | monolithic/cooked hook은 Editor NullRHI 성공으로 검증되지 않음 |
| 진단·soak·cooked fixture | 명시적 opt-in, 자신이 만든 actor·trace·자원 정리, 실제 결과 파일 | fixture 성능과 실제 authored gameplay/FPS 구분 |
| 실험 플러그인 | 기본 비활성 Editor 전용, worker 값 전용, RT/Audio/Physics 작업 결과 전달 경계 | R14. GPU readback 장기 미완료 시 자원 수명을 유지하면서 runner에 stall을 명확히 보고하는 경로 보강 가능 |
| 측정 스크립트 | 결과 JSON·누락 테스트·실제 trace 확인, 원본 evidence 보존, direct UBT 및 프로세스 대기 | 비교 report의 스키마와 필수 테스트 명세를 맞추기 |

검토 후 채택하지 않은 주장도 있다. Gateway의 `GetWorld()->GetTimerManager()` 사용만으로 travel 시 timer 유실을 단정할 수 없다. UE 5.8 `UWorld::GetTimerManager()`는 OwningGameInstance가 있으면 GameInstance timer manager를 반환한다. 엔진 소스까지 대조하여 이 항목은 결함에서 제외했다.

## 4. 실행 검증과 한계

| 검증 | 결과 | 해석 |
| --- | --- | --- |
| 이전 캐릭터 감사 | Editor Development 빌드 성공, 선정 자동화 79개 성공 | 이전 보고서의 근거. 이번 실행 수에 중복 합산하지 않음 |
| 이번 잔여 영역 자동화 | **46개: 일반 성공 39, 경고 동반 성공 6, 실패 1, 미실행 0** | 선택한 기존 테스트의 결과이며 새 R01–R12 경계 사례를 모두 재현한 것이 아님 |
| 실패 테스트 단독 실행 | 1개 실패, 동일 두 assertion | R13 재현. suite 순서 의존으로 보지 않을 근거 |
| `Validate-MMOArchitecture.py` | 성공: 205 features / 20 domains, module DAG와 생성 catalog 일치 | 구현 기능 205개를 검증했다는 의미는 아님 |
| `Docs/Benchmarks/recompute.py` | 성공: 보관 데이터 checksum 일치 및 수치 재계산 | 과거 benchmark 근거의 정합성 확인. 오늘의 신규 성능 측정 아님 |
| 코드·설정·asset 변경 | 없음 | 보고서·범위표·검증 요약과 인덱스만 추가/수정 |

추가 suite 필터:

```text
ProjectJ.MMO+ProjectJ.AsyncTargeting+ProjectJ.NPCDecision+ProjectJ.NPCAction+ProjectJ.NPCGameplay+ProjectJ.Integrated+ProjectJ.GroupA+ProjectJ.GroupD+ProjectJ.Modernization+ProjectJ.EquipmentClient
```

실행은 `UnrealEditor-Cmd.exe`, `-unattended -NullRHI -nosound -NoLiveCoding -nop4 -nosplash`, `-TestExit=Automation Test Queue Empty` 및 별도 report/log 경로를 사용했다. 최초 제한된 실행은 테스트 전에 writable DDC 부재로 종료되었으며, 필요한 로컬 DDC 접근이 허용된 재실행으로 결과를 얻었다. 실행 중인 build/editor를 중단하지 않았다. 소스를 수정하지 않아 추가 C++ 빌드는 수행하지 않았다. Python launcher에는 설치된 interpreter가 없어 UE에 포함된 Python으로 두 읽기 전용 validator를 실행했다.

경고 동반 성공 6개는 AsyncTargeting의 임시 world 정리, NPC weapon teardown의 world context, native navigation fixture 초기 Recast/CrowdManager 준비 로그에 해당한다. 경고를 실패로 바꾸거나 깨끗한 로그로 숨기지 않고 결과 요약에 보존했다.

추적용 파일:

- [저장된 실행 검증 요약](Project_J_Remaining_Source_Validation_2026-10-03.json)
- 원본 suite report: `Saved/Automation/RemainingArchitectureAudit_20261003/index.json`
- 원본 suite log: `Saved/Logs/RemainingArchitectureAudit_20261003_Tests.log`
- 단독 report: `Saved/Automation/RemainingArchitectureAudit_20261003_Isolated/index.json`
- 단독 log: `Saved/Logs/RemainingArchitectureAudit_20261003_Isolated.log`

원본 `Saved` 산출물은 로컬 evidence다. 저장된 요약에는 테스트별 상태·오류·경고와 실패 세부를 보존한다. 플러그인 GPU/Audio/PCG 실험, 렌더링·cook·실제 네트워크 다중 프로세스 fixture는 이번에 다시 실행하지 않았다.

## 5. 구현 순서와 완료 기준

1. **검증 기반 복구:** R13의 fixture를 현재 계약에 맞추고 실패 거절 테스트도 유지한다.
2. **현재 정확성:** R01 자원 불변식, R04 Notify 실행 소유권, R09 편집 좌표계. 기존 캐릭터 감사 A01–A05와 함께 변경 충돌·회귀 범위를 잡는다.
3. **전환 복구:** R02 Mass와 R03 비행의 실패 전이. 정상 성공보다 중단·재진입·소유자 교체를 먼저 검증한다.
4. **콘텐츠 생산:** R10 제작 검증, R11 NPC 조립, 필요 시 R08 LocalPlayer 소유권. 이전 A06–A09의 effective configuration·스킬·DA 개선과 연결한다.
5. **서비스 연결:** R05–R07. 실제 backend 계약이 정해지기 전에 일반 HTTP 성공을 업무 commit으로 사용하지 않는다.
6. **측정과 도구:** R12는 전체 비용·지연·적용률에 근거해 선택하고, R14는 migration 재사용 시 진행한다.

각 구현은 기존 경계를 유지한 작은 변경과 명확한 실패 사례로 완료를 판단한다. OTM·Start·Stop, 장비 전환, 공격 판정과 표현 분리는 기존 감사의 회귀 기준을 그대로 적용한다. 이번 문서는 그 구현을 완료했다는 보고서가 아니라, 다음 변경을 구체적으로 선택할 수 있도록 만든 감사 결과다.
