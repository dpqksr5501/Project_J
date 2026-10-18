# 6850ed45 소스 감사 수정·검증 마감

원본 `Project_J_Source_Audit_6850ed45.zip`의 README·Runtime·Tools 보고서와 기준 커밋 `6850ed4556b5df0468d6809fd190950e1b1a4c82`를 대조한 1·2·3차 누적 결과다. 별도 MMORPG 아키텍처 보고서의 미래 기능은 구현 범위에 포함하지 않았다. 1·2차 수정과 기존 미커밋 작업을 보존하고, 재현된 지원 경로의 결함을 최소 변경으로 수정했다.

원본 Runtime 지적 16개, 조건부 위험 R1/R2/R3 3개, Tools 원본 절 1~9를 모두 포함한 **28개**다. `TOOLS-01`~`TOOLS-09`는 원본 Tools 보고서의 실제 절 번호에 대응하도록 붙인 별칭이다. 신규 수정 14개, 앞선 수정의 해결 상태를 코드·회귀로 확인한 13개, 퇴역 도구의 지원 범위 밖 판단 1개로 정리했다. 조건부 위험은 아래 실제 재현 범위만 결함으로 확정했다.

이 문서는 커밋 전 검증 대상과 결과를 보존한다. 마지막 코드 검증 이후 커밋 준비에서는 문서만 추가했다. 검증된 입력 지문 885개를 다시 대조했으며 변경은 없었다. 관련 없는 `Scripts/World/Build-StarterMountainHLOD.py`는 수정·삭제·커밋에서 제외했다.

## 최종 검증과 제한

| 검증 | 실제 결과와 경계 |
| --- | --- |
| 최종 native 통합 회귀 | 예상 70개 모두 실행: 일반 성공 68, 경고 포함 성공 2, 실패·미실행·누락 0. MouseChordLifecycle 1건과 RetainedAvatarEquipment 2건의 프로필 경고를 일반 성공과 구분했다. |
| 기존 Handover 영향 회귀 | 통합 70개와 별개의 5개 모두 성공, 경고·누락 0. 두 집합 총 75개이며 반복 실행을 합산한 수가 아니다. |
| Tools 회귀 | 최종 Python 16개, wrapper 모의 11개, provenance 비교 6개 성공. 0개 검증·invalid 자산·저장 I/O 실패·출력 충돌·퇴역 복구 거절의 의도된 실패를 별도 확인했다. |
| 실제 자산 검증 | WorldDefinition 1 + LocomotionProfile 1 + Montage 91 = 93개, 실패·skipped·unable·validationWarnings 0. 0개 실행은 exit 2, invalid fixture는 checked 2 / passed 1 / failed 1 / exit 2로 거절했다. |
| 실제 네트워크 | dedicated Editor 서버와 실제 socket 클라이언트 2개에서 Baseline, LagLoss, ReorderLoss 3조건 성공. 실제 지연 타격 Health 변경과 cue 복제·rate·착지 상태를 관찰했다. |
| Editor / Game 빌드 | direct UnrealBuildTool 전체 target 모두 exit 0. 최종 Source 670개가 두 빌드 입력과 일치한다. 진행 중 빌드 중단·동시 실행·과거 바이너리 복원 없이 수행했다. |
| standalone Server 빌드 | **환경 차단**: 설치 엔진이 Server target을 지원하지 않아 exit 6. 첫 오류는 “Server targets are not currently supported from this engine distribution.” 호환 UE 5.8 소스 빌드 엔진이 준비되면 direct UBT를 재실행해야 한다. 새 엔진 다운로드·설치는 하지 않았다. Editor dedicated 서버 증거로 Server 바이너리 성공을 대체하지 않는다. |
| MainWorld Cook | exit 0, **경고 포함 성공**. 원시 warning 57줄 / 고유 메시지 24개, error 0. GameplayCue fallback, 메시 물리 데이터 재저장 권고, texture GUID·HLOD streaming 경고가 남는다. 경고 제거 목적으로 기존 에셋을 임의 저장하지 않았다. |
| Stage·패키지 / 실제 실행 | Stage·패키지 exit 0, warning 0. 실제 패키지의 MainWorld smoke exit 0 / success true: DA_MainWorld, WorldState 1, place 20, registered 1, partition 1, netmode 0. 패키지 실행 파일이 최종 Game 빌드와 일치했다. |
| 에셋·작업 보존 | 기존 Content 11,445개 파일의 크기·수정 시각 변화 0. 승인된 격리 WP 맵·외부 패키지와 invalid fixture는 검증 후 제거했다. MainWorld와 기존 에셋 이동·저장·삭제는 하지 않았다. |
| 주관적 품질 | 애니메이션 자연스러움·타격감은 사용자 플레이 확인이 남는다. 아래 체크리스트로 기술 검증과 구분한다. |

실제 네트워크 조건은 Baseline 0, LagLoss 지연 75±25ms / loss 10% / dup 5%, ReorderLoss 지연 0 / order 1 / loss 10% / dup 15%다. NetDriver 적용값과 각 클라이언트 영수증을 보존했다. ReorderLoss에서 실제 sequence inversion 관찰 수는 0이므로 임의 재정렬의 완전한 탐색을 주장하지 않는다. cue 복구 cases 10/11은 특정 A 이벤트 누락과 B RPC/속성 도착 순서를 제어하고 ACK로 확인했다. 성능·대규모 부하 검증은 수행하지 않았다.

## 항목별 종료 상태

| 원본 항목 | 상태 |
| --- | --- |
| CHAR-01 | 이미 해결됐음을 코드·테스트로 확인 |
| CHAR-04 | 이미 해결됐음을 코드·테스트로 확인 |
| COMBAT-04 | 이미 해결됐음을 코드·테스트로 확인 |
| CHAR-02 | 이미 해결됐음을 코드·테스트로 확인 |
| CHAR-03 | 이미 해결됐음을 코드·테스트로 확인 |
| G1 | 이미 해결됐음을 코드·테스트로 확인 |
| COMBAT-01 | 이미 해결됐음을 코드·테스트로 확인 |
| COMBAT-02 | 수정하고 관련 검증 통과 |
| COMBAT-03 | 수정하고 관련 검증 통과 |
| G2 | 수정하고 관련 검증 통과 |
| FND-01 | 수정하고 관련 검증 통과 |
| ANIM-1 | 이미 해결됐음을 코드·테스트로 확인 |
| ANIM-2 | 이미 해결됐음을 코드·테스트로 확인 |
| ANIM-EXTRA-01 | 이미 해결됐음을 코드·테스트로 확인 |
| ANIM-3 | 수정하고 관련 검증 통과 |
| G3 | 수정하고 관련 검증 통과 |
| R1 | 수정하고 관련 검증 통과 |
| R2 | 수정하고 관련 검증 통과 |
| R3 | 수정하고 관련 검증 통과 |
| TOOLS-01 | 이미 해결됐음을 코드·테스트로 확인 |
| TOOLS-02 | 수정하고 관련 검증 통과 |
| TOOLS-03 | 수정하고 관련 검증 통과 |
| TOOLS-04 | 수정하고 관련 검증 통과 |
| TOOLS-05 | 이미 해결됐음을 코드·테스트로 확인 |
| TOOLS-06 | 이미 해결됐음을 코드·테스트로 확인 |
| TOOLS-07 | 지원 범위 밖 확인 |
| TOOLS-08 | 수정하고 관련 검증 통과 |
| TOOLS-09 | 수정하고 관련 검증 통과 |

각 항목의 파일은 누적 수정·회귀와 관련된 위치다. 이미 해결된 항목은 3차에서 같은 문제를 중복 수정했다는 뜻이 아니다. 증거 경로는 프로젝트 루트 기준의 로컬 `Saved` 자료이며 Git에 로그·바이너리를 포함하지 않는다. 새 PC에서는 영구 테스트와 아래 조건으로 다시 실행해야 한다.

### CHAR-01 — 이미 해결됐음을 코드·테스트로 확인

**재현 조건:** 같은 서버 시각에 TIP false 또는 true/false 요청을 반복한다.

**변경·유지 판단:** 1차 서버 시간/yaw 예산과 admission을 보존했다. client active=false가 burst를 재생성하지 않는다.

**관련 파일:** [Source/Project_JCharacter/Private/Project_JPlayerCharacter.cpp](../../../Source/Project_JCharacter/Private/Project_JPlayerCharacter.cpp), [Source/Project_JCharacter/Public/Project_JPlayerCharacter.h](../../../Source/Project_JCharacter/Public/Project_JPlayerCharacter.h), [Source/Project_JCharacter/Private/Tests/Project_JSourceAuditTests.cpp](../../../Source/Project_JCharacter/Private/Tests/Project_JSourceAuditTests.cpp).

**테스트:** `ProjectJ.SourceAudit.TIPBudget`.

**실제 native 결과:** `ProjectJ.SourceAudit.TIPBudget`: Success (warning 0, error 0).

