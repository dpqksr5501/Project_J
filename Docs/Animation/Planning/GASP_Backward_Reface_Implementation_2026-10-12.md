# GASP 후진 Reface 조사 및 Project_J 구현

> **이전 이동 구조의 기록 — 2026-10-21:** 본문은 당시 CMC/MM·마스터·후보 정책의 구현·제안·측정 이력이다. 현재 생산 제작은 [플레이어 로코모션](../Architecture/Player_Locomotion_Architecture.md)을 따른다. 과거 구조를 복원하라는 지시나 현재 구조의 성능 결과로 해석하지 않는다.

작성: 2026-10-12. 기준 브랜치 `codex/strafe-backward-turn-trial`, 시작 HEAD `a987c44e`.

이번 대화에서 사용자는 조사, 컴퓨터 use, Unreal MCP, 로그 수집, 빌드·재빌드, 코드·에셋 수정·저장, 문서화와 구현을 허용했다. 기존 [인수인계](GASP_Naturalness_Handoff_2026-10-12.md)의 조사 전용 권한 설명은 작성 당시 기록이며 이번 실행 범위를 제한하지 않는다. 이후 에디터에서 직접 테스트하고 "잘된다, 좋다"고 확인한 뒤, 불필요한 코드 정리와 커밋·push를 요청했다. 기존 미추적 인수인계와 관련 없는 사용자 변경을 보존한다.

## 결과와 원인

현재 Combat Strafe Run은 후진 Reface 원본 시퀀스를 가지고 있었으나, 운영 Cycle/Turn/GeneralTurn PSD의 활성 엔트리에는 포함하지 않았다. `ShouldSpinTransitionForContext` 값이 존재해도 지속 MM 후보 공급으로 이어지지 않았다. S를 유지하면서 카메라만 빠르게 돌리면 의미 입력 revision은 그대로인 반면 world 이동 의도와 Facing은 바뀐다. 이 상황에서 기존 후진 Cycle만 검색해 몸 전체의 방향 전환 포즈가 부족한 것이 확인한 차이다.

원본 `M_Neutral_Run_Reface_Start_B_L_180`, `B_R_180` 두 개를 기존 방향별 Cycle과 경쟁시키는 전용 구간을 구현했다. 특정 클립을 강제하거나 재생 시간을 지정하지 않는다. 실제 CMC + 운영 `BP_GreatSword` + Master + 표시 follower의 120Hz 재생에서 좌·우 900°/s, 180° S 급회전 모두 Reface가 선택되고 기존 후진 Cycle로 복귀했다.

**확인한 개선은 후보 공급과 몸 방향 전환이며, 대표 장면은 사용자의 실제 에디터 테스트에서도 긍정적으로 확인됐다. 발 미끄러짐 해결이나 모든 캐릭터의 체감 승인을 뜻하지 않는다.** 아래 실제 렌더와 발 추정치가 그 판단 범위를 보여 준다.

## 실제 경로와 비교

| 항목 | GASP 원본 | Project_J 변경 전 / 이번 구현 |
| --- | --- | --- |
| 엔진 | 5.7.4, 원본 프로젝트 읽기 전용 | 5.8.3, 설치된 엔진 직접 UBT 빌드 |
| Pawn | `/Game/Blueprints/SandboxCharacter_CMC` | `/Game/Character_BPs/GreatSword/BP_GreatSword` |
| ABP | `/Game/Blueprints/SandboxCharacter_CMC_ABP` | `/Game/Animation_Logic/ABPs/ABP_Humanoid_Master` |
| Spin/후진 후보 | `PSD_Dense_Stand_Run_SpinTransition`: F/B × L/R × 090/180, 총 8개 | 이전 Run Cycle 70개, Loop 8개, GeneralTurn 12개, Turn 4개에 Reface 없음 → 원본 B_180 두 개 추가 |
| 검색 특징 | `PSS_Default`, `PSN_Dense_All`, Loops 20개와 공통 정규화 | 기존 `PSS_Combat_Run_Continuity` 유지. 전용 Cycle 복사본 70개와 Reface 2개만 별도 정규화 |
| 연결 방식 | Epic은 Spin을 이동 중 몸 방향 변경으로 설명하며 refacing starts 사용을 명시 | 기존 단발 Start로 보내지 않고 Cycle 지속 MM에서 경쟁 |
| 원본 시퀀스 | 2.4초, 후반은 직선 후진 꼬리 | 원본 수정 없음. Reface entry의 sampling range 0–1.1초, 원본만 검색, 같은 asset 재선택 차단 |
| 루트·이동 소유권 | 캡슐 기반 이동 + OffsetRoot/Steering | 기존 CMC와 primary 루트/플레이어별 Steering 유지. root-motion 이동, 무제한 루트 누적 추가 없음 |

