# Spellblade 스킬 구현·개선 인계

2026-11-10 기준 `codex/spellblade-combat-kit`의 실행 경로다. `DA_Spellblade_Equip` → `DA_CombatStyle_SpellbladeKit` → 콤보·커맨드·무기 애니메이션 프로필 순서로 실제 사용 중인 에셋을 찾는다. 원본 대조와 자세 제작은 [전투 에셋 검토](Spellblade_CombatKit.md), 궤적의 생성·종료와 군중 예산은 [전투 VFX 구조](../../Combat/Architecture/CombatVFXArchitecture.md)를 따른다.

## 직업별 실행 경로

| 직업 ID | 캐릭터 BP | 시작 무기 | 전투 스타일 DA | 전투 레이어 |
| --- | --- | --- | --- | --- |
| `Spellblade` | `BP_Spellblade` | `DA_Spellblade_Equip` | `DA_CombatStyle_SpellbladeKit` | `ABP_Spellblade` |
| `GreatSword` | `BP_Greatsword` | `DA_Greatsword_Equip` | `DA_CombatStyle_Spellblade` | `ABP_Greatsword_Basic` |

두 캐릭터는 `BP_Player_Master`와 `ABP_Player_Master`를 공유한다. 각 직업의 기본 전투 스타일은 자신의 시작 무기와 동일한 DA를 사용하며 각자 입력 매핑, 커맨드, 콤보, 공격 정의와 몽타주를 선택한다. `GA_Spellblade`는 두 스타일의 데이터를 실행하는 공통 근접 어빌리티다. `GA_Spellblade_MovementDash`는 Spellblade 스타일만 부여한다. `GreatSword` 저장 ID는 유지한다. Basic 전투 데이터에 남은 예전 `Spellblade` 파일명은 현재 스타일 경로와 저장 식별자 호환을 위해 유지한다.

`BP_Project_JGameMode`의 `PlayerPawnClassesByJob`이 서버 PlayerState에 확정된 직업을 해당 BP에 연결한다. 첫 스폰과 리스폰이 같은 규칙을 사용한다. 매핑을 사용하는 게임 모드는 연결이 없거나 잘못된 직업의 스폰을 거절한다. 직업이 정해지지 않은 기본 맵 세션은 `BP_Spellblade`로 시작한다. Greatsword는 Manny 외형을 사용하고 `DA_Greatsword_Presentation`이 발도 시 `WeaponSocket_Greatsword_Combat`, 납도 시 `WeaponSocket_Back`을 선택한다. 두 BP의 시작 무기는 프로필이 없는 테스트 스폰에 적용하며 저장 프로필의 장비는 기존 복원 경로가 소유한다.

기존 Spellblade 몽타주 중 교체된 `Light01`~`Light04`, `Light04Recovery`, `Kick`, `Buff`, `CastLeft`, `CastRight`는 양쪽 스타일, 저장 참조, 제작 코드에서 사용처가 없어 로컬 중앙 보관소로 옮겼다. 원본 시퀀스는 유지한다. 복구 경로와 SHA-256은 `Saved/StorageCleanup_20261104/ArchiveLocations.json`에서 확인할 수 있다.

독립 PIE에서 두 직업의 발도와 전용 레이어를 검증했다. Spellblade는 기본 공격, Q, Z의 몽타주와 시전 자연 종료 후 기본 공격을 재생했다. Greatsword는 기존 Q와 기본 공격을 재생했고 Z 시전을 시작하지 않았다. Z 시전 중에는 기본 공격으로 이어지는 콤보 전이가 없다. 몽타주가 블렌드아웃을 시작해도 GAS가 자연 종료를 처리할 때까지 시전이 입력을 소유한다. 직업 레지스트리 갱신과 장비·직업의 어빌리티 중복 부여 회귀 2개가 통과했다. 이전 BP·ABP·프로필 복구 묶음은 중앙 보관소에서 `SpellbladeSkillSplitBefore.zip`으로 찾을 수 있다.

`ProjectJ.MMO.Persistence.JobPawnRestore`는 별도의 SQLite 프로필 두 개에서 직업과 장착 무기를 저장·재열기하고 복원한 직업으로 실제 Pawn을 스폰한다. `ProjectJ.PCStructure.JobPawnSelection`은 두 직업의 첫 스폰과 교체 Pawn, 연결이 없는 직업의 스폰 거절을 확인한다.

