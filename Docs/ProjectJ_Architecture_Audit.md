Project_J 전체 MMORPG architecture와 performance 구조를 감사해 주세요.

Repository:
https://github.com/dpqksr5501/Project_J

Engine:
Unreal Engine 5.8

작업 전에 반드시 `Docs/Architecture/ProjectJ_Architecture_Audit.md`를 처음부터 읽고 해당 원칙을 이번 작업의 기준으로 사용하세요.

이번 작업은 Animation이나 Pivot 하나에 국한된 작업이 아닙니다.

Project_J 전체를 다음 관점에서 분석해야 합니다.

- overall architecture
- module / dependency structure
- gameplay / GAS
- Game Thread
- Worker Thread / Task Graph
- networking / replication / Iris
- CharacterMovement
- animation / Motion Matching
- AI / NPC scalability
- physics / collision
- memory / GC
- asset management / async loading
- world streaming
- rendering
- modular character
- Niagara / VFX
- data architecture
- dedicated server
- security / trust boundary
- backend/persistence boundary
- UI
- profiling / observability
- testing
- long-term MMORPG scalability

중요:

Project_J는 아직 개발 초기 단계입니다.

따라서 class, plugin, struct, setting, DataAsset 등이 존재한다는 이유만으로 해당 시스템이 구현되어 있다고 판단하지 마세요.

각 주요 시스템을 반드시:

- Implemented
- Partially Implemented
- Infrastructure Only
- Planned
- Experimental
- Deprecated / Dead

중 하나로 구분하세요.

현재 코드와 실제 runtime path를 최우선 source of truth로 사용하세요.

오래된 Docs와 현재 코드가 충돌하면 현재 코드와 최근 git history를 우선하세요.

---

## Unreal Engine Source

로컬 Unreal Engine 5.8 source에 접근할 수 있다면 적극적으로 조사하세요.

특히 정확한 engine behavior가 architecture 판단에 영향을 주는 경우 추측하지 마세요.

필요하면 다음을 조사하세요.

- AnimInstance / AnimInstanceProxy
- PoseSearch
- Motion Matching
- BlendStack
- Chooser
- CharacterMovement
- Iris
- Replication
- Animation Budget Allocator
- Significance Manager
- Mass
- Asset Manager
- Async Loading
- Task Graph
- SkeletalMeshComponent

---

## Reference Projects

필요한 경우 다음을 참고하세요.

- GASP
- Lyra
- Unreal Engine source
- Epic official documentation

하지만 어떤 reference architecture도 Project_J의 정답이라고 가정하지 마세요.

왜 해당 구조가 Project_J에 적합하거나 적합하지 않은지 비교하세요.

특히 GASP의 Full Motion Matching과 Experimental State Machine / Blend Stack 경로를 구분하세요.

---

## Threading

Game Thread 최적화를 매우 중요하게 다루세요.

단순히 "멀티스레드가 적용되어 있다"는 평가로 끝내지 마세요.

Project_J의 주요 runtime 작업을 가능한 범위에서 다음으로 분류하세요.

- Game Thread
- Worker Thread
- Task Graph
- Parallel Animation Update
- Parallel Animation Evaluation
- Physics
- Render Thread
- Async Loading

다음을 찾으세요.

- serial bottleneck
- unnecessary Tick
- repeated UObject lookup
- Blueprint VM hot path
- allocation / memory churn
- synchronization
- forced task completion
- lock contention
- duplicated computation

작업을 무조건 thread로 옮기는 방식은 사용하지 마세요.

---

## MMORPG Scalability

1명의 캐릭터가 아니라 다음을 기준으로 architecture를 평가하세요.

- local player
- nearby remote players
- distant remote players
- boss
- important NPC
- generic monsters
- ambient NPC/crowd

다수 actor가 존재할 때 CPU, memory, network, animation, collision, AI가 어떻게 증가하는지 분석하세요.

단순 distance LOD 외에도:

