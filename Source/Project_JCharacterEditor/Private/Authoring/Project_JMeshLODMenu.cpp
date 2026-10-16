#include "Authoring/Project_JMeshLODAuthoringLibrary.h"
#include "Authoring/Project_JMeshLODAuthoring.h"
#include "Authoring/Project_JMeshLODSettings.h"
#include "ContentBrowserMenuContexts.h"
#include "ToolMenus.h"
#include "Misc/MessageDialog.h"

FString UProject_JMeshLODAuthoringLibrary::InspectMeshes(const TArray<UObject*>& Assets,UProject_JMeshLODSettings* Policy,bool bSurfaceReviewed)
{
    using namespace ProjectJ::MeshLODAuthoring;
    if(!Policy)Policy=LoadPolicy();if(!Policy)return TEXT("Shared LOD policy missing. Run the documented CreatePolicy command once.");
    if(Assets.Num()>32)return TEXT("Select at most 32 explicit assets per operation.");
    FString Text;
    for(UObject* Asset:Assets)
    {
        const auto R=Inspect(Asset,*Policy,bSurfaceReviewed ? ERole::Surface : ERole::Unclassified);
        Text+=FString::Printf(TEXT("%s | LODs=%d | %s\n"),Asset ? *Asset->GetPathName() : TEXT("None"),R.LODs,*R.Reason);
    }
    return Text;
}
void ProjectJ::MeshLODAuthoring::RegisterMenus()
{
    FToolMenuOwnerScoped Owner(TEXT("ProjectJContentAuthoring"));
    auto* Menu=UToolMenus::Get()->ExtendMenu(TEXT("ContentBrowser.AssetContextMenu"));
    auto& Section=Menu->FindOrAddSection(TEXT("ProjectJ"),NSLOCTEXT("ProjectJMeshLOD","Section","Project J"));
    Section.AddEntry(FToolMenuEntry::InitMenuEntry(TEXT("ProjectJInspectMeshLODs"),
        NSLOCTEXT("ProjectJMeshLOD","Inspect","Inspect Mesh LODs"),NSLOCTEXT("ProjectJMeshLOD","Tip","Inspect the explicit selection without generating, saving or changing meshes."),FSlateIcon(),
        FToolMenuExecuteAction::CreateLambda([](const FToolMenuContext& Context)
        {
            const auto* Selection=Context.FindContext<UContentBrowserAssetContextMenuContext>();TArray<UObject*> Assets;
            if(Selection && Selection->SelectedAssets.Num()>32)
            {FMessageDialog::Open(EAppMsgType::Ok,NSLOCTEXT("ProjectJMeshLOD","Limit","Select at most 32 explicit assets. Nothing inspected."));return;}
            if(Selection)for(const auto& Data:Selection->SelectedAssets)Assets.Add(Data.GetAsset());
            FMessageDialog::Open(EAppMsgType::Ok,FText::FromString(UProject_JMeshLODAuthoringLibrary::InspectMeshes(Assets)));
        })));
}
