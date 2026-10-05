# 일회성 이동 동작의 요청 수명 — 2026-10-05

## 사용자 PIE에서 확인한 원인

원본 로그를 `Saved/Validation/PivotLandingLifetime_20261005/UserPIE.log`에 보존했다. 이전 입력 기준 수정만으로 해결되지 않았으며, 이번 원인은 사용자의 실제 연속 입력 로그에서 확인했다.

- 우→좌와 뒤→앞도 속도 500, 반전 180도로 Pivot 감지가 성공했다. 하지만 진행 중인 Pivot의 `MoveIntentRevision`이 바뀌었다는 이유로 `ReconcilePivot`이 **새로 승인된 Pivot까지 Redirect로 억제**했다. 요청 1→2(08:18:14.426 UTC), 3→4(08:18:17.093 UTC)가 모두 `Reason=3`으로 취소됐다. 방향 자체의 데이터 누락이 아니라 이전 동작 재생 중 새 요청을 처리하는 우선순위 결함이다.
- 같은 착지 `LandEpoch=1`에서 전투 bool 변경(08:18:12.069 UTC)과 Strafe 변경(08:18:12.076 UTC)이 각각 Chooser 재평가를 만들었다. 선택 revision 5와 6이 모두 `ForceBlend=true`였다. 첫 변경은 같은 에셋 StartTime 1.1부터, 두 번째는 다른 착지 에셋 StartTime 1.0부터 다시 시작했다. 논리 hold는 2.336→2.344초로 진행했으므로 실제 Blend Stack과 hold clock이 서로 다른 시간에 있었다.

이전에 네 방향을 새 AnimInstance마다 검사한 자동화는 연속 반전 수명을 검증하지 못했다. 이번 테스트는 같은 instance와 실제 Chooser에서 이전 Pivot이 끝나기 전에 세 번 반전한다.

## 선택과 재생의 소유권

`FProject_JStateControllerRuntime`은 요청 소비와 hold를 관리하고, AnimInstance의 Chooser cache는 해당 동작의 에셋과 출력(StartTime, BlendTime 등)을 보관한다. 모드·발 접촉·방향 같은 최신 context는 계속 snapshot/MM에 전달하지만, 그것만으로 재생 명령을 다시 만들지 않는다.

| 동작 | 같은 동작 동안 유지 | 새 선택을 허용하는 경계 |
| --- | --- | --- |
| Start | 선택 에셋·출력·hold clock | 입력 해제 후 새 이동, 초기 gait 보정; 확정 이후 gait 변경은 기존 MM 복귀 |
| Pivot | 승인된 요청 revision의 에셋·발·출력 | 조건을 충족한 새 Pivot 요청. Stop 우선, 일반 redirect/camera 취소는 기존 MM 복귀 |
| Stop | 같은 입력 해제의 에셋·출력 | 이동 입력을 다시 시작한 뒤 다음 해제 |
| Land | 같은 착지 epoch의 에셋·출력 | 새 착지 epoch, 완료·취소·전신 동작·탑승 경계 |
| Jump/FallOff | 같은 공중 진입의 에셋·출력 | 기존 명시적 점프 방향 재선택, FallOff 변경, 착지/상태 이탈 |
| TIP | 같은 회전 sequence의 에셋·출력 | 새 sequence의 명시적 재선택; 같은 에셋이어도 새 명령이면 재시작 |

Pivot의 새 승인 요청을 일반 입력 redirect보다 먼저 처리한다. 중단된 일반 redirect의 지연 snapshot은 계속 억제한다. Start는 이동 episode에서 소비되며, 중단/종료 후 늦은 Start phase가 남아 있어도 같은 이동 입력으로 재진입하지 않는다. 입력 해제로 재활성화하며 Pivot은 독립 요청으로 처리한다.

Chooser lock은 유효한 선택이 있는 같은 presentation에만 적용한다. 유효하지 않은 선택은 다음 평가를 막지 않는다. 상태 이탈·cache 무효화와 기존 full-body/mount 경계는 유지한다. 점프의 명시적 재선택은 기존 진행률 보존 경로를 통과한다.

추가 tick, 검색 loop, 군중 manager, per-frame 할당을 도입하지 않았다. 모드 전환 중 불필요한 Chooser/MotionMatch 재평가를 줄이고, [MM 복귀 검색·군중 예산](MotionMatching_Return_Crowd_2026-10-05.md)의 최신 snapshot·요청 보관·실제 검색 확인 정책을 유지한다. ABP·Chooser·PSS·레벨 에셋은 수정하거나 저장하지 않았다.

## 검증 범위

- `ProjectJ.StrafePivot.ConsecutiveReversals`: 네 시작 방향 × 두 발 × 세 연속 반전, 같은 AnimInstance에서 감지→snapshot→hold→실제 Chooser→commit/pulse를 확인한다. 같은 snapshot 재평가의 중복 pulse도 확인한다.
- `ProjectJ.Animation.OneShotModeContinuity`: Start·Stop·Land·Jump/FallOff·TIP에서 양방향 OTM/Strafe 전환을 네 차례 반복하고, 전투와 회전 변경이 서로 다른 update에 도착하는 경우를 포함한다. 에셋·StartTime·selection revision·hold clock과 중복 pulse를 확인한다. 실제 착지 Chooser/hold와 새 착지 epoch도 검사한다.
- `ProjectJ.Animation.StartEpisodeLifetime`: 중단 후 같은 Start 재진입 방지, 새 이동 재활성화, 독립 Pivot, full-body 및 owner reset을 확인한다.
- 기존 Pivot redirect 테스트는 **새 승인 Pivot과 일반 redirect를 구분**한다. 과거에 새 Pivot도 MM으로 보내도록 요구하던 잘못된 기대값을 수정했다.

빌드 및 회귀 결과는 [검증 기록](OneShot_Command_Lifetime_Validation_2026-10-05.json)에 기록한다. 자동화는 실제 렌더된 자세의 연결 품질을 보증하지 않는다. 수정 후 사용자 PIE에서 연속 네 반전과 Start/Land 중 Tab 양방향 전환을 확인해야 한다.

최종 Editor/Game Development 빌드는 직접 UnrealBuildTool로 완료했다. 회귀 121개는 정상 성공 117, 기존 경고 포함 성공 4, 실패·미실행·실행 중 0이다. 신규 세 테스트는 경고·오류 0이며 기존 경고 8건은 이전 report와 동일하다. 최종 report는 `Saved/Automation/PivotLandingLifetime_Final_20261005/index.json`이다. 최초 빌드의 테스트 friend 선언 누락과 최초 smoke의 미해결 입력 fixture 오류는 보완했고 중간 결과도 보존했다.

2026-10-05 후속 사용자 확인: 수정 후 "잘된다"고 보고했다. 이는 사용자 PIE 확인이며, 수정 후 별도의 영상이나 로그 분석 결과는 아니다.

진단은 그대로 유지한다. `p.ProjectJ.StrafePivotDebug`의 Editor 기본값은 1, Game 기본값은 0이다. 같은 요청 중 `SelectionRevision`이 유지되고 `ForceBlend`가 반복되지 않는지, 새 Pivot마다 새 commit이 생기는지 실제 `ExternalStack/StackPlayer`와 함께 확인한다. 확인 후 `p.ProjectJ.StrafePivotDebug 0`으로 끌 수 있다.