- significance
- scheduling
- update frequency
- event-driven processing
- batching
- search staggering
- shared evaluation
- inactive system suppression

등을 검토하세요.

---

## Animation

현재 Project_J의 Motion Matching / State Controller / Chooser / Blend Stack 구조도 전체 architecture audit의 한 부분으로 분석하세요.

기존 구조를 보존할 필요는 없습니다.

더 나은 구조가 있다면 변경을 제안하세요.

다만 GASP를 그대로 복제하지 마세요.

특히 다음을 확인하세요.

- continuous locomotion
- authored one-shot transition
- Start
- Stop
- Pivot
- TIP
- Jump / Fall / Landing
- Combat Strafe
- OTM
- Pose History
- Motion Matching re-entry
- State authority duplication
- AnimInstance thread architecture
- Motion Matching search scheduling

Pivot에 분석을 과도하게 집중하지 마세요.

---

## Networking

다음을 실제 코드 기준으로 조사하세요.

- authority
- RPC
- replicated properties
- FastArray
- Iris
- NetUpdateFrequency
- relevancy
- dormancy
- prediction
- CharacterMovement
- autonomous vs simulated proxy
- remote presentation

Presentation data를 불필요하게 replicate하고 있지 않은지도 확인하세요.

---

## Optimization

아직 profiling evidence가 없는 micro-optimization을 바로 적용하지 마세요.

우선순위는:

P0 Architecture / correctness

P1 Scalability blocker

P2 Significant performance issue

P3 Maintainability

P4 Profile-proven micro optimization

순으로 잡으세요.

---

## Screenshots / Unreal Assets

GitHub에서 확인할 수 없는 `.uasset`, Animation Blueprint, Chooser, State Controller, Blueprint graph 등의 정보가 architecture 판단에 필요하면 추측하지 마세요.

필요한 자료를 나에게 요청하세요.

다음 형식으로 요청하세요.

1. Asset 이름
2. Graph / node 이름
3. 어떤 영역을 보여줘야 하는지
4. Details Panel 필요 여부
5. 해당 자료가 왜 필요한지

가능하면 한 번에 필요한 스크린샷 목록을 정리하세요.

---

# Phase 1 — Audit Only

처음에는 코드를 수정하지 마세요.

먼저 repository 전체를 충분히 조사하고 다음 보고서를 작성하세요.

## 1. Executive Summary

Project_J의 현재 architecture를 전체적으로 평가.

## 2. Runtime Architecture Map

실제로 실행되는 주요 흐름을 구조도로 작성.

예:

Input
→ Gameplay
→ GAS
→ Character
→ Movement
→ Animation
→ Network

등 실제 Project_J 구조.

## 3. Implementation Status Matrix

각 주요 subsystem의 구현 수준.

## 4. Threading Map

Game Thread / Worker Thread / Task Graph 등의 현재 responsibility.

## 5. Networking Map

Server / Autonomous / Simulated Proxy 데이터 흐름.

## 6. Strengths

현재 구조 중 유지해야 할 부분.

## 7. Architecture Problems

실제 구조상 문제.

## 8. Performance Risks

현재 또는 향후 scalability blocker.

## 9. Overengineering Risks

개발 초기 Project_J에서 아직 구현할 필요가 없는 시스템.

## 10. Recommended Target Architecture

현재 구조 유지 여부와 관계없이 Project_J에 가장 적합하다고 판단되는 architecture.

## 11. Priority

P0 ~ P4로 분류.

## 12. Migration Roadmap

안전하게 단계적으로 변경할 순서.

## 13. Profiling Plan

어떤 변경을 하기 전에 무엇을 측정해야 하는지.

## 14. Required Screenshots / Editor Information

추가 자료가 필요한 경우 목록 작성.

---

보고서를 작성한 뒤 멈추세요.

아직 repository를 수정하지 마세요.

내가 Audit 결과를 검토한 뒤 실제 refactor 범위를 결정하겠습니다.