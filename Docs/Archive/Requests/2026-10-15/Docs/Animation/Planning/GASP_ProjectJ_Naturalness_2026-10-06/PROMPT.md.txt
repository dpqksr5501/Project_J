# 후속 작업 프롬프트

새 채팅에서는 공통 인계 프롬프트를 먼저 전달한다. 특정 단계로 범위를 좁히려는 경우에만 단계 프롬프트를 추가한다. 목표는 전체 애니메이션 자연스러움이며, 큰 반전은 그 안의 검증 항목이다. 단계 A-E는 문제별 작업 구분이며 모든 작업이 MovingTurn 수정부터 시작해야 한다는 순서가 아니다. 아래프롬프트는 작업권한을 자동확대하지 않는다. 분석/설계 기본은 read-only이며 구현/에셋저장은 사용자가 해당범위를 새로 요청해야 한다.

## 1. 공통 인계 프롬프트

```text
Project_J의 전체 애니메이션 자연스러움을 고도화하는 작업을 이어가줘.
목표를 180도 전환이나 Turn 개선만으로 좁히지 마.
입력과 실제 이동/trajectory, Idle↔Start↔이동↔Stop, Walk/Run/Sprint 가감속,
작은 곡선 이동, 점프↔낙하↔착지, 동작 중 WASD/마우스 변경,
자세·몸 회전·발 접지와 원격/군중 비용을 함께 평가해.
큰 반전과 170/190도·연속 반전은 전체 연결을 평가하는 스트레스 항목이다.
먼저 프로젝트 AGENTS.md와 다음 조사묶음을 읽어:
전체 문서 경로:
C:\Users\I\Documents\GitHub\Project_J\Docs\Animation\Planning\GASP_ProjectJ_Naturalness_2026-10-06
위 폴더의 README.md부터 읽고,
01_EVIDENCE.md, 03_PROJECT_J.md, 02_GASP.md, 04_ROADMAP.md,
05_VALIDATION.md 및 Evidence/README.md.

분석 기준은 ABP_Player가 아니라 실제 운영 BP_GreatSword →
DA_GreatSword_AnimProfile → DA_Player_Profile/DA_Player_Combat →
ABP_Humanoid_Master다. 실제 spawn/프로필/compiled상태가 바뀌었는지 확인해.

작은 회전의 DynamicCycle(Arc/Diamond/Hourglass 등), 기존 Strafe키보드
Pivot, Start/Stop/Land/Jump/TIP의 command/episode/epoch/revision 수명,
최신snapshot과 actual-search 소비, 원격/군중 예산은 보존해.
각도별Turn강제나 GASP전체교체를 기본 해법으로 삼지 마.

확인된 그래프/설정(G/A), 소스(S), 과거실행(H), 해석(I), 미확인(U)을 구분해.
연결되지 않은노드/Disable행/default/globalflag/asset파일존재를 active기능으로
보고하지 마. node default와 proxy runtimeoverride를 구분해.
새플레이를 하지 않았다면 최종 시각품질을 검증했다고 쓰지 마.

현재 조사에선 신규검색 후보는 C++가 고른 단일PSD다.
Cycle이 Turn전체와 비용경쟁에서 이겼다고 단정하지 마.
현재CombatRunTurn은 PSD_Player_Locomotion 폴더의 PSS_Combat 네180후보다.
같은basename의 다른폴더PSD/과거문서와 구분해.

우선 진행:
1) 기존 조사 근거를 재사용하고 현재 운영 경로/설정이 바뀐 부분만 재확인해.
2) 05의 N1-N6 전체 baseline과 S1-S15 예외를 기준으로 잘되는 부분과
   실제 부족한 부분을 구분해. 새 플레이 권한이 없으면 시각효과는 미확인으로 남겨.
3) 문제를 이동·예측 / 데이터·검색 / 동작수명·연결 / 자세·발 / 원격예산으로
   나누고 효과→구현근거→원리→현재차이→비용/검증 순서로 평가해.
4) 기존 구조 유지 / 설정·데이터 조정 / 코드·그래프 변경으로 나눠
   재현된 영향, 근거 확실성, 비용과 회귀 위험으로 우선순위를 정해.
   GASP의 모든 기능을 가져오거나 Turn부터 고치는 것을 전제하지 마.

기존 조사에서 확인할 구체 이슈도 보존해:
1) InAirLoop는 FallLoop선택이 있지만 external override가 비loop transition만
   허용하고 logicalSM pose는 비어 있다. longFall의 실제최종출력을 추적해.
2) MovingTurn active중에도 speed>=180 자격이 필요해 정상감속으로 즉시
   종료될 수 있다. entry와 maintenance 조건을 분리해 평가해.
3) externalOW enable_warping은 Alpha가 아니라 CurrentAnimAssetTime에 연결,
   TargetTime=0이다. MM내부는 Input→Result, externalSteering은 TIP전용이다.
4) OffsetRoot는 실제연결이며 일반Release/TIPInterpolate다. 옛 disconnected
   문서를 현재사실로 쓰지 마. Release를 즉시0으로 표현하지 마.

이번 요청에서 별도허용하지 않은 소스/Config/에셋/레벨 수정·저장·컴파일은
하지 마. UnrealMCP는 이대화에서 내가 명시허용한 경우에만 관련asset을
최소범위 조회해. 직접조회가 불가능하면 unknown과 read-only확인방법을 남겨.
구현이 허용된 경우에도 한원인/한owner씩 변경하고 기존변경을 보존해.
UBT직접실행/중복빌드금지 등 AGENTS의 빌드정책을 따라.
결과는 효과→근거→원리→현재차이→비용/검증 순서로 설명해.
```

