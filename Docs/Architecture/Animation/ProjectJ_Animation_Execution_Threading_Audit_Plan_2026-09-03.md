# Project_J Animation Execution & Threading Audit Plan

**Date:** 2026-09-03  
**Purpose:** `ABP_Humanoid_Master`와 native animation pipeline의 실제 실행 경로, Game Thread 비용, parallel animation eligibility를 증거로 확인하기 위한 사전 감사 계획.  
**Scope:** 분석·프로파일링·제안만 포함한다. C++/Blueprint/AnimGraph/PSD/프로젝트 설정을 변경하지 않는다.

## 1. 현재 확인된 책임 경계

```text
Gameplay / CharacterMovement / GAS
  -> UProject_JLocomotionAnimStateComponent (semantic locomotion facts)
  -> UProject_JCharacterAnimInstance (GT snapshot)
  -> FProject_JCharacterAnimInstanceProxy
  -> ABP_Humanoid_Master
       MM: continuous locomotion
       Chooser + BlendStack: authored one-shots
       AnimGraph / linked layers: pose routing and composition
```

이 문서는 위 경계를 사실로 가정하지 않는다. 각 단계가 실제로 어느 thread에서 실행되는지, Actor/UObject를 다시 읽는지, Blueprint 때문에 parallel path가 내려오는지를 UE 5.8 source·에디터·Insights로 검증한다.

## 2. 증거 우선순위

1. 현재 `Source/`, `Config/`, `ABP_Humanoid_Master` 및 연결된 asset graph
2. UE 5.8 local engine source (`AnimInstance`, `AnimInstanceProxy`, `SkeletalMeshComponent`, PoseSearch, Chooser, BlendStack)
3. Unreal Insights trace와 runtime debug trace
4. 최근 git history
5. 기존 문서와 GASP reference

GASP는 비교 자료이며 실행 ownership의 증거가 아니다. `Game Thread`, `worker`, `parallel update`, `parallel evaluation`을 서로 같은 것으로 취급하지 않는다.

## 3. Thread ownership 조사표

| 작업 | 확인 대상 | 최종 분류 |
|---|---|---|
| movement facts | CMC velocity/acceleration/movement mode, network role | GT / unknown |
| semantic locomotion | gait, rotation mode, stance, combat/mount, one-shot authority | GT / unknown |
| trajectory / significance | generation cadence, remote reconstruction, policy | GT / parallel / unknown |
| AnimInstance snapshot | `NativeUpdateAnimation`, proxy publish/copy | GT / unknown |
| thread-safe update | proxy consumer, property access, Blueprint node safety | parallel update / GT fallback / unknown |
| Chooser | evaluation site, row/filter complexity, frequency | GT / parallel / unknown |
| Motion Matching | PSD selection, query build, search, result/reselect | GT / parallel / task / unknown |
| BlendStack / State Controller | selected asset, player update, one-shot transition | GT / parallel / unknown |
| pose evaluation | Aim Offset, warping, foot placement, leg IK, PoseHistory | parallel evaluation / GT fallback / unknown |
| linked layers | load/switch/evaluate and full-body/upper-body composition | classify per path |
| events | notifies, montage callbacks, curves, delegates | GT / unknown |
| final mesh | skeletal tick, cloth, render handoff | GT / task / render-related / unknown |

`unknown`은 실패가 아니라 engine source 또는 trace가 더 필요하다는 의미다. 추측으로 worker ownership을 선언하지 않는다.

## 4. Game Thread hot-path checklist

- Actor/Component getter, `GetWorld`/`GetOwner`, cast, skeletal mesh/animation asset lookup
- GameplayTag query/container copy, profile/PSD/database switching
- Blueprint function/getter, dynamic cast, struct make/break, UObject dereference, delegates
- Chooser filter/row evaluation, State Controller decision, linked-layer switching
- per-frame `TArray`/`TMap`, candidate buffer, `FString`/debug/log formatting, temporary allocations
- repeated C++ locomotion computation versus ABP recomputation
- AnimNotify and montage callback의 gameplay/physics/trace 비용
- forced task completion, lock, fence, atomic contention, Game Thread stall

발견 항목마다 **빈도**, **population별 호출 수**, **CPU/alloc 비용**, **correctness dependency**, **안전한 개선 방향**을 함께 기록한다. 단순 getter caching 같은 micro-optimization은 trace 없이 적용하지 않는다.

## 5. Snapshot / Fast Path audit

`UProject_JCharacterAnimInstance -> FProject_JCharacterAnimInstanceProxy` 경로에서 아래를 점검한다.

| 검증 | 통과 기준 |
|---|---|
| UObject 재접근 | parallel path에서 Actor/CMC/UObject를 임의로 재조회하지 않는다. 필요한 값은 GT snapshot에서 전달한다. |
| 중복 계산 | semantic state는 C++ authority가 결정하고, ABP는 asset/pose selection을 위해 같은 의미 상태를 재작성하지 않는다. |
| 복사량 | hot path에 큰 container, 불필요 UObject ref, 희소 debug data가 섞여 있지 않다. |
| Fast Path | direct property access를 우선하고, Blueprint VM/function/dynamic cast가 필요한 부분을 명확히 표시한다. |
| Hot/Cold 분리 | proxy copy/locality가 측정상 문제일 때만 hot snapshot과 rare debug/cold snapshot 분리를 제안한다. |

## 6. Motion Matching / one-shot 검증 범위

- Continuous MM: Idle/Cycle/Turn Redirect 계열의 PSD, query, search, reselect, PoseHistory 사용
- BlendStack: Start/Stop/Pivot/TIP/Air/Land의 asset/time/blend profile/steering 경로
- Chooser: semantic presentation state에서 data-driven asset을 선택하는 빈도와 threading
- re-entry: one-shot 종료 또는 interrupt 후 trajectory/current facing/PoseHistory/force reselect/reset reason
- observability: MM debug가 one-shot asset을 보이지 않아도 구조 결함으로 결론 내리지 않는다. StateController/Chooser/BlendStack/MM trace를 하나의 presentation trace로 합칠 필요만 판단한다.

## 7. 결과물 형식

감사 종료 시 아래 결과를 남긴다.

1. UE 5.8 source 근거가 있는 thread ownership map
2. Fast Path / GT fallback asset-node 목록
3. snapshot field·copy·중복 계산 표
4. 1/10/30/50/100 population별 Insights 표와 p50/p95/p99
5. 실제 병목 순위와 변경하지 않을 이유
6. 작은 safe change 후보, 예상 이득, regression scenario

## 8. 선행 에디터 정보

현재 제공된 screenshots는 Master AnimGraph의 주요 composition을 입증하지만 Fast Path와 실제 node settings 전부를 판정하기에는 부족할 수 있다. 필요한 경우 아래만 추가 확인한다.

| Asset | Graph / node | 캡처 범위 | Details | 이유 |
|---|---|---|---|---|
| `ABP_Humanoid_Master` | Event Graph / `Blueprint Thread Safe Update Animation` / property access | update entry부터 snapshot 변수 publish까지 | 예 | GT와 thread-safe update의 실제 경계 확인 |
| `ABP_Humanoid_Master` | Motion Matching, Pose History, BlendStack | 각 node와 Details의 threading/search/reset settings | 예 | actual re-entry/search/update policy 확인 |
| 각 linked layer | AnimGraph | entry~output, function call/property access 부분 | 필요 시 | Fast Path와 layer별 evaluation 비용 판정 |
| SkeletalMeshComponent owner BP | Mesh/Animation settings | multi-thread update/URO/visibility tick relevant settings | 예 | engine parallel eligibility와 tier policy 확인 |

