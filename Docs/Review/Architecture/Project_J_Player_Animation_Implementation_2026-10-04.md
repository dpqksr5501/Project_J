# 플레이어·궤적·애니메이션 고도화 적용

기준: 2026-10-04, UE 5.8. [플레이어 재점검 P01–P13](Project_J_Player_Animation_Audit_2026-10-04.md)의 후속 구현이다. 기존 PlayerState/ASC, Avatar 표현, 의미 Locomotion, GT snapshot/worker 소비, GAS 실행, DA 선택 구조를 유지한다. 기존 미커밋 R01–R14 구현 위에 필요한 수명·유효성·복구 계약을 보강한다.

## 항목별 적용

| 감사 항목 | 적용 내용 | 유지한 계약·주의점 |
| --- | --- | --- |
| P01 장비 ASC | 장비 ledger가 부여 당시 ASC를 weak reference로 보관한다. grant source, GE handle, stat fallback을 이 ASC에서만 회수하고 ledger를 초기화한다. | UnPossessed에서 무조건 장비를 해제하지 않는다. 탑승 중 지속 장비 효과를 보존한다. ASC 자체가 소멸하면 다른 ASC에 회수 작업을 전가하지 않는다. 지속 소유자로 전체 장비 구현을 이주한 것은 아니다. |
| P02 탑승 | Mounted tag의 원래 ASC를 보관한다. 강제 하차는 위치 탐색 실패에도 관계·태그를 정리한다. Mount Destroy에서 live rider를 복구하고, travel/component EndPlay에서는 재possess를 생략한다. | 일반 하차는 위치·비행 조건을 유지한다. 출구를 네 방향에서 찾고 모두 막히면 강제 종료는 rider의 현재 위치를 보존한다. 충돌·이동 모드는 탑승 전 값으로 복구한다. 모든 지형의 안전 착지까지 보장하지 않는다. |
| P03 궤적 | 미래 속도 조회는 현재 generation demand, 생성 이후 reset 여부, sample age를 검사한다. 무효면 zero/false를 반환하여 기존 CMC 가속·제동 추정으로 돌아간다. | 기존 generation/reset 데이터와 배열을 재사용한다. 유효 시간 기본값은 0.25초이며 제작 설정으로 조정한다. 임의의 한 프레임 만료를 사용하지 않는다. |
| P04 가시성 | 공통 character visual demand 함수를 AnimInstance, Locomotion, trajectory가 사용한다. 숨긴 leader와 보이는 follower/equipment의 기준을 일치시킨다. | 시각 수요와 권한/게임플레이 pose 수요를 합치지 않는다. hidden leader의 shadow timestamp만으로 시각 수요를 만들지 않는다. |
| P05 프레임 출처 | snapshot에 생성 프레임·소비 프레임·reset reason·예측 유효성·소비 후 reset revision/reason을 추가한다. | Stop/Chooser가 소비한 이전 이력을 같은 프레임에 덮어쓰지 않는다. 로컬 combat strafe의 history 보존 예외를 유지한다. |
| P06 Layer | 동일 경로의 실패를 기록해 Refresh 재요청을 차단한다. 경로 변경/명시적 Retry가 새 generation을 만든다. 늦은 callback과 EndPlay callback은 무시한다. | async/default pose를 유지한다. linked class뿐 아니라 원래 mesh/master와 실제 linked instance를 확인한다. 원래 master가 교체됐으면 새 master의 다른 소유자 연결을 해제하지 않는다. |
| P07 Combo Notify | mesh + Notify instance ID가 원래 GA의 window token을 보관한다. node/activation 경계가 이전 토큰을 폐기한다. End는 해당 토큰만 회수하고 겹친 창을 보존한다. | 기존 event tag와 authored external event 경로를 유지한다. 외부 제작 이벤트는 실행 소유권을 제공하지 않으면 기존의 현재 창 제어 계약이 남는다. |
| P08 VFX Notify | presentation component가 cue lease를 발급한다. Notify End는 원래 component/token으로 종료한다. 같은 tag의 다른 lease가 있으면 loop를 유지한다. | 공격 교체/취소/표현 변경 시 lease를 폐기한다. one-shot dedup과 active looping resource를 분리해 loop Stop→Play를 허용한다. 기존 네트워크 recovery/order 정책을 유지한다. |
| P09 제작·조합 | 최종 ASC의 ability specs에서 input 충돌을 진단한다. Command runtime/editor의 최대 입력 수 16을 통일하고 NaN/무한 시간 값을 거절한다. GE 수명·stat finite·montage section 검증을 추가한다. | 입력 충돌이 의도된 activation exclusivity일 수 있어 일괄 제거하지 않는다. Instant 장비 GE를 적용하지 않고 validator에서 거절한다. 기존 timed GE는 유지하며 만료 의도를 확인하도록 경고한다. RootMotionWarped enum만으로 native warping executor가 생기지는 않는다. |
| P10 설정 변경 | 실행 시작 시 gameplay style/revision을 잡고, 기본적으로 현재 combo/commands를 완료한다. style가 선택하면 gameplay style 변경 시 취소한다. | 시각용 style 변경은 gameplay 취소 이유가 아니다. 장비 회수는 기존 즉시 취소 계약이 우선한다. 모든 종류의 GA를 새 실행 프레임워크로 바꾸지 않는다. |
| P11 MM 관측 | 대표 결과의 node index, 후보 수, capture frame, 공개 cached weight, 결과 변경 프레임을 보관한다. | 기존 대표 결과 선택 정책을 유지한다. cached weight/관측 시각은 현재 traversal 또는 최종 blend 기여의 증명이 아니다. 엔진 private state 접근·엔진 수정은 사용하지 않는다. |
| P12 ABP 비용 | 현재 snapshot/worker 경계와 profiling scope를 유지한다. | master의 43 bound-function inventory만으로 일괄 제거·에셋 migration을 적용하지 않았다. 실제 Insights 측정 후 고비용 getter만 변경할 조건부 항목이다. |
| P13 시간 기준 | StateController와 TIP의 재생 경과를 native animation delta 누적 clock으로 통일한다. TIP getter는 worker와 공유하는 published elapsed를 읽는다. | UE가 제공한 dilation/URO accumulated delta를 한 번만 사용한다. pause에서는 clock을 진행하지 않는다. 실시간 debug/watchdog deadline은 유지한다. 숨겨진 원격의 갱신 생략 자체를 제거하지 않는다. |