**추가 결과:** 위 명명된 최종 통합 회귀가 모두 Success. 경고 수는 actualNativeResults에 별도 기록.

**증거:** `Saved/Audit6850/Round3/Automation/FinalSourceIntegration`, `Saved/Audit6850/ReviewResults.txt`.

**남은 제한:** 이번 네트워크 실험은 SSR/복구용이다. TIP 조작의 실제 socket 재현이나 화면상의 자연스러움을 대신 증명하지 않는다.

### CHAR-04 — 이미 해결됐음을 코드·테스트로 확인

**재현 조건:** 구형 equip/unequip RPC와 새 요청을 섞거나 교체 후 stale instance ID로 해제한다.

**변경·유지 판단:** 1차 공통 네트워크 admission과 instance identity 정책을 보존했다. 신뢰된 서버 내부 API는 별도로 유지한다.

**관련 파일:** [Source/Project_JCharacter/Private/Components/Project_JEquipmentManagerComponent.cpp](../../../Source/Project_JCharacter/Private/Components/Project_JEquipmentManagerComponent.cpp), [Source/Project_JCharacter/Public/Components/Project_JEquipmentManagerComponent.h](../../../Source/Project_JCharacter/Public/Components/Project_JEquipmentManagerComponent.h), [Source/Project_JCharacter/Private/Tests/Project_JSourceAuditTests.cpp](../../../Source/Project_JCharacter/Private/Tests/Project_JSourceAuditTests.cpp).

**테스트:** `ProjectJ.SourceAudit.EquipmentNetwork`.

**실제 native 결과:** `ProjectJ.SourceAudit.EquipmentNetwork`: Success (warning 0, error 0).

**추가 결과:** 위 명명된 최종 통합 회귀가 모두 Success. 경고 수는 actualNativeResults에 별도 기록.

**증거:** `Saved/Audit6850/Round3/Automation/FinalSourceIntegration`, `Saved/Audit6850/ReviewResults.txt`.

**남은 제한:** 구형 slot-only RPC는 식별자 부족으로 거절된다. 과거 배포 클라이언트 호환은 별도 검증하지 않았다.

### COMBAT-04 — 이미 해결됐음을 코드·테스트로 확인

**재현 조건:** 동일 prediction key로 A→B→A 후 첫 A 시각을 재전송하거나, 전환 직전 A의 정상 요청을 늦게 보낸다.

**변경·유지 판단:** 1차 실행 시작시각/history 격리를 유지했다. 사용자 위임 판단: 현재 실행만 허용한다. 완료 노드의 새 피해 보상은 추가하지 않고 서버가 이미 적용한 피해는 보존한다.

**관련 파일:** [Source/Project_JCharacter/Private/Components/Project_JCombatHitValidationComponent.cpp](../../../Source/Project_JCharacter/Private/Components/Project_JCombatHitValidationComponent.cpp), [Source/Project_JCharacter/Public/Components/Project_JCombatHitValidationComponent.h](../../../Source/Project_JCharacter/Public/Components/Project_JCombatHitValidationComponent.h), [Source/Project_JCharacter/Private/Tests/Project_JSourceAuditTests.cpp](../../../Source/Project_JCharacter/Private/Tests/Project_JSourceAuditTests.cpp), [Source/Project_J/Testing/Project_JAuditNetworkFixture.cpp](../../../Source/Project_J/Testing/Project_JAuditNetworkFixture.cpp), [Source/Project_J/Testing/Project_JAuditNetworkFixture.h](../../../Source/Project_J/Testing/Project_JAuditNetworkFixture.h), [Source/Project_J/Testing/Project_JNetworkLoadFixture.cpp](../../../Source/Project_J/Testing/Project_JNetworkLoadFixture.cpp), [Scripts/Tests/Run-SourceAuditNetwork.ps1](../../../Scripts/Tests/Run-SourceAuditNetwork.ps1).

**테스트:** `ProjectJ.SourceAudit.SSRExecution`, `실제 socket cases 0..5: 정상/중복/서버 선판정/이전 노드/A-B-A/EndAttack/새 정상 실행`.

**실제 native 결과:** `ProjectJ.SourceAudit.SSRExecution`: Success (warning 0, error 0).

**추가 결과:** 실제 socket 3조건 모두 성공; 정상/새 정상 피해1회, 서버 선판정 피해 보존, 나머지 이전 실행 피해0; 양 클라이언트 Health 일치.

**증거:** `Saved/Audit6850/Round3/Automation/FinalSourceIntegration`, `Saved/Audit6850/Round3/Network/Summary.json`, `Saved/Audit6850/Round3/Network/FinalBaseline`, `Saved/Audit6850/Round3/Network/FinalLagLoss`, `Saved/Audit6850/Round3/Network/FinalReorderLoss`.

**남은 제한:** 실제 dedicated Editor-Cmd -server + 두 클라이언트다. 상속된 실제 ServerRequestSSRHit와 실제 Health -10 효과/복제를 사용했다. 이력 조회 테스트나 입력 부하의 대체 증거가 아니다. 별도 Server target 바이너리 지원 여부는 빌드 항목과 구분한다. 새 유예 보상을 원하면 불변 실행 문맥과 실행별 중복 정책 결정이 필요하다.

### CHAR-02 — 이미 해결됐음을 코드·테스트로 확인

**재현 조건:** bag split/merge/swap의 동기 콜백에서 실제 UnequipSlot을 호출한다.

**변경·유지 판단:** 1차 unlock 성공 뒤 슬롯 제거와 알림 후 수명 재확인을 보존했다. 실패 시 슬롯과 lock 상태를 함께 유지한다.

**관련 파일:** [Source/Project_JCharacter/Private/Components/Project_JEquipmentManagerComponent.cpp](../../../Source/Project_JCharacter/Private/Components/Project_JEquipmentManagerComponent.cpp), [Source/Project_JCharacter/Private/Tests/Project_JSourceAuditTests.cpp](../../../Source/Project_JCharacter/Private/Tests/Project_JSourceAuditTests.cpp).

**테스트:** `ProjectJ.SourceAudit.BagUnequip`.

**실제 native 결과:** `ProjectJ.SourceAudit.BagUnequip`: Success (warning 0, error 0).

**추가 결과:** 위 명명된 최종 통합 회귀가 모두 Success. 경고 수는 actualNativeResults에 별도 기록.

**증거:** `Saved/Audit6850/Round3/Automation/FinalSourceIntegration`, `Saved/Audit6850/ReviewResults.txt`.

**남은 제한:** 인벤토리 없는 prototype 경로의 별도 계약을 유지했다. 모든 제작 BP 리스너가 이 콜백을 만든다는 주장은 하지 않는다.

### CHAR-03 — 이미 해결됐음을 코드·테스트로 확인

**재현 조건:** 부분/최종 소비 알림에서 owner/component 종료, ASC 교체 또는 사망을 만든다.

**변경·유지 판단:** 소비 유지 + 효과/cooldown 없음 + Unavailable 정책을 보존했다. 2차 UI 경고는 소비 가능성을 설명하며 동일 요청을 새 ID로 자동 재시도하지 않는다.

**관련 파일:** [Source/Project_JCharacter/Private/Components/Project_JInventoryComponent.cpp](../../../Source/Project_JCharacter/Private/Components/Project_JInventoryComponent.cpp), [Source/Project_J/UI/Project_JInventoryViewModel.cpp](../../../Source/Project_J/UI/Project_JInventoryViewModel.cpp), [Source/Project_J/Private/Tests/Project_JSourceAuditPhase2UITests.cpp](../../../Source/Project_J/Private/Tests/Project_JSourceAuditPhase2UITests.cpp), [Source/Project_JCharacter/Private/Tests/Project_JSourceAuditTests.cpp](../../../Source/Project_JCharacter/Private/Tests/Project_JSourceAuditTests.cpp).

**테스트:** `ProjectJ.SourceAudit.ConsumableContinuation`, `ProjectJ.SourceAudit2.ConsumedUnavailableUI`, `ProjectJ.UI.Compact.ConsumableAuthorityAndCooldown`, `ProjectJ.UI.Inventory.DelayedReplyTimeoutAndRebind`.

**실제 native 결과:** `ProjectJ.SourceAudit.ConsumableContinuation`: Success (warning 0, error 0); `ProjectJ.SourceAudit2.ConsumedUnavailableUI`: Success (warning 0, error 0); `ProjectJ.UI.Compact.ConsumableAuthorityAndCooldown`: Success (warning 0, error 0); `ProjectJ.UI.Inventory.DelayedReplyTimeoutAndRebind`: Success (warning 0, error 0).

**추가 결과:** 위 명명된 최종 통합 회귀가 모두 Success. 경고 수는 actualNativeResults에 별도 기록.

**증거:** `Saved/Audit6850/Round3/Automation/FinalSourceIntegration`, `Saved/Audit6850/Round2/ReviewResults.txt`.

