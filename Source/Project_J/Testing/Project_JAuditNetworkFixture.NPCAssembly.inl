// CLI-only native NPC component creation, late-join recovery and actor-lifetime reuse.
// No client component creation, property injection, attack, montage or damage producer.
FProject_JReplicatedCombatPresentationState AProject_JAuditNetworkController::ReadNPCAssemblyState(
 AProject_JNPCCharacter* NPC,int32& OutPending,int32& OutNiagara) const
{
 OutPending=OutNiagara=0;
 if(!AuditNPCAssemblyEnabled()||!HasAuthority()||!IsValid(NPC)||NPC->IsActorBeingDestroyed()||NPC->GetWorld()!=GetWorld())return {};
 auto* C=NPC->FindComponentByClass<UProject_JCombatPresentationComponent>();
 if(!IsValid(C)||C->IsBeingDestroyed())return {};
 OutPending=C->PendingLoopingCues.Num();OutNiagara=C->ActiveLoopingCues.Num();return C->ReplicatedPresentationState;
}

void AProject_JAuditNetworkController::ClientNPCAssemblyStep_Implementation(int32 InStep,AProject_JNPCCharacter* NPC,uint32 Instance,int32 Order)
{
 if(!AuditNPCAssemblyEnabled()||HasAuthority()||InStep<0||InStep>5||InStep<NPCAssemblyStep)return;
 if(IsValid(NPC)&&((NPCAssemblyIdentity.IsValid()&&NPCAssemblyIdentity.Get()!=NPC)
  ||(NPCAssemblyActorId&&!NPCAssemblyIdentity.IsValid())))
 {ServerObserve(70,InStep,false,TEXT("NPC assembly control changed actual actor reference"));return;}
 const bool bNewStep=InStep!=NPCAssemblyStep;
 if(!bNewStep&&bNPCAssemblyRecorded)return; // Identity was validated above; never duplicate a recorded row.
 const bool bNewBinding=IsValid(NPC)&&!NPCAssemblyIdentity.IsValid();
 const bool bWasBound=NPCAssemblyIdentity.IsValid();
 if(bNewBinding)NPCAssemblyIdentity=NPC;
 if(Case!=70)
 {
  Case=70;NPCAssemblyRows=TEXT("case,step,slot,mode,role,npc_id,owner,frame,npc_present,hit_count,presentation_count,hit_id,presentation_id,registered,begun,replicated,instance,revision,order,warning_count,marker,render_state,lines,tick,attached,profile_null,pending_loops,active_niagara,passed\n");
 }
 FParse::Value(FCommandLine::Get(),TEXT("ProjectJAuditSlot="),Slot);
 NPCAssemblyStep=InStep;NPCAssemblyInstance=Instance;NPCAssemblyOrder=Order;
 if(bNewStep)
 {NPCAssemblyStableFrame=MAX_uint64;bNPCAssemblyRecorded=false;ObserveStarted=FPlatformTime::Seconds();}
 if(bNewStep||bNewBinding)
 {
  int32 NativeNPCs=0;bool bCapped=false;
  for(TActorIterator<AProject_JNPCCharacter> It(GetWorld());It;++It)
  {
   if(It->IsActorBeingDestroyed()||It->GetClass()!=AProject_JNPCCharacter::StaticClass())continue;
   if(NativeNPCs==8){bCapped=true;break;}++NativeNPCs;
  }
  UE_LOG(LogProjectJAuditNetwork,Display,TEXT("NPC_ASSEMBLY_CONTROL step=%d slot=%d param=%s param_valid=%d bound_before=%d new_binding=%d native_npcs=%d capped=%d"),
   InStep,Slot,*GetNameSafe(NPC),int32(IsValid(NPC)),int32(bWasBound),int32(bNewBinding),NativeNPCs,int32(bCapped));
 }
}

