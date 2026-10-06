# 05. 검증과 읽기 전용 에디터 확인

## 1. 권한과 실행 전 체크

이 문서는 앞으로 수행할 절차다. 이번 문서 작성에서 새 PIE/빌드/에셋 저장을 수행했다는 뜻이 아니다.

1. 프로젝트 AGENTS.md와 사용자 최신 권한을 읽는다. 분석-only면 에셋/Config/코드를 바꾸지 않는다.
2. Unreal MCP는 해당 대화의 명시 허가가 있어야 하며 관련 에셋만 조회한다. 에셋 전체검색/대량schema 전에 범위를 알린다.
3. 에디터·MCP가 GASP/Project_J 중 어느 프로젝트인지 .uproject/engine/asset path로 확인한다. stale server/다른 editor를 피한다.
4. 열린 운영Pawn/Mesh/AnimClass/profile/actual world override를 확인한다. ABP_Player를 Master로 대체하지 않는다.
5. 분석-only에서 Save/Compile/Build/PIE auto-compile 옵션 변경을 하지 않는다. PIE가 필요하면 사용자의 실행 권한과 현재 editor 상태를 먼저 확인한다.
6. 구현 작업에서 Unreal C++ 빌드는 직접 UnrealBuildTool.exe를 사용하고 완료까지 기다린다. UBT/dotnet/Editor/LiveCoding/MSBuild/ShaderCompileWorker가 실행 중이면 중단하거나 중복 빌드를 시작하지 않는다. dotnet exception dialog는 최초 compiler/UBT error와 Application Event Log부터 확인한다.
7. 자동화·실행·영상·성능의 조건과 결과를 별도로 기록한다. 기존 사용자 uncommitted 변경은 baseline이다.

## 2. P0: 공중 출력 추적

이 항목은 조사에서 발견한 특정 출력 불일치의 확인 우선순위다. 전체 자연스러움의 목표나 모든 후속 작업의 선행 조건을 뜻하지 않는다. 아래 N1-N6으로 전체 baseline을 먼저 확보하고, S1-S15로 입력 변화·큰 반전·예외 상황을 추가한다.

확인된 G/A/S는 FallLoop leaf, IsTransitionState에서 InAirLoop 제외, loop=true, override 비loop 조건, logicalSM pose empty, legacyChooser=None다. 실제 최종pose는 U.

재현: ground Run→Jump, standJump, edgeFallOff, 충분히 긴 fall. JumpStart/FallOff가 끝나고 InAirLoop 상태가 지속되는 구간을 관측한다.

읽을 항목:

- runtime PresentationState/held state/phase, hasSelected/loop/override.
- actual chooser leaf/result asset/output/starttime/revision.
- BoolBlend ActiveValue와 final branch weight.
- applied/requested/selected MM PSD, asset/time, continuing, node weight.
- output source leader pose와 final retargeted visual mesh pose.
- 착지 진입/취소의 epoch·groundintent와 복귀 DB.

판정: 'Chooser가 fallLoop를 선택'만으로 성공하지 않는다. 어느 활성 출력 owner가 낙하 pose를 만들었는지 연결·가중치·최종body로 확인한다. mismatch가 실제면 외부loop 또는 airMM 중 책임을 명시하고, Idle/Cycle의 모든loop를 override하도록 광범위 조건을 바꾸지 않는다.

## 3. 최소 기능 시나리오

### 전체 움직임 baseline: N1-N6

기존에 잘되는 동작과 부족한 동작을 같은 조건으로 남긴다. 관련 출력·데이터·보정이 확인되었다는 사실과 실제 화면에서 문제가 재현되었다는 사실을 구분한다. 아래 기준은 후속 실험의 비교 목표이며 이번 조사에서 플레이 통과를 확인한 결과가 아니다.