**남은 제한:** 소비 rollback이나 새 보상을 추가하지 않았다. 원본의 크래시 가능성을 재현된 크래시로 승격하지 않는다.

### G1 — 이미 해결됐음을 코드·테스트로 확인

**재현 조건:** 동일 스킬 두 퀵슬롯/UI+직접 입력/chord를 누른 뒤 한 출처만 해제하거나 종료한다.

**변경·유지 판단:** 2차 공통 입력 실행 경계의 출처별 receipt/lease와 마지막 출처 해제를 보존했다.

**관련 파일:** [Source/Project_J/UI/Project_JPlayerUIComponent.cpp](../../../Source/Project_J/UI/Project_JPlayerUIComponent.cpp), [Source/Project_J/UI/Project_JPlayerUIComponent.h](../../../Source/Project_J/UI/Project_JPlayerUIComponent.h), [Source/Project_JCharacter/Private/Components/Project_JSkillInputExecutionComponent.cpp](../../../Source/Project_JCharacter/Private/Components/Project_JSkillInputExecutionComponent.cpp), [Source/Project_JCharacter/Public/Components/Project_JSkillInputExecutionComponent.h](../../../Source/Project_JCharacter/Public/Components/Project_JSkillInputExecutionComponent.h), [Source/Project_JCharacter/Private/Components/Project_JPlayerInputBindingComponent.cpp](../../../Source/Project_JCharacter/Private/Components/Project_JPlayerInputBindingComponent.cpp), [Source/Project_JCharacter/Public/Components/Project_JPlayerInputBindingComponent.h](../../../Source/Project_JCharacter/Public/Components/Project_JPlayerInputBindingComponent.h), [Source/Project_JCharacter/Private/Components/Project_JSkillInputRouterComponent.cpp](../../../Source/Project_JCharacter/Private/Components/Project_JSkillInputRouterComponent.cpp), [Source/Project_JCharacter/Private/Tests/Project_JSourceAuditPhase2Tests.cpp](../../../Source/Project_JCharacter/Private/Tests/Project_JSourceAuditPhase2Tests.cpp).

**테스트:** `ProjectJ.SourceAudit2.SharedSkillHold`, `ProjectJ.Input.MouseChordLifecycle`.

**실제 native 결과:** `ProjectJ.SourceAudit2.SharedSkillHold`: Success (warning 0, error 0); `ProjectJ.Input.MouseChordLifecycle`: Success (warning 1, error 0).

**추가 결과:** 위 명명된 최종 통합 회귀가 모두 Success. 경고 수는 actualNativeResults에 별도 기록.

**증거:** `Saved/Audit6850/Round3/Automation/FinalSourceIntegration`, `Saved/Audit6850/Round2/ReviewResults.txt`.

**남은 제한:** MouseChordLifecycle의 bare native 캐릭터 프로필 누락 경고를 일반 무경고 성공으로 표기하지 않는다.

### COMBAT-01 — 이미 해결됐음을 코드·테스트로 확인

**재현 조건:** 시작 입력에 대응하는 combo start node가 없어 공격 시작이 거절된다.

**변경·유지 판단:** 2차 실패 시 EndAbility 경로를 보존해 활성 공격 상태와 소유 자원을 잔존시키지 않는다.

**관련 파일:** [Source/Project_JCharacter/Private/Combat/Project_JGameplayAbility_Melee.cpp](../../../Source/Project_JCharacter/Private/Combat/Project_JGameplayAbility_Melee.cpp), [Source/Project_JCharacter/Private/Tests/Project_JSourceAuditPhase2Tests.cpp](../../../Source/Project_JCharacter/Private/Tests/Project_JSourceAuditPhase2Tests.cpp).

**테스트:** `ProjectJ.SourceAudit2.RejectedComboStart`, `ProjectJ.Combat.MeleeLifetime`.

**실제 native 결과:** `ProjectJ.SourceAudit2.RejectedComboStart`: Success (warning 0, error 0); `ProjectJ.Combat.MeleeLifetime`: Success (warning 0, error 0).

**추가 결과:** 위 명명된 최종 통합 회귀가 모두 Success. 경고 수는 actualNativeResults에 별도 기록.

**증거:** `Saved/Audit6850/Round3/Automation/FinalSourceIntegration`, `Saved/Audit6850/Round2/ReviewResults.txt`.

**남은 제한:** 현재 콘텐츠에서 거절 노드가 드물다는 이유로 지원 경로를 제외하지 않았다.

### COMBAT-02 — 수정하고 관련 검증 통과

**재현 조건:** 같은 montage의 다른 section으로 이동하면서 play rate 1→2를 요청한다.

**변경·유지 판단:** 기존 montage/task를 재사용하는 분기에서 ASC CurrentMontageSetPlayRate를 갱신한다. section 이동과 task/notify 소유권을 유지한다.

**관련 파일:** [Source/Project_JCharacter/Private/Combat/Project_JGameplayAbility_Melee.cpp](../../../Source/Project_JCharacter/Private/Combat/Project_JGameplayAbility_Melee.cpp), [Source/Project_JCharacter/Private/Tests/Project_JSourceAuditPhase3Tests.cpp](../../../Source/Project_JCharacter/Private/Tests/Project_JSourceAuditPhase3Tests.cpp), [Source/Project_J/Private/Tests/Project_JSourceAuditPhase3CoreTests.cpp](../../../Source/Project_J/Private/Tests/Project_JSourceAuditPhase3CoreTests.cpp), [Source/Project_J/Testing/Project_JAuditNetworkFixture.cpp](../../../Source/Project_J/Testing/Project_JAuditNetworkFixture.cpp).

**테스트:** `ProjectJ.SourceAudit3.SharedMontageRate`, `실제 socket case 30: authority/owner/simulated montage rate 1→2 및 task 보존`.

**실제 native 결과:** `ProjectJ.SourceAudit3.SharedMontageRate`: Success (warning 0, error 0).

**추가 결과:** 실제 socket 3조건 case30에서 authority/owner/simulated rate1→2, authority/owner 기존 task 보존 성공.

**증거:** `Saved/Validation/Continuation_20261014/Audit6850Phase3BeforeTests`, `Saved/Audit6850/Round3/Automation/FinalSourceIntegration`, `Saved/Audit6850/Round3/Network/Summary.json`.

**남은 제한:** 현재 대검 제작 그래프의 11개 노드는 서로 다른 montage였지만 지원 경로 결함을 격리 재현해 수정했다. 원격 재생 속도는 실제 GAS 복제에서 확인했으며 화면상의 타격감은 사용자 확인 대상이다.

### COMBAT-03 — 수정하고 관련 검증 통과

**재현 조건:** A start/stop 델타가 빠진 뒤 더 최신 B RPC가 전체 recovery snapshot보다 먼저 도착한다.

**변경·유지 판단:** cue multicast에 기존 전체 presentation snapshot을 함께 전달하고 더 새 snapshot을 적용한 뒤 델타/order guard를 처리한다. 새 per-cue manager/history는 추가하지 않았다.

**관련 파일:** [Source/Project_JCharacter/Private/Components/Project_JCombatPresentationComponent.cpp](../../../Source/Project_JCharacter/Private/Components/Project_JCombatPresentationComponent.cpp), [Source/Project_JCharacter/Public/Components/Project_JCombatPresentationComponent.h](../../../Source/Project_JCharacter/Public/Components/Project_JCombatPresentationComponent.h), [Source/Project_JCharacter/Private/Tests/Project_JSourceAuditPhase3Tests.cpp](../../../Source/Project_JCharacter/Private/Tests/Project_JSourceAuditPhase3Tests.cpp), [Source/Project_J/Testing/Project_JAuditNetworkFixture.cpp](../../../Source/Project_J/Testing/Project_JAuditNetworkFixture.cpp).

**테스트:** `ProjectJ.SourceAudit3.CueDeltaRecovery`, `실제 socket cases 10/11: A start 또는 stop 누락 + B RPC 확인 후 full property 발행`.

**실제 native 결과:** `ProjectJ.SourceAudit3.CueDeltaRecovery`: Success (warning 0, error 0).

**추가 결과:** 실제 socket 3조건 cases10/11에서 cue/revision/event/order 복구 양 클라이언트 성공.

**증거:** `Saved/Audit6850/Round3/NativeBeforeCueFix`, `Saved/Validation/Continuation_20261014/Audit6850Phase3HorizonBeforeTests`, `Saved/Audit6850/Round3/Automation/FinalSourceIntegration`, `Saved/Audit6850/Round3/Network/Summary.json`.

**남은 제한:** 두 클라이언트의 실제 Niagara looping cue 상태와 event/revision을 확인했다. 패킷 재정렬 설정은 실제 적용됐지만 수신 SSR 요청 순번 역전은 관측되지 않았다.

### G2 — 수정하고 관련 검증 통과

