# 이동 착지의 MM 복귀와 연결 레이어 검색 완료 — 2026-10-05

## 문제와 결과

사용자 실행 로그에서 이동 착지 도중 카메라를 돌려 Land 표시를 취소하면, 속도 500·이동 입력 활성 상태인데도 Idle DB를 약 60ms 거쳐 이동 Cycle로 복귀했다. 또 일부 그래프 업데이트에서는 실제 MM 결과가 바뀌는데 강제 재검색 요청이 미처리로 남았다. 상세 관측은 [흐름 진단 기록](Start_Land_Input_Flow_Trace_2026-10-05.md)에 보존한다.

이번 변경은 착지 표시를 취소한 직후부터 최신 지상 이동 의도로 MM에 복귀하게 하고, 연결 레이어의 내부 traversal이 바깥 traversal의 재검색 상태를 먼저 정리하지 않도록 한다.

## 착지 의미와 표시 수명

기존 `bIsMotionMatchingMoving`은 공중·착지 중 false다. 물리적 착지 상태는 표시 취소보다 늦게 끝날 수 있으므로 이 값만으로 취소 후 Cycle/Idle을 고르면 이동 중에도 Idle을 선택한다. 취소 프레임만 보완해도 다음 producer snapshot이 다시 Landing과 non-moving을 보내므로 지속적인 복귀 기준이 필요하다.

컴포넌트는 `bHasGroundMovementIntent`를 먼저 계산하고, 착지 표시 중에는 기존 `bIsMotionMatchingMoving`을 계속 억제한다. 지상 의도 계산은 기존 속도·입력·예측 속도 정책을 사용한다. 실제 공중은 여전히 제외하며 로컬 입력 해제는 잔여 속도보다 우선한다. 원격 캐릭터는 기존 복제 속도 판단을 유지한다.

공중 차단은 `bIsPhysicallyInAir || (bIsInAir && !IsLandingStateActive())`를 사용한다. 실제 공중은 미정리 착지 요청보다 우선하고, 착지 표시를 위해 유지한 `bIsInAir`는 접지 후 이동 의도를 막지 않는다. 착지 없는 JumpStart의 공중 의미는 CharacterMovement가 아직 공중으로 바뀌기 전에도 유지한다. 이 판정은 기존 캐시만 읽는다.

AnimInstance는 Land 표시 취소 직후와 이미 소비한 동일 착지 리비전의 후속 snapshot에서 지상 의도를 사용해 Cycle/Idle·MM 선택 컨텍스트를 정규화한다. 아직 재생 중인 Land나 새 착지 리비전은 기존 착지 경로로 보낸다. 물리적 착지 플래그·리비전·CharacterMovement·복제 이벤트를 취소하거나 재발행하지 않는다. 기존 runtime의 소비한 착지 리비전으로 중복 재생을 막으며 새 latch·manager·Tick을 만들지 않는다. 전투 표시 경계에서 이미 소비한 Land도 같은 복귀 기준을 사용한다.

## 연결 레이어와 실제 검색

엔진은 연결 레이어에서 같은 proxy의 그래프 업데이트로 재진입할 수 있다. 기존 hook은 매 진입에서 arm/policy/complete를 실행했다. 내부 traversal에서 MM 노드가 아직 업데이트되지 않으면 complete가 임시 검색 timer·throttle·interrupt를 정리하고 armed revision을 지운다. 이후 바깥쪽의 cached pose가 MM을 업데이트해도 요청의 검색 완료를 확인하지 못한다. 이는 진단 카운터뿐 아니라 요청 소비와 query·검색 간격에도 영향을 줄 수 있다.

proxy에는 업데이트 중 재진입을 구분하는 scope guard를 추가한다. 내부 traversal은 정상 엔진 그래프 업데이트를 계속 실행한다. 외부 traversal이 검색 정책 적용·arm·complete·대표 결과 관측을 소유하고, 완료 뒤 정상 검색 간격을 복원한다. 아직 실제 검색하지 않은 비활성 노드의 요청은 유지한다. 처리된 노드는 같은 요청을 반복하지 않고, interaction의 선택 소유권도 기존대로 유지한다.

snapshot은 계속 최신 것으로 교체한다. 숨김 캐릭터를 깨우거나 ABA/URO·수요 정책을 바꾸지 않는다. 재진입 guard는 proxy당 bool 하나이며, 새 Tick·노드 목록 순회·복제 필드를 추가하지 않는다. 내부 traversal은 기존 중복 정책 적용·노드 관측 순회를 건너뛴다. 성능 개선률은 이번 단위 회귀 검증으로 산출하지 않는다.

## 진단과 후속 방향 보정

`p.ProjectJ.AnimFlow 1`은 기존대로 기본 비활성이다. `Policy.GroundIntent`로 착지 중에도 지상 이동 의도가 있는지 확인하고, `Evaluated.NestedTraversals`로 내부 traversal을 확인한다. `Traversals`는 이제 외부 traversal 횟수이며 내부 실행은 별도 카운터다. 실제 검색 확인은 여전히 검색 timer의 엔진 의미에 의존하므로 엔진 업그레이드 때 회귀 테스트를 실행한다.

같은 이동 착지·카메라 회전 재현에서 다음을 확인한다.

1. `GroundIntent=1`이면 Land 취소 후 Cycle을 요청하고 Idle을 경유하지 않는다.
2. 실제 검색 후 `ReselectSearches`와 요청 번호가 갱신되고 `ReselectPending`이 해제된다. 비활성 경로의 pending은 필요한 실제 업데이트까지 유지될 수 있다.
3. OTM↔Strafe 전환에서 같은 착지 리비전의 외부 스택 시간이 재시작되지 않는다.

Start·Land를 유지하는 동안의 방향 보정은 다음 시각 비교 단계다. 기존 Start 15도·Land 25도 취소 임계값과 TIP 전용 Steering getter를 이번에 확대하지 않는다. 인계 사진에서 Steering은 `enable_turninplacesteering` 커브와 TIP 전용 getter에 연결되어 있으며, Start에 getter만 허용해도 해당 커브가 0이면 보정되지 않는다. OTM root offset 정책도 함께 고려해야 한다. 실제 ABP alpha·커브·최종 몸체를 확인하지 않고 임계값이나 TIP 보정을 일괄 확장하면 현재 Pivot·접지 동작을 훼손할 수 있다. 이번 수정의 목적은 비교의 기준이 되는 MM 복귀 경로를 먼저 바로잡는 것이다.

## 23:08 사용자 재현과 공중 판정 보완

두 수정 중 연결 레이어 검색 완료는 실제 PIE에서도 확인했다. 예를 들어 Start 취소 Frame 2086 뒤 2087에서 `NestedTraversals=120`, `ReselectSearches=1`, `LastSearchRequest=5`, `FromHistory=1`이 기록되고 2088에서 pending이 해제됐다. 내부 traversal이 존재해도 검색 완료를 정상 소비한다.

그러나 이동 착지 취소는 아직 해결되지 않았다. OTM Frame 2448~2450과 Strafe Frame 3639~3642에서 입력 1·속도 500·Land 1인데 `GroundIntent=0`, Phase Idle이었다. Idle 검색 요청 11/31이 처리된 뒤 착지 컴포넌트가 별도로 끝나면서 이동 Cycle 검색 12/32가 발생했다. 따라서 pending 해제만으로 올바른 복귀를 판단할 수 없다.

원인은 `BeginLandingState`가 착지 표시 수명 동안 `bIsInAir=true`, `bIsPhysicallyInAir=false`를 게시하는데, 최초 구현이 전자를 실제 공중으로 해석한 것이다. 최초 테스트는 착지 producer를 실행하지 않은 기본 컴포넌트와 수동 ground intent를 사용해 이 경로를 놓쳤다. 보완 테스트는 실제 캐릭터의 `BeginLandingState`와 `BuildDerivedLocomotionContext`를 실행한다. 수정 전 동일 오류와 Cycle 복귀 실패를 재현했으며, 단위 fixture의 수동 값만으로 해결을 선언했던 검증 범위를 정정한다.

## 23:28 사용자 재현 — 이동 Cycle 복귀 확인

공중 판정 보완 후 사용자 로그에서는 이동 착지 취소 3건 모두 `GroundIntent=1`을 유지하고 취소 프레임부터 이동 Cycle DB를 요청했다. 다음 worker 프레임에 실제 검색이 완료됐으며 `FromHistory=1`, 이동 DB의 대표 노드 가중치 1이었다.