GASP Spin PSD 전체 경로는 `/Game/Characters/UEFN_Mannequin/Animations/MotionMatchingData/Databases/Dense/PSD_Dense_Stand_Run_SpinTransition`이다. Chooser는 같은 MotionMatchingData 아래 `CHT_PoseSearchDatabases_Dense`이다. 같은 basename을 가진 Project_J Turn PSD들을 혼동하지 않았다.

원본 5.7의 CDO는 `UseExperimentalStateMachine=False`, `MMDatabaseLOD=0`, `LocomotionSetup=0`, `RootMotionMode=RootMotionFromMontagesOnly`다. 과거 snake_case 조회의 `None`은 실제 false나 기능 부재의 근거가 아니었으며, 이번에는 원래 property 이름으로 값을 확인했다.

원본 엔진의 UObject 텍스트 exporter로 `ShouldSpinTransition`의 실제 연결을 확인했다. 반환값은 **`abs(NormalizedDeltaRotator(CharacterTransform.Rotation, RootTransform.Rotation).Yaw) >= 130` AND `Speed2D >= 150` AND `!CurrentDatabaseTags.Contains("Pivots")`**다. 따라서 로컬 키 revision이나 짧은 카메라 입력 시간 자체를 검사하는 조건이 아니다. `Get_OffsetRootRotationMode`는 `IsSlotActive("DefaultSlot")`일 때 Release, 그 밖에는 Accumulate를 반환한다. Project_J에서는 기존 bounded visual root/Steering을 보호하고, Control–visual 및 실제 velocity–요청 heading 오차를 사용하므로 GASP의 130°를 그대로 복제하지 않았다.

같은 exporter로 Dense Chooser의 `Stand Runs` 실제 행을 확인했다. index 1은 Loops, index 2는 Pivots, index 3은 SpinTransition이며 모두 enabled다. Spin 행은 `IsPivoting=MatchFalse`, `ShouldSpinTransition=MatchTrue`이고 Loop 행은 이 두 열에서 MatchAny다. 따라서 조건이 맞으면 Loop와 Spin이 함께 후보가 될 수 있다. `Update_MotionMatching`은 선택 배열을 `SetDatabasesToSearch`로 제공하며, post-selection은 실제 selected DB를 저장하고 `GetDatabaseTags`를 통해 `CurrentDatabaseTags`를 갱신한다. Spin/슬롯/판정의 정적 연결 확인과 실제 매 프레임 승자의 확인은 구분한다.

`Update_MotionMatching`가 평가하는 실제 루트 표는 `CHT_PoseSearchDatabases`이고 평가 모드는 **AllResults**다. 루트 표의 MMDatabaseLOD=0 enabled 행은 Dense로, 1은 Sparse, 2는 ExtremeSparse로 연결된다. fallback도 Dense다. 따라서 이번에 읽은 CDO LOD 0과 Dense의 Loop+Spin 후보 경로는 실제 저장 연결로 확인했다.

원본 AnimGraph의 `AnimGraphNode_MotionMatching_0`에는 자체 `AnimationBlendStackGraph_0`과 ResetRoot/Steering 노드가 있다. 저장된 MM blend time은 0.5초, play-rate 범위는 0.85–1.15이며 `Update_MotionMatching` 함수가 연결돼 있다. OffsetRoot의 저장된 회전 모드도 Accumulate이며 `Get_OffsetRootRotationMode` property binding이 있다. 이 값들은 원본의 정적 저장/바인딩 근거이고 모든 runtime 프레임의 effective 값으로 보증하지 않는다. Project_J에도 이미 플레이어별 ResetRoot/Steering과 내부 스택이 있으므로 그 기능을 새로 없던 것처럼 추가하지 않았다.

원본 샘플링에서는 B_L_180의 루트 Facing이 +180°에서 약 0.6초에 0°로, B_R_180은 -180°에서 약 0.9초에 0°로 돌아왔다. 이름의 L/R과 실제 카메라 회전 방향을 동일하다고 강제하지 않았다. 실제 +900°/s 검색은 B_R_180, -900°/s 검색은 B_L_180을 선택했다.