**재현 조건:** age=0에서 61초 뒤 replay, 또는 age=90/future timestamp+2.5초에서 receipt만 먼저 만료시킨다.

**변경·유지 판단:** 사용자 위임 판단: nonpositive/nonfinite age는 거절하고 기본 30초를 유지한다. 허용하는 기존 future skew 3초까지 receipt 보유 기간에 더해 수신 유효기간 전체를 보호한다.

**관련 파일:** [Source/Project_J/Backend/Project_JHandoverManager.cpp](../../../Source/Project_J/Backend/Project_JHandoverManager.cpp), [Source/Project_J/Backend/Project_JHandoverManager.h](../../../Source/Project_J/Backend/Project_JHandoverManager.h), [Source/Project_J/Private/Tests/Project_JRewardTransportAdmissionTests.cpp](../../../Source/Project_J/Private/Tests/Project_JRewardTransportAdmissionTests.cpp), [Source/Project_J/Private/Tests/Project_JSourceAuditPhase3CoreTests.cpp](../../../Source/Project_J/Private/Tests/Project_JSourceAuditPhase3CoreTests.cpp).

**테스트:** `ProjectJ.SourceAudit3.HandoverReplayLifetime`, `ProjectJ.Continuation.HandoverConfiguration`, `ProjectJ.Continuation.LoopbackFiniteClock`, `ProjectJ.Continuation.QuestRewardRollback`, `ProjectJ.Architecture.Handover.EnvelopeValidation`, `ProjectJ.Architecture.Handover.StateMachine`, `ProjectJ.Architecture.Handover.TransportReentrancy`, `ProjectJ.Architecture.Handover.NotificationReentrancy`, `ProjectJ.Architecture.Handover.PlayerCharacterSerialization`.

**실제 native 결과:** `ProjectJ.SourceAudit3.HandoverReplayLifetime`: Success (warning 0, error 0); `ProjectJ.Continuation.HandoverConfiguration`: Success (warning 0, error 0); `ProjectJ.Continuation.LoopbackFiniteClock`: Success (warning 0, error 0); `ProjectJ.Continuation.QuestRewardRollback`: Success (warning 0, error 0); `ProjectJ.Architecture.Handover.EnvelopeValidation`: Success (warning 0, error 0); `ProjectJ.Architecture.Handover.StateMachine`: Success (warning 0, error 0); `ProjectJ.Architecture.Handover.TransportReentrancy`: Success (warning 0, error 0); `ProjectJ.Architecture.Handover.NotificationReentrancy`: Success (warning 0, error 0); `ProjectJ.Architecture.Handover.PlayerCharacterSerialization`: Success (warning 0, error 0).

**추가 결과:** 위 명명된 최종 통합 회귀가 모두 Success. 경고 수는 actualNativeResults에 별도 기록.

**증거:** `Saved/Validation/Continuation_20261014/Audit6850Phase3HorizonBeforeTests`, `Saved/Audit6850/Round3/Automation/FinalSourceIntegration`, `Saved/Audit6850/Round3/Automation/HandoverImpactFinal`.

**남은 제한:** 0을 무제한으로 지원하는 정책은 폐기했다. 영속 중복 영수증/새 백엔드는 추가하지 않았다. age90 경계는 실제 수신 나이가 아직 유효한 91초에서 재적용 방지, 94초에서 만료 거절을 시간 주입으로 확인했다.

### FND-01 — 수정하고 관련 검증 통과

**재현 조건:** dismount tag/관계 콜백이 controller를 새 pawn으로 possession한 뒤 원래 함수가 복귀한다.

**변경·유지 판단:** controller가 여전히 해당 mount/session을 소유할 때만 rider 복원을 진행해 콜백이 만든 최신 possession을 덮지 않는다.

**관련 파일:** [Source/Project_JMount/Private/Mount/Project_JMountCharacter.cpp](../../../Source/Project_JMount/Private/Mount/Project_JMountCharacter.cpp), [Source/Project_JCharacter/Private/Tests/Project_JSourceAuditPhase3Tests.cpp](../../../Source/Project_JCharacter/Private/Tests/Project_JSourceAuditPhase3Tests.cpp), [Source/Project_JCharacter/Private/Tests/Project_JSourceAuditPhase3Receiver.h](../../../Source/Project_JCharacter/Private/Tests/Project_JSourceAuditPhase3Receiver.h).

**테스트:** `ProjectJ.SourceAudit3.DismountPossession`.

**실제 native 결과:** `ProjectJ.SourceAudit3.DismountPossession`: Success (warning 0, error 0).

**추가 결과:** 위 명명된 최종 통합 회귀가 모두 Success. 경고 수는 actualNativeResults에 별도 기록.

**증거:** `Saved/Validation/Continuation_20261014/Audit6850Phase3BeforeTests`, `Saved/Audit6850/Round3/Automation/FinalSourceIntegration`.

**남은 제한:** 기존 탑승 소유권 규칙과 신뢰된 콜백의 최신 결정을 보존하는 수정이다. 별도 탑승 권한이나 게임 규칙을 도입하지 않았다.

### ANIM-1 — 이미 해결됐음을 코드·테스트로 확인

**재현 조건:** 공중 clip 중간에서 재선택하면서 이미 진행한 시간을 보존한다.

**변경·유지 판단:** 2차 실제 선택 시각/offset 기준을 보존해 StartTime과 hold 경과시간이 같은 구간을 두 번 차감하지 않는다.

**관련 파일:** [Source/Project_JCharacter/Private/Animation/Project_JCharacterAnimInstance.Presentation.cpp](../../../Source/Project_JCharacter/Private/Animation/Project_JCharacterAnimInstance.Presentation.cpp), [Source/Project_JCharacter/Public/Animation/Project_JCharacterAnimInstance.h](../../../Source/Project_JCharacter/Public/Animation/Project_JCharacterAnimInstance.h), [Source/Project_JCharacter/Public/Animation/Project_JStateControllerRuntime.h](../../../Source/Project_JCharacter/Public/Animation/Project_JStateControllerRuntime.h), [Source/Project_JCharacter/Private/Tests/Project_JSourceAuditPhase2Tests.cpp](../../../Source/Project_JCharacter/Private/Tests/Project_JSourceAuditPhase2Tests.cpp).

**테스트:** `ProjectJ.SourceAudit2.AirReselectionCommand`.

**실제 native 결과:** `ProjectJ.SourceAudit2.AirReselectionCommand`: Success (warning 0, error 0).

**추가 결과:** 위 명명된 최종 통합 회귀가 모두 Success. 경고 수는 actualNativeResults에 별도 기록.

**증거:** `Saved/Audit6850/Round3/Automation/FinalSourceIntegration`, `Saved/Audit6850/Round2/ReviewResults.txt`.

**남은 제한:** 현재 사용하는 공중 재선택 경로를 검증했다. 임의 timer/reset이나 특정 clip 예외를 추가하지 않았다. 주관적인 자연스러움은 별도다.

### ANIM-2 — 이미 해결됐음을 코드·테스트로 확인

**재현 조건:** 같은 프레임에서 pending 공중 방향 승인, snapshot, chooser와 trajectory 발행이 이어진다.

**변경·유지 판단:** 2차 승인 방향을 한 snapshot/command로 소비하는 순서를 보존했다. chooser/hold/trajectory 방향을 함께 확인한다.

**관련 파일:** [Source/Project_JCharacter/Private/Animation/Project_JCharacterAnimInstance.Presentation.cpp](../../../Source/Project_JCharacter/Private/Animation/Project_JCharacterAnimInstance.Presentation.cpp), [Source/Project_JCharacter/Private/Animation/Project_JCharacterAnimInstance.Snapshot.cpp](../../../Source/Project_JCharacter/Private/Animation/Project_JCharacterAnimInstance.Snapshot.cpp), [Source/Project_JCharacter/Private/Tests/Project_JSourceAuditPhase2Tests.cpp](../../../Source/Project_JCharacter/Private/Tests/Project_JSourceAuditPhase2Tests.cpp).

**테스트:** `ProjectJ.SourceAudit2.AirReselectionCommand`.

**실제 native 결과:** `ProjectJ.SourceAudit2.AirReselectionCommand`: Success (warning 0, error 0).

**추가 결과:** 위 명명된 최종 통합 회귀가 모두 Success. 경고 수는 actualNativeResults에 별도 기록.

**증거:** `Saved/Audit6850/Round3/Automation/FinalSourceIntegration`, `Saved/Audit6850/Round2/ReviewResults.txt`.

**남은 제한:** 방향·시간·선택 값의 객관적 회귀이며 화면 품질 승인이 아니다.

### ANIM-EXTRA-01 — 이미 해결됐음을 코드·테스트로 확인

**재현 조건:** 원격 jump의 복제 지속시간이 local watchdog보다 짧고 age=0/0.1에서 시작한다.

**변경·유지 판단:** 2차 원격 실행 deadline과 generation 기준을 보존해 timeout callback도 같은 종료 시각을 사용한다.

