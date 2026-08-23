# Project_J Locomotion & State Controller Implementation Summary

> **Historical snapshot (2026-08-01).** Do not use the example Chooser rows or
> AnimGraph diagram in this file as current editor-authoring instructions.
> The authoritative current integration policy is
> `Docs/GASP_ProjectJ_Locomotion_Parity.md`; it records the later OTM Reface,
> Stop, Strafe six-direction, and actual AnimGraph decisions.

**Date**: 2026-08-01  
**Target Engine**: Unreal Engine 5.8  
**Module**: `Project_JCharacter` (C++ & Animation Blueprint)

---

## 1. Executive Summary

This historical document captures an earlier integration snapshot of the **GASP-style State Controller & Chooser System** with Unreal Engine 5.8's native **Motion Matching** framework for `Project_J`. Later validation changed several chooser rows and removed the assumption that an Inertialization node is present in the current AnimGraph.

Through iterative empirical log analysis and architectural refinement, the locomotion system now seamlessly handles:
1. **Ground Locomotion**: Idle Loop, Run Start, Sprint Start, Run Stop, Sprint Stop.
2. **Air Locomotion**: Takeoff (Jump Start), FallOff (Ledge Step-off), InAir Fall Loops.
3. **Landing System**: Stand Light/Heavy Land, Run Light/Heavy Land, Sprint Light/Heavy Land with zero pose popping.
4. **AnimGraph Pose Assembly**: Corrected BlendStack, Inertialization node placement, and `Blend Poses by bool` transition smoothing.

---

## 2. System Architecture & Layers

```text
+-----------------------------------------------------------------------+
| 1. C++ Character & AnimInstance Thread-Safe Snapshot (Game Thread)    |
|    - LocomotionContext (GaitIntent, RotationMode, PhaseFamily)       |
|    - Landing & Air Context (bIsLanding, bIsJumping, bIsFallOffStart)   |
|    - Presentation State Resolver & Playback Hold Controller           |
+-----------------------------------------------------------------------+
                                   |
                                   v
+-----------------------------------------------------------------------+
| 2. Chooser Table Evaluation (Worker Thread)                           |
|    - Result Class Filter: UObject (AnimationAsset + PoseSearchDB)     |
|    - State Controller Presentation State Matching                         |
|    - ChooserGaitIntent Matching (Run vs Sprint)                       |
|    - Latched landing semantics (Moving/Sprint/Heavy/Foot)             |
+-----------------------------------------------------------------------+
                                   |
                                   v
+-----------------------------------------------------------------------+
| 3. AnimGraph Pose Assembly (ABP_Humanoid_Master)                      |
|    - StateController (State Machine) + Motion Matching Engine         |
|    - Blend Stack (One-Shot Animation Player)                          |
|    - Blend Poses by bool (True/False Blend Time = 0.3s)              |
|    - Inertialization Node (Placed before [Locomotion] Pose Cache)     |
+-----------------------------------------------------------------------+
```

---

## 3. C++ State Controller Presentation State Resolver

### State Enum Mapping (`EProject_JStateControllerPresentationState`)
- `IdleLoop` (1): Stand Idle Loop.
- `TransitionToLocomotion` (2): Run Start / Sprint Start / Reface Start.
- `LocomotionLoop` (3): Regular Motion Matching Ground Locomotion Loop.
- `TransitionToIdle` (4): Run Stop / Sprint Stop.
- `TransitionToInAir` (5): Jump Takeoff / Ledge FallOff Start.
- `InAirLoop` (6): In Air Falling Loop.
- `TransitionToLand` (7): Landing (Stand, Run, Sprint Light/Heavy).

### Key C++ Logic in `Project_JCharacterAnimInstance.cpp`
```cpp
// ResolveStateControllerPresentationState
if (Data.Landing.bIsLanding)
{
    // Always request TransitionToLand state so Chooser evaluates CHT_Player_Land
    return EProject_JStateControllerPresentationState::TransitionToLand;
}

if (bIsJumpingOrJumpStart || Data.Air.bIsFallOffStart)
{
    // Stepping off ledge or jumping requests TransitionToInAir
    return EProject_JStateControllerPresentationState::TransitionToInAir;
}
```

### Interrupt Mode Policy (`Project_JCharacterAnimInstanceProxy.cpp`)
- Landing onto ground (`!bIsInAir && bWasInAir`) uses `EPoseSearchInterruptMode::DoNotInterrupt` matching GASP standards.
- This allows One-Shot Land animations to blend out smoothly into ground locomotion without being forcibly cut or invalidated.

---

## 4. Chooser Table Configurations

### A. `CHT_Player_OTM_Ground`
- **Result Object Class Filter**: `Object` (allows both `AnimationAsset` and `PoseSearchDatabase`).
- **Columns**:
  1. `State Controller Presentation State` (Enum)
  2. `ChooserGaitIntent` (Enum)
  3. `Project_JStateControllerChooserOutput` (Struct: `StartTime`, `BlendTime`, `bUseMotionMatch`)