void AProject_JAuditNetworkController::ObserveNPCAssemblyAudit()
{
 if(NPCAssemblyStep==INDEX_NONE||bNPCAssemblyRecorded)return;
 auto* NPC=NPCAssemblyIdentity.Get();const bool Alive=IsValid(NPC)&&!NPC->IsActorBeingDestroyed();
 auto* Hit=Alive?NPC->FindComponentByClass<UProject_JCombatHitValidationComponent>():nullptr;
 auto* C=Alive?NPC->FindComponentByClass<UProject_JCombatPresentationComponent>():nullptr;
 TInlineComponentArray<UProject_JCombatHitValidationComponent*> Hits;
 TInlineComponentArray<UProject_JCombatPresentationComponent*> Presentations;
 if(Alive){NPC->GetComponents(Hits);NPC->GetComponents(Presentations);}
 const uint64 Id=Alive?AuditMountPublicId(NPC):0;
 bool Binding=Alive&&NPC->HasActorBegunPlay()&&NPC->GetWorld()==GetWorld()&&GetNetMode()==NM_Client
  &&NPC->GetLocalRole()==ROLE_SimulatedProxy&&!NPC->IsLocallyControlled()&&Id;
 if(Binding&&!NPCAssemblyActorId){NPCAssemblyActorId=Id;NPCAssemblyOwner=NPC->GetName();}
 Binding=Binding&&Id==NPCAssemblyActorId&&NPCAssemblyOwner==NPC->GetName();
 const bool Pair=IsValid(Hit)&&!Hit->IsBeingDestroyed()&&IsValid(C)&&!C->IsBeingDestroyed()&&Hits.Num()==1&&Presentations.Num()==1
  &&Hit->GetOwner()==NPC&&C->GetOwner()==NPC&&Hit->GetWorld()==GetWorld()&&C->GetWorld()==GetWorld();
 if(Pair&&!NPCAssemblyHitId)
 {NPCAssemblyHit=Hit;NPCAssemblyPresentation=C;NPCAssemblyHitId=Hit->GetUniqueID();NPCAssemblyPresentationId=C->GetUniqueID();}
 const bool SamePair=Pair&&NPCAssemblyHit.Get()==Hit&&NPCAssemblyPresentation.Get()==C
  &&Hit->GetUniqueID()==NPCAssemblyHitId&&C->GetUniqueID()==NPCAssemblyPresentationId;
 const bool Registered=Pair&&Hit->IsRegistered()&&C->IsRegistered();
 const bool Begun=Pair&&Hit->HasBegunPlay()&&C->HasBegunPlay();
 const bool Replicated=Pair&&Hit->GetIsReplicated()&&C->GetIsReplicated();
 auto* M=IsValid(C)?C->WarningMarker.Get():nullptr;const bool Marker=IsValid(M)&&!M->IsBeingDestroyed();
 if(Marker)NPCAssemblyMarker=M;
 const bool Render=Marker&&M->IsRegistered()&&M->IsRenderStateCreated();
 const int32 Lines=Marker?M->BatchedLines.Num():0;
 const bool Tick=Marker&&M->IsComponentTickEnabled();
 const bool Attached=Marker&&M->GetOwner()==NPC&&M->GetWorld()==GetWorld()&&M->GetAttachParent()==NPC->GetRootComponent()
  &&M->IsA<UProject_JGameplayWarningMarkerComponent>();
 const bool ProfileNull=!IsValid(C)||!C->BasePresentationSet;
 const int32 PendingLoops=IsValid(C)?C->PendingLoopingCues.Num():0,Niagara=IsValid(C)?C->ActiveLoopingCues.Num():0;
 const int32 Warnings=IsValid(C)?C->ActiveWarningCues.Num():0;
 const int32 Revision=IsValid(C)?C->LastAppliedPresentationRevision:0,Order=IsValid(C)?C->LastAppliedRecoveryEventOrder:0;
 const uint32 Instance=IsValid(C)?C->ReplicatedPresentationState.AttackInstance:0;
 const bool Active=NPCAssemblyStep==1||NPCAssemblyStep==3;
 const bool Meaning=Active?(Warnings==1&&C&&C->ActiveWarningCues.HasTagExact(AuditCueA)&&Marker&&Render&&Lines==20&&!Tick&&Attached)
  :(Warnings==0&&!Marker);
 const bool State=NPCAssemblyStep==0||NPCAssemblyStep==5||(Instance==NPCAssemblyInstance&&Order>=NPCAssemblyOrder&&Revision>0);
 const bool Shape=NPCAssemblyStep==0?(Binding&&Hits.IsEmpty()&&Presentations.IsEmpty()):
  (NPCAssemblyStep==5?(!Alive&&NPCAssemblyActorId&&!NPCAssemblyHit.IsValid()&&!NPCAssemblyPresentation.IsValid()&&!NPCAssemblyMarker.IsValid()):
   (Binding&&SamePair&&Registered&&Begun&&Replicated&&!Hit->PrimaryComponentTick.bCanEverTick&&!C->PrimaryComponentTick.bCanEverTick));
 const bool Complete=Shape&&Meaning&&State&&ProfileNull&&PendingLoops==0&&Niagara==0;
 if(Complete)
 {
  if(NPCAssemblyStableFrame==MAX_uint64){NPCAssemblyStableFrame=GFrameCounter;return;}
  if(GFrameCounter<=NPCAssemblyStableFrame)return;
  if(!bNPCAssemblyPacketsLogged)
  {
#if DO_ENABLE_NET_TEST
   auto* Driver=GetWorld()->GetNetDriver();if(!Driver){ServerObserve(70,NPCAssemblyStep,false,TEXT("NPC assembly client NetDriver missing"));bNPCAssemblyRecorded=true;return;}
   const auto& Sim=Driver->PacketSimulationSettings;
   UE_LOG(LogProjectJAuditNetwork,Display,TEXT("NPC_ASSEMBLY_PACKETS slot=%d lag=%d variance=%d loss=%d order=%d duplicate=%d netmode=%d"),Slot,Sim.PktLag,Sim.PktLagVariance,Sim.PktLoss,Sim.PktOrder,Sim.PktDup,int32(GetNetMode()));
#endif
   bNPCAssemblyPacketsLogged=true;
  }
  if(NPCAssemblyOwner.Contains(TEXT(","))||NPCAssemblyOwner.Contains(TEXT("\n"))){ServerObserve(70,NPCAssemblyStep,false,TEXT("NPC assembly CSV actor name invalid"));bNPCAssemblyRecorded=true;return;}
  TArray<FString> Cells;const auto Number=[&](auto Value){Cells.Add(LexToString(Value));};
  Number(70);Number(NPCAssemblyStep);Number(Slot);Number(int32(GetNetMode()));Number(Alive?int32(NPC->GetLocalRole()):0);Number(NPCAssemblyActorId);Cells.Add(NPCAssemblyOwner);Number(GFrameCounter);
  Number(int32(Alive));Number(Hits.Num());Number(Presentations.Num());Number(NPCAssemblyHitId);Number(NPCAssemblyPresentationId);
  Number(int32(Registered));Number(int32(Begun));Number(int32(Replicated));Number(Instance);Number(Revision);Number(Order);Number(Warnings);
  Number(int32(Marker));Number(int32(Render));Number(Lines);Number(int32(Tick));Number(int32(Attached));Number(int32(ProfileNull));Number(PendingLoops);Number(Niagara);Number(1);
  NPCAssemblyRows+=FString::Join(Cells,TEXT(","))+TEXT("\n");bNPCAssemblyRecorded=true;
  ServerObserve(70,NPCAssemblyStep,true,TEXT("Actual native NPC/pair/warning state observed over two PlayerTicks"));
 }
 else
 {
  NPCAssemblyStableFrame=MAX_uint64;
  if(FPlatformTime::Seconds()-ObserveStarted>8)
  {bNPCAssemblyRecorded=true;ServerObserve(70,NPCAssemblyStep,false,FString::Printf(TEXT("NPC assembly unresolved alive=%d binding=%d pair=%d same=%d counts=%d/%d registered=%d begun=%d replicated=%d instance=%u/%u revision=%d order=%d/%d warnings=%d marker=%d render=%d lines=%d pending=%d niagara=%d"),Alive,Binding,Pair,SamePair,Hits.Num(),Presentations.Num(),Registered,Begun,Replicated,Instance,NPCAssemblyInstance,Revision,Order,NPCAssemblyOrder,Warnings,Marker,Render,Lines,PendingLoops,Niagara));}
 }
}