**관련 파일:** [Source/Project_JCharacter/Private/Project_JLocomotionAnimStateComponentTransitions.cpp](../../../Source/Project_JCharacter/Private/Project_JLocomotionAnimStateComponentTransitions.cpp), [Source/Project_JCharacter/Public/Project_JLocomotionAnimStateComponent.h](../../../Source/Project_JCharacter/Public/Project_JLocomotionAnimStateComponent.h), [Source/Project_JCharacter/Private/Tests/Project_JSourceAuditPhase2Tests.cpp](../../../Source/Project_JCharacter/Private/Tests/Project_JSourceAuditPhase2Tests.cpp).

**테스트:** `ProjectJ.SourceAudit2.RemoteJumpDeadline`, `ProjectJ.Animation.StateOwnership.RemoteJumpFreshness`.

**실제 native 결과:** `ProjectJ.SourceAudit2.RemoteJumpDeadline`: Success (warning 0, error 0); `ProjectJ.Animation.StateOwnership.RemoteJumpFreshness`: Success (warning 0, error 0).

**추가 결과:** 위 명명된 최종 통합 회귀가 모두 Success. 경고 수는 actualNativeResults에 별도 기록.

**증거:** `Saved/Audit6850/Round3/Automation/FinalSourceIntegration`, `Saved/Audit6850/Round2/ReviewResults.txt`.

**남은 제한:** 원격 짧은 deadline과 local watchdog를 구분해 검증했다. 원격 화면의 애니메이션 체감은 사용자 확인 대상이다.

### ANIM-3 — 수정하고 관련 검증 통과

**재현 조건:** 중첩 AnimationBudget 7개 값을 편집해도 legacy flat 필드로 덮인다.

**변경·유지 판단:** 기존 flat 필드를 canonical authoring으로 유지하고 중첩 필드의 EditAnywhere를 제거해 효력 없는 편집을 노출하지 않는다. 직렬화/Blueprint 읽기와 기존 자산 값은 보존한다.

**관련 파일:** [Source/Project_JCharacter/Public/Animation/Project_JLocomotionProfile.h](../../../Source/Project_JCharacter/Public/Animation/Project_JLocomotionProfile.h), [Source/Project_JCharacter/Private/Tests/Project_JSourceAuditPhase3Tests.cpp](../../../Source/Project_JCharacter/Private/Tests/Project_JSourceAuditPhase3Tests.cpp).

**테스트:** `ProjectJ.SourceAudit3.BudgetAuthoringContract`, `실제 Project_JLocomotionProfile DataValidation 1/1`.

**실제 native 결과:** `ProjectJ.SourceAudit3.BudgetAuthoringContract`: Success (warning 0, error 0).

**추가 결과:** 위 명명된 최종 통합 회귀가 모두 Success. 경고 수는 actualNativeResults에 별도 기록.

**증거:** `Saved/Validation/Continuation_20261014/Audit6850Phase3BeforeTests`, `Saved/Audit6850/Round3/Automation/FinalSourceIntegration`, `Saved/Validation/Continuation_20261014/Audit6850Phase3NativeAssetsFinal/Project_JLocomotionProfile`.

**남은 제한:** 7개 flat 값의 적용과 reflection authoring 플래그를 확인했다. 기존 자산 값의 우선순위를 뒤집거나 자산을 재저장하지 않았다.

### G3 — 수정하고 관련 검증 통과

**재현 조건:** OneShot 두 fixture 또는 반복 실행이 기본 CurvePlayback.txt를 공유해 앞선 증거를 덮어쓴다.

**변경·유지 판단:** test name + GUID별 기본 출력과 capture 폴더를 고정한다. 명시 출력의 부모 생성 결과를 확인하고 기존 파일을 거절한다. 단순 상대 파일명도 지원한다.

**관련 파일:** [Source/Project_J/Testing/Project_JOneShotReturnPlayback.cpp](../../../Source/Project_J/Testing/Project_JOneShotReturnPlayback.cpp).

**테스트:** `ProjectJ.Animation.OneShotInput.AuthoredCurvePlayback`, `ProjectJ.Animation.OneShotReturn.RecordedStrafeLand`, `명시 fresh 출력 성공 / 기존 출력 거절(예상 실패 1)`.

**추가 결과:** 두 fixture×두 실행4회 성공; fresh 명시 경로1회 성공; 기존 명시 출력 거절은 예상 실패1/native exit255이며 파일 해시 동일.

**증거:** `Saved/Audit6850/Round3/G3CollisionBefore`, `Saved/Audit6850/Round3/Automation/G3DefaultsAfter1`, `Saved/Audit6850/Round3/Automation/G3DefaultsAfter2`, `Saved/Audit6850/Round3/G3RepeatProof.json`, `Saved/Audit6850/Round3/G3ExplicitCollisionProof.json`.

**남은 제한:** 실제 두 fixture×두 실행=4회 성공, 고유 출력 4개와 첫 출력 해시 보존을 확인했다. 원본의 fresh non-render 폴더 실패 주장은 UE5.8에서 수정 전부터 성공했으므로 확정 버그로 취급하지 않았다.

### R1 — 수정하고 관련 검증 통과

**재현 조건:** BeginPlay 후 살아 있는 이전 avatar를 남겨 같은 PlayerState/ASC/장비 manager에 새 avatar를 possession한다.

**변경·유지 판단:** 실제 +10 장비가 AttackPower120/effects2가 되는 것을 확인한 뒤 기존 ASC grant-provider lease로 장비 효과를 공유했다. avatar별 시각 수명은 유지한다.

**관련 파일:** [Source/Project_JCharacter/Private/Components/Project_JEquipmentRuntimeComponent.cpp](../../../Source/Project_JCharacter/Private/Components/Project_JEquipmentRuntimeComponent.cpp), [Source/Project_J/Private/Tests/Project_JSourceAuditPhase3CoreTests.cpp](../../../Source/Project_J/Private/Tests/Project_JSourceAuditPhase3CoreTests.cpp).

**테스트:** `ProjectJ.SourceAudit3.RetainedAvatarEquipment`, `ProjectJ.Continuation.EquipmentStatLease`, `ProjectJ.Continuation.EquipmentStatTuple`, `ProjectJ.Continuation.EquipmentGrantIdentity`.

**실제 native 결과:** `ProjectJ.SourceAudit3.RetainedAvatarEquipment`: Success (warning 2, error 0); `ProjectJ.Continuation.EquipmentStatLease`: Success (warning 0, error 0); `ProjectJ.Continuation.EquipmentStatTuple`: Success (warning 0, error 0); `ProjectJ.Continuation.EquipmentGrantIdentity`: Success (warning 0, error 0).

**추가 결과:** 위 명명된 최종 통합 회귀가 모두 Success. 경고 수는 actualNativeResults에 별도 기록.

**증거:** `Saved/Validation/Continuation_20261014/Audit6850Phase3ResidualBeforeTests`, `Saved/Audit6850/Round3/Automation/FinalSourceIntegration`.

**남은 제한:** 보정된 실제 possession fixture에서 재현한 뒤 위험을 결함으로 승격했다. 최초 AttributeSet 누락 fixture 실패는 재현 근거에서 제외했다. overlap110/효과1, 이전 avatar 파괴 뒤110, 해제 뒤100을 확인했다. bare native 프로필 누락 경고 2개를 별도 표시한다.

### R2 — 수정하고 관련 검증 통과

**재현 조건:** 정상 BeginPlay/mount 후 살아 있는 rider의 MountComponent만 DestroyComponent한다.

**변경·유지 판단:** UE5.8에서 EndPlay(Destroyed)가 먼저 RetireMount(false)를 잠그는 순서를 확인했다. live component 제거에서만 복원하고 owner/world 종료에는 복원하지 않는다.

**관련 파일:** [Source/Project_JMount/Private/Mount/Project_JMountComponent.cpp](../../../Source/Project_JMount/Private/Mount/Project_JMountComponent.cpp), [Source/Project_JCharacter/Private/Tests/Project_JSourceAuditPhase3Tests.cpp](../../../Source/Project_JCharacter/Private/Tests/Project_JSourceAuditPhase3Tests.cpp).

**테스트:** `ProjectJ.SourceAudit3.BegunMountComponentDestroy`, `관련 Mount lifecycle 회귀`.

**실제 native 결과:** `ProjectJ.SourceAudit3.BegunMountComponentDestroy`: Success (warning 0, error 0).

**추가 결과:** 위 명명된 최종 통합 회귀가 모두 Success. 경고 수는 actualNativeResults에 별도 기록.

**증거:** `Saved/Validation/Continuation_20261014/Audit6850Phase3BeforeTests`, `Saved/Audit6850/Round3/Automation/FinalSourceIntegration`.

**남은 제한:** pre-BeginPlay 파괴 테스트만으로 판정하지 않았다. owner 종료와 월드 teardown에는 새 possession을 만들지 않는다.

