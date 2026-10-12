# Strafe 실제 입력 회귀 데이터

`StrafeMicroCamera_20261012.csv`는 2026-10-12 21:26~21:27 사용자 재현 로그의 `Stage=Facing` 중 Frame 3200~4007에서 가져온 808개 제어 카메라 yaw 표본이다. 첫 표본의 시간과 yaw를 빼서 `경과 초,yaw 도`로 저장했다. 기간은 6.736초다. 초기 큰 회전 구간은 포함하지 않는다.

`ProjectJ.Animation.Naturalness.RecordedStrafeMicro`가 실제 캐릭터 BP, CMC, 충돌 바닥과 Master ABP로 재생한다. 파일 부재·잘못된 표본·Box/Diamond 선택·Loop 유지 부족은 실패한다. 기본은 120Hz이며 `-ProjectJStrafePlaybackHz=60` 또는 `30`으로 동일한 시간 축을 재표본화할 수 있다.

카메라 입력을 재현하는 데이터이며 원래 월드의 지형, 전체 포즈 이력, 프레임 부하와 렌더 결과를 복제하지 않는다. 따라서 자동화 성공과 실제 에디터 체감 확인은 구분한다.

`-ProjectJStrafeReplayPrecurve`는 같은 입력 전에 90도/초 곡선을 2초 재생한다. `-ProjectJStrafeReplayWarmup`을 함께 주면 그 곡선 전에도 2초 동안 Loop를 재생한다. 실제 Arc 선택을 확인한 뒤 미세 입력의 첫 0.5초는 전환 구간으로 기록하고, 나머지 구간의 Box/Diamond 선택과 Loop 유지를 검사한다. 기본 데이터 재생에는 이 관찰 지연을 적용하지 않는다.

`StrafeCurveCamera_20261012.csv`는 같은 날 23:09 사용자 Arc→Box 재현 로그의 `Stage=Facing` Frame 20268~22900에서 추출한 2,633개 카메라 yaw 표본이다. 기간은 21.95초이며 여러 방향 반전과 회전 속도 변화도 포함한다. 월드 yaw의 ±180도 경계를 풀고 첫 시간과 yaw를 빼서 저장했다. 입력이나 방향 반전 구간을 삭제하지 않는다.

`ProjectJ.Animation.Naturalness.RecordedStrafeCurve`는 이 곡선 기록을 기본으로 재생한다. 실제 이동과 Arc 선택 비율, Box/Diamond 노출 비율을 검사하며, 미세 입력 검사의 Loop 유지 조건을 곡선에 적용하지 않는다. 기록 전체에서 Arc 85% 이상·Box/Diamond 1% 이하를 회귀 기준으로 사용한다. 이 수치는 해당 재현의 품질 기준이며 런타임에서 특정 클립을 선택하거나 차단하는 규칙이 아니다. 30/60/120Hz 재표본화와 Brute 검색·렌더 검증은 같은 파일을 사용한다.

단발 복귀 검사는 `-ProjectJOneShotPlaybackCase=N`으로 한 장면만 재생할 수 있다. 예를 들어 `ProjectJ.Animation.OneShotInput.AuthoredCurvePlayback`의 5번은 Strafe Run 착지 이후 지속 회전이다. `-ProjectJOneShotPlaybackHz=120`으로 재생 간격을 지정하고, `-ProjectJOneShotCurveOutput=`으로 공유 스택의 outgoing weight·단발 보정 방향과 실제 가슴 회전 변위를 기록한다. 가슴 변위는 컴포넌트 공간의 프레임 차이이며 체감이나 월드 발 미끄러짐 판정이 아니다. 장면 번호 옵션을 생략하면 기존 전체 장면을 실행한다.

전방 생산 에셋 검사 `ProjectJ.StrafeForward.ProductionData`와 `TrajectorySchema`는 읽기 전용이다. 작성용 Apply 옵션과 임시 Forward Loop bias·정규화·가중치 sweep 옵션은 커밋 정리 때 제거했다. `-ProjectJForwardCostAudit`는 Editor/dev 자동화에서 명시적으로 요청한 경우에만 비용을 재구성하며, 원래 엔진 검색의 query/filter 캡처로 해석하지 않는다.