| ID | 입력/상황 | 기록 | 비교·합격 기준 |
| --- | --- | --- | --- |
| N1 | Idle→Start→이동→Stop→Idle, 짧은 탭·반복 입력, 양발 위상 | 상태/command/clip/time/weight, 접촉, 입력 지연 | 동일 사건 재시작·자세 튐·발 위상 단절을 억제하며 입력 반응 유지 |
| N2 | 직선 Walk/Run/Sprint 전환, 가속·감속, OTM/Strafe | CMC speed/accel, authored speed, play rate, 실제 PSD, foot drift | 속도 변화와 보폭 연결이 baseline보다 개선되고 전환 지연이 악화하지 않음 |
| N3 | 작은 15/30/45/60도 방향 변화와 연속 곡선 이동 | trajectory/선택 pose·time·Cycle 분포/clip 교체, 발 위상 | 이미 자연스러운 Cycle 선택과 연결을 보존; 각도별 Turn 강제 없음 |
| N4 | 이동 중 완만한 마우스 회전, 경로 유지/변경 각각 | path/facing/capsule/visual yaw, AO/root/feet | 몸 방향과 경로의 역할이 일치하고 회전 보정 중복·발 미끄러짐이 증가하지 않음 |
| N5 | 정지·이동 점프→낙하→Light/Heavy 착지→Idle/이동 | 상태/출력 owner/clip clock, grounded, ground intent | 활성 공중 pose와 착지 출력 확인; 중복 착지·stale 복귀 없이 최신 의도로 연결 |
| N6 | 정지·걷기·달리기의 접지, 평지/경사/계단, 상체 동작·리타깃 조합 | contact/plant, foot drift, pelvis, root, leader/visual 단계 | 발 접지와 자세가 유지되고 AO/slot/리타깃의 추가 오차를 분리 가능 |

### 입력 변화와 예외: S1-S15

| ID | 입력/상황 | 기록 | 합격 기준 |
| --- | --- | --- | --- |
| S1 | OTM Run W→S/S→W, 좌우 camera고정 | entry/eligible/speed/active/exit/PSD/time | 정상 reversal decel만으로 즉시 종료되지 않음; input release 취소 유지 |
| S2 | OTM Run W유지+mouse 좌우180 | velocity/input/actor/controller/futureFacing | body/path 동시반전 자격과 검색이 일치 |
| S3 | Strafe Run W유지+mouse 좌우180 | old/new forward 조건·target·TurnPSD | 전진→전진 경로와 body가 함께 바뀔 때만 forwardTurn |
| S4 | Strafe world path유지+몸만 큰 회전, sideways/backpedal | trajectory translation/facing, phase | forward180Turn을 강제하지 않고 기존Cycle 유지 |
| S5 | Strafe W↔S/A↔D, 양발 시작 | Pivot request/revision/asset/starttime/pulse | 기존Pivot 유지; 새로운 승인request는 oldredirect보다 우선 |
| S6 | 좌우170/180/190, 179→-179 | signed/unwrapped delta, candidate/time, rootyaw | 숫자경계로 불필요한 좌우재선택/몸튐 없음 |
| S7 | 50/150/300ms 연속반전, 같은instance | lifetime/sequence/target/rearm/cooldown | 최신입력 반영, stale hold/resurrection/매frame restart 없음 |
| S8 | Start 중mouse5/10/15/30·move15/30/90 | 유지/취소/asset clock/force pulse | 작은변화는 설정대로 유지, 취소는 최신MM복귀, 재시작 없음 |
| S9 | Light/Heavy stand/run/sprint Land 중WASD/mouse/mode변경 | epoch/latchedgait/override/groundintent | 같은착지 중복없음; moving취소는Cycle, release는Stop/Idle 의도 |
| S10 | Jump 중입력/마우스 변경, 길게fall | progress/blend/reselect/airloop output | 명시reselect는progress보존; 실제fallpose 출력 |
| S11 | Start/Land/Pivot 중OTM↔Strafe 반복, combat bool 지연 | samecommand asset/time/revision/clock | mode change만으로 동일사건 재생restart 없음 |
| S12 | action/montage/rootmotion/air/mount 진입 | correction owner/phase/interrupt | 이동Turn·steering이 상위owner 침범 안함 |
| S13 | wall/corner/slope/stair/uneven/moving platform | predicted vs actual path/foot/root | blockedpath 예측·ground contact 오류 분리 |
| S14 | mid/far/hidden 전환과relevance복귀 | pending/search/budget/follower/IK | hidden wake 없음, 복귀request는 actual search 뒤소비 |
| S15 | remote latency/jitter/late event/teleport | replicated event/yaw/smoothing/reset | camera 추정/중복command/staletrajectory 없음 |

