# Strafe 실제 입력 회귀 데이터

`StrafeMicroCamera_20261012.csv`는 2026-10-12 21:26~21:27 사용자 재현 로그의 `Stage=Facing` 중 Frame 3200~4007에서 가져온 808개 제어 카메라 yaw 표본이다. 첫 표본의 시간과 yaw를 빼서 `경과 초,yaw 도`로 저장했다. 기간은 6.736초다. 초기 큰 회전 구간은 포함하지 않는다.

`ProjectJ.Animation.Naturalness.RecordedStrafeMicro`가 실제 캐릭터 BP, CMC, 충돌 바닥과 Master ABP로 재생한다. 파일 부재·잘못된 표본·Box/Diamond 선택·Loop 유지 부족은 실패한다. 기본은 120Hz이며 `-ProjectJStrafePlaybackHz=60` 또는 `30`으로 동일한 시간 축을 재표본화할 수 있다.

카메라 입력을 재현하는 데이터이며 원래 월드의 지형, 전체 포즈 이력, 프레임 부하와 렌더 결과를 복제하지 않는다. 따라서 자동화 성공과 실제 에디터 체감 확인은 구분한다.

`-ProjectJStrafeReplayPrecurve`는 같은 입력 전에 90도/초 곡선을 2초 재생한다. `-ProjectJStrafeReplayWarmup`을 함께 주면 그 곡선 전에도 2초 동안 Loop를 재생한다. 실제 Arc 선택을 확인한 뒤 미세 입력의 첫 0.5초는 전환 구간으로 기록하고, 나머지 구간의 Box/Diamond 선택과 Loop 유지를 검사한다. 기본 데이터 재생에는 이 관찰 지연을 적용하지 않는다.

`StrafeCurveCamera_20261012.csv`는 같은 날 23:09 사용자 Arc→Box 재현 로그의 `Stage=Facing` Frame 20268~22900에서 추출한 2,633개 카메라 yaw 표본이다. 기간은 21.95초이며 여러 방향 반전과 회전 속도 변화도 포함한다. 월드 yaw의 ±180도 경계를 풀고 첫 시간과 yaw를 빼서 저장했다. 입력이나 방향 반전 구간을 삭제하지 않는다.

`ProjectJ.Animation.Naturalness.RecordedStrafeCurve`는 이 곡선 기록을 기본으로 재생한다. 실제 이동과 Arc 선택 비율, Box/Diamond 노출 비율을 검사하며, 미세 입력 검사의 Loop 유지 조건을 곡선에 적용하지 않는다. 기록 전체에서 Arc 85% 이상·Box/Diamond 1% 이하를 회귀 기준으로 사용한다. 이 수치는 해당 재현의 품질 기준이며 런타임에서 특정 클립을 선택하거나 차단하는 규칙이 아니다. 30/60/120Hz 재표본화와 Brute 검색·렌더 검증은 같은 파일을 사용한다.

단발 복귀 검사는 `-ProjectJOneShotPlaybackCase=N`으로 한 장면만 재생할 수 있다. 예를 들어 `ProjectJ.Animation.OneShotInput.AuthoredCurvePlayback`의 5번은 Strafe Run 착지 이후 지속 회전이다. `-ProjectJOneShotPlaybackHz=120`으로 재생 간격을 지정하고, `-ProjectJOneShotCurveOutput=`으로 공유 스택의 outgoing weight·단발 보정 방향과 실제 가슴 회전 변위를 기록한다. 가슴 변위는 컴포넌트 공간의 프레임 차이이며 체감이나 월드 발 미끄러짐 판정이 아니다. 장면 번호 옵션을 생략하면 기존 전체 장면을 실행한다.

전방 생산 에셋 검사 `ProjectJ.StrafeForward.ProductionData`와 `TrajectorySchema`는 읽기 전용이다. 작성용 Apply 옵션과 임시 Forward Loop bias·정규화·가중치 sweep 옵션은 커밋 정리 때 제거했다. `-ProjectJForwardCostAudit`는 Editor/dev 자동화에서 명시적으로 요청한 경우에만 비용을 재구성하며, 원래 엔진 검색의 query/filter 캡처로 해석하지 않는다.

`ProjectJ.Animation.Naturalness.StrafeLateralCamera`는 A/D 키 각각과 양방향 카메라 회전 21/90/240도/초의 12장면을 검사한다. ForwardCycle이 열리지 않는지, 기존 측면 후보에 Box와 Diamond가 있는지, 실제 측면 회전이 선택되는지 확인한다. `LateralPose`에 선택 에셋·시간과 전체 로컬 포즈 CRC를 저장하여 동일 바이너리·프로필의 전방 PSD 변경 전후를 비교할 수 있다. 순수 측면 경로에서 Diamond 선택을 강제하지 않는다. 전방 PSD의 Arc 6종/Loop 8종 구성은 [후속 문서](../../../../Docs/Animation/Planning/Strafe_Forward_Intent_2026-10-13.md)에 기록한다.

착지 재현에는 `-ProjectJOneShotCameraPitch=2.8`, `-ProjectJOneShotCurveRate=85`, `-ProjectJOneShotJumpAtSeconds=2.5`로 실제 카메라 시점·곡선 회전율·점프 시점을 지정할 수 있다. 가슴의 `Angle3D`는 쿼터니언의 실제 컴포넌트 공간 프레임 차이다. 기존 수평/Euler yaw 차이와 구분한다. 기본값을 생략한 기존 장면은 유지한다. `Inertial` 행은 실제 엔진 debug traversal의 활성 상태이며, 프레임 수는 사용자 체감 승인이나 성능 측정이 아니다. 임시 착지 전용 CVar 전환 옵션은 제거했다.

