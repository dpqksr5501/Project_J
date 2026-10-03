# Combat Locomotion Architecture

이 문서의 상세 본문은 [전투 애니메이션 합성과 책임 통합 가이드](Combat_Animation_System.md#layers)에 통합했다. 설정·예외·수식·검증 내용을 유지하며, [이전 전체 원문](../../Archive/SourceDocuments/2026-10-03/Combat/Architecture/CombatLocomotionArchitecture.md)도 보존한다.

## 기존 절 바로 찾기

<a id="combat-locomotion-architecture"></a>
- [Combat Locomotion Architecture](Combat_Animation_System.md#layers-combat-locomotion-architecture)
<a id="scope"></a>
- [Scope](Combat_Animation_System.md#layers-scope)
<a id="job-blueprint-pattern"></a>
- [Job Blueprint Pattern](Combat_Animation_System.md#layers-job-blueprint-pattern)
<a id="editor-asset-setup"></a>
- [Editor Asset Setup](Combat_Animation_System.md#layers-editor-asset-setup)
<a id="authoring-contract"></a>
- [Authoring Contract](Combat_Animation_System.md#layers-authoring-contract)
<a id="full-body-montage-and-procedural-leg-ik"></a>
- [Full-body montage and procedural leg IK](Combat_Animation_System.md#layers-full-body-montage-and-procedural-leg-ik)
<a id="persistent-combat-state"></a>
- [Persistent combat state](Combat_Animation_System.md#layers-persistent-combat-state)
<a id="animation-priority"></a>
- [Animation Priority](Combat_Animation_System.md#layers-animation-priority)
<a id="weapon-sockets-and-ik-contract"></a>
- [Weapon Sockets and IK Contract](Combat_Animation_System.md#layers-weapon-sockets-and-ik-contract)
<a id="state-and-interruption-policy"></a>
- [State and Interruption Policy](Combat_Animation_System.md#layers-state-and-interruption-policy)
<a id="combat-blend-space-data-bridge"></a>
- [Combat Blend Space Data Bridge](Combat_Animation_System.md#layers-combat-blend-space-data-bridge)
<a id="data-driven-weapon-combos"></a>
- [Data-Driven Weapon Combos](Combat_Animation_System.md#layers-data-driven-weapon-combos)
<a id="command-inputs-black-desert-style"></a>
- [Command Inputs (Black Desert-style)](Combat_Animation_System.md#layers-command-inputs-black-desert-style)
<a id="greatsword-first-attack-editor-setup"></a>
- [Greatsword First Attack: Editor Setup](Combat_Animation_System.md#layers-greatsword-first-attack-editor-setup)
<a id="tag-convention"></a>
- [Tag Convention](Combat_Animation_System.md#layers-tag-convention)
<a id="assets-to-create"></a>
- [Assets to Create](Combat_Animation_System.md#layers-assets-to-create)
<a id="da_greatsword_weaponprofile"></a>
- [DA_Greatsword_WeaponProfile](Combat_Animation_System.md#layers-da_greatsword_weaponprofile)
<a id="da_greatsword_combo-one-node"></a>
- [DA_Greatsword_Combo: One Node](Combat_Animation_System.md#layers-da_greatsword_combo-one-node)
<a id="ability-set-and-input"></a>
- [Ability Set and Input](Combat_Animation_System.md#layers-ability-set-and-input)
<a id="add-the-second-light-attack-later"></a>
- [Add the Second Light Attack Later](Combat_Animation_System.md#layers-add-the-second-light-attack-later)
<a id="add-a-command-skill-later"></a>
- [Add a Command Skill Later](Combat_Animation_System.md#layers-add-a-command-skill-later)
<a id="validation-checklist"></a>
- [Validation Checklist](Combat_Animation_System.md#layers-validation-checklist)
<a id="removed-compatibility-paths"></a>
- [Removed Compatibility Paths](Combat_Animation_System.md#layers-removed-compatibility-paths)