170/190은 authored180 재사용의 예시이며 모든 상황에180을 강제하라는 뜻이 아니다. 키보드 반전과 마우스 body-only의 구분을 유지한다. 전체 품질 평가는 N1-N6과 S1-S15를 함께 사용하며 반전 성공만으로 완료하지 않는다.

## 4. 공통 진단 필드

한 frame에 최소 다음을 묶는다. 기존 diagnostic이 없으면 추가 구현 권한을 먼저 확인하고 기본off·target제한으로 설계한다.

| 영역 | 필드 |
| --- | --- |
| 시점 | worldtime/frame, snapshot generation/revision, worker update frame, graph traversal/relevance |
| 이동 | actor/camera/velocity/input yaw, speed, acceleration, physical ground/air, gait/mode |
| 예측 | trajectory age/generation, future position/velocity/facing(.35/.5/.7), past/current 기준transform |
| policy | phase/MovingTurn180/eligibility/guard/reason/armed/entrytarget/start/exit/cooldown |
| 선택 | actualAssetSet/PSD path/schema/candidate count, requestRevision/appliedDB/selectedDB |
| 검색 | Executed, searchtimer/throttle, continuing/force/invalidate, pending consumed/restored |
| 클립 | asset fullpath/actual time/playrate/mirror, startTime, perplayer weight/activeblends |
| 단발 | command revision/episode/epoch, held state/clock, chooser leaf/output, forceBlend pulse |
| 보정 | Steering/OW Alpha/currentAsset/time/TargetTime/targetFacing, root mode/offset/residual |
| 발 | contact/plant/unplant, world footposition, trace count, pelvis offset, Foot/Leg alpha |
| 최종 | selector branchweight, slot/upperbody/AO, leader/visual/follower pose timestamp |
| 예산 | significance/tier/visibility/interval/ABA/URO/relevance, follower/IK demand |

cached result 이름은 output 증거가 아니다. actual search가 실행되어도 같은clip 유지가 가능하다. alpha0 노드·Disable 행·unusedDefault를 실행 기능으로 계산하지 않는다.

## 5. 자연스러움의 측정

다음은 아직 측정하지 않은 권장 지표다. 허용 threshold는 baseline와 gameplay 요구로 사전에 정한다.

- 입력부터 경로/몸반응까지 latency, 목표 정렬 시간, overshoot, frame간 visual yaw 변화량.
- Turn 창의 실제 길이와 종료사유, 창내 검색횟수/clipjump횟수, newtarget 반응시간.
- 접지 구간 발의 world drift(cm), stance duration, unplant/replant 이벤트 수, pelvis jerk.
- clip/time restart와 동일command forceBlend 횟수, 초당select/reselect/DBswitch.
- authored speed 대비 actual speed·external1X/MMrate 차이, retarget limb길이별 drift.
- Leader와 최종VisualMesh의 시간차·방향차, slot/AO/root/foot 각 단계 비교.

카메라 follow/고정 view를 모두 사용한다. 저속재생만으로 입력반응을 평가하지 않으며 정상1X과 동일FPS도 남긴다. Frame rate30/60/120, 같은지형·같은입력·같은data에서 비교한다. 그래프의 연결만으로 영상효과를 입증하지 않는다.

## 6. read-only 그래프·설정 추출 절차

### MCP가 가능한 경우

관련asset 정확경로를 먼저 지정한다. get_graph/get_node_infos/get_connected_subgraph 또는 대응 read-only API로 pose/exec/data pin 연결을 읽는다. get_properties로 component/CDO/profile/Chooser ColumnsStructs/PSD/PSS를 읽는다. schema 재조회는 필요한도구만 수행한다. get_properties 실패는 unknown으로 남긴다. mutation tool은 호출하지 않는다.