bool AProject_JAuditNetworkGameMode::NPCAssemblyCurrent() const
{
 auto* NPC=NPCAssemblyActor.Get();auto* ASC=NPCAssemblyASC.Get();
 return !bFinished&&IsValid(NPC)&&!NPC->IsActorBeingDestroyed()&&NPC->HasActorBegunPlay()&&NPC->HasAuthority()&&NPC->GetWorld()==GetWorld()
  &&GetWorld()&&!GetWorld()->bIsTearingDown&&IsValid(ASC)&&!ASC->IsBeingDestroyed()&&ASC->IsRegistered()&&ASC->GetOwner()==NPC
  &&NPC->GetAbilitySystemComponent()==ASC&&ASC->GetOwnerActor()==NPC&&ASC->GetAvatarActor()==NPC&&!ASC->IsActorInfoChanging()&&ASC->GetActorInfoRevision()==NPCAssemblyEpoch;
}

bool AProject_JAuditNetworkGameMode::BeginNPCAssemblyWarning()
{
 const TStrongObjectPtr<AProject_JNPCCharacter> NPC(NPCAssemblyActor);
 if(!NPCAssemblyCurrent()||NPC->HasCombatActions()||!NPC->StartCombatActions(NPCAssemblyDefinition,1)||!NPCAssemblyCurrent()||!NPC->HasCombatActions())return false;
 auto* Hit=NPC->FindComponentByClass<UProject_JCombatHitValidationComponent>();auto* C=NPC->FindComponentByClass<UProject_JCombatPresentationComponent>();
 if(!IsValid(Hit)||!IsValid(C))return false;
 if(!NPCAssemblyHitId){NPCAssemblyHit=Hit;NPCAssemblyPresentation=C;NPCAssemblyHitId=Hit->GetUniqueID();NPCAssemblyPresentationId=C->GetUniqueID();NPCAssemblyController=NPC->GetController();}
 if(NPCAssemblyHit.Get()!=Hit||NPCAssemblyPresentation.Get()!=C||NPCAssemblyController.Get()!=NPC->GetController())return false;
 const TStrongObjectPtr<UProject_JCombatPresentationComponent> KeepC(C);
 C->BeginAttackPresentation(AuditNodeA);
 if(!NPCAssemblyCurrent()||NPCAssemblyPresentation.Get()!=C||C->IsBeingDestroyed())return false;
 const uint64 Previous=NPCAssemblyLease;NPCAssemblyLease=C->BeginCueLease(AuditCueA,true);
 return NPCAssemblyCurrent()&&NPCAssemblyLease&&NPCAssemblyLease!=Previous&&C->IsCueLeaseCurrent(NPCAssemblyLease)&&RecordNPCAssemblyAuthority();
}

