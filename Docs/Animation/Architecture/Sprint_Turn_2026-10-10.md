# Sprint 회전 후보와 보행 속도 전환

## 근거와 적용 범위

Epic의 [GASP 설명](https://dev.epicgames.com/documentation/en-us/unreal-engine/game-animation-sample-project-in-unreal-engine)은 캡슐 이동과 상황별 후보 필터링, 현재 포즈와 예측 경로를 이용하는 구조를 설명한다. [Motion Matching 문서](https://dev.epicgames.com/documentation/en-us/unreal-engine/motion-matching-in-unreal-engine)의 여러 PSD 간 정규화 통계 공유 원칙을 적용한다. 로컬 GASP 5.7 분석과 기존 `Planning/GASP_ProjectJ_Naturalness_2026-10-06/02_GASP.md`도 참고했다. Project J의 UE5.8 이동 정책이나 GASP의 experimental SM 전체를 교체하는 작업은 아니다.

Run에서 검증한 Tracking → Preparing → Active 물리 보정 이벤트를 Sprint에도 사용한다. 카메라 누적 회전 각도만으로 Turn을 강제로 선택하지 않는다. 느린 곡선은 Cycle에서, 일반 보정은 Cycle + GeneralTurn에서, 큰 반전은 TurnRedirect + Cycle에서 검색한다. 후보는 최대 2개이며 실제 선택은 Motion Matching이 한다.

## 소유권과 설정

- OTM/Strafe × Run/Sprint의 acute 설정을 LocomotionProfile에서 따로 제공한다. Run의 180cm/s qualification과 기존 Combat 입장/회복 설정은 유지한다. Sprint 기본 qualification은 300cm/s, 일반 보정 entry speed는 300cm/s이다. 저작 데이터에 맞게 조정할 수 있는 초기값이다.
- Strafe Sprint의 전방 대각선 경계에는 부동소수점 오차용 0.5도 여유를 둔다. 전방/전방 좌우 대각선의 Sprint 허용은 기존 실제 입력 정책을 재사용한다. 옆/뒤 이동에 전방 Turn을 빌려주지 않는다.
- GT 물리 요청과 완료된 primary-mesh 선택 피드백에 gait를 포함한다. 이전 Run 선택 결과가 Sprint 완료/연장을 결정할 수 없다. 일반 보정의 짧은 각도 이력도 gait가 바뀌면 초기화한다.
- 동일한 물리 반전이 진행 중이면 급회전의 qualified origin은 유지하되, gait가 바뀔 때 이전 PSD 선택/완료 상태를 폐기하고 Preparing에서 새 설정으로 재검증한다. 새 Sprint 후보 데이터가 호환되지 않으면 입장을 차단하고 `SprintCandidateDataUnavailable`을 기록한다.
- 입력 해제, 공중/착지/Start/Pivot/TIP, 전투 동작, Montage/root motion, 소유자/회전 모드 변경은 기존 우선순위를 유지한다. 물리 입력, RPC, 이동 복제 또는 재생 위치를 강제로 바꾸지 않는다.

## Sprint 데이터

각 모드의 기존 Sprint Turn PSD에서 활성화된 90/180도 항목을 읽어 별도 GeneralTurn/TurnRedirect PSD를 구성한다. 원본 Turn PSD와 Cycle의 애니메이션 활성화 상태는 보존한다. 없는 저작 커버리지는 추정해서 채우지 않는다. 각 모드의 Sprint Cycle, GeneralTurn, TurnRedirect는 기존 모드 schema와 하나의 전용 Normalization Set을 공유한다. Run PSD/PSS/Normalization은 수정하지 않는다.

일회성 Editor 저작 도구로 해당 데이터의 인덱스를 검증한 뒤 사용자 승인을 받아 10개 패키지를 저장했다. 커밋 정리 단계에서 이 생성·저장 명령과 전용 AssetRegistry 의존성을 제거했다. 최종 코드에는 에셋을 다시 구성하거나 저장하는 테스트 경로가 없다. 이후 데이터 편집은 기존 PoseSearch 에셋 에디터를 사용한다.

## 검증

`ProjectJ.MovingTurn.CoordinatedHandoff`는 Run/Sprint, OTM/Strafe, 좌우, 30/60/144fps를 검사한다. `SprintFamilyOwnership`은 gait 피드백 분리, 전환 중 물리 보정 유지, 전방 대각선/옆 방향 경계, 정렬된 곡선의 Turn 오입장 방지를 검사한다.

`-ProjectJVerifySprintTurns`와 `ProjectJ.Animation.Naturalness.SprintTurnCMC`는 실제 저작 Blueprint/GAS/CMC와 충돌 바닥으로 검사한다. 항상 저장된 실제 데이터를 사용한다. `SprintTurnData`는 실제 연결·90/180 분리·양발/좌우 커버리지와 저장된 cook 호환성 플래그를 확인한다. 기록은 `Saved/Validation/SprintTurn_20261010/SprintPlayback.txt`에 남긴다. NullRHI 검사는 실제 렌더링된 발 접지나 시각 자연스러움의 A/B 측정이 아니다.

완료된 저장 전 검증:

- Editor/Game 직접 UBT 빌드 성공. 마지막 Game 빌드는 `GameBuildFinal.log`, 데이터 검사 Editor 빌드는 `EditorBuildDataChecks.log`, 검증 로그 헤더 정정 후 Editor 빌드는 `EditorBuildReportHeader.log`이다.
- 회귀 자동화 40개 성공, 실패/경고 0: `RegressionTests/index.json`.
- 실제 Sprint 29개 시나리오와 기존 Run 57개 시나리오 통과. Sprint 전방/대각선 급회전은 양방향 180도 포즈를 실제 선택했다. 600도/초 전방 반전도 OTM/Strafe 좌우 모두 실제 Turn을 선택했다. 90도/초의 정렬된 곡선은 General/Acute 입장 0이었다.
- Sprint 각 시나리오의 측정 240프레임 모두 접지, held-input FalseStop 0, 전환 안정화 이후 Run pool 오선택 0. 입력 해제/옆·뒤/Dodge 중단과 Run↔Sprint 전환을 검사했다. 프레임 로그는 `SprintPlayback_Memory.txt`, Run 원본 회귀 기록은 `RunPlayback.txt`이다.
- Computer Use로 Editor 명령을 실행해 실제 Sprint 10개 패키지를 저장했다. 저장 전 비교에서 Run의 PSD/PSS/Normalization 참조와 애니메이션 목록, Sprint Cycle의 애니메이션 목록을 보존했다. 전후 비교는 `EditorAssetsBeforeSave.json` / `EditorAssetsAfterSave.json`, 저장 기록은 `EditorAuthoring.log`, 기존 4개 패키지의 SHA-256/백업은 `AssetsOriginalHashes.json` / `AssetBackup/`에 있다. 레벨과 Run 에셋은 저장하지 않았다.

저장 후 새 프로세스 검증:

- 디스크의 저장된 에셋을 불러와 SprintTurnCMC/SprintTurnData 및 GeneralTurning/LocomotionContinuity 16개 검사 성공, 실패/경고/미실행 0: `SavedDataTests/index.json`.
- 저장된 실제 후보를 사용한 Sprint 29개 시나리오 모두 측정 240프레임 접지, FalseStop 0, 안정화 이후 Run pool 오선택 0: `SprintPlayback.txt`.
- Git Content 변경 범위는 승인한 기존 4개·신규 6개 패키지로 확인했다. Config·레벨·Run PSD/PSS에는 변경이 없다. 요약 기록은 `Saved/Validation/SprintTurn_20261010/Verification.json`이다.

에디터에서는 OTM/Strafe의 전방·전방 좌우 대각선 Sprint에서 느린 곡선, 좌우 180도 반전, 회전 중 Sprint 해제·회피를 확인한다. 추가 연결 작업은 필요 없다. 기존 `p.ProjectJ.MovingTurnTrace 1`을 켜면 입력/물리 보정/실제 PSD 선택 진단을 남길 수 있고 `0`으로 끈다.

사용자 PIE 로그에서도 OTM 8회·Strafe 8회 물리 급회전이 모두 실제 180도 포즈를 선택하고 Aligned/CycleHandoff로 복귀했다. Loop/Diamond 및 일반 90도 보정 선택도 확인했다. `MovingTurnTrace Stage=Result Candidates`는 검색 PSD 수가 아니라 결과를 가진 MM 노드 수이다. 실제 PSD 후보 수는 `GetThreadSafeMotionMatchingCandidateCount`/`LocomotionContinuityTrace`로 확인해야 한다.

커밋 전 정리와 재검증:

- 일회성 에셋 생성·저장 코드 2개와 AssetRegistry 의존성, 메모리 데이터 준비 플래그/분기를 제거했다. 테스트가 저장된 에셋의 누락이나 cook 호환성 오류를 생성 코드로 가리지 않는다.
- 회전 후보 창에서 Turn + Cycle 두 PSD가 실제 제공되는지 검사한다. 급회전뿐 아니라 600도/초 반전에서도 실제 180도 선택을 명시적으로 검사한다. 테스트 fixture 생성 실패 시 null Player 정리도 처리했다.
- 최종 Editor/Game 직접 UBT 빌드 성공: `EditorBuildCommitCleanup.log` / `GameBuildCommitCleanup.log`.
- 정리 후 회전 정책·GeneralTurning·LocomotionContinuity·Sprint 실제 데이터 검사 29개 성공, 실패/경고/미실행 0: `CommitCleanupTests/index.json`. 실제 Sprint 29개 시나리오도 모두 측정 240프레임 접지, FalseStop 0, 안정화 이후 Run pool 오선택 0이었다.
- 디버깅 기록·백업·빌드/테스트 산출물은 기존 ignored `Saved` 경로에 보존하며 커밋에 포함하지 않는다. 검증된 런타임 정책에 추가 계층이나 새 타이머를 도입하지 않는다.