### R3 — 수정하고 관련 검증 통과

**재현 조건:** retained old avatar가 재할당된 ASC tag를 요청하거나 remote fall-off→landing 뒤 소유 lease가 남는다.

**변경·유지 판단:** 현재 ASC avatar==owner일 때만 movement tag를 요청하며 기존 소유 lease를 해제한다. remote landing도 자신이 요청한 InAir contribution을 해제한다.

**관련 파일:** [Source/Project_JCharacter/Private/Project_JLocomotionAnimStateComponentSupport.cpp](../../../Source/Project_JCharacter/Private/Project_JLocomotionAnimStateComponentSupport.cpp), [Source/Project_JCharacter/Private/Project_JLocomotionAnimStateComponentTransitions.cpp](../../../Source/Project_JCharacter/Private/Project_JLocomotionAnimStateComponentTransitions.cpp), [Source/Project_JCharacter/Private/Tests/Project_JSourceAuditPhase3Tests.cpp](../../../Source/Project_JCharacter/Private/Tests/Project_JSourceAuditPhase3Tests.cpp), [Source/Project_J/Testing/Project_JAuditNetworkFixture.cpp](../../../Source/Project_J/Testing/Project_JAuditNetworkFixture.cpp).

**테스트:** `ProjectJ.SourceAudit3.MovementOwnerAndTimers`, `실제 socket case20: fall-off/landing authority·owner·simulated InAir 1→0 및 remote lease/sequence`.

**실제 native 결과:** `ProjectJ.SourceAudit3.MovementOwnerAndTimers`: Success (warning 0, error 0).

**추가 결과:** 실제 socket 3조건 case20에서 authority/owner/simulated InAir1→0, simulated lease 해제 확인.

**증거:** `Saved/Validation/Continuation_20261014/Audit6850Phase3BeforeTests`, `Saved/Audit6850/Round3/Automation/FinalSourceIntegration`, `Saved/Audit6850/Round3/Network/Summary.json`.

**남은 제한:** 원격 replicated loose-tag count와 local lease를 함께 확인했다. retirement 후 timer 분기는 수정 전부터 회귀 통과해 별도 timer 결함으로 확정하거나 불필요하게 변경하지 않았다.

### TOOLS-01 — 이미 해결됐음을 코드·테스트로 확인

**재현 조건:** 공용 콘솔 script가 if __name__ == __main__ 진입을 기대한다.

**변경·유지 판단:** 1차 runpy.run_path(run_name=__main__)를 보존했다. 실제 생성 명령을 UE Python에서 실행해 Saved 영수증까지 확인했다.

**관련 파일:** [Scripts/Editor/WorldEditorConsole.py](../../../Scripts/Editor/WorldEditorConsole.py), [Scripts/Tests/test_source_audit_tools.py](../../../Scripts/Tests/test_source_audit_tools.py).

**테스트:** `test_console_executes_guarded_and_unguarded_scripts`, `ConsoleNativeEntry`.

**추가 결과:** Python guarded/unguarded 성공 + 실제 UE Python 생성 명령의 main 완료 영수증 passed=true/native exit0.

**증거:** `Saved/Audit6850/Round3/PythonFinal.log`, `Saved/Audit6850/Round3/ConsoleNativeEntry.json`.

**남은 제한:** 실제 UE interpreter 실행 증거다. MCP UI 전송과 제출 후 완료 확인은 다른 계약이며 미연결 MCP transport를 실행했다고 주장하지 않는다.

### TOOLS-02 — 수정하고 관련 검증 통과

**재현 조건:** 동일 월드/소유 경로의 unrelated dirty 또는 authoring 중 concurrent dirty가 존재한다.

**변경·유지 판단:** clean-start를 요구하고 명시 객체의 정확한 GetOutermost 외부 package만 소유한다. 외부 dirty 발견 시 저장 전 거절하며 save 실패 뒤 broad-save fallback을 하지 않는다.

**관련 파일:** [Scripts/World/Starter-OwnedSave.py](../../../Scripts/World/Starter-OwnedSave.py), [Scripts/World/Starter-SettlementAuthoring.py](../../../Scripts/World/Starter-SettlementAuthoring.py), [Scripts/World/Save-StarterForests.py](../../../Scripts/World/Save-StarterForests.py), [Scripts/World/Prepare-StarterLighting.py](../../../Scripts/World/Prepare-StarterLighting.py), [Scripts/World/Build-StarterWorld.py](../../../Scripts/World/Build-StarterWorld.py), [Scripts/World/README.md](../../../Scripts/World/README.md), [Scripts/Tests/test_source_audit_remaining_tools.py](../../../Scripts/Tests/test_source_audit_remaining_tools.py), [Source/Project_JCharacterEditor/Private/Tests/Project_JSourceAuditSaveTests.cpp](../../../Source/Project_JCharacterEditor/Private/Tests/Project_JSourceAuditSaveTests.cpp).

**테스트:** `OwnedSaveTests 4개`, `ProjectJ.SourceAudit3.ExternalActorPackageOwnership`, `실제 신규 Material 저장/foreign dirty/preexisting dirty/실제 I/O 실패 전파`, `승인된 격리 WP 맵 + owned actor 저장/두 번째 actor dirty 거절`.

**실제 native 결과:** `ProjectJ.SourceAudit3.ExternalActorPackageOwnership`: Success (warning 0, error 0).

**추가 결과:** Python4/4 + 실제 package native 단언 성공 + SaveContractSummary.passed=true. 실제 I/O 실패 전파는 예상 native exit1이며 일반 성공에 합산하지 않음. 승인 WP map/external package 삭제 완료.

**증거:** `Saved/Audit6850/Round3/PythonFinal.log`, `Saved/Audit6850/Round3/SaveContractSummary.json`, `Saved/Audit6850/Round3/OwnedWorldSave.json`, `Saved/Audit6850/Round3/OwnedWorldSave.Cleanup.json`, `Saved/Audit6850/Round3/Python/RealWriteFailureCorrected`.

**남은 제한:** 실제 사용자 dirty 편집은 저장하지 않았다. 실제 I/O 실패는 예상 native exit1/guard 영수증으로 구분한다. 빈 디렉터리 충돌은 실제 실패를 만들지 못한 fixture 오류였다. 새 폴더/공유 맵 metadata는 actor 소유권에 자동 포함하지 않아 중단된다. 초기 bootstrap/과거 GENERATED_NOT_SAVED 복구를 MainWorld 재생성기로 지원하지 않는다. 재사용 전 대상·정확한 metadata 패키지·복구 계획을 작업 사본에서 검토해야 한다. 승인된 WP map/external actor/object와 모든 테스트 uasset은 삭제했다.

### TOOLS-03 — 수정하고 관련 검증 통과

**재현 조건:** 서로 다른 clean HEAD/Config/runtime binary/deleted Source 또는 누락 provenance로 성능 비교를 시도한다.

**변경·유지 판단:** HEAD + 추적/미추적 Source/Scripts/Config/Plugins/uproject 및 삭제 tombstone + 실제 프로젝트/host 바이너리 지문을 수집해 세 run 전체 계약을 비교한다.

**관련 파일:** [Scripts/Validation/Source-Provenance.ps1](../../../Scripts/Validation/Source-Provenance.ps1), [Scripts/Validation/Measure-Network.ps1](../../../Scripts/Validation/Measure-Network.ps1), [Scripts/Validation/Compare-DStage.ps1](../../../Scripts/Validation/Compare-DStage.ps1), [Scripts/Tests/Test-AuditSourceProvenance.ps1](../../../Scripts/Tests/Test-AuditSourceProvenance.ps1).

**테스트:** `Same, DifferentHead, MissingProvenance, ConfigChange, BinaryChange, DeletedSource 6개`, `실제 네트워크 세 실행 provenance 일치 / 이후 도구 빌드와 비교 거절`.

**추가 결과:** 실제 comparator 합성6/6 성공; 기존 네트워크3 run provenance 일치, 이후 도구 빌드와 혼합 비교 거절 성공. 성능 측정 아님.

**증거:** `Saved/Audit6850/Round3/ProvenanceClosureFinal.log`, `Saved/Audit6850/Round3/Network/ActualRunProvenanceCheck.json`, `Saved/Audit6850/Round3/ProvenanceAfterToolBuild.json`.

**남은 제한:** 비교기의 합성 trace 회귀와 실제 collector 검증이다. 새 성능 우열이나 과거 성능 결과의 재인증을 주장하지 않는다. Editor 도구가 추가된 뒤 이전 native run과 현재 provenance를 섞으면 거절됨을 확인했다.

### TOOLS-04 — 수정하고 관련 검증 통과

**재현 조건:** child exit nonzero/null, 검사0, 카운터 누락, failed/all-skipped/unable 또는 warning을 일반 성공처럼 처리한다.

