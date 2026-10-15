# Strafe 대각 점프·착지의 보정 분기 복구 — R20

사용자가 실제 Strafe 대각 점프·이동 착지가 전방 동작으로 나오는 문제를 보고했다. R19의 선택 존재 시험만으로 이 동작을 확인했다고 판단할 수 없었다. **선택된 전방 clip을 대각으로 보정하는 sample graph가 처음부터 실행되지 못하는 순환 의존성을 재현했다.** R19의 Stand 행 보완과는 별개의 native 결함이며 Jump/Land_Run/Land_Sprint 에셋은 R19에서도 변경되지 않았다.

검증 기준 HEAD는 `e5e534e81ad909145fc34130f2c2d420b244049a`다. 검증 당시에는 추가 커밋을 하지 않았고, 이후 사용자의 최신 요청으로 R19·R20을 함께 커밋한다. push·병합은 하지 않는다. 사용자 에디터와 Live Coding 종료를 실제 확인한 뒤 directUBT와 독립 검증을 순서대로 실행했다. 읽기 전용 담당은 source/engine/graph 근거만 조사했고 코드·에디터·시험은 주 작업자만 수행했다.

## 원인과 수정

기존 계약은 전용 대각 sequence가 없는 Jump/Land에서 F/B clip과 정확한 대각 조건을 선택한 뒤 Orientation Warping으로 보정하는 것이다. 따라서 결과 asset 이름이 F라는 사실만으로 잘못된 선택이라고 판정하지 않는다.

```text
Unified Locomotion의 현재 authored sample
  → 엔진이 sample execution helper에 현재 Player 설치
  → sample graph Bool selector
  → GetThreadSafePlaybackUsesAuthoredCorrections
  → true: Input_0 + authored Orientation Warping
  → false: Input_1 + MM 처리
```

종전 getter는 `Input_0.Player != null`을 selector의 선행 조건으로 요구했다. 그런데 UE5.8은 해당 Input의 첫 Update에서만 이 포인터를 연결한다. selector가 false이면 다른 Input만 갱신되므로 true 분기는 계속 꺼져 있었다. 실제 초기화 상태에서 방향 값은 대각이고 native OrientationGate는1인데도 OrientationWarping4개가 모두 `ActualAlpha=0, Asset=None, Time=0`으로 남았다.

수정은 **현재 sample graph 진입 전에 엔진이 이미 설치한 helper의 Player**를 읽도록 소유권 조회를 바꾼 것이다. `BlendStackAllocationIndex → 역순 property index → parent BlendStack → SampleGraphExecutionHelpers[SampleIndex].Player`는 설치 엔진의 실제 연결 방식과 같다. parent type·allocation/sample 범위·null·정확한 pose index/asset 검사를 유지한다. lazy Input 포인터를 강제로 채우거나 다른 MM sample까지 무조건 허용하지 않는다.

변경한 production 소스는 `Source/Project_JCharacter/Private/Animation/Project_JCharacterAnimInstance.SnapshotAccessors.cpp` 한 곳이다. 서버 상태·예측·snapshot 생성·Chooser 선택·제자리 회전/OTM 정책·curve·blend·clip 예외·timer/reset은 바꾸지 않았다. `git blame`에서 종전 getter는 `6d9abe1f`(2026-10-12 통합 재생 스택)부터 존재함을 확인했다. 이전 구조 대조가 이 초기화 순서를 놓쳤다는 사실도 남긴다.

## 재현·회귀 방법과 한계

- 실제 BP_GreatSword·Master ABP·GAS·CMC·충돌 바닥과 실제 graph/pose 평가를 사용한다. 기존 fixture의 수동 snapshot 선택 시험과 구분한다.
- 초기 bare `Player->Jump()` 시험은 실제 입력 binding의 semantic Jump 알림을 생략해 FallOff 경로를 탔다. 추가 WarpProbe 시험은 실제 binding과 같은 순서인 공개 `Locomotion.HandleJumpStarted → Player.Jump`를 사용한다. 물리 OS/EnhancedInput 키 이벤트 전수 시험이라고 부르지 않는다.
- 현재 선택한 clip과 일치하는 OW node만 보정 프레임으로 센다. 비활성 clone이 가진 이전 Start의 debug 입력을 현재 Jump/Land 성과로 세지 않는다.
- optional WarpProbe에서 public alpha·angle·current asset/time·실제 `enable_warping`·root motion과 엔진 공개 GatherDebugData를 기록한다. private/protected 엔진 필드를 강제로 읽지 않는다. 원본 곡선0 구간은 보정이 꺼지는 기존 계약을 유지한다.
- 동일한 cold 초기화 상태의 수정 전 시험은 Jump/Land 보정 프레임0과 assertion2실패를 기록했다. helper 수정 뒤 같은 시험에서 활성 보정을 확인했다.
- RenderOffscreen 시험의 실제 Jump 시작은 `FallOff=false`, 대각 sector/angle을 유지했고 평가 보정각 약−44.6도를 확인했다. Jump·Land 캡처를 육안 확인했다. 화면2장만으로 모든 clip/시점·품질을 보증하지 않는다.