[Epic 공식 GASP 문서](https://dev.epicgames.com/documentation/en-us/unreal-engine/game-animation-sample-project-in-unreal-engine)는 capsule-driven 이동과 refacing starts의 Spin 용도를 설명한다. 프로젝트별 검색 비용·속도·소유권이 다르므로 PSS, BlendTime, 스택 설정을 일괄 복사하지 않았다.

## 런타임 구조

`FProject_JBackwardRefacePolicy`는 게임 스레드의 완료된 primary 시각 피드백과 실제 CMC 이동을 읽는다. 다음 자격을 모두 충족해야 한다.

- 로컬 primary 플레이어, Combat Strafe, Run, OnFoot, grounded, 지속 Cycle.
- 기존 Steering 소유권과 fresh trajectory/evaluated visual sample 통과. 원격, 군중, montage, root-motion action, 공중, 단발 override는 제외.
- 요청한 world 이동이 Control Facing의 후방 ±30° 안에 있고 속도 ≥150cm/s.
- 시각 Facing 오차 ≥100° 또는 실제 velocity와 요청 이동의 heading 오차 ≥120°.
- 해당 AssetSet에 같은 Schema와 공유 정규화를 가진 전용 Cycle/Reface 두 PSD가 있어야 함. 부분 설정으로는 열리지 않음.

후진 입력 revision 변경이나 특정 마우스 픽셀 수를 요구하지 않는다. 짧은 active 구간은 최대 1.2초다. 최소 0.25초 후 Facing/경로 오차 <35°, 카메라 변화율 <90°/s가 0.12초 유지되면 닫는다. Reface가 실제 선택됐을 때만 최대 0.35초 continuing grace를 준다. Cycle이 새 검색에서 이기거나 새 타깃이 25° 이상 바뀌면 grace를 끝낸다. 방향 반전·자격 상실·stale sample·timeout은 취소한다. 같은 잔여 오차로 반복 진입하지 않고 정상 Run 0.15초 후 재무장한다.

기존 Combat Run 정규화에 Reface를 넣으면 A/D/W 비용도 달라진다. 따라서 active/grace 동안에만 `PSD_Combat_Run_RefaceCycle`을 primary로 사용하고 active 동안 `PSD_Combat_Run_BackwardReface`를 companion으로 제공한다. grace에는 Cycle만 검색하면서 이미 선택된 Reface continuing pose의 자연스러운 완료를 허용한다. 전용 정규화 진입·이탈 시 기존 비용 기준의 continuing result를 한 번 invalidate한다. active→grace 자체는 비용 기준이 같으므로 계속 유지한다.

`PreSave`는 전용 family의 cook 호환성을 인증한다. cooked build에는 editor-only NormalizationSet을 직접 읽지 않는다. 전용 PSD 두 개의 인덱스 메모리는 추가되지만 원격·군중에 새 검색을 열지 않는다.

## 저장한 에셋과 보호 범위

| 변경 | 경로 |
| --- | --- |
| 새 Reface PSD | `/Game/Animation_Logic/PSD/PSD_Player_Combat_Locomotion/PSD_Combat_Run_BackwardReface` |
| 새 scoped Cycle PSD | 같은 폴더 `PSD_Combat_Run_RefaceCycle` |
| 새 정규화 | 같은 폴더 `NS_Combat_Run_BackwardReface` — 두 PSD만 포함 |
| 운영 연결 | `/Game/DataAssetSets/Animation_Profiles/Combat_MMProfile/DA_Player_Combat_Strafe`, Run family의 새 두 슬롯 |

기존 Cycle 70개 엔트리의 sampling/mirror/reselection 등 전체 설정을 native reflection으로 비교했고 복사본이 같았다. 기존 `NS_Combat_Run_Continuity`의 멤버는 Cycle/GeneralTurn/Turn 세 개 그대로다. 원본 Reface 시퀀스, 기존 PSD/PSS/Chooser/Master 그래프, 레벨은 수정·저장하지 않았다. Sprint는 전방 F/FL/FR 계약을 유지하고 새 Reface 슬롯은 비어 있다. 폐기한 역순 Turn 실험은 재사용하지 않았다.

이번 생성 작업에서는 원본 AssetSet을 백업하고 source compression과 완성된 공통 정규화 인덱스를 기다린 뒤 지정한 네 package만 저장했다. 생성이 끝난 뒤 커밋 전 정리에서 일회성 apply 플래그, package 생성·복사·백업·저장 및 인덱싱 CVar 변경 코드를 제거했다. `ProjectJ.BackwardReface.ProductionData`에는 저장된 운영 데이터의 읽기 전용 감사만 남겼다. 생성 당시 코드와 로그는 로컬 증거 폴더에 보존했다.

## 동일 조건 실제 재생

입력과 카메라를 매 프레임 동일하게 공급하고 기존 CMC를 실제 tick했다. 테스트용 transient 바닥과 light/capture는 파일로 저장하지 않았다. 120Hz, 각 420프레임이며 180프레임부터 회전한다. CVar on/off의 속도와 actor yaw는 13개 케이스 모두 출력 정밀도에서 차이 0이었다.

| 장면 | 결과 |
| --- | --- |
| S +900°/s, 180° | active 55프레임, Reface 선택 70프레임. 첫 검색 B_R_180 약 0.3초 구간, 이후 Loop_B 복귀 |
| S -900°/s, 180° | active 54프레임, Reface 선택 96프레임. B_L_180 약 0.2초 구간, 이후 Loop_B 복귀 |
| S ±450°/s, 180° | 새 후보 없음. 기존 PSD/clip 선택 전후 동일 |
| S ±60°/s, 90° | 새 후보 없음. 기존 PSD/clip 선택 전후 동일 |
| A/D/W + 급회전 | 새 후진 후보 없음. 기존 PSD/clip 선택 전후 동일 |
| 입력 해제 / dodge / 실제 장착 montage / Combat 종료 | 소유권 상실 이후 active/grace 즉시 제외. 늦은 구간(330프레임 이후) Reface 잔류 없음 |

평균 Control–Visual Facing 오차(180–300프레임)는 우회전 52.84°→39.75°, 좌회전 52.83°→39.66°였다. 최대 한 프레임 visual yaw 변화는 기존 3°에서 우 4.30°, 좌 5.75°였다. 애니메이션의 빠른 방향 회복으로 해석하며, 순간이동이 전혀 없다는 보증으로 해석하지 않는다.

![우회전 on/off 비교](GASP_Backward_Reface_2026-10-12_Evidence/Comparison_Right.png)

![좌회전 on/off 비교](GASP_Backward_Reface_2026-10-12_Evidence/Comparison_Left.png)

각 쌍은 왼쪽 OFF, 오른쪽 ON이다. 전체 GIF는 `Saved/Validation/BackwardReface_20261012/Comparison_Case0.gif`, `Comparison_Case1.gif`에 있다. GIF는 원본 120Hz의 매 2프레임을 캡처하고 20ms 지연으로 재생하므로 약 0.83배속이다. 카메라는 액터 중심을 따라가므로 캡슐 world 이동을 화면만으로 판정하지 않는다.

양발 좌표는 `foot_l/foot_r`가 있는 **UEFN primary**에서 측정했다. 실제 표시 follower는 렌더에서 확인했으며, follower의 다른 bone 이름에 대한 수치 계측으로 오인하지 않는다. root component-space translation은 전후 0이었다. foot bone z≤13cm를 접지의 임시 proxy로 둔 XY 속도 평균은 우 231.74→283.66cm/s, 좌 252.79→264.07cm/s였다. 접촉 notify나 실제 ground-contact 판정이 아니며, 접지 샘플 수와 포즈도 달라 직접 품질 점수로 사용할 수 없다. **이 결과는 foot sliding 감소를 증명하지 않는다.** 발 잠금·striding·리타깃 접지 품질은 별도 후속 과제로 남긴다.

프레임·선택 변화와 측정 원본은 [ComparisonMetrics.json](GASP_Backward_Reface_2026-10-12_Evidence/ComparisonMetrics.json)에 있다.

원본 CDO와 관련 그래프 연결 요약은 [GASP57Defaults.json](GASP_Backward_Reface_2026-10-12_Evidence/GASP57Defaults.json), [GASP57GraphSummary.json](GASP_Backward_Reface_2026-10-12_Evidence/GASP57GraphSummary.json)에 있다. pin GUID/LinkedTo를 보존해 미연결 노드와 실제 반환값 경로를 구분할 수 있다. 원본 T3D와 Chooser는 아래 로컬 증거 폴더에 있다.

## 빌드·검증과 증거

모든 C++ 빌드는 설치 엔진의 직접 `UnrealBuildTool.exe`로 실행하고 종료까지 기다렸다. 각 실행 전에 실제 Windows 프로세스를 확인했으며 빌드를 중단하거나 에디터/ShaderCompileWorker와 중복 빌드하지 않았다.

| 최종 검사 | 결과 |
| --- | --- |
| Editor Development 직접 UBT (`Build05.txt`) | 성공 |
| Game Development 직접 UBT (`GameBuild.txt`) | 성공 — editor-only NormalizationSet이 없는 코드 경계 컴파일. 전체 패키징/cook 실행을 뜻하지 않음 |
| 생성·저장 절차 재검증 (`InstallFinal`) | 1개 성공, 오류·경고 0 |
| 새 정책·후보·운영 데이터·실제 RHI 재생 (`FinalOn`) | 4개 성공, 오류·경고 0. 정책 수명은 30/60/120Hz, 실제 RHI fixture는 120Hz |
| 동일 RHI fixture OFF (`FinalOff`) | 1개 성공, 오류·경고 0 |
| 기존 회귀 sweep (`Regression`) | 57개 성공, 오류·경고 0. Strafe CMC, OneShot Start/Land/입력·곡선/몽타주 복귀, TIP/Pivot, Steering, 전방 Turn, 표시 보정, MM 군중 정책 포함 |
| 실제 Sprint 플래그를 켠 추가 검사 (`SprintActual`) | 2개 성공, 오류·경고 0. sweep의 플래그 없는 Sprint pass를 실제 재생 증거로 세지 않고 별도 실행 |
| 소스/문서 diff 공백 검사 | 통과 |
| 커밋 전 정리 후 Editor 직접 UBT | 성공 (`PreCommitCleanup/EditorBuild.txt`). 운영 런타임 조건·에셋은 유지하고 editor 테스트·진단만 정리 |
| 커밋 전 정리 후 실제 RHI/데이터·정규화·소유권·군중 보호 재검증 | 10개 성공, 오류·경고 0 (`PreCommitCleanup/Tests/index.json`) |
| 사용자 실제 에디터 테스트 | 대표 후진 급회전이 잘 동작한다고 확인 |

조사·실행 전체 원본은 `Saved/Validation/BackwardReface_20261012/`에 보존했다. 이 폴더는 ignored 로컬 자료이며 clone에 포함되지 않는다. 재현 핵심 자료와 화면은 위 Docs Evidence에도 보존했다.

| 자료 | 의미 |
| --- | --- |
| `JAudit.json`, `GASPAudit.json` | 각 엔진에서 읽은 Pawn/ABP, PSD 엔트리, root metadata/샘플, 관련 Chooser |
| `ProductionData.txt`, `InstallFinal/Tests/index.json` | 운영 연결, 기존 normalization 3개, exact Cycle entry 복사, cook 호환, 오류 없는 생성·저장 |
| `FinalOn/Playback.txt`, `FinalOff/Playback.txt` | 13개 실제 CMC 케이스의 frame-aligned selection/visual/foot 증거 |
| `FinalOn/Frames`, `FinalOff/Frames` | 실제 RHI 비교 렌더, 방향별 88프레임 |
| `Build05.txt` | 최종 Editor 코드 빌드 |
| `GameBuild.txt`, `SprintActual/Tests/index.json` | Game 타깃 및 실제 Sprint 재생 |
| `Regression/Tests/index.json`, `Regression.log` | 기존 회귀 검사 |
| `GASP57Defaults.json`, `GASP57_*.t3d`, `GASP57GraphSummary.json` | 원본 5.7 CDO, Spin/Pivot/OffsetRoot/MM 그래프 연결 텍스트 |
| `AssetBackups` | 원본 AssetSet 및 첫 생성 데이터 보존본 |

첫 authoring 실행에서는 복사 중 비동기 인덱싱과 압축 준비 순서로 ensure가 발생했다. 이를 수정한 뒤 원본 백업에서 생성·저장을 다시 검증했고 `InstallFinal`은 성공했다. 과거 실패 로그를 지우지 않았다. 초기 링크 실패도 첫 오류를 확인해 수정했고 build 로그를 보존했다.

GASP 5.7 에셋을 5.8 ToolsetLibrary로 읽으려던 graph DSL은 비어 있었으며 정상 추출 근거로 사용하지 않는다. 5.8 종료와 5.7 startup ABP 편집기 열기에서 AnimationBlueprintEditor의 `EXCEPTION_ACCESS_VIOLATION 0x58`이 발생했고, 첫 오류와 Application Event Log를 확인했다. 당시 Windows 기록에는 추가 오류 항목이 없었다. 원본 파일을 저장하지 않았고 경로 변경·삭제로 우회하지 않았다. 컴퓨터 use의 해당 앱 승인 대기도 응답 없이 만료되어 UI 재확인은 중단했다. 이후 원본 5.7 commandlet에서 이미 확인한 graph 경로의 UObject exporter로 관련 그래프, Chooser 및 AnimGraph를 정상 추출했다(`GASP57ExportComplete.log`, exit 0). protected `FunctionGraphs`/`Nodes` 배열을 Python 직접 조회한 실패도 남겼으며, 지원되는 exporter를 사용했다.

Python remote execution 설정 변경은 자동 승인 검토가 거절했으므로 변경하지 않았다. 조사에는 기존 MCP 읽기와 지원되는 startup Python/commandlet 경로를 사용했다. GASP와 Project_J를 같은 속도·카메라 조건으로 새 PIE 재생한 trace는 확보하지 못했다. GASP 부분은 원본 그래프/에셋/CDO와 기존 사용자 화면, 이번 frame-aligned on/off 재생은 Project_J끼리의 비교임을 구분한다.

## 사용·진단·후속 범위

기본 활성화되어 있으며 profile의 `bEnableBackwardRefaceCandidates` 또는 아래 CVar로 끌 수 있다.

```text
p.ProjectJ.BackwardRefaceCandidates 0
p.ProjectJ.BackwardRefaceCandidates 1
p.ProjectJ.LocomotionContinuityTrace 2
```

trace에는 `Stage=BackwardReface`, 판정 reason, Facing/Path error, active/grace가 추가된다. 기본 trace 0은 로그 비용을 내지 않는다. 전체 Turn/Cycle 후보 스위치를 끄면 이 기능도 꺼진다.

테스트는 `ProjectJ.BackwardReface.Lifetime`, `CandidatesAndInterrupt`, `ProductionData`, `ProjectJ.Animation.Naturalness.BackwardRefaceCMC`다. 실제 렌더는 `-ProjectJRefaceRender`, 연속 프레임은 `-ProjectJRefaceRenderSequence`, 출력 위치는 `-ProjectJRefacePlaybackOutput=<절대 경로>`다. 기본 테스트는 에셋을 저장하지 않는다.

구현 위치는 `Source/Project_JCharacter/Public/Animation/Project_JBackwardRefacePolicy.h`, `Project_JMotionMatchingAssetSet.h/.cpp`, `Project_JCharacterAnimInstance.Continuity.cpp`, `Project_JCharacterAnimInstanceProxy.cpp`다. 실제 재생 fixture는 `Source/Project_J/Testing/Project_JBackwardRefacePlayback.cpp`, 읽기 전용 운영 감사는 `Source/Project_JCharacterEditor/Private/Tests/Project_JBackwardRefaceDataTests.cpp`다. 다른 캐릭터에도 공유 Master 코드는 적용되지만 compatible pair 설정과 해당 retarget/skeleton 품질 확인이 필요하다.

이번 구현은 후진 180°의 명확한 물리/시각 오차 구간을 다룬다. 450°/s에서 기존 Cycle을 유지하는 결과를 특정 Reface를 항상 출력해야 하는 실패로 취급하지 않는다. B_090 확대, 모든 속도·스켈레톤의 사용자 체감, 발 접지 개선은 이번 결과로 보증하지 않는다. Start/Land/TIP와 액션은 기존 소유권을 유지한다.

## 커밋 전 정리

운영 진입/완료/중단 조건과 에셋은 사용자가 확인한 상태를 유지했다. 일회성 생성 코드를 제거하고 데이터 감사의 null/entry 수 검사와 두 PSD의 정규화 멤버십 검사를 유지했다. 렌더 fixture는 좌·우 핵심 두 장면에만 capture/조명/발 계측을 만들며, 커맨드라인 렌더 옵션도 매 프레임 다시 파싱하지 않는다. 기본 재생 로그는 `Saved/Validation/BackwardReface/Playback.txt`에 쓴다.

문서 증거에서는 실패한 property 별칭 조회와 관련 없는 그래프·미연결 노드, 중복 export 상태를 제거했다. 전체 원본은 기존 ignored 증거 폴더에 보존했다. 정리 후 빌드와 재검증 결과는 `Saved/Validation/BackwardReface_20261012/PreCommitCleanup/`에 남긴다.

정리 후 네 운영 에셋의 SHA-256이 사용자 테스트 전 저장본과 같은 것도 확인했다. 원격에는 조사용 Python/MCP 스크립트, 종료 helper, 전체 로그·프레임·백업을 포함하지 않는다. 코드와 운영 에셋, 유지할 회귀 검사, 설명과 비교 화면만 커밋 대상으로 삼았다.