| 취소 프레임 | 회전 모드 | 요청 DB | 실제 검색 프레임 | 검색까지 관측 시간 | pending 해제 관측 프레임 |
| --- | --- | --- | --- | --- | --- |
| 1431 | OTM | `PSD_Run_Cycle` | 1432 | 약 8ms | 1433 |
| 1639 | OTM | `PSD_Run_Cycle` | 1640 | 약 9ms | 1641 |
| 2034 | Strafe | `PSD_Combat_Run_Cycle` | 2035 | 약 9ms | 2036 |

착지 컴포넌트가 2프레임 뒤 별도로 취소되더라도 이미 복귀한 Cycle과 검색 요청 번호는 유지됐다. 기록된 이동 입력·속도 100 이상·Land 활성 snapshot 중 Phase Idle 사례는 0이었다. 취소 프레임에 남은 기존 Idle 캐시(1431)는 가중치 0이며 이전 worker snapshot의 결과이므로 Idle 재생으로 판단하지 않는다.

Start 취소 2753·2916도 각각 다음 프레임 2754·2917에서 이동 Cycle 검색을 완료했다. 입력 해제 착지(2678)는 다음 프레임 Stop 표시(2679)로 이어졌다. 취소 후 이동을 유지하는 사례와 입력을 해제하는 사례를 구분한다.

OTM↔Strafe 전환은 1878·2452에 기록됐으나 두 경계 모두 Land 비활성이다. 따라서 이 로그는 착지 도중 모드 전환의 중복 재생 검증을 추가하지 않는다. 검색과 복귀 경로는 확인했지만 최종 몸체의 부드러움·발 미끄러짐을 수치로 입증하는 기록은 아니다. 원본은 당시 `Saved/Logs/Project_J.log`(최종 수정 23:28:29), 파싱한 관측은 `Saved/Logs/LandingPhysicalAir_PIE_Parsed.json`에 보존했다.

## 검증

직접 UBT로 `Project_JEditor Win64 Development`와 `Project_J Win64 Development` 빌드가 모두 성공했다. 공중 판정 보완 후 최종 빌드 로그는 `Saved/Logs/LandingPhysicalAir_BuildEditor.log`와 `Saved/Logs/LandingPhysicalAir_BuildGame.log`다. 실행 중인 빌드·에디터를 중단하거나 빌드를 겹쳐 실행하지 않았다. 최종 NullRHI 자동 테스트 29개가 모두 성공했고 경고 포함 성공·실패·미실행·실행 중은 0이다. 보고서는 `Saved/Automation/LandingPhysicalAir/index.json`, 실행 로그는 `Saved/Logs/LandingPhysicalAir_Automation.log`다. 최초 변경의 빌드·보고서는 기존 `LandingReturn` 이름으로 별도 보존한다.

실제 착지 진입으로 보강한 테스트는 코드 보완 전 1개 실패(`Saved/Automation/LandingPhysicalAirRed/index.json`), 보완 후 성공했다. 이동 의도와 OTM/Strafe Cycle 복귀 실패를 실제 producer에서 재현했으며, 착지 요청이 남은 상태에서 다시 실제 공중이 되는 경우와 착지 없는 JumpStart도 확인했다. 새 Tick·CharacterMovement 조회·검색 주기 확대 없이 기존 캐시의 의미를 분리한 변경이다.

새 `ProjectJ.Animation.LandingReturnContext`는 이동 Land 취소 직후와 후속 snapshot, OTM/Strafe의 같은 착지 중복 방지, 서 있는 착지·로컬 입력 해제, 원격 이동, 실제 공중·탑승·새 착지 리비전 제외를 확인한다. 새 `ProjectJ.MMCrowd.NestedGraphSearch`는 실제 엔진 History→MM 검색 전에 proxy 업데이트를 중첩시키고, 임시 timer/throttle 유지·검색 소비·정상 예산 복원·비활성 traversal의 요청 보관을 확인한다. 그 외 기존 Start·Stop·TIP·네 방향/연속 Pivot·모드 전환·Strafe facing·원격·mount·군중 schedule 회귀를 포함한다.

단위 fixture의 빈 후보 검색은 실제 ABP의 후보 품질·최종 몸체 포즈 검증을 대신하지 않는다. 연결 레이어의 요청 소비는 23:08 PIE 로그로 확인했고, 공중 판정 보완 후 이동 착지 취소의 Cycle 복귀는 23:28 PIE 로그에서 OTM·Strafe 모두 확인했다. 최종 몸체의 발 접지와 연결 품질은 별도 시각 평가가 필요하다.
