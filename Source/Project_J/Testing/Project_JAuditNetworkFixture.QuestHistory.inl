// CLI-only owner page RPC and private-summary audience proof. No client state injection or reward execution.
namespace {
const TCHAR* QuestWireClientHeader=TEXT("case,step,slot,mode,frame,own_state,foreign_state,own_role,source_current,summary_count,summary_revision,active_count,page_has,page_status,page_ordinal,page_count,page_revision,returned_nonce,page_nonce,page_ids,foreign_present,foreign_count,foreign_revision_valid,foreign_active_count,foreign_page_has,foreign_page_count,passed\n");
const TCHAR* QuestWireAuthorityHeader=TEXT("case,step,mode,frame,owner_state,source_current,summary_count,summary_revision,history_count,active_count,expected_ordinal,expected_count,expected_revision,expected_ids,observer_state,observer_count,observer_revision,observer_active_count,passed\n");
FString QuestWireGuid(FGuid Value){return Value.IsValid()?Value.ToString(EGuidFormats::DigitsWithHyphens):TEXT("0");}
FString QuestWireIds(const TArray<FName>& Values){TArray<FString> Text;for(FName Id:Values)Text.Add(Id.ToString().ToLower());return FString::Join(Text,TEXT("|"));}
int32 QuestWireActive(const UProject_JQuestComponent* Component){int32 Result=0;if(Component)for(const auto& Item:Component->States)if(!Item.bClaimed)++Result;return Result;}
}

void AProject_JAuditNetworkController::ClientQuestHistoryStep_Implementation(int32 InStep,AProject_JPlayerState* OwnState,AProject_JPlayerState* ForeignState)
{
 if(!AuditQuestHistoryEnabled()||HasAuthority()||InStep<0||InStep>5||InStep<QuestHistoryStep)return;
 if((IsValid(OwnState)&&QuestHistoryOwnState.IsValid()&&QuestHistoryOwnState.Get()!=OwnState)||
    (IsValid(ForeignState)&&QuestHistoryForeignState.IsValid()&&QuestHistoryForeignState.Get()!=ForeignState))
 {ServerObserve(80,InStep,false,TEXT("Quest control changed exact native PlayerState identity"));return;}
 if(InStep==QuestHistoryStep&&bQuestHistoryRecorded)return;
 const bool NewStep=InStep!=QuestHistoryStep;
 const bool NewBinding=IsValid(OwnState)&&!QuestHistoryOwnState.IsValid();
 if(IsValid(OwnState))QuestHistoryOwnState=OwnState;
 if(IsValid(ForeignState))QuestHistoryForeignState=ForeignState;
 if(Case!=80){Case=80;QuestHistoryRows=QuestWireClientHeader;}
 FParse::Value(FCommandLine::Get(),TEXT("ProjectJAuditSlot="),Slot);
 QuestHistoryStep=InStep;
 if(NewStep){bQuestHistoryRecorded=false;bQuestHistoryQuerySent=false;QuestHistoryStableFrame=MAX_uint64;ObserveStarted=FPlatformTime::Seconds();}
 if(NewStep||NewBinding)UE_LOG(LogProjectJAuditNetwork,Display,TEXT("QUEST_HISTORY_CONTROL slot=%d step=%d own=%s foreign=%s new_binding=%d"),Slot,InStep,*GetNameSafe(OwnState),*GetNameSafe(ForeignState),int32(NewBinding));
}