## 2. 단계 A: 전체 자연스러움 baseline과 활성 경로 확인

```text
이번엔 분석만 해. 기존 조사 전체를 재사용하고 현재 변경된 근거만 재확인해.
05의 N1-N6을 중심으로 일반 이동·가감속·동작 연결·자세·발 접지 baseline을
정리하고 S1-S15 입력 변화/예외를 추가해. 내가 플레이를 허용한 경우에만
실행/관측하고 저장·compile는 하지 마. 부족한 장면과 이미 자연스러운 장면을
구분하고 실제 owner·선택·출력·보정·update 흐름에 연결해.
longFall에서 chooser result, heldstate, loop/override, BoolBlend branchweight,
actualMM PSD/clip/time, leader와visual 최종pose를 한 흐름으로 추적해.
largeTurn은 eligibility guard/entry/active/exit, speed와 inputrelease, actualsearch,
selectedDB/clip/time/weight를 같이 봐. 옛8episodes는 역사적증거로만 사용해.
몸만회전/경로반전, keyPivot/mouse반전, 170/190/연속반전을 구분해.
파일·함수·그래프·pin·설정과 runtime증거를 남기고 확인되지 않은 것은 표시해.
구현안은 아직적용하지 말고 전체 문제표와 최소수정대안/검증게이트를 정리해.
큰 반전의 결과만으로 전체 품질을 판단하거나 다른 개선을 보류하지 마.
```

## 3. 단계 B: 확인된 동작수명·연결·출력 문제 설계

```text
코드/에셋을 수정하지 말고 Gate B의 설계만 구체화해.
Start/Stop/Land/Pivot/TIP/Jump/공중loop의 진입·유지·취소·재선택·복귀 중
실제로 확인된 문제를 선택해. 잘되는 episode/epoch/revision 계약은 보존해.
아래 MovingTurn과 InAirLoop는 기존 조사에서 나온 구체 후보이며 유일한 목표가 아니다.
MovingTurn entryqualification과 active lifetime의 조건표를 만들어줘.
normal reversal decel은 무조건종료하지 않되 inputrelease, stop, newtarget,
air/action/rootmotion/mode/timeout은 현재owner계약을 유지해.
새Tick/manager/RPC/frozenSnapshot/강제각도별Turn 없이 existingrevision과
proxy actual-search 계약을 활용하는 최소변경을 제시해.
InAirLoop는 externalloop vs airMM 대안을 비교하고 Idle/LocomotionLoop가
외부에 잘못override되지 않는 분기계약을 적어줘.
변경함수·이전/이후흐름·의미있는회귀·rollback을 정리해.
구현완료/visual개선완료라고 표현하지 마.
```

## 4. 단계 C: 필요한 자세·회전·발 보정 실험 설계