| Row | Result Asset | State Controller Presentation State | ChooserGaitIntent | Output Struct Settings |
| :--- | :--- | :--- | :--- | :--- |
| **Row 0** | `M_Neutral_Stand_Idle_Loop` | `Idle Loop` | *(Any / None)* | `BlendTime = 0.2s` |
| **Row 1** | `M_Neutral_Run_Start_F_Lfoot` | `Transition to Locomotion` | `Run` | `bUseMotionMatch = True` |
| **Row 2** | `M_Neutral_Sprint_Start_F_Lfoot` | `Transition to Locomotion` | `Sprint` | `bUseMotionMatch = True` |
| **Row 3** | `M_Neutral_Run_Stop_F_Lfoot` | `Transition to Idle` | `Run` | `bUseMotionMatch = True` |
| **Row 4** | `M_Neutral_Run_Stop_F_Rfoot` | `Transition to Idle` | `Sprint` | `bUseMotionMatch = True` |

### B. `CHT_Player_Land`
- **Columns**:
  1. `State Controller Presentation State` = `Transition to Land`
  2. Latched `Gait Intent` (`Walk` / `Run` / `Sprint`)
  3. `One Shot Foot`
  4. `Use Heavy Land`
  5. `Project_JStateControllerChooserOutput` (`BlendTime = 0.3s`, `bUseMotionMatch = False`)

| Landing Type | Latched Gait | Selected Animation Asset |
| :--- | :--- | :--- |
| Stand Land (Light/Heavy) | `Walk` | `M_Neutral_Jump_B_Land_Stand_Light_Lfoot / Rfoot` |
| Run Land (Light/Heavy) | `Run` | `M_Neutral_Jump_F_Land_Run_Light_Lfoot / Rfoot` |
| Sprint Land (Light/Heavy) | `Sprint` | `M_Neutral_Jump_F_Land_Sprint_Light_Lfoot / Rfoot` |

> 2026-08-23 correction: the former live GroundSpeed Float Range was redundant
> with latched gait and could reject a simulated proxy near the 500 uu/s Run
> boundary. It is intentionally not part of the current Land selector contract.

---

## 5. AnimGraph Assembly & Node Fixes (`ABP_Humanoid_Master`)

### Root Cause of Previous 1-Frame Popping
Previously, a `Two Way Blend` node had its `Alpha` driven by a `Select Float` node returning discrete `0.0` or `1.0` based on `GetThreadSafeStateControllerHasSelectedAnimation`. When `HasSelectedAnimation` flipped to `false` upon Land completion, Alpha dropped from `1.0` to `0.0` in 1 frame (0.000s), causing a 1-frame hard pose snap from `Blend Stack` to `Motion Matching`.

### Resolved AnimGraph Wiring
```text
                                        +-----------------------+
                                        |  Blend Stack          |
                                        +-----------------------+
                                                    |
                                                    v (True Pose)
+------------------------------------+    +------------------------------------+    +-------------------+    +--------------+
| GetThreadSafeStateControllerHas... | -> | Blend Poses by bool                | -> | Inertialization   | -> | [Locomotion] |
+------------------------------------+    | - True Blend Time:  0.3s           |    | (Default: 0.2s)   |    | (Pose Cache) |
                                          | - False Blend Time: 0.3s           |    +-------------------+    +--------------+
                                          +------------------------------------+
                                                    ^ (False Pose)
                                                    |
                                        +-----------------------+
                                        | Top MM / State Machine|
                                        +-----------------------+
```

1. **`Blend Poses by bool` Node**:
   - `Active Value`: `GetThreadSafeStateControllerHasSelectedAnimation`
   - `True Pose`: `Blend Stack` (One-Shot Output)
   - `False Pose`: Top Motion Matching & State Controller Line
   - **`True Blend Time`**: `0.3s`
   - **`False Blend Time`**: `0.3s`
2. **`Inertialization` Node**:
   - Positioned between `Blend Poses by bool` output and `[Locomotion]` Pose Cache node.
   - `Forward Inertialization Request` enabled (`bForwardInertializationRequest = True`).

---

## 6. Empirical Verification & Log Diagnostics

- Console Command for Debug Trace: `p.ProjectJ.MMTransitionDebug 1`
- Dump Pivot Trace: `DumpMotionMatchingTransitionTrace`

### Verified Behaviors
- **Landing Transition**: When 2.2s `Land_Run` or 2.8s `Land_Sprint` finishes, `Blend Poses by bool` smoothly transitions over `0.3s` into `Run_Loop` with zero 1-frame popping.
- **Start / Sprint Start**: Pressing WASD triggers `M_Neutral_Run_Start_F_Lfoot` instantly (0s delay). Holding Shift + WASD triggers `M_Neutral_Sprint_Start_F_Lfoot` instantly with zero input lag.