마감 검증에서 위 두 테스트와 `ComboBlendOutWindows`, `ComboInputSubscription`, `WeaponAttackLifetime`이 통과했다. 이 결과는 에디터 집중 회귀다. 이전 PIE 관측과 함께 참고하며 최신 Cook·패키지·다군중 성능 결과로 사용하지 않는다.

전용 서버와 두 클라이언트 PIE에서 각 직업을 부여한 뒤 리스폰했다. 서버·소유 클라이언트·상대 클라이언트에서 Q의 직업별 몽타주가 일치했고, Z는 Spellblade에서만 재생됐다. 독립 PIE의 R/T도 직업별로 재생됐으며 X/C/G는 Spellblade에서만 시작됐다. 직업 매핑과 Manny 소켓의 변경 전 패키지 및 실행 기록은 중앙 보관소의 `SpellbladeJobLifecycle` 항목에 보존했다.

## 원본과 프로젝트 경로

| 항목 | 경로 |
| --- | --- |
| 원본 공격·이동·발도 FBX | `C:/Users/I/Desktop/Greatsword_A` |
| 원본 Longblade와 장식 FBX, 텍스처 | `C:/Users/I/Desktop/GreatSword_Test/weapons` 및 `textures` 하위 폴더. `longblade`, `longblade_in`, `aiguillette`, `aiguillette_in` |
| 초기 UE5 리깅 패키지 | 최초 공유 경로 `C:/Users/I/Desktop/GreatSword_UE5_Final`은 인계 시점에 확인되지 않았다. 현재 실행 기준은 아래 프로젝트 메시 |
| 실행 몸체·무기 | `/Game/Characters/Spellblade/Meshes/SK_Spellblade_Complete`, `/Game/Weapons/Spellblade` |
| 리타깃 원본·리타기터 | `/Game/Characters/Spellblade/Spellblade_Woman`, `/Game/Characters/Spellblade/Animation/CombatKit/RTG_SpellbladeLegacy_to_Manny_CombatKit`, 같은 폴더의 `RTG_SpellbladeLegacy_to_DirectKit` |
| 시퀀스 / 몽타주 | `/Game/Anim_Assets/Spellblade/Animations/Sword/CombatKit/Sequences` (`DirectBody`, `Aerial`, `Locomotion` 하위 포함) / `/Game/Anim_Assets/Spellblade/Animations/Sword/Montage/CombatKit` |
| 전투 입력·데이터 | `/Game/DataAssetSets/Skill_Input_Mapping_Data/Spellblade/DA_Spellblade_Mapping_Data`, `/Game/DataAssetSets/CombatStyle/Spellblade/DA_CombatStyle_SpellbladeKit`, `/Game/DataAssetSets/AttackDefinition/Spellblade/CombatKit` |
| 제작 자료 | [401개 원본 조사](Greatsword_A_Animation_Survey_2026-11-03.md), [포즈·연계 후보 미리보기](Data/GreatswordA_20261103/Review.html), [원본 파일명·해시·프로젝트 경로](Data/SpellbladeCombatKit/MotionProvenance.json) |

기존 Greatsword 시퀀스 45개와 몽타주 15개는 `/Game/Anim_Assets/Great_Sword/Animations/Sword`와 그 아래 `Montage`로 복원했다. `AM_Spellblade_*`였던 기존 대검 몽타주 13개는 원래 `AM_Greatsword_*` 이름을 사용한다. 두 직업 레이어가 공유하는 `Sword_Idle_Seq`는 대검 폴더에서 참조한다. Spellblade 전용 시퀀스와 몽타주의 위치는 위 표의 `CombatKit/Sequences`와 `Montage/CombatKit`이다. 이전 패키지와 참조 복구 묶음은 로컬 중앙 보관소의 `GreatswordAnimationRestoreBefore.zip`이다.

## 현재 플레이에서 사용 중

아래의 축약된 몽타주 이름에는 공통 접두사 `AM_SpellbladeKit_`가 붙는다. 공격 원본 파일에는 `.fbx`를 붙여 원본 폴더에서 찾을 수 있다.