최종 검증 집계와 소스/hash는 [R20 요약](PCStructureEvidence_2026-10-15/StrafeWarpRepair/Summary.json), 원자료 위치와 해시는 [원자료 색인](PCStructureEvidence_2026-10-15/StrafeWarpRepair/ReceiptIndex.json)에 기록했다. R19의9개 성공이나 이전 Cook을 R20 native 수정의 재실행으로 합산하지 않는다.

| 최종 확인 범위 | 결과와 한계 |
| --- | --- |
| 호환 회귀17개 | 통합 graph, 실제 OTM/Strafe Run/Sprint Start/Land8개 시나리오, Start/Stop16방향 시나리오, 착지 문맥·단발/MM 복귀·remote runtime·TIP/전투 Stop·Strafe/Pivot 통과. 카운터 엄격화/명시 include 이전 실행이며 production 수정은 동일하다. |
| 별도 cold 제자리 회전1개 | 실제 BP/ABP/CMC, 이동0·montage0·TIP169프레임으로 진입/하반신 보호 통과. 연속 카메라 회전 시험으로 최종 TIP→Idle의 시각 품질까지 보증하지 않는다. |
| 최종 대각6회 | 동일 AuthoredCurvePlayback 시험의 Sprint FL/FR와 Run FL/FR/BL/BR. 각 Jump 보정62프레임, Land는 Sprint18·Run23프레임. 현재 선택한 clip과 정확히 일치하는 node만 집계. 고유 테스트24개라고 합산하지 않는다. |
| 실제 렌더 | 최종 시나리오16은 D3D12 SM6 RenderOffscreen, Jump/Land 화면 확인. 나머지5회 NullRHI도 실제 BP/CMC/graph/pose 평가이며 성능 측정과 구분한다. |
| Game·Editor | 최종 Game3작업/22.86초, Editor4작업/15.62초 directUBT 성공. Game 실행·새 패키지 검증은 아니다. |

Game 첫 실행에서 `-NoSharedPCH`를 추가해 기존 증분 상태를 무효화한 것은 불필요한 빌드 비용이었다. 334작업이 계획된 빌드는 중단 없이 정상 종료까지 기다렸다. 여기서 드러난 기존 Mass 시험의 불완전 UPackage 타입 참조는 `UObject/Package.h` include 한 줄로 해결했다. 이후 Game3작업·Editor4작업으로 성공했다. 다음 Game 빌드는 현재 옵션과 증분 상태를 고려해야 한다. UBA 이전 세션 정리 접근 경고는 보존하고 ACL/캐시를 우회하지 않았다.

R20은 에셋·직렬화·반영 시그니처 변경이 없어 추가 Cook을 실행하지 않았다. R19 Cook은 기존 데이터 검증 근거로만 유지한다. startup Condition failed 및 GameplayCue 경고 원로그와 최종 structured 성공을 구분한다. 모든 프로세스는 정상 종료했고 추가 커밋은 하지 않았다.

## 보존과 별도 후보

OTM/Strafe·Run/Sprint·단발/MM 복귀·Land·측면 후보·전투·장비·리타깃 및 지속 곡선0.35초·100cm·3도·85%를 보존한다. R19 최종본 대비 관련 에셋51개 해시가 모두 같고 snapshot·presentation·공통 locomotion node·profile·Master ABP의 보호 해시가 R18과 같다. 이번 R20에서 콘텐츠 에셋을 추가 수정하거나 테스트 자산을 삭제하지 않았다. 원자료는 ignored Saved에 보존한다.

조사 중 공중 방향 재선택의 pending 게시 순서 후보도 찾았으나, 이번 재현은 대각 sector/angle이 이미 맞는데 후단 분기가 꺼진 문제였다. 별도 후보를 이 수정에 섞어 바꾸지 않는다. 현재 재현에서 그 후보의 증상 영향은 확정하지 않았다. CPU/GPU·부하/성능 조사는 계속 보류한다.

## 최신 범위 마감과 커밋 검토

사용자는 잔여 세 항목인 공중 방향 재선택 pending 순서 후보, Traversal·Mirror 실제 설치/소비 경계, preview 설정·시험 CSV 의미 대조를 직접 담당하기로 했다. 이들은 해결·검증 완료로 바꾸지 않고 이번 작업 범위에서 제외한다. 성능 조사는 계속 보류한다. 이 제외 조건에서 이번 소스·구조 정리 작업을 마감하며 모든 함수·에셋의 무결성 보증으로 확대하지 않는다.

R19·R20의 source5개·Stand Chooser1개·관련 기준/검토 문서·요약/해시 색인만 커밋한다. 회귀 소스와 기존 테스트/제작용 자산은 보존한다. 일회성 제작 코드·실행 스크립트·MCP 세션·빌드/Cook/test 로그·캡처·바이너리·캐시는 ignored Saved/Binaries/Intermediate에 남기며 커밋하지 않는다. 사용자 인계 파일 변경도 제외한다. 기존 검증 뒤 runtime 소스를 추가 변경하지 않았으므로 해시와 최종 결과를 대조하고 같은 빌드·시험을 반복하지 않는다.