**변경·유지 판단:** 2차 exit 보존에 더해 strict completion counters/status를 추가했다. UE5.8 기본 commandlet이 요약을 출력하지 않아, 엔진 selection/validators를 그대로 상속하고 ProcessValidationResults만 보강했다.

**관련 파일:** [Scripts/Audit/Run-ContinuationAssetValidation.ps1](../../../Scripts/Audit/Run-ContinuationAssetValidation.ps1), [Scripts/Tests/Test-AssetValidationWrapper.ps1](../../../Scripts/Tests/Test-AssetValidationWrapper.ps1), [Source/Project_JCharacterEditor/Private/Authoring/Project_JAssetValidationCommandlet.h](../../../Source/Project_JCharacterEditor/Private/Authoring/Project_JAssetValidationCommandlet.h), [Source/Project_JCharacterEditor/Private/Authoring/Project_JAssetValidationCommandlet.cpp](../../../Source/Project_JCharacterEditor/Private/Authoring/Project_JAssetValidationCommandlet.cpp).

**테스트:** `실제 wrapper mock 11개`, `CountedZero: checked0 / native exit2`, `CountedInvalid: checked2 passed1 failed1 / native exit2`, `정상 WorldDefinition1 + LocomotionProfile1 + Montage91`.

**추가 결과:** wrapper 모의11/11; 실제 0개 exit2/checked0, invalid exit2/checked2 passed1 failed1; 정상93/93, skipped/unable/validationWarnings0.

**증거:** `Saved/Audit6850/Round3/WrapperClosureFinal.log`, `Saved/Audit6850/Round3/NativeValidationContract.json`, `Saved/Validation/Continuation_20261014/Audit6850Phase3CountedZero`, `Saved/Validation/Continuation_20261014/Audit6850Phase3CountedInvalid`, `Saved/Validation/Continuation_20261014/Audit6850Phase3NativeAssetsFinal`.

**남은 제한:** 정상 실제 93자산은 warnings/failed/skipped/unable 모두0. partial skipped와 passed-with-warnings를 구분하고 unable/누락 카운터는 거절한다. Engine 기본 commandlet은 0개에서 exit0/요약없음, invalid에서는 exit1/요약없음이었으며 그 이전 실패 로그를 보존했다. 실제 negative 자산은 삭제했다.

### TOOLS-05 — 이미 해결됐음을 코드·테스트로 확인

**재현 조건:** 요청 seed/dependency/edge closure 중 한 package가 공급 사본에 없다.

**변경·유지 판단:** 1차 복사 전 strict closure 검사를 보존했다. 완전 graph만 복사/hash/receipt를 작성하며 누락 또는 외부 game dependency는 거절한다.

**관련 파일:** [Scripts/World/Stage-WorldAssets.py](../../../Scripts/World/Stage-WorldAssets.py), [Scripts/Tests/test_source_audit_tools.py](../../../Scripts/Tests/test_source_audit_tools.py).

**테스트:** `test_complete_dependency_closure_copies_and_receipts`, `test_missing_requested_seed_is_rejected_before_any_copy`, `test_missing_dependency_is_rejected_before_any_copy`, `test_incomplete_edge_closure_is_rejected_before_any_copy`, `test_external_game_dependency_is_rejected_before_any_copy`.

**추가 결과:** Python closure 관련5/5 성공. negative는 예상 거절을 확인한 정상 회귀 결과.

**증거:** `Saved/Audit6850/Round3/PythonFinal.log`, `Saved/Audit6850/ReviewResults.txt`.

**남은 제한:** 격리 작업 사본에서 검사했다. 공급 manifest의 진실성을 새 backend 인증으로 보장하지 않는다. 명시적 --local-packages의 약한 보장을 유지한다.

### TOOLS-06 — 이미 해결됐음을 코드·테스트로 확인

**재현 조건:** 늦게 추가된 CSV 열이 앞 프레임의 0으로 포함되거나 metric 전체가 없다.

**변경·유지 판단:** 1차 실제 표본/누락 분리를 보존했다. 10/20 두 GPU 표본은 samples2/mean15이고 missing을 별도 보고한다.

**관련 파일:** [Scripts/World/Summarize-StarterPerformance.py](../../../Scripts/World/Summarize-StarterPerformance.py), [Scripts/Tests/test_source_audit_tools.py](../../../Scripts/Tests/test_source_audit_tools.py).

**테스트:** `test_late_csv_column_excludes_unmeasured_frames`, `test_missing_metric_has_explicit_zero_samples`.

**추가 결과:** Python CSV 관련2/2 성공; metric 전체 누락은 samples0/null 통계이며 유효 표본 성공으로 세지 않음.

**증거:** `Saved/Audit6850/Round3/PythonFinal.log`, `Saved/Audit6850/ReviewResults.txt`.

**남은 제한:** 원본의 Summarize-StarterProfile.py에 대한 동일 지적은 기준 코드부터 누락을 제외해 부정확하다. 올바른 parser를 중복 수정하거나 과거 성능 결과를 재인증하지 않았다.

### TOOLS-07 — 지원 범위 밖 확인

**재현 조건:** 명시 -ProjectJRepairExternalWarping으로 과거 External BlendStack 그래프를 복구한다.

**변경·유지 판단:** 현행 unified graph/authoring 문서와 대체 경로를 확인했다. 폐기된 repair를 현대화하지 않고, opt-in도 자산 load/mutation 전에 명시 오류로 종료하도록 철회했다.

**관련 파일:** [Source/Project_JCharacterEditor/Private/Tests/Project_JNaturalnessGraphTests.cpp](../../../Source/Project_JCharacterEditor/Private/Tests/Project_JNaturalnessGraphTests.cpp), [Docs/Animation/Planning/GASP_ProjectJ_Naturalness_2026-10-06/06_IMPLEMENTATION.md](../../../Docs/Animation/Planning/GASP_ProjectJ_Naturalness_2026-10-06/06_IMPLEMENTATION.md).

**테스트:** `ProjectJ.Authoring.Naturalness.ExternalWarpingRepair -ProjectJRepairExternalWarping: 예상 실패1 / native exit255`, `ABP_Humanoid_Master 실제 파일 해시 전후 일치`.

**추가 결과:** 명시 opt-in 예상 실패1/native exit255, Master hash 전후 동일. 미실행 NotApplicable와 구분.

**증거:** `Saved/Audit6850/Round3/Automation/RetiredRepairExplicitRejection`, `Saved/Audit6850/Round3/LegacyRepairRejectionProof.json`.

**남은 제한:** no-flag NotApplicable는 그래프 복구 성공으로 세지 않는다. 과거 topology 재사용 시 현행 unified 소유권/컴파일/저장 계약부터 작업 사본에서 새로 설계해야 한다. 기존 Master를 저장/복구하지 않았다.

### TOOLS-08 — 수정하고 관련 검증 통과

**재현 조건:** 현행 Source20개 장소와 S08 4점 경계에 역사적 18개/8점 상수를 적용한다.

**변경·유지 판단:** Source plan에서 개수를 유도하고 exact place IDs/좌표/레벨/유한 polygon 및 실제 경계 형상을 대조한다. corridor 보고 수량도 실제 plan을 따른다.

**관련 파일:** [Scripts/World/Starter-WorldDefinitionContract.py](../../../Scripts/World/Starter-WorldDefinitionContract.py), [Scripts/World/Verify-StarterWorld.py](../../../Scripts/World/Verify-StarterWorld.py), [Scripts/World/Verify-StarterCorridor.py](../../../Scripts/World/Verify-StarterCorridor.py), [Scripts/Tests/test_source_audit_remaining_tools.py](../../../Scripts/Tests/test_source_audit_remaining_tools.py).

**테스트:** `WorldPlanTests 4개: current20/four-point, explicit historical18, missing ID, wrong equal-size shape`, `실제 WorldContracts2 native20/Source 경계 대조`.

**추가 결과:** Python WorldPlan4/4 성공 + 실제 Native20/Source 계약 passed=true/native exit0.

**증거:** `Saved/Audit6850/Round3/PythonFinal.log`, `Saved/Audit6850/Round3/WorldContracts.json`, `Saved/Audit6850/Round3/WorldContracts2.Process.json`.

**남은 제한:** actual verifier의 pre-terrain prefix를 실행하는 격리 회귀와 실제 Native DataAsset 대조다. 전체 terrain/도시/환경 아트의 완성도 검사로 확대하지 않았다.

### TOOLS-09 — 수정하고 관련 검증 통과

**재현 조건:** 현재 MainWorld는 저장됐지만 manifest/AssetManager/MapsToCook은 옛 Starter를 가리킨다.

**변경·유지 판단:** 실물 map/definition, redirector와 남은 HLOD 실물을 먼저 조사한 뒤 manifest의 현재 package roots, WorldDefinition scan, MapsToCook/검증기와 문서를 MainWorld에 맞췄다.