void AProject_JAuditNetworkController::ObserveQuestHistoryAudit()
{
 if(QuestHistoryStep==INDEX_NONE||bQuestHistoryRecorded)return;
 auto* OwnState=QuestHistoryOwnState.Get();auto* ForeignState=QuestHistoryForeignState.Get();
 auto* OwnQuest=IsValid(OwnState)?OwnState->FindComponentByClass<UProject_JQuestComponent>():nullptr;
 auto* ForeignQuest=IsValid(ForeignState)?ForeignState->FindComponentByClass<UProject_JQuestComponent>():nullptr;
 const bool Current=GetNetMode()==NM_Client&&IsValid(OwnState)&&!OwnState->IsActorBeingDestroyed()&&OwnState==GetPlayerState<AProject_JPlayerState>()&&OwnState->GetOwner()==this&&
    OwnState->GetWorld()==GetWorld()&&!OwnState->HasAuthority()&&IsValid(OwnQuest)&&!OwnQuest->IsBeingDestroyed()&&OwnQuest->GetOwner()==OwnState&&OwnQuest->IsRegistered()&&OwnQuest->HasBegunPlay();
 const bool ForeignPresent=IsValid(ForeignState)&&!ForeignState->IsActorBeingDestroyed()&&ForeignState!=OwnState&&ForeignState->GetWorld()==GetWorld()&&
   !ForeignState->HasAuthority()&&IsValid(ForeignQuest)&&!ForeignQuest->IsBeingDestroyed()&&ForeignQuest->GetOwner()==ForeignState&&ForeignQuest->IsRegistered()&&ForeignQuest->HasBegunPlay();
 const auto Summary=OwnQuest?OwnQuest->GetHistorySummary():FProject_JQuestHistorySummary();
 auto Page=OwnQuest?OwnQuest->GetHistoryPage():FProject_JQuestHistoryPageView();
 const auto OtherSummary=ForeignQuest?ForeignQuest->GetHistorySummary():FProject_JQuestHistorySummary();
 const auto OtherPage=ForeignQuest?ForeignQuest->GetHistoryPage():FProject_JQuestHistoryPageView();
 const bool Private=ForeignPresent&&OtherSummary.Count==0&&!OtherSummary.Revision.IsValid()&&ForeignQuest->States.IsEmpty()&&!OtherPage.bHasPage&&OtherPage.Ids.IsEmpty();
 const bool bHistoryOwner=Slot==0;const int32 ExpectedCount=bHistoryOwner?(QuestHistoryStep>=4?66:65):0;
 bool Ready=Current&&Private&&Summary.Revision.IsValid()&&Summary.Count==ExpectedCount&&QuestWireActive(OwnQuest)==(bHistoryOwner?1:0);
 if(Ready&&QuestHistoryStep==0&&!QuestHistoryInitialRevision.IsValid())QuestHistoryInitialRevision=Summary.Revision;
 if(Ready)Ready=QuestHistoryInitialRevision.IsValid()&&(bHistoryOwner&&QuestHistoryStep>=4?Summary.Revision!=QuestHistoryInitialRevision:Summary.Revision==QuestHistoryInitialRevision);
 const bool Query=bHistoryOwner?(QuestHistoryStep==1||QuestHistoryStep==2||QuestHistoryStep==3||QuestHistoryStep==5):QuestHistoryStep==1;
 const int32 Ordinal=QuestHistoryStep==2?1:QuestHistoryStep==3?2:0;
 if(Ready&&Query&&!bQuestHistoryQuerySent)
 {
  const FGuid Issued=OwnQuest->RequestCompletedPage(Ordinal);
  if(Issued.IsValid()){QuestHistoryReturnedNonce=Issued;bQuestHistoryQuerySent=true;}
  Page=OwnQuest->GetHistoryPage();
 }
 if(Ready&&QuestHistoryStep==0)Ready=!Page.bHasPage&&Page.Ids.IsEmpty();
 else if(Ready&&bHistoryOwner&&QuestHistoryStep==4)
  Ready=Page.bHasPage&&Page.Status==EProject_JQuestHistoryPageStatus::Stale&&Page.Ordinal==2&&Page.Ids.Num()==1&&Page.Summary.Revision==QuestHistoryInitialRevision&&Page.Request==QuestHistoryReturnedNonce;
 else if(Ready)
 {
  const int32 ExpectedOrdinal=bHistoryOwner?Ordinal:0;const int32 ExpectedIds=bHistoryOwner?(QuestHistoryStep==3?1:32):0;
  Ready=Page.bHasPage&&Page.Status==EProject_JQuestHistoryPageStatus::Fresh&&Page.Ordinal==ExpectedOrdinal&&Page.Ids.Num()==ExpectedIds&&Page.Summary==Summary&&
   QuestHistoryReturnedNonce.IsValid()&&Page.Request==QuestHistoryReturnedNonce&&(!Query||bQuestHistoryQuerySent);
 }
 if(!Ready)QuestHistoryStableFrame=MAX_uint64;
 else if(QuestHistoryStableFrame==MAX_uint64){QuestHistoryStableFrame=GFrameCounter;return;}
 else if(QuestHistoryStableFrame==GFrameCounter)return;
 if(!Ready&&FPlatformTime::Seconds()-ObserveStarted<=8)return;
 if(!bQuestHistoryPacketsLogged)
 {
  bQuestHistoryPacketsLogged=true;
#if DO_ENABLE_NET_TEST
  if(auto* Driver=GetWorld()->GetNetDriver()){const auto& Sim=Driver->PacketSimulationSettings;
   UE_LOG(LogProjectJAuditNetwork,Display,TEXT("QUEST_HISTORY_PACKETS slot=%d lag=%d variance=%d loss=%d order=%d duplicate=%d netmode=%d"),Slot,Sim.PktLag,Sim.PktLagVariance,Sim.PktLoss,Sim.PktOrder,Sim.PktDup,int32(GetNetMode()));}
#endif
 }
 TArray<FString> Cells;const auto Number=[&](auto Value){Cells.Add(LexToString(Value));};
 Number(80);Number(QuestHistoryStep);Number(Slot);Number(int32(GetNetMode()));Number(GFrameCounter);Cells.Add(GetNameSafe(OwnState));Cells.Add(GetNameSafe(ForeignState));Number(OwnState?int32(OwnState->GetLocalRole()):0);
 Number(int32(Current));Number(Summary.Count);Cells.Add(QuestWireGuid(Summary.Revision));Number(QuestWireActive(OwnQuest));Number(int32(Page.bHasPage));Number(int32(Page.Status));Number(Page.Ordinal);Number(Page.Ids.Num());
 Cells.Add(QuestWireGuid(Page.Summary.Revision));Cells.Add(QuestWireGuid(QuestHistoryReturnedNonce));Cells.Add(QuestWireGuid(Page.Request));Cells.Add(QuestWireIds(Page.Ids));Number(int32(ForeignPresent));Number(OtherSummary.Count);Number(int32(OtherSummary.Revision.IsValid()));Number(QuestWireActive(ForeignQuest));Number(int32(OtherPage.bHasPage));Number(OtherPage.Ids.Num());Number(int32(Ready));
 QuestHistoryRows+=FString::Join(Cells,TEXT(","))+TEXT("\n");bQuestHistoryRecorded=true;
 WriteAudit(FString::Printf(TEXT("quest-history-client_%d.csv"),Slot),QuestHistoryRows);
 ServerObserve(80,QuestHistoryStep,Ready,FString::Printf(TEXT("current=%d foreign_present=%d private=%d count=%d active=%d page=%d ids=%d status=%d nonce_match=%d"),Current,ForeignPresent,Private,Summary.Count,QuestWireActive(OwnQuest),Page.Ordinal,Page.Ids.Num(),int32(Page.Status),int32(Page.Request==QuestHistoryReturnedNonce)));
}

