# Stop → Idle 포즈 전환 보완 (2026-09-12)

정지 후 제자리 걷기가 남는 사용자 보고를 조사하면서, Motion Matching 데이터베이스 교체 정책의 누락을 확인했다. Stop과 Idle은 모두 `bIsMotionMatchingMoving=false`이므로 기존 정책은 이 경계에서 `DoNotInterrupt`를 반환했다. UE 5.8의 PoseSearch는 이 모드에서 이전 데이터베이스의 continuing pose를 허용한다. 이는 사용자 화면에서 발생한 모든 경우를 직접 재현했다는 뜻은 아니다.

목적지 스냅샷이 지상 Idle이며 이동 의도가 없으면 `InterruptOnDatabaseChangeAndInvalidateContinuingPose`를 사용한다. 새 Idle 데이터베이스에 속하지 않는 기존 포즈를 버리고 pose history로 다음 검색을 구성한다. 강제 재검색 경로도 같은 무효화 계약을 유지한다.

- 정책은 캐릭터별 애니메이션 프록시의 값 스냅샷만 읽는다. 게임 이동 처리나 스레드 소유권은 바꾸지 않는다.
- 한 프레임의 Stop→Idle 이벤트에 의존하지 않으므로 갱신 간격 제한 후 늦게 적용되는 데이터베이스에도 유효하다.
- 실제 데이터베이스가 바뀔 때만 설정하므로 안정된 Idle에서 매 프레임 재검색하거나 애니메이션을 재시작하지 않는다.
- 기존 Cycle, 공중 상태 및 전환 애니메이션의 재생 시간 정책은 유지한다.

`ProjectJ.Animation.StopIdleInterrupt`는 전투/비전투에서 즉시 및 지연 Idle 적용을 검사한다. 수정 전 실제 실행은 4개 assertion 실패, 수정 후 통과했다. 지속 이동과 공중 상태의 기존 정책도 검사한다. 이 테스트는 전환 정책 회귀 검사이며, 사용자 플레이 장면의 영상 재현이나 다인원 네트워크 성능 측정은 아니다.

로컬 증거:

- `Saved/Validation/EF_20260912/StopIdleBaseline01`: 수정 전 실패.
- `Saved/Validation/EF_20260912/StopIdleAfter01`: 정지 정책, 에셋 적용 후 검증, 장비 100개체 및 애니메이션 검사 14개 통과. 전투 연속성 5개는 필수 `a.Budget.BudgetMs=0.1` 누락으로 준비 단계 실패했다.
- `Saved/Validation/StopIdle_20260912/CombatAfter01`: 올바른 예산 설정으로 전투 연속성 5개 통과, 오류·경고 0. 앞 실행의 성공 14개와 합쳐 서로 다른 관련 검사 19개를 통과했다.
- `Saved/Validation/StopIdle_20260912/FixBuild03.log`: 수정된 캐릭터 모듈과 검증 플러그인 직접 UBT 빌드 성공.
- `Saved/Validation/StopIdle_20260912/Apply`: 승인된 ABP 2개·Trail 1개 저장 기록. 적용 후 새 프로세스에서 ABP 연결과 컴파일, Trail 25m 컬링 설정을 확인했다.

적용 후 사용자 확인: 에디터를 새로 열고 걷기·달리기에서 짧게 입력 후 해제, 방향 전환 직후 해제, 전투 모드에서 해제를 반복해 정지 포즈가 Idle로 이어지는지 확인한다.