```text
대상 동작의 실제 출력·수명이 보정을 평가할 수 있는 상태인지 먼저 확인해.
해당 경로에 문제가 있으면 그 선행 문제를 명시하고 독립된 경로는 별도로 진행해.
일반 Cycle·가감속·Start/Land·Turn·발 접지 중 실제 부족한 상황에 필요한
최소 보정 실험을 설계해. largeTurn에만 범위를 한정하지 마.
이미 보정이 있는 기능, 비활성 기능, 데이터/설정 차이를 구분해.
각 blendplayer currentasset/time, rootmotionprovider, space, targetfacing,
Alpha/curve, LOD/visibilitygate, capsule/TIP/AO/foot owner 표를 만들어줘.
externalOW curve의 Alpha/timepin 의도를 별도선택지로 구분해.
170/190 보정은 남은authoredroot와 최신target으로 평가하고 clip이름의180을
고정비율로 줄이거나 일반Cycle에 전역보정을 강제하지 마.
Start/Land는 선택lock을 유지하며 작은변화보정/큰변화취소를 비교해.
보폭·속도·접지 문제는 authored speed/playrate/trajectory/retarget/Foot/Leg의
영향을 분리해. DistanceMatching/StrideWarping 등 이름만으로 도입을 결정하지 마.
에셋수정/저장은 이번요청에서 별도허용된 범위만 수행해.
```

## 5. 단계 D: 후보/데이터 비교 프롬프트

```text
baseline에서 실제 선택·데이터 문제로 확인된 상황을 대상으로 분석해.
Cycle 방향·속도·곡선·발 위상, 가감속과 전환 구간의 데이터 coverage,
PSS 특징·가중치·trajectory, continuing/research/entry 설정을 비교해.
수명/보정/데이터 원인을 분리하고 설정·데이터 조정부터 평가해.
큰반전의 후보 부족이 확인된 경우 그 자격구간에 한해 Cycle+Turn 후보겹침과
현재단일PSD를 비교해. 전체 다중PSD화나 Turn 강제를 전제하지 마.
schema/normalization/features/weights/base-loop-continuingbias,
BranchInDB/Exclude/Block/Override/earlyTransition/mirror/footphase를 실제로확인해.
BranchIn=None에서 animation단독entryMM을 무조건켜지 마.
existingCycle 선택분포와 searchCPU/indexmemory/clipjump를 baseline비교해.
native일회선택권한·runtime수명을 새Chooser에중복시키지 마.
현재OTM과CombatCycle 데이터가다르므로 상황별actualPSD를 구분해.
적용전데이터diff/저장대상/rollback과 최소시나리오를 제시해.
```

## 6. 단계 E: 원격/군중 비용 프롬프트

```text
로컬품질검증을 전제로 remote/crowd 적용범위를 평가해.
1/50/100/200, near/mid/far/hidden, idle/run/reversal/air/action으로 나눠
AnimationUpdate/Evaluate/PoseSearch/perplayerCorrection/foottrace/retarget/follower,
allocation-copy-memory와 p50/p95/p99를 기록해.
현재worker baseline.05와midbudget.033/far.083, explicitedge임시0를 구분해.
mid를30HzMM검색으로 단정하지 마. significance/visibility/ABA/URO도 봐.
localcamera yaw를remote에추정하지 말고 event/revision/net smoothing과
relevance복귀/lateevent/teleport reset을 확인해.
hiddenwake/새RPC/perframeforce를 기본해법으로 넣지 마.
실측없이 FPS개선율이나 허용인원을 만들지 마.
```

## 7. 구현 요청을 만들 때 추가할 문장

사용자가 실제구현을 결정한 경우에만 아래 템플릿의 범위를 채운다.

```text
이번엔 [선택한단계/문제]의 구현을 허용한다.
수정가능: [구체 Source/Config/문서/에셋경로].
에셋저장/Blueprint컴파일 허용: [예/아니오와대상].
PIE/빌드/자동화 허용범위: [구체조건].
금지: 기존Cycle/Pivot/단발수명/remotebudget의 불필요한전면교체,
관련없는user변경수정, 승인범위밖asset/level저장.
먼저현재상태와최소변경을 확인하고 승인된범위는끝까지 구현·검증해.
에셋승인이필요하면 실제변경예정과diff/검증계획을 먼저 review가능하게 만들어.
실측한효과와 남은미확인을구분해 보고해.
```

## 8. 완료 보고 프롬프트

```text
구현/검증결과를 05의템플릿으로 남겨줘. 전체 목표 대비 완료·잔여 범위를 적고,
N1-N6 일반 움직임과 관련 S시나리오를 함께 평가해.
변경한owner와파일/graph/pin/data, baseline대비수명/선택/최종visual/비용,
기존Cycle·Pivot·StartLand·TIP·remote·mount 회귀, 미확인과rollback을 적어.
자동화/사용자play/영상/성능실측을합치지마.
이번조사와달라진항목을 명시하고 PDF원본Markdown과링크를 갱신해.
이제완료한단계를다시해야할futuretask로 남기지마.
```