| 입력 | 몽타주 | 원본 파일명 | 동작 |
| --- | --- | --- | --- |
| 지상 좌클릭 | `BasicCut01` → `BasicCut02` → `BasicCut03Ground` | `pdw_01_01_att_normal_01`, `pdw_01_01_att_normal_02`, `pdw_01_01_att_normal_r_02` | 지상 3연타, 각 42/53/38cm 전진 |
| 지상 우클릭 | `RightAttack` | `pdw_01_01_att_normal_stand_01` | 강베기, 60cm |
| Q → 좌클릭 또는 Q를 누른 채 좌클릭 | `QuickSkill` → `QuickFollow` | `pdw_01_01_att_skill_crossatt_01`, `pdw_01_01_att_skill_crossatt_02` | 빠른 베기와 후속, 16/59cm |
| 좌·우클릭 조합 → R | `ChordLunge` → `ChargeRelease` | `pdw_01_01_att_skill_lunge_00`, `pdw_01_01_att_skill_charging_att_00` | 찌르기에서 R로 연계. 기본 1·2타에서도 찌르기로 분기 가능 |
| R 단독 / T | `ChargeRelease` / `Finisher` | `pdw_01_01_att_skill_charging_att_00` / `pdw_01_01_att_skill_finish_00` | 즉시 발동하는 강공격 42/44cm. R 홀드 차징은 미구현 |
| 지상 전투 F | `AM_Longblade_Kick` | `pdw_01_01_att_kick_01` | Longblade 전투 커맨드가 활성화된 동안 발차기. 비전투 상호작용과 탑승 입력은 기존 경로 사용 |
| E → 좌클릭 또는 E | `AM_Longblade_Flurry_1`~`4` | `pdw_05_01_att_skill_comboatt_01`~`04`, `04_return` | 별도 4연격. 각 타의 입력 창에서 후속 타를 승인하며 마지막 타 몽타주가 전용 복귀를 재생 |
| 지상 전투 Z | `AM_Longblade_Focus` | `pdw_01_01_att_skill_buff_00` | 집중. 시전 완료 후 6초간 초당 마나 5 회복 |
| 지상 전투 X | `AM_Longblade_Mend` | `pdw_05_00_att_skill_stand_magic_l_00` | 재생. 시전 완료 후 6초간 초당 체력 4 회복 |
| 지상 전투 C | `AM_Longblade_Haste` | `pdw_05_00_att_skill_stand_magic_r_00` | 가속. 시전 완료 후 6초간 달리기·질주 속도 15% 증가 |
| 지상 전투 G → 좌클릭 | `ShortDash` → `BasicCut01` | `pdw_01_01_att_shotdash_elbow_00` | 오른팔꿈치 돌진. 루트 키 180cm. 이동 입력 방향으로 돌진하고 타격 뒤 기본 3연타의 첫 타로 연계 |
| 전투 질주 + 좌클릭 | `SprintAttack` | `pdw_01_01_att_rundash_00` | 이동 입력 방향 돌진 공격, 280cm |
| 공중 좌·우클릭 | `AirLight` / `AirHeavy` | `pdw_05_00_att_jumpatt_00` / `pdw_05_01_att_skill_explosion_2jump_00` | 상체 포즈와 수평 루트 45/72cm, 기존 낙하·착지 유지 |
| 공중 V | `GroundSkill` | `pdw_01_01_att_skill_ground_att_01` | 공중 내려베기. 상체 타격과 원본 첫 0.20초의 수직 하강 약 102cm, Mover 착지 |
| 지상 전투 Caps+E → E 또는 좌클릭 | `AM_Longblade_Bash1`~`Bash4` | `pdw_01_01_att_skill_bash4combo_att_00`~`03` | 별도 4연격. 후속 입력이 없거나 마지막 타가 끝나면 복귀 포즈를 재생 |
| 지상 전투 Caps+Q → Q 또는 우클릭 | `AM_Longblade_Twin1`~`Twin2` | `pdw_05_01_att_skill_combo_01`~`02` | 2연격. Q로 양쪽 타를 실행할 수 있음 |
| 지상 전투 Caps+V | `AM_Longblade_GroundGush` | `pdw_05_00_att_skill_groundgush_00` | 원본처럼 짧게 디디고 제자리로 돌아오는 지면 강타. 검 궤적이 타격 범위를 결정 |
| Shift+우클릭 | `MovementDash` | `pdw_01_01_att_move_dash_ing_01` | 이동 전용 루트 모션 1,000cm. 입력이 없으면 몸 방향. 장애물이 있으면 Mover 충돌 처리 적용 |
| 전투 진입·해제 | `Draw` / `Sheathe` | `pdw_01_01_stand_weapon_out_00` / `pdw_01_01_stand_weapon_in_00` | 기존 `UpperBody` 슬롯, 이동 중 하체 보행 유지. 노티파이에서 같은 무기 액터를 손↔등으로 이동 |