bool AProject_JAuditNetworkGameMode::QuestHistoryCurrent() const
{
 if(Clients.Num()!=2||!GetWorld()||GetWorld()->bIsTearingDown)return false;
 for(int32 Index=0;Index<2;++Index)
 {
  const auto* PC=Clients[Index].Get();const auto* State=QuestHistoryStates[Index].Get();const auto* Component=QuestHistoryComponents[Index].Get();
  if(!IsValid(PC)||PC->IsActorBeingDestroyed()||PC->Slot!=Index||!PC->GetNetConnection()||!IsValid(State)||State->IsActorBeingDestroyed()||
   PC->GetPlayerState<AProject_JPlayerState>()!=State||State->GetOwner()!=PC||State->GetWorld()!=GetWorld()||!State->HasAuthority()||
   !IsValid(Component)||Component->IsBeingDestroyed()||Component->GetOwner()!=State||State->FindComponentByClass<UProject_JQuestComponent>()!=Component||!Component->IsRegistered()||!Component->HasBegunPlay())return false;
 }
 return true;
}

bool AProject_JAuditNetworkGameMode::PrepareQuestHistoryAudit()
{
 if(Clients.Num()!=2)return false;
 for(int32 Index=0;Index<2;++Index)
 {
  QuestHistoryStates[Index]=Clients[Index]->GetPlayerState<AProject_JPlayerState>();
  QuestHistoryComponents[Index]=QuestHistoryStates[Index].IsValid()?QuestHistoryStates[Index]->FindComponentByClass<UProject_JQuestComponent>():nullptr;
 }
 if(!QuestHistoryCurrent())return false;
 const FString Prefix=TEXT("WireQuest.")+FGuid::NewGuid().ToString(EGuidFormats::Digits);
 ProjectJ::QuestHistory::FViewPtr History=ProjectJ::QuestHistory::Empty();FString Failure;
 for(int32 Index=0;Index<65;++Index){ProjectJ::QuestHistory::FViewPtr Next;if(!ProjectJ::QuestHistory::Append(History,FName(*(Prefix+FString::Printf(TEXT(".%03d"),Index))),Next,Failure))return false;History=MoveTemp(Next);}
 QuestHistoryInitialValues=History;QuestHistoryNextId=FName(*(Prefix+TEXT(".065")));
 QuestHistoryDefinition=NewObject<UProject_JQuestDefinition>(this);QuestHistoryDefinition->QuestId=FName(*(Prefix+TEXT(".Active")));
 QuestHistoryDefinition->ObjectiveEvent=TEXT("WireOnly.NotProduced");QuestHistoryDefinition->Title=FText::FromString(TEXT("Wire current detail"));
 QuestHistoryComponents[0]->Definitions.Add(QuestHistoryDefinition);
 for(int32 Index=0;Index<2;++Index)
 {
  auto* Component=QuestHistoryComponents[Index].Get();auto Snapshot=Component->CaptureSnapshot();Snapshot.States.Reset();Snapshot.Version=Index==0?3:1;
  Snapshot.History=Index==0?History:ProjectJ::QuestHistory::Empty();Snapshot.HistoryCount=Index==0?65:0;Snapshot.HistoryRootDigest=ProjectJ::QuestHistory::RootDigest(Snapshot.History);
  if(Index==0){auto& Active=Snapshot.States.AddDefaulted_GetRef();Active.QuestId=QuestHistoryDefinition->QuestId;}
  if(!Component->ValidateCommittedQuests(Snapshot))return false;
  Component->InstallCommittedQuests(Snapshot);Component->PublishCommittedQuests();if(!QuestHistoryCurrent())return false;
 }
 QuestHistoryInitialRevision=QuestHistoryComponents[0]->GetHistorySummary().Revision;
 QuestHistoryRows=QuestWireAuthorityHeader;return QuestHistoryInitialRevision.IsValid();
}

