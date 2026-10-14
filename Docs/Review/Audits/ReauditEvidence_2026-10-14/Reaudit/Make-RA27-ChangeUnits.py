from pathlib import Path
import json
out=Path(__file__).resolve().parent
changes=json.loads((out/'RA27SourceChangeManifest.json').read_text(encoding='utf-8-sig'))
units=[
 ('RA01','실제 ASC delegate lease·FieldNotify 재진입/GC','Project_JCharacterUIBindingComponent','CharacterUIBindingReentry'),
 ('RA02/10','전투 시각 retirement·Niagara 소유권·새 play admission','Project_JCombatPresentationComponent','CombatPresentationRetirement,ComponentPlayReuse'),
 ('RA03','미니맵 재구성·공통 아이콘 취소/세대','Project_JMinimapWidget','MinimapReconstruction'),
 ('RA04','제작/런타임 이동 수치 계약·CMC tuple publication','Project_JLocomotionProfile,Project_JAnimationProfileValidation,Project_JPlayerCharacter.Movement','MovementDataValidation,MovementPublication'),
 ('RA05/10','탑승 전이·실제 ASC 태그·현재 rider 복원 소유권','Project_JMountComponent,Project_JMountCharacter','MountTagReentry,ComponentPlayReuse'),
 ('RA06','인벤토리 표시의 현재 source 세대','Project_JInventoryViewModel','InventoryProjectionGeneration'),
 ('RA07','UI 저장 public admission·private final flush','Project_JUILayoutSettings','UIPreferenceRetirement'),
 ('RA08','follower 역할의 공통 resolver·중복 탐색/캐시 제거','Project_JCharacterAnimInstanceBase,Project_JPresentationMeshResolver','FollowerIdentityReplacement,AuthoredFollowerRole'),
 ('RA09','Super 전용 override와 미사용 주석 제거','Project_JModularMeshComponent','기존 inheritance와 전체 회귀; 별도 버그 재현 없음'),
 ('RA11','enabled ground contact 공통 숫자 계약','Project_JWeaponPresentationProfile,Project_JWeaponPresentationComponent','GroundContactDataValidation'),
 ('RA12','검증 child process handle/nonnull exit 기록','Measure-AuthoredAnimation,Measure-Network,Validate-CookedUI','현재 실제 UI·원격·AuthoredCPU 실행'),
 ('RA13','단발 전이 상태의 동일 판정 복사본 제거','Project_JAnimationPresentationHelpers,Project_JCharacterAnimInstance.Presentation','기존 단발/통합/전체 native 회귀'),
 ('RA14','Mass step 완료 이후 소유권/저장소 반환·불필요 의존 정리','Project_JMassRepresentationSubsystem,Project_JPlayerMaturityFixtures','MassReentrantRetirement'),
 ('RA15','새 NPC play의 scoring/action/activation admission','Project_JNPCActivationComponent,Project_JNPCActionComponent,Project_JTargetScoringComponent','NPCPlayReuse'),
 ('RA16','새 player play의 실제 입력/GAS/modifier admission','Project_JPlayerInputBindingComponent,Project_JSkillInputExecutionComponent,Project_JSkillInputRouterComponent','PlayerInputPlayReuse'),
 ('RA17','새 combat Intro와 mount actor admission','Project_JCombatIntroComponent,Project_JMountCharacter','CombatIntroPlayReuse,MountActorPlayReuse'),
 ('RA25','새 Controller UI/Quest admission·진행 보존','Project_JPlayerUIComponent,Project_JQuestComponent','ControllerUIPlayReuse,QuestPlayReuse'),
 ('RA20','Mass active GT owner/NPC GC lease','Project_JMassRepresentationSubsystem','MassReentrantRetirement; strict processor-root release'),
 ('RA21','Bounded GT subsystem/completion participant GC lease','Project_JTargetScoringSubsystem,Project_JVisualAssetSubsystem,Project_JNPCPathSubsystem,Project_JNPCDecisionSubsystem,Project_JPresentationBudgetSubsystem','SubsystemCallbackGC; existing async/path/decision/presentation regressions'),
 ('RA22','Unused native MM cadence accessors removed','Project_JCharacterAnimInstance,Project_JCharacterAnimInstance.Budget','Native build and existing cadence/whole tests; no perf claim'),
 ('RA23','EditorOnly toolsets excluded in game/server validation host','Measure-Network','Reaudit_Network7-9; normal Editor/MCP retained'),
 ('RA24','Authored early-transition sole clip/revision permission owner','Project_JCharacterAnimInstance,Project_JCharacterAnimInstance.Snapshot','AuthoredConditionAndOwnership; existing one-shot/whole tests'),
 ('RA25','Unused direct GAS activation loop removed','Project_JAbilitySystemComponent','Existing native input/GAS/combat and real remote/package fixtures'),
 ('RA26','Game/server Python startup excluded without changing normal MCP','Measure-Network','Reaudit_Network10-12; raw startup logs checked'),
 ('RA27','Read-only production comparison uses values instead of derived-index UObject clone','Project_JForwardStrafeDataTests','ForwardIndexFixed1-3; canonical70 assets/5288 poses; WholeRefinement2'),
]
result=[]
for unit,reason,stems,tests in units:
    files=[r for r in changes if Path(r['path']).stem in stems.split(',')]
    result.append(dict(unit=unit,reason_ko=reason,files=files,regression_scope=tests))
payload=dict(baseline='648개 텍스트+9개 Editor artifact, DS01–18 이후/RA 이전, 사용자 변경 포함',
    frozen_final='650개 텍스트+9개 Editor artifact, RA01–27',
    units=result,new_regression_files=[r for r in changes if r['status']=='added'],
    asset_unit=dict(unit='RA18',reason_ko='MCP 참조/dirty/빈 정의 확인 뒤 정확한 두 중복 placeholder 백업·삭제',
                    records='EmptyEquipmentCleanupBefore.json; EmptyEquipmentCleanupPreservation.json; DataAssetValidationSummary.json'),
    limitation_ko='같은 파일의 RA02/05와 RA10은 같은 lifetime 계약 보완이므로 함께 리뷰한다. 테스트는 명명된 계약 범위이며 전체 무결성 보증이 아니다. 커밋은 하지 않았다.')
(out/'RA27ChangeUnits.json').write_text(json.dumps(payload,ensure_ascii=False,indent=2),encoding='utf-8')
covered={r['path'] for u in result for r in u['files']}
unmapped=[r['path'] for r in changes if r['status']!='added' and r['path'] not in covered]
print(json.dumps(dict(units=len(result),modified_files_mapped=len(covered),unmapped=unmapped)))
if unmapped:raise RuntimeError('Change unit mapping incomplete.')