bool AProject_JAuditNetworkGameMode::EndNPCAssemblyWarning()
{
 const TStrongObjectPtr<AProject_JNPCCharacter> NPC(NPCAssemblyActor);
 const TStrongObjectPtr<UProject_JCombatPresentationComponent> C(NPCAssemblyPresentation.Get());
 if(!NPCAssemblyCurrent()||!C.IsValid()||!C->IsCueLeaseCurrent(NPCAssemblyLease))return false;
 C->EndAttackPresentation(); // This fixture owns the manual warning; the helper does not own a foreign presentation lease.
 if(!NPCAssemblyCurrent()||C->IsBeingDestroyed()||C->IsCueLeaseCurrent(NPCAssemblyLease))return false;
 NPC->StopCombatActions();
 return NPCAssemblyCurrent()&&!NPC->HasCombatActions()&&RecordNPCAssemblyAuthority();
}

bool AProject_JAuditNetworkGameMode::RecordNPCAssemblyAuthority()
{
 auto* NPC=NPCAssemblyActor.Get();const bool Alive=IsValid(NPC)&&!NPC->IsActorBeingDestroyed();
 auto* Hit=Alive?NPC->FindComponentByClass<UProject_JCombatHitValidationComponent>():nullptr;
 auto* C=Alive?NPC->FindComponentByClass<UProject_JCombatPresentationComponent>():nullptr;
 TInlineComponentArray<UProject_JCombatHitValidationComponent*> Hits;TInlineComponentArray<UProject_JCombatPresentationComponent*> Presentations;
 if(Alive){NPC->GetComponents(Hits);NPC->GetComponents(Presentations);}
 if(Alive&&!NPCAssemblyActorId){NPCAssemblyActorId=AuditMountPublicId(NPC);NPCAssemblyOwner=NPC->GetName();}
 const bool Pair=IsValid(Hit)&&!Hit->IsBeingDestroyed()&&IsValid(C)&&!C->IsBeingDestroyed()
  &&Hit->GetOwner()==NPC&&C->GetOwner()==NPC&&Hit->GetWorld()==GetWorld()&&C->GetWorld()==GetWorld();
 const bool Registered=Pair&&Hit->IsRegistered()&&C->IsRegistered(),Begun=Pair&&Hit->HasBegunPlay()&&C->HasBegunPlay(),Replicated=Pair&&Hit->GetIsReplicated()&&C->GetIsReplicated();
 int32 PendingLoops=0,Niagara=0;FProject_JReplicatedCombatPresentationState State;
 if(Clients.Num()&&IsValid(Clients[0]))State=Clients[0]->ReadNPCAssemblyState(NPC,PendingLoops,Niagara);
 NPCAssemblyExpected=State;
 const bool Active=Step==1||Step==3,Owned=Alive&&NPC->HasCombatActions(),ProfileNull=!IsValid(C)||!C->BasePresentationSet;
 const uint64 Lease=Pair&&C->IsCueLeaseCurrent(NPCAssemblyLease)?NPCAssemblyLease:0;
 auto* Action=Alive?NPC->FindComponentByClass<UProject_JNPCActionComponent>():nullptr;
 const auto Grant=Action?Action->GetAttackAbilityHandle():FGameplayAbilitySpecHandle();
 bool Passed=NPCAssemblyActorId&&!NPCAssemblyOwner.IsEmpty()&&!NPCAssemblyOwner.Contains(TEXT(","))&&!NPCAssemblyOwner.Contains(TEXT("\n"))&&ProfileNull&&PendingLoops==0&&Niagara==0;
 if(Step==0)Passed=Passed&&NPCAssemblyCurrent()&&Hits.IsEmpty()&&Presentations.IsEmpty()&&!Owned;
 else if(Step==5)Passed=Passed&&!Alive&&!NPCAssemblyHit.IsValid()&&!NPCAssemblyPresentation.IsValid()
  &&(!bNPCAssemblyOwnsController||!NPCAssemblyController.IsValid()||NPCAssemblyController->IsActorBeingDestroyed());
 else Passed=Passed&&NPCAssemblyCurrent()&&Hits.Num()==1&&Presentations.Num()==1&&Registered&&Begun&&Replicated
  &&NPCAssemblyHit.Get()==Hit&&NPCAssemblyPresentation.Get()==C&&Hit->GetUniqueID()==NPCAssemblyHitId&&C->GetUniqueID()==NPCAssemblyPresentationId
  &&!Hit->PrimaryComponentTick.bCanEverTick&&!C->PrimaryComponentTick.bCanEverTick&&!Grant.IsValid()&&Owned==Active
  &&State.ActiveWarningCueTags.Num()==(Active?1:0)&&bool(Lease)==Active&&State.AttackInstance&&State.Revision>0&&State.EventOrder>0
  &&(Active?State.ActiveWarningCueTags.HasTagExact(AuditCueA):!C->GetActiveAttackTag().IsValid());
 TArray<FString> Cells;const auto Number=[&](auto Value){Cells.Add(LexToString(Value));};
 Number(70);Number(Step);Number(int32(GetNetMode()));Number(Alive?int32(NPC->GetLocalRole()):0);Number(NPCAssemblyActorId);Cells.Add(NPCAssemblyOwner);Number(GFrameCounter);
 Number(int32(Alive));Number(Hits.Num());Number(Presentations.Num());Number(NPCAssemblyHitId);Number(NPCAssemblyPresentationId);Number(int32(Registered));Number(int32(Begun));Number(int32(Replicated));
 Number(State.AttackInstance);Number(State.Revision);Number(State.EventOrder);Number(State.ActiveWarningCueTags.Num());Number(Lease);Number(int32(Owned));Cells.Add(Grant.IsValid()?Grant.ToString():TEXT("0"));
 Number(int32(ProfileNull));Number(PendingLoops);Number(Niagara);Number(int32(Passed));NPCAssemblyRows+=FString::Join(Cells,TEXT(","))+TEXT("\n");return Passed;
}