## 미연결 후보와 다음 판단

| 후보 | 에셋 또는 원본 | 연결 전에 볼 점 |
| --- | --- | --- |
| R 홀드 차징 | `ChargeStart`, `ChargeLoop`, 활성 `ChargeRelease` | 홀드·해제·피격·장비 해제 시 실행 수명과 서버 판정을 먼저 설계 |
| 전용 점프 | `JumpUp`/`JumpLoop`/`JumpLand` 후보 | 점프는 현재 공용 GASP 경로 사용 |

## 이어서 작업할 때 유지할 계약

- 입력 조합은 직업별 커맨드 데이터, 가능한 후속 동작은 콤보 그래프에 둔다. 근접 GAS가 타격·연출·콤보 실행을 소유하고 Mover가 루트 이동을 실행한다. `DefaultSlot`은 지상 공격·이동 대시, `UpperBody`는 발도·납도와 공중 공격에 사용한다.
- 카메라는 자유롭게 움직이고, 몸 방향은 한 타의 실행 중 유지한다. 다음 타가 실제 시작되는 순간에 새 카메라 방향을 읽는다. 버퍼를 누른 순간의 방향은 저장하지 않는다. 질주 공격과 이동 대시는 이동 입력으로 방향을 잡는다.
- 기본 대검 세트와 공용 Master AnimBP의 참조를 보존한다. 새 변형은 Longblade 데이터와 기존 링크드 레이어 경로에서 먼저 해결한다. 검 각도는 `SK_Spellblade_longblade`의 `WeaponGrip_R_Aligned`와 칼끝 판정 오프셋을 함께 맞춘다. `aiguillette`는 검의 장식이며 독립 왼손 보조무기 메시가 아니다.
- [루트 제작 스크립트](../../../Scripts/Authoring/Refine-SpellbladeRootMotion.py)와 [복귀 구간 제작 스크립트](../../../Scripts/Authoring/Trim-SpellbladeRecovery.py)를 참고한다. R/T/질주/공중기의 긴 복귀를 이미 줄였다. 공격 도중 대시로 취소하는 별도 창은 아직 없다. 새 캔슬이 필요하면 타격·루트·콤보 소유권을 정리하고 기술별로 허용한다.
- 이후 네이티브 `Project_JSpellbladePolish` 적용에서 지상 공격 10개의 원본 꼬리를 되살려 자연 블렌드아웃을 늘렸다. 새 발차기는 Leader의 왼발 볼로 타격을 추적한다. 4연격의 전용 복귀는 마지막 몽타주의 두 번째 세그먼트에서 실행한다.
- 기본 1~3타, Q와 Q 후속, 우클릭 강공격, R, 질주 공격의 기존 시퀀스에서 타격·콤보 노티파이 이후 원본 복귀를 0.12~0.16초 더 읽는다. 두 번째 세그먼트의 속도는 1.24~1.52배이며, 블렌드아웃을 줄여 복귀 자세가 더 오래 보이게 했다. 몽타주 전체 길이, 노티파이 데이터, 복귀 전 루트 이동은 보존한다. T는 시험에서 복귀 손 이동 속도가 높아져 해당 시안을 적용하지 않았다.
- 정면 이동 대시는 전투 중 전방 입력을 유지하고 Shift+우클릭으로 시작한다. 방향은 발동 시점의 카메라 전방을 사용하고, 이동 종료 뒤에는 그 시점에 누르고 있는 이동·질주 입력으로 이어간다.
- 로컬 카메라는 실제 Mover 대시의 전진 속도와 수락된 질주 상태를 읽는다. 전투 기본 FOV는 85°, 질주 최대 91°, 대시 최대 96°다. 정지 상태에서 Shift만 누르면 기본 FOV를 사용한다. 대시 중 화면 가장자리는 최대 25% 불투명도의 비네트로 어두워지며, 카메라와 HUD가 같은 대시 가중치로 감쇠한다. HUD 강도는 `ProjectJ.UI.DashEdgeIntensity`에서 0~1로 조정한다. 렌더링 PIE에서 85° → 96° → 91° → 85° 전환을 확인했고, 마지막 25% 튜닝은 빌드로 확인했다.
- 단독 PIE에서 실제 입력으로 기본 단타와 1→2→3 연계, 우클릭, Q, R, Z 시전 완료와 Z 효과 적용을 확인했다. 정면 이동 대시의 화면은 진입 전, 이동 중, 종료 후에 캡처했다. 벽에 막힌 표본에서는 몽타주 0.223초에 이동 속도가 0cm/s가 됐다. 몽타주 원본과 측정 자료는 중앙 보관 규칙에 따라 `SpellbladeRecoveryTails` 이름으로 찾을 수 있다.
- 지상 공격 10개의 전용 가드 복귀 및 상체 인계는 시각 검토용으로 시험했다. 현재는 적용 직전 몽타주를 복원해 해당 공격의 기존 시퀀스와 블렌드아웃을 사용한다. E 4타 전용 복귀와 Z/X/C 시전 레이어는 각각 기존 경로를 사용한다.
- 새 기술은 몽타주 Details의 Meta Data에 `Project_JMontageLocomotionHandoff`를 추가할 수 있다. 전신 공격은 `RecoveryStart`를 타격·연출과 루트 키가 모두 끝난 시점으로 잡고, `RecoveryBlendTime`으로 하체 전환을 조절한다. 기존 콤보 입력 창은 유지할 수 있다. 공격 데이터의 에셋 검증에서 타격·연출 창과 겹치거나 남아 있는 루트 이동을 자르는 설정을 오류로 표시한다. 단일 섹션으로 끝나는 몽타주에만 이 경로를 쓴다.
- Z/X/C는 기존 `CastUpperBody` 슬롯의 같은 재생 위치를 사용한다. 발이 안정된 원본 구간에만 `StationaryPoseWindow`와 `StationaryPoseWeight`를 저작한다. 시전 중 이동 입력이나 실제 이동으로 하체를 넘겼으면, 그 시전이 끝날 때까지 모션매칭이 하체를 맡는다. 멈출 때 원본 시전 발자세가 다시 들어오면서 발의 궤적을 바꾸는 현상을 방지한다. 시전 타이밍과 버프 권위는 기존 GAS가 유지한다. 메타데이터 없는 캐릭터와 몽타주의 새 가중치는 0이다.
- 세 버프 공격 정의는 `Use Locomotion Facing`을 사용한다. 이 옵션이 켜진 동작은 직업의 공격 방향 고정을 상속하지 않고, 기존 이동 입력과 카메라에 따른 방향 분류를 사용한다.
- 시각 품질 점검에서 C 시작과 E 4타 복귀의 손목 회전 피크를 원본에서도 재현했다. 해당 시퀀스의 손목 회전만 정리하고, X/C와 T는 최종 팔 자세를 기존 전투 가드에 가깝게 저작했다. 원본 시퀀스 길이, 루트와 몽타주 노티파이는 보존한다. PIE 자세 표본과 24인 동시 평가 기록은 중앙 보관소의 `SpellbladeQuality` 항목에서 찾는다.
- 적용한 몽타주의 공격 정의 13개가 에디터 검증을 통과했다. 단독 PIE의 Z/X/C 이동 전환과 각각의 효과 적용, E 4연격, 새 복귀가 연결된 공격의 진입·인계를 확인했다. 최종 빌드의 전용 서버 PIE에서는 기본 1타가 종료되기 전에 서버와 클라이언트 모두 이동했고, Z 완료 효과는 서버에서 한 번 적용됐다. 대검 마네킹의 기본 공격과 가중치 0도 확인했다. 원본과 진단 자료는 중앙 보관소의 `SpellbladeActionHandoff` 항목에 연결한다.
- 이후 플레이 검토에서 새 가드 복귀 자세가 어색해 `ReturnToGuard`가 연결된 공격 몽타주 10개를 적용 직전 백업으로 복원했다. 해당 몽타주의 복귀 시퀀스 연결과 상체 인계 메타데이터도 함께 되돌렸다. 생성했던 시퀀스 10개의 복원 경로와 SHA-256은 `Saved/StorageCleanup_20261104/ArchiveLocations.json`에 기록했다.
- `Project_JSpellbladePolish -ApplyBuffs` 적용에서 4연격 진입을 E로 옮기고 Z/X/C 상체 시전을 추가했다. 원본의 시전·유지·복귀를 이어 재생하고 자연 종료 때 서버가 Gameplay Effect를 적용한다. 같은 효과가 활성 상태면 재시전을 차단한다.
- `Project_JSpellbladePolish -ApplyExtraAttacks`는 기존 팔꿈치 돌진과 내려찍기 에셋을 G와 공중 V에 연결했다. 커맨드 해석, 콤보 선택, 상호작용과 조합 입력은 동일한 Mover 공중 상태를 기준으로 조건을 판단한다.
- `Project_JSpellbladePolish -ImportExpansion`과 `-ApplyExpansion`은 원본 7개를 현재 몸체의 `DirectBody/Expansion`에 리타깃하고 Longblade 입력·커맨드·콤보 데이터에 연결한다. 게임은 Q/E/V를 누른 시점의 물리 Caps Lock 상태로 입력을 한 번 선택하고 같은 입력으로 해제를 처리한다. Alt 커서 전환 중에도 키보드만으로 두 콤보를 완료할 수 있다. 텍스트 입력창이 키보드 포커스를 가지면 해당 위젯이 입력을 처리한다.
- 배시는 지상 전투에서 Caps Lock을 누른 채 E를 입력해 시작한다. 활성 배시의 후속 타는 E 또는 좌클릭을 사용한다. 저장된 매핑에서 Caps 전용 시작 키는 E·Q·V다.
- 2026-11-05 전용 서버 PIE에서 실제 키로 Caps 지면 강타, 커서 표시 중 Bash 1→4와 Twin 1→2, 게임 입력 중 좌·우클릭 연계와 Caps+Q 반복을 확인했다. 중간 타에 멈추거나 마지막 타가 끝나면 양쪽의 공격 상태가 해제됐다. Caps 단독·납도 후 Caps+E·공중 Caps+V에는 공격이 시작되지 않았고, 일반 Q/E와 공중 V는 기존 노드를 재생했다. E가 Caps보다 먼저 들어오는 빠른 입력은 기존 E로 처리했다.
- 후속 `Project_JSpellbladePolish -ApplyAttackTravel`은 Caps 7개 시퀀스와 E 4타·전용 복귀에 원본 수평 루트를 적용한다. E 1~3타는 이미 캡슐을 움직이던 루트 키에 공격 데이터 정책을 맞췄다. 새 루트와 고정 메시 포즈를 합친 결과를 원본과 비교한 표본에서 양발·골반·오른손의 최대 위치 차이는 0.11cm였다. 지면 강타는 마지막 이동 키가 재생된 뒤 블렌드아웃하도록 조정했다. 평지 전용 서버 PIE에서 지면 강타 2회 종료의 시작점 편차는 회당 약 1~2.3cm였고, 실제 Caps Bash·Twin과 E Flurry 전타 및 복귀, F의 제자리 발차기를 확인했다. 복귀를 끝낸 위치와 복귀 도중 최대 이동 거리는 원본처럼 다를 수 있다.
- 후속 PIE 품질 검증에서는 Bash 1·2타와 Twin 1타의 타격 이후 일부 손목 키만 다듬었다. 원본 시퀀스 길이와 루트 키를 검증했고, Caps Bash 4연격과 1·2·3타 단독 종료, Twin 2연격과 1타 종료, 지면 강타를 재현했다. T의 쇄골 및 팔 타이밍 시안은 왼손 재접촉을 개선하지 못해 복원했다. T는 기존 자세에서 손이 접근하는 동안 최대 약 16cm 떨어졌고, 그립 가중치 0.9 이상 표본은 약 0.21cm 이내였다. 원본과 비교 자료는 중앙 보관소의 `SpellbladeCaps` 항목에 있다.
- `-ApplyDowncutTravel`은 공중 V의 원본 첫 0.20초에서 하강 102cm를 분리한다. 새 시퀀스의 복귀 루트는 0이며, 몽타주 메타데이터가 하강 시간만 기존 Mover 레이어에 전달한다. 전용 서버 PIE에서 실제 V를 점프의 두 시점에 눌러 재생과 수직 이동을 확인했다. 낮은 경우 약 0.15초에 착지했고, 높은 경우 루트 종료 뒤 자연 낙하가 이어져 약 0.28초에 착지했다. 둘 다 서버와 클라이언트가 Walking과 공격 해제로 복귀했다.
- 후속 공중 V 점검에서 Post Process를 꺼도 복귀 때 왼손이 크게 움직였다. `AM_SpellbladeKit_GroundSkill`의 두 번째 세그먼트를 원본 0.32–2.20초, 약 2.07배속으로 저작했다. 기존 4.5배속 구간 후반의 별도 팔 스윙을 잘라냈다. 전체 길이 약 1.227초, 첫 0.32초의 타격 포즈, 기존 노티파이, 하강 0.20초는 보존한다. 시퀀스 120Hz 표본에서 복귀 중 왼손의 상완 기준 최대 이동 속도는 1,291→219cm/s였다. `UProject_JMontageVerticalMotion.bAirborneOnly`가 설정된 몽타주는 Falling이 끝나는 시점에 수직 루트 모션을 넘긴다. 일반 점프 발사 속도는 `BP_Spellblade` 클래스 기본값의 **Movement → Jump → Jump Launch Speed**에서 612.37cm/s로 설정한다.
- 단독 PIE에서 점프 상승량은 121.4→179.6cm였다. 같은 입력 시점의 V 복귀에서 머리 기준 왼손 최대 이동 속도는 688→229cm/s였다. 전용 서버 PIE에서 일반 점프 상승량은 서버·클라이언트 모두 182.5cm였다. 네 가지 공중 입력 시점에서 양쪽 V 재생과 평지 착지를 확인했고, 마지막 표본의 높이 차이는 네 경우 모두 0.01cm 미만이었다. 높은 곳에서 쓰면 0.20초의 저작 하강 뒤 자연 낙하가 이어진다. 평지 입력·자세 검증으로, 정점 V의 지상 적 피해와 경사 착지는 별도 검증 범위다.
- 전용 서버 PIE에서 실제 E로 4타까지 이어졌고 Z/X/C는 각 서버 효과가 적용됐다. 같은 조건의 평지 가속 비교에서 이동 구간 평균 속도가 약 478→550cm/s였다. 실제 G의 평지 전진은 양쪽 약 180cm였고, 실제 좌클릭으로 기본 1타에 이어졌다. 공중 V는 두 번 모두 양쪽에서 몽타주를 재생했고, 지상에서는 공중 노드에 진입하지 않았다.
- 현재 제작 명령은 `Project_JSpellbladePolish -ApplyStance -FbxRoot=<원본 폴더>`와 `-ApplyGuardLayer`다. 실제 몸체로 변환한 원본 `Idle00` 양손 준비 루프와 방향별 이동 루프를 사용한다. 아래 `Idle02`·`-RefineGuard` 수치는 이전 제작 이력이다. 현재 포즈와 Z/X/C의 단일 시전 테이크는 [전투 제작 가이드](Spellblade_CombatKit.md)를 따른다.
- 공용 팔 리타깃을 비교하려면 `Project_JSpellbladePolish -AuditSharedArmRetarget -FbxRoot=<원본 폴더>`를 실행한다. 원본과 DirectKit 재출력에서 팔 길이축 회전을 표본 추출하며 에셋을 저장하지 않는다. `-TrialResetArmAlignment`는 복제본의 상완·전완·손 정렬만 초기화한다. 첫 Idle02 프레임의 왼손목 회전은 각 메시 기준 포즈 대비 원본 123.3°, 기본 리타깃 147.3°였다. 정렬 초기화는 후진 왼쪽 전완의 중앙값을 19°에서 89°로 키워 적용하지 않았다. 탑승 포즈는 `ABP_Rider_Dragon`의 `ANIM_Rider_Idle` 경로에서 별도로 검증한다.
- `-TrialArmJointMapping`은 리타기터 복제본에서 양쪽 3관절 팔 체인을 일대일로 전달한다. Idle02 첫 프레임의 비틀림은 같았다. `-BakeArmRollTrial`은 새 리타깃 사본에서 전완과 손목의 반대 방향 축 회전 중 겹치는 부분을 줄이고, 손의 컴포넌트 변환을 전 키에서 비교한다. 결과 17개는 `Sequences/SourceTransient/ArmRollTrial`에 저장했다. 게임 참조는 기존 에셋을 사용한다. 같은 방향의 회전과 몸체 Post Process 결과는 별도 검증 대상이다.
- 이전 제작 자세의 첫 교체에서는 검이 바닥으로 향하고 왼팔이 바깥으로 돌아갔다. `-RefineGuard`에서 양쪽 팔꿈치와 손 위치를 함께 다시 작성하고, 오른손은 기존 손·검 소켓의 관계를 써서 칼끝을 앞쪽 대각선 위로 향하게 했다. 왼손은 팔뚝을 따라 내려가며 손바닥이 몸 쪽을 향한다. `-VerifyGuard`의 저장본 표본에서 검끝은 그립보다 정지에서 25cm 이상, 이동에서 37cm 이상 높았다.
- 실제 플레이에서 팔이 다시 꺾이는 원인은 전투 전용 `CombatUpperBody`가 기본 흉추에 다른 몸통 기준의 로컬 팔 회전을 합친 것이었다. 양팔의 쇄골 필터 깊이도 서로 달랐다. `-ApplyGuardLayer`로 몸통·쇄골은 기본 포즈가 담당하게 하고, 양쪽 상완부터 같은 가중치로 메시 공간 회전을 적용했다. 전용 서버 PIE의 전투 정지 표본에서 저장 자세 대비 오른손 회전·어깨 기준 위치 오차는 52.8°/38.7cm에서 4.2°/2.0cm로, 왼손은 73.7°/16.4cm에서 4.1°/3.0cm로 줄었다. 후진·후진 후 정지·Q 공격 복귀·납도도 확인했다. 양팔과 상체 17본의 값은 `Saved/SpellbladeCombatNext/GuardPoseComparison.md`에 있다.
- 위 오차는 저장 자세의 재현 여부만 측정했다. 후속 `-RefineGuard`는 비전투 정지 루프를 양팔의 축 회전 기준으로 삼고 오른팔 전완과 손에만 검 자세에 필요한 회전을 나눠 준다. 전용 서버 PIE에서 비전투 손 회전과 비교한 전완 길이축 기준 손목의 추가 비틀림 절댓값 중앙값은 오른쪽 160.9°에서 8.5°, 왼쪽 122.7°에서 7.0°로 줄었다. 오른손의 어깨 대비 높이는 −29.5cm에서 −29.9cm, 왼손은 −40.4cm에서 −39.9cm였다. 로그는 `Saved/SpellbladeCombatNext/GuardAxialPIE.log`에 있다.
- 이전 로컬 PIE 23개와 관련 자동화 4개에서 정지·W/A/D 발도/납도, 발도 중단 복구, 카메라를 돌린 3연타, 대시, 공중 착지, 후딜 후 대시를 확인했다. 후속 전용 서버 PIE는 실제 T/F/Z 입력으로 양쪽의 피니셔, 발차기, 4연격 1→4를 확인했다. 납도 후 F에는 발차기가 시작되지 않았고, 평지 Shift+우클릭 키 입력에서 서버와 클라이언트는 각각 약 1,009cm 이동했다. 우선 남은 검증은 신규 스킬별 피해와 시각 접촉, 후진 방향 경계의 손목 프레임, 벽·경사에서의 루트 이동이다. 수치와 세부 제작 기준은 [전투 제작 가이드](Spellblade_CombatKit.md)에 있다.