bool AProject_JAuditNetworkGameMode::RecordQuestHistoryAuthority()
{
 if(!QuestHistoryCurrent())return false;
 const auto* Component=QuestHistoryComponents[0].Get();const auto Snapshot=Component->CaptureSnapshot();const auto Summary=Component->GetHistorySummary();
 const auto Observer=QuestHistoryComponents[1]->GetHistorySummary();
 const bool Changed=Step>=4;bool Passed=Summary.Count==(Changed?66:65)&&Summary.Count==Snapshot.HistoryCount&&Summary.Revision.IsValid()&&
  (Changed?Summary.Revision!=QuestHistoryInitialRevision:Summary.Revision==QuestHistoryInitialRevision)&&QuestWireActive(Component)==1&&Observer.Count==0&&Observer.Revision.IsValid()&&QuestWireActive(QuestHistoryComponents[1].Get())==0;
 int32 Ordinal=INDEX_NONE;TArray<FName> Expected;FGuid PageRevision;
 if(Step>0)
 {
  Ordinal=Step==2?1:(Step==3||Step==4?2:0);PageRevision=Step==4?QuestHistoryInitialRevision:Summary.Revision;
  Passed=ProjectJ::QuestHistory::GetRecentPage(Step==4?QuestHistoryInitialValues:Snapshot.History,Ordinal*32,32,Expected)&&Passed;
 }
 TArray<FString> Cells;const auto Number=[&](auto Value){Cells.Add(LexToString(Value));};
 Number(80);Number(Step);Number(int32(GetNetMode()));Number(GFrameCounter);Cells.Add(QuestHistoryStates[0]->GetName());Number(1);Number(Summary.Count);Cells.Add(QuestWireGuid(Summary.Revision));Number(Snapshot.HistoryCount);Number(QuestWireActive(Component));
 Number(Ordinal);Number(Expected.Num());Cells.Add(QuestWireGuid(PageRevision));Cells.Add(QuestWireIds(Expected));Cells.Add(QuestHistoryStates[1]->GetName());Number(Observer.Count);Cells.Add(QuestWireGuid(Observer.Revision));Number(QuestWireActive(QuestHistoryComponents[1].Get()));Number(int32(Passed));
 QuestHistoryRows+=FString::Join(Cells,TEXT(","))+TEXT("\n");return Passed;
}