void AProject_JAuditNetworkGameMode::SendNPCAssemblyStep()
{
 for(AProject_JAuditNetworkController* PC:Clients)PC->ClientNPCAssemblyStep(Step,Step==5?nullptr:NPCAssemblyActor.Get(),NPCAssemblyExpected.AttackInstance,NPCAssemblyExpected.EventOrder);
 CaseStarted=StepStarted=FPlatformTime::Seconds(); // Real phase transition only; retries never extend this deadline.
}

void AProject_JAuditNetworkGameMode::UpdateNPCAssemblyAudit()
{
 for(AProject_JAuditNetworkController* PC:Clients)if(!IsValid(PC)||PC->IsActorBeingDestroyed()||!PC->GetNetConnection()){Finish(false,TEXT("NPC assembly observer connection retired"));return;}
 if(Case==INDEX_NONE)
 {
  if(Clients.Num()!=1||Clients[0]->Slot!=0)return;
  Case=70;Step=0;CaseStarted=StepStarted=FPlatformTime::Seconds();
  if(!RecordNPCAssemblyAuthority()){Finish(false,TEXT("NPC assembly initial native empty boundary failed"));return;}SendNPCAssemblyStep();return;
 }
 const double Now=FPlatformTime::Seconds();
 if(Step!=1&&Now-CaseStarted>20){Finish(false,FString::Printf(TEXT("NPC assembly step%d timeout"),Step));return;}
 if((Step==0||Step==1)&&Now-StepStarted>1)
 {
  for(AProject_JAuditNetworkController* PC:Clients)
   if(PC->ConfirmedCase!=70||PC->ConfirmedStep!=Step)
    PC->ClientNPCAssemblyStep(Step,NPCAssemblyActor.Get(),NPCAssemblyExpected.AttackInstance,NPCAssemblyExpected.EventOrder);
  StepStarted=Now; // Retry clock only; keep CaseStarted and each client ObserveStarted unchanged.
 }
 if(Step==0&&Clients[0]->ConfirmedCase==70&&Clients[0]->ConfirmedStep==0)
 {Step=1;if(!BeginNPCAssemblyWarning()){Finish(false,TEXT("NPC native helper/first warning admission failed"));return;}SendNPCAssemblyStep();return;}
 if(Step==1)
 {
  if(!bNPCAssemblyLateJoinReady&&Clients[0]->ConfirmedCase==70&&Clients[0]->ConfirmedStep==1)
  {bNPCAssemblyLateJoinReady=true;UE_LOG(LogProjectJAuditNetwork,Display,TEXT("NPC_ASSEMBLY_LATE_JOIN_READY case=70 slot=1 step=1"));}
  if(bNPCAssemblyLateJoinReady&&Clients.Num()==2&&!bNPCAssemblyLateJoinSent)
  {bNPCAssemblyLateJoinSent=true;Clients[1]->ClientNPCAssemblyStep(1,NPCAssemblyActor.Get(),NPCAssemblyExpected.AttackInstance,NPCAssemblyExpected.EventOrder);}
  if(!AllConfirmed(1))return;
  Step=2;if(!EndNPCAssemblyWarning()){Finish(false,TEXT("NPC explicit warning end/helper Stop failed"));return;}SendNPCAssemblyStep();return;
 }
 if(Step==2&&AllConfirmed(2))
 {Step=3;if(!BeginNPCAssemblyWarning()){Finish(false,TEXT("NPC helper restart did not preserve the actor-lifetime pair"));return;}SendNPCAssemblyStep();return;}
 if(Step==3&&AllConfirmed(3))
 {Step=4;if(!EndNPCAssemblyWarning()){Finish(false,TEXT("NPC restart warning end/helper Stop failed"));return;}SendNPCAssemblyStep();return;}
 if(Step==4&&AllConfirmed(4))
 {
  Step=5;const TStrongObjectPtr<AProject_JNPCCharacter> Old(NPCAssemblyActor);Old->Destroy();
  if(!RecordNPCAssemblyAuthority()){Finish(false,TEXT("NPC native actor/controller retirement failed"));return;}SendNPCAssemblyStep();return;
 }
 if(Step==5&&AllConfirmed(5))Finish(true,TEXT("Actual native dynamic NPC pair, late-join warning recovery, exact reuse and normal retirement observed; no attack or capacity claim"));
}

void AProject_JAuditNetworkGameMode::CleanupNPCAssemblyAudit()
{
 const TStrongObjectPtr<AProject_JNPCCharacter> Old(NPCAssemblyActor);NPCAssemblyActor=nullptr;
 if(Old.IsValid()&&!Old->IsActorBeingDestroyed())
 {
  if(auto* C=NPCAssemblyPresentation.Get();IsValid(C)&&!C->IsBeingDestroyed()&&C->IsCueLeaseCurrent(NPCAssemblyLease))C->EndAttackPresentation();
  Old->StopCombatActions();Old->Destroy();
 }
 NPCAssemblyDefinition=nullptr;
}