`StrafeLandCamera_20261013.csv`는 2026-10-13 10:02 사용자 로그에서 가져온 W 이동·카메라 yaw·Aim Pitch·점프 11회의 재현 데이터다. 원래 3,126개 관측점을 약 41.113초 입력 및 종료 관찰 구간으로 재표본화했다. 열은 `Seconds,Yaw,Pitch,Input,Jump`이며 yaw는 경계를 풀고 첫 값을 뺐다. Pitch는 기록된 Aim Pitch 근사다. `ProjectJ.Animation.OneShotReturn.RecordedStrafeLand`는 Strafe Run Case 5와 이 파일을 고정 사용한다. `-ProjectJOneShotPlaybackHz=120`으로 실제 검증했고, 다른 프레임 간격은 이 기록의 최종 검증에 합산하지 않았다.

Strafe CMC 재생의 기본 보고서는 실행별 GUID 하위 폴더에 저장하고 로그의 `CMC playback evidence:`로 경로를 알린다. 여러 검사가 같은 `Playback.txt`를 덮어쓰지 않는다. 명시한 `-ProjectJStrafePlaybackOutput=` 경로는 유지하며 그 실제 부모 폴더를 만든다. 보고서 저장 실패와 움직임 기준 실패는 구분해 보존한다. [파이프라인 리팩토링 검증](../../../../Docs/Animation/Architecture/Animation_Pipeline_Refactor_2026-10-13.md)에서 후속 재확인 결과를 읽는다.

수동 재현은 `ProjectJ.Animation.OneShotInput.AuthoredCurvePlayback`에 `-ProjectJOneShotPlaybackCase=5 -ProjectJOneShotCameraReplay=<CSV 절대 경로>`를 전달한다. 이 기록 검사는 전방 Strafe Run의 착지 보간 중 실제 MM 재선택을 요구하므로 다른 이동 모드의 일반 회귀 대신 사용하지 않는다. 점프 이벤트는 낮은 재생 주기에서도 건너뛴 표본 전체에서 수집한다. 기존 고정 회전 테스트에는 기록 옵션을 적용하지 않는다. `-ProjectJRenderOneShotReturn`은 실제 RHI에서 복귀 구간의 이미지를 추가한다. `Unified`의 `InertialReturnRemaining`은 요청 보간 수명이며, 실제 엔진 보간 상태와 구분한다.

## 전체 감사의 추가 실패 경로 — 2026-10-13

`ProjectJ.Combat.AuthoredSkillContinuation`은 실제 BP·DA·R montage와 Router의 빠른 RMB tap/의미 입력 대조군을 사용한다. `-ProjectJSkillContinuationHz=30` 또는 `120`으로 같은 시간 지점을 검사하며, 자연 종료·버퍼 중 unequip·새 grant의 독립 시작도 확인한다. MouseChordLifecycle은 chord/hold/cancel/동기 재진입의 입력 수명을 확인한다.

`Scripts/Validation/Measure-Network.ps1 -Animation -CombatInput`은 실제 소켓의 authority/owner/remote R→후속 공격→종료 3회와 기존 AOI/FastArray/animation stages·disconnect를 검사한다. 전투 구간은 CMC가 진행하고 유효한 grant의 활성화 준비를 기다린다. 입력을 반복 재전송하거나 강제 combo window를 열지 않는다.

`Scripts/Validation/Measure-RenderedCrowd.ps1 -RunName <고유 이름>`은 200개 실제 BP construction/retarget 몸체의 viewport 표시와 동시 R, frame delta/GPU counter/PNG를 기록한다. 빈 Entry의 고정 카메라 장면이며 접속자 200명·실제 게임 맵 품질 또는 CPU 비교의 LMB workload와 동일하다는 뜻은 아니다. [감사 결과](../../../../Docs/Review/Audits/Project_J_Whole_Project_Audit_2026-10-13.md)의 조건과 제한을 함께 읽는다.

`-Executable <새 Cook/stage의 Project_J.exe>`를 추가하면 같은 실제 BP 부하를 패키지에서 실행한다. 실행별 새 UserDir를 사용하고 실행 파일·에셋 SHA256과 packaged 여부를 남긴다. 기존 사용자 설정과 이전 검증 결과를 덮어쓰지 않는다. `ProjectJ.Internal.Input.CommandAliasRelease`는 서버 별명 이벤트의 동기 Avatar 교체 후 남은 원입력이 교체 Avatar로 전달되지 않는지도 검사한다.

`ProjectJ.Structure.UI.IconRequestLifetime`은 실제 texture stream completion을 지연해 A→empty→A의 이전 요청과 owner GC 뒤의 적용을 차단하는지 확인한다. CVar 지연은 테스트 종료 시 복구한다. `ProjectJ.PlayerMaturity.Animation.LayerLoadLifecycle`은 shared lease의 stale same-path callback, 실패 캐시, resident non-animation class와 BeginPlay 이전 component 파괴도 검사한다. [구조 개선](../../../../Docs/Review/Audits/Project_J_Structural_Improvement_2026-10-13.md)에서 리뷰 단위와 시스템별 유지 이유를 읽는다.