탑승 왕복 회귀에서 `Character::Restart`가 재possess 시 복구한 이동 모드를 기본 모드로 덮어쓰는 것을 확인했다. 원래 이동 모드를 재possess 후 다시 확정하여 flying/custom 등 기존 이동 정책을 보존한다. 탑승 테스트는 추상 기본 클래스 대신 구체 native fixture를 사용하고 실제 world 시작·component 초기화·Destroy 순서로 실행한다.

P05/P11의 새 정보는 기존 `GetAnimationDebugSummary()`에서도 요청 시 출력한다. 궤적 생성/소비 프레임, reset 전후 revision/reason, 예측 유효성, 대표 MM node·후보 수·capture frame·cached weight·결과 변경 프레임을 함께 비교할 수 있다. 매 프레임 문자열을 추가 생성하지 않는다. 대표 결과를 최종 blend 기여로 단정하지 않도록 표시한다.

## 검증

직접 `UnrealBuildTool.exe`로 `Project_JEditor Win64 Development`와 `Project_J Win64 Development`를 빌드하여 둘 다 성공·exit 0을 확인했다. Editor 최종 로그는 `Saved/Validation/PlayerMaturity_20261004/BuildEditor_Retry4.log`, Game 로그는 같은 폴더의 `BuildGame.log`다. 실행 중인 build/editor를 중단하거나 빌드와 중첩하지 않았다.

새 `ProjectJ.PlayerMaturity` 회귀 13개는 전부 성공했다(정상 12, 경고 동반 1). 기존 플레이어 감사의 87개를 모두 포함하고 기존 표현 복구 통합 1개를 더한 최종 회귀는 **101개 성공: 정상 97, 경고 동반 4, 실패·미실행·실행 중 0**이다. 이전 실행의 성공 수와 합산한 고유 테스트 수가 아니다. `UnrealEditor-Cmd.exe -EnablePlugins=ProjectJExperiments -unattended -NullRHI -nosound -NoLiveCoding -nop4 -nosplash`로 실행하고 프로세스 exit뿐 아니라 report의 개별 상태와 오류 0을 확인했다.

최종 report는 `Saved/Automation/PlayerMaturity_20261004_Regression/index.json`, log는 `Saved/Validation/PlayerMaturity_20261004/Regression.log`다. 경고 8건은 기존 TwoHandIKTransitionAndCurve(1), WeaponPresentationIdentity(5), StableGripTargetsAndAuthoredAlpha(1)의 Draw/Sheathe socket 미설정과 새 PossessionAndBlockedExit(1)의 native fixture 애니메이션 프로필 미설정이다. 제작 경고를 삭제하거나 runtime 검증을 완화하지 않았다.

작업 전 소스·빌드 설정·검증 스크립트 444개의 파일 내용을 별도 보관해 기존 미커밋 구현을 기준으로 비교했다. 이번에는 40개 수정·5개 추가, 나머지 404개는 hash가 동일하며 기존 파일 삭제는 없다. [검증 JSON](Project_J_Player_Animation_Implementation_Validation_2026-10-04.json)에 빌드·report hash, 새 테스트 이름, 경고 원문, 변경 파일별 작업 전후 SHA-256을 기록했다.

추가 회귀는 원래 ASC 회수, 외부 Mounted tag 보존, possession 왕복/출구 차단/Destroy, 궤적 reset·age·희소 갱신, 공통 가시성, 콤보 중첩/새 노드, VFX 중첩/새 공격, layer callback generation, 30/60/120fps·pause·URO delta·TIP snapshot, 최종 input 충돌, command/GE/montage 제작 경계를 검사한다.

Combo/VFX 신규 회귀는 owned token·lease의 중첩·폐기 및 복구 상태를 검사한다. 실제 Niagara 렌더링과 Notify 에셋 전체 연결을 검증한 것은 아니다. 설정 변경 정책 회귀 역시 취소 판단을 검사하며 active GA/실제 graph 전체 실행과 구분한다. 조건부 P12 ABP 성능 작업은 실제 profiling 근거가 생긴 뒤 적용할 항목으로 유지한다.

실제 Blueprint·Chooser·Notify 연결 전체, 네트워크 지연 PIE, GPU 시각 품질, 대규모 프레임 비용은 이 코드/자동화 검증과 구분한다. 변경된 시간·layer·notify 정책은 실제 대표 캐릭터의 OTM/Start/Stop/Pivot/TIP 재생에서 추가 확인해야 한다. 에셋을 수정하거나 저장하지 않았고 Unreal MCP를 사용하지 않았다.