void AProject_JAuditNetworkGameMode::SendQuestHistoryStep()
{
 for(int32 Index=0;Index<2;++Index)Clients[Index]->ClientQuestHistoryStep(Step,QuestHistoryStates[Index].Get(),QuestHistoryStates[1-Index].Get());
 CaseStarted=StepStarted=FPlatformTime::Seconds();QuestHistoryAckAt=-1;
}

void AProject_JAuditNetworkGameMode::UpdateQuestHistoryAudit()
{
 const double Now=FPlatformTime::Seconds();
 if(Case==INDEX_NONE)
 {
  if(Clients.Num()!=2)return;
  if(!PrepareQuestHistoryAudit()){Finish(false,TEXT("Quest authority validated seed/source readiness failed"));return;}
  Case=80;Step=0;if(!RecordQuestHistoryAuthority()){Finish(false,TEXT("Quest initial authority projection failed"));return;}SendQuestHistoryStep();return;
 }
 if(!QuestHistoryCurrent()){Finish(false,TEXT("Quest source/controller/component changed identity"));return;}
 if(Now-CaseStarted>20){Finish(false,FString::Printf(TEXT("Quest history phase%d timed out"),Step));return;}
 if(Now-StepStarted>1)
 {
  for(int32 Index=0;Index<2;++Index)if(Clients[Index]->ConfirmedCase!=80||Clients[Index]->ConfirmedStep!=Step)
   Clients[Index]->ClientQuestHistoryStep(Step,QuestHistoryStates[Index].Get(),QuestHistoryStates[1-Index].Get());
  StepStarted=Now; // Retry only; CaseStarted/client observation deadlines do not reset.
 }
 if(!AllConfirmed(Step))return;
 if(Step==5){Finish(true,TEXT("Actual owner page32/32/1, private foreign projection, authoritative append/stale/requery66 and normal source cleanup; no reward/UI/CCU claim"));return;}
 if(QuestHistoryAckAt<0){QuestHistoryAckAt=Now;return;}
 if(Now-QuestHistoryAckAt<0.55)return; // After actual response ACK, preserving the production0.5s page admission policy even under jitter.
 ++Step;
 if(Step==4)
 {
  auto* Component=QuestHistoryComponents[0].Get();auto Snapshot=Component->CaptureSnapshot();ProjectJ::QuestHistory::FViewPtr Appended;FString Failure;
  if(!ProjectJ::QuestHistory::Append(Snapshot.History,QuestHistoryNextId,Appended,Failure)){Finish(false,TEXT("Quest actual append failed"));return;}
  Snapshot.History=MoveTemp(Appended);Snapshot.HistoryCount=ProjectJ::QuestHistory::Count(Snapshot.History);Snapshot.HistoryRootDigest=ProjectJ::QuestHistory::RootDigest(Snapshot.History);
  if(!Component->ValidateCommittedQuests(Snapshot)||!Component->InstallCommittedQuests(Snapshot)){Finish(false,TEXT("Quest appended checkpoint rejected"));return;}
  Component->PublishCommittedQuests();
 }
 if(!RecordQuestHistoryAuthority()){Finish(false,TEXT("Quest authority phase projection failed"));return;}SendQuestHistoryStep();
}

void AProject_JAuditNetworkGameMode::CleanupQuestHistoryAudit()
{
 if(bQuestHistoryCleaned)return;bQuestHistoryCleaned=true;
 // No fixture callbacks/tasks, actors, grants or client components were installed. Native PS/components retire with the world.
 QuestHistoryInitialValues.Reset();QuestHistoryDefinition=nullptr;
 for(int32 Index=0;Index<2;++Index){QuestHistoryStates[Index].Reset();QuestHistoryComponents[Index].Reset();}
}