**관련 파일:** [Config/DefaultGame.ini](../../../Config/DefaultGame.ini), [Content/World/Starter/Source/StarterRegion.manifest.json](../../../Content/World/Starter/Source/StarterRegion.manifest.json), [Scripts/World/Verify-StarterWorld.py](../../../Scripts/World/Verify-StarterWorld.py), [Scripts/Audit/Run-ContinuationPackaging.ps1](../../../Scripts/Audit/Run-ContinuationPackaging.ps1), [Docs/World/README.md](../../../Docs/World/README.md), [Scripts/World/README.md](../../../Scripts/World/README.md).

**테스트:** `WorldReadOnly2 actual registry49/dependencies/referencers`, `WorldContracts2 primary asset path + Source20`, `WorldDefinition actual DataValidation1/1`, `MainWorld Cook / package / smoke 결과는 아래 실행 상태`.

**추가 결과:** Cook exit0(경고 포함), Stage/package exit0(경고0), 실제 packaged smoke success=true/exit0: MainWorld, WorldState1, DA_MainWorld, places20, registered1, partition1.

**증거:** `Saved/Audit6850/Round3/WorldReadOnly.json`, `Saved/Audit6850/Round3/WorldContracts.json`, `Saved/Validation/Continuation_20261014/Audit6850Phase3NativeAssetsFinal/Project_JWorldDefinition`, `Saved/Validation/Continuation_20261014/Audit6850Phase3MainWorldCook`, `Saved/Validation/Continuation_20261014/Audit6850Phase3MainWorldPackage`, `Saved/Audit6850/Round3/PackagedMainWorldSmoke`.

**남은 제한:** Starter.Lv1_30 안정 ID와 물리적 Source 경로는 유지했다. 구 Starter map은 redirector가 아닌 별개 사본이며 삭제하지 않았다. 이전 HLOD 실물/redirector 참조를 일괄 문자열 교체하거나 기존 에셋을 이동/저장하지 않았다. Cook·패키지·smoke가 끝나기 전 전환 전체 검증 완료로 분류하지 않는다.

## 최종 근거와 재현 진입점

다음 원자료를 로컬에 보존했다. 요약·로그·해시·실제 hit CSV를 유지하므로 실패 원인과 실행 조건을 다시 읽을 수 있다. 과거 기준 커밋과 미커밋 소스 지문은 당시 검증 입력을 뜻하며, 후속 커밋의 HEAD로 바꾸어 기록하지 않았다.

- 원본 입력: `Saved/Audit6850/Input/README.txt`, `Project_J_Runtime_Audit_6850ed45.md`, `Project_J_Tools_Audit_6850ed45.md`, `Project_J_File_Coverage_6850ed45.csv`. 모두 같은 Input 디렉터리다.
- 1·2차: `Saved/Audit6850/ReviewResults.txt`, `Saved/Audit6850/Round2/ReviewResults.txt`.
- 항목·테스트 대응: `Saved/Audit6850/Round3/ClosureItems.json`, `ClosureStatus.txt`.
- 최종 통합과 영향 회귀: `Saved/Audit6850/Round3/Automation/FinalSourceIntegration/`, `HandoverImpactFinal/`.
- 실제 네트워크: `Saved/Audit6850/Round3/Network/FinalBaseline/`, `FinalLagLoss/`, `FinalReorderLoss/`, `Summary.json`, `ActualRunProvenanceCheck.json`.
- 저장·출력·퇴역 도구: `Saved/Audit6850/Round3/SaveContractSummary.json`, `G3RepeatProof.json`, `G3ExplicitCollisionProof.json`, `LegacyRepairRejectionProof.json`.
- 도구: `Saved/Audit6850/Round3/PythonFinal.log`, `WrapperClosureFinal.log`, `ProvenanceClosureFinal.log`.
- 빌드·Cook·자산: `Saved/Validation/Continuation_20261014/Audit6850Phase3ValidationCounts/`, `Audit6850Phase3GameFinal/`, `Audit6850Phase3ServerFinal/`, `Audit6850Phase3MainWorldCook/`, `Audit6850Phase3MainWorldPackage/`, `Audit6850Phase3NativeAssetsFinal/`의 로그·process 영수증.
- 패키지 실제 실행: `Saved/Audit6850/Round3/PackagedMainWorldSmoke/`; 경고: `CookWarningSummary.json`; 차단: `ServerBuildBlock.json`.
- 소스·바이너리 일치: `Saved/Audit6850/Round3/FinalArtifactConsistency.json`, `FinalSourceAndBinaryProvenance.json`, `PreCommitInputCheck.json`; Game 실행 파일 SHA-256 `D89BDEEF568501E72B00CCB158579EA1C78E44846E11487F64E90AE304D0A675`.
- 에셋 정리·원본 보존: `Saved/Audit6850/Round3/FinalFixtureCleanup.json`, `ContentAfterCookCheck.json`, `UnrelatedPreservation.json`.

영구 재현 진입점은 [네트워크 실행기](../../../Scripts/Tests/Run-SourceAuditNetwork.ps1), [wrapper 회귀](../../../Scripts/Tests/Test-AssetValidationWrapper.ps1), [provenance 회귀](../../../Scripts/Tests/Test-AuditSourceProvenance.ps1), [Tools 회귀](../../../Scripts/Tests/test_source_audit_tools.py), [잔여 Tools 회귀](../../../Scripts/Tests/test_source_audit_remaining_tools.py)와 항목별 native automation 테스트다. 엔진 위치·target·출력 디렉터리는 실제 프로젝트 환경에서 확인한다. 저장 실패 검증은 기존 사용자 미저장 에셋 대신 격리 자산으로 수행한다. standalone Server 환경 차단과 Cook 경고를 일반 성공으로 승격하지 않는다.

## 검증 산출물 정리

사용자 요청에 따라 실행이 끝난 감사 전용 산출물 63개 대상 / 481개 파일을 삭제했다. 논리 크기는 **7.06GiB**, 드라이브 여유 공간의 실제 관측 증가는 **2.80GiB**다. 전후 여유 공간은 97,749,327,872 → 100,755,898,368 bytes이며 정리 후 약 93.84GiB다. 공유 데이터·하드링크와 다른 디스크 작업 때문에 논리 크기 합계가 실제 확보 용량을 뜻하지 않는다.

삭제 범위는 `Saved/Validation/Continuation_20261014/Audit6850Phase3MainWorldPackage/Package`, `Audit6850Phase3MainWorldCook/Cooked` 및 감사 실행별 `Intermediate`·Cook shader 출력이다. 각 절대 경로가 감사 전용 범위 안인지, reparse point가 없는지, 사용 중인 Unreal·빌드 프로세스가 없는지 확인했다. 현재 프로젝트 `Binaries`·`Intermediate`, 공용 DDC·Zen 저장소, 소스·실제 에셋·영구 테스트와 관련 없는 작업은 보존했다. 현재 주요 바이너리의 정리 전후 해시가 모두 같았다.

**퇴역 제한:** Cook·패키지 payload는 삭제했으므로 보존된 패키지를 즉시 다시 실행하거나 payload 자체를 재분석할 수 없다. 다시 Cook·Stage해야 한다. 실행 성공 영수증·로그·지문·hit CSV와 원본 감사 입력은 남아 있다. `Saved/StorageCleanup_20261018/Plan.json`, `RemovedFileHashes.json`, `PrimaryBinariesBefore.json`, `Result.json`에서 대상·삭제 파일 SHA-256·전후 여유 공간·바이너리 보존을 확인한다. 이 원자료도 로컬 자료이며 Git에는 이 요약을 보존한다.

## 커밋 준비 문서 검증

`node Scripts/Documentation/Update-DocumentCatalog.mjs`로 목차를 갱신했다. `git diff --check`는 통과했다. 전체 `Validate-DocLinks.mjs`는 기존 문서의 끊어진 링크 54건과 부재 로컬 archive 12건을 보고해 exit 1이었다. HEAD 원문과 현재 문서의 source/target 쌍을 비교한 결과 고유 broken link는 양쪽 모두 49개, 새 broken link는 0개다. 이번 보고·목차 링크는 정상이며 무관한 과거 문서의 링크를 이번 커밋에서 수정하지 않았다. 상세 비교는 `Saved/Audit6850/Round3/CommitDocLinkCheck.json`에 보존했다.

## 사용자 플레이 확인

1. 공중에서 방향을 다시 선택하고 착지했을 때 방향과 자세 전환을 확인한다.
2. 같은 클립의 속도가 다른 연속 공격에서 재시작 느낌·타격감을 확인한다.
3. 다른 클라이언트의 점프·착지 후 공중 자세가 남거나 불필요하게 끊기지 않는지 확인한다.

이는 기술 회귀와 별도의 체감 확인이다. standalone Server 빌드 환경 차단, Cook 경고, 재정렬 관찰의 제한과 사용자 플레이 확인이 남아 있으므로 전체 검증 완료로 표현하지 않는다.