graph text에는 root/output에서 역추적한 활성경로와 disconnected nodes를 구분한다. nested graphs/function binding body도 함께 읽는다. CDO의 node default와 proxy runtime override를 나누고 Chooser table의 result type·row condition·disable·fallback·output struct를 모두 확인한다.

### 직접조회가 불가능한 경우

관련asset만 에디터에서 연다. graph를 선택/복사해 clipboard text를 확보하거나 screenshot을 남기는 것은 읽기용이며 노드붙여넣기·graph edit·compile·save를 하지 않는다. 접속project/engine/assetfullname/graphname을 함께 기록한다. Details category copy로 읽은설정은 provenance를 표시한다. Chooser Disable은 메뉴체크를 보기만하고 클릭으로전환하지 않는다.

PSD/PSS는 schema, normalization, searchmode, PCA/neighbors, bias/ranges, entries/mirror/loop, feature weight/times/origin axes, notify 구간/DB를 읽는다. BranchIn null, block vs earlyTransition, override vs sum을 명시한다. 열람중 auto-DDC/index/log가 생성될 수 있으므로 이를 '원본asset저장'과 구분하되 자동compile를 유발하는조작은 피한다.

### 최소 에셋별 읽기 체크

| 자산 | 필수 확인 |
| --- | --- |
| BP_GreatSword/Mesh | AnimClass/profile/component/PP/defaultPawn와실제spawn |
| Master | output역추적, innerMM/external, statebody, binding, CDOlegacy None |
| Profile/AssetSet | singlePSD refpath, 같은basename의 다른asset, budget/cancel/runtimeoverride |
| root/leafChooser | resulttype, recursion, enum/range/bool/foot, rowDisable, fallback/StartTime/UseMM |
| PSD | schema/entries/searchmode/bias/range/normalization/searchable |
| PSS | sample30Hz/preprocess/features/time/weight/origin/strip/continuing |
| TurnSequence | root/length/rate/contact/BranchInDB/Exclude/Override/Block/EarlyTransition |
| Visual/retarget | leader/follower/PP/space/timestamp/additionalIK/LOD |

## 7. 비용 검증

1/50/100/200캐릭터를 동일하드웨어·map·camera·render설정·빌드에서 비교한다. local1+remote다수, near/mid/far/hidden 분포를 기록한다. idle/run/반전/점프/전투/visibility전환을 구분한다.

측정: game/worker animation update/evaluation, PoseSearch, perplayer steering/OW/root, foottrace/IK, retarget/follower, allocation/copy/indexmemory, CPU p50/p95/p99, GPU/frametime. debuglog 자체 overhead를 별도baseline으로 구분한다.

MM baseline .05, mid budget .033, far .083, explicitedge 일시0라는 현재계약을 유지하고 단계별 실제search executed 횟수로 빈도를 계산한다. update frequency와search frequency를혼합하지 않는다.

예산이 넘으면 lookup/profile 수요·LOD·candidate 제한부터 검토하고 단발 수명이나 최신snapshot을 끊는 방식은 피한다. 성능수치를이번조사에서측정했다고보고하지 않는다.

## 8. 검증 결과 템플릿

```text
날짜 / 프로젝트 경로 / 엔진 / commit + dirty 상태:
실행 binary와 소스 일치 확인 방법:
권한 / 수정·저장·컴파일 범위:
Pawn / Leader / Visual / AnimBP / Profile / AssetSet / PSD / Schema:
변경 한 가지 / baseline / rollback:
시나리오 ID / 실제 입력 / fps / 지형 / mode / netrole:
관측 frame / 선택·검색·출력 연결:
clip/time/weight/continuing / lifetime/exit / correction/foot:
자동화 / 실제플레이 / 영상 / 비용 측정 각 결과:
합격 / 실패 / 미확인 / 증거 파일과 한계:
기존Cycle/Pivot/StartLand/remote/mount 회귀:
다음 단계의 진입 조건:
```

과거 H01/H02/H03 검증 개수를 합산하거나 새 실행 통과로 복제하지 않는다. 실패한 가설도 원인/조건과 함께 보존한다.
