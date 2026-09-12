#include "Misc/AutomationTest.h"
#include "Animation/AnimBlueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Variable.h"
#include "NiagaraSystem.h"
#include "NiagaraEffectType.h"
#include "Serialization/JsonSerializer.h"
#include "ExperimentOutput.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJPresentationAudit, "ProjectJ.GroupE.AuthoringAudit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJPresentationAudit::RunTest(const FString&)
{
    TArray<TSharedPtr<FJsonValue>> Assets;
    for (const TCHAR* Path : {TEXT("/Game/Animation_Logic/ABPs/ABP_Humanoid_Master.ABP_Humanoid_Master"),
        TEXT("/Game/Animation_Logic/ABPs/GreatSword/ABP_Greatsword_Layers.ABP_Greatsword_Layers")})
    {
        auto* BP = LoadObject<UAnimBlueprint>(nullptr, Path);
        if (!TestNotNull(TEXT("Explicit animation asset exists"), BP)) { continue; }
        auto Asset = MakeShared<FJsonObject>(); Asset->SetStringField(TEXT("path"), Path);
        Asset->SetBoolField(TEXT("dirtyBefore"), BP->GetOutermost()->IsDirty());
        TArray<UEdGraph*> Graphs; BP->GetAllGraphs(Graphs);
        TArray<TSharedPtr<FJsonValue>> Nodes;
        for (const auto* Graph : Graphs)
        {
            for (const UEdGraphNode* Node : Graph->Nodes)
            {
                if (!Node) { continue; }
                auto N = MakeShared<FJsonObject>();
                N->SetStringField(TEXT("graph"), Graph->GetName());
                N->SetStringField(TEXT("type"), Node->GetClass()->GetName());
                N->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
                N->SetStringField(TEXT("guid"), Node->NodeGuid.ToString(EGuidFormats::Digits));
                if (const auto* Call = Cast<UK2Node_CallFunction>(Node)) { N->SetStringField(TEXT("function"), Call->FunctionReference.GetMemberName().ToString()); }
                if (const auto* Variable = Cast<UK2Node_Variable>(Node)) { N->SetStringField(TEXT("variable"), Variable->VariableReference.GetMemberName().ToString()); }
                TArray<TSharedPtr<FJsonValue>> Pins;
                for (const auto* Pin : Node->Pins)
                {
                    if (!Pin || Pin->Direction != EGPD_Input) { continue; }
                    auto P = MakeShared<FJsonObject>(); P->SetStringField(TEXT("name"), Pin->PinName.ToString());
                    P->SetStringField(TEXT("default"), Pin->DefaultValue);
                    TArray<TSharedPtr<FJsonValue>> Links;
                    for (const auto* Link : Pin->LinkedTo)
                    {
                        auto L = MakeShared<FJsonObject>();
                        L->SetStringField(TEXT("node"), Link->GetOwningNode()->NodeGuid.ToString(EGuidFormats::Digits));
                        L->SetStringField(TEXT("pin"), Link->PinName.ToString()); Links.Add(MakeShared<FJsonValueObject>(L));
                    }
                    P->SetArrayField(TEXT("links"), Links); Pins.Add(MakeShared<FJsonValueObject>(P));
                }
                N->SetArrayField(TEXT("inputs"), Pins); Nodes.Add(MakeShared<FJsonValueObject>(N));
            }
        }
        Asset->SetArrayField(TEXT("nodes"), Nodes);
        Asset->SetBoolField(TEXT("dirtyAfter"), BP->GetOutermost()->IsDirty());
        Assets.Add(MakeShared<FJsonValueObject>(Asset));
    }
    auto Root = MakeShared<FJsonObject>(); Root->SetArrayField(TEXT("animation"), Assets);
    auto* Niagara = LoadObject<UNiagaraSystem>(nullptr, TEXT("/Game/SlashTrail_SoftTofu/Niagara/Basic/NS_SlashTrail_Basic.NS_SlashTrail_Basic"));
    if (TestNotNull(TEXT("Explicit player trail exists"), Niagara))
    {
        const auto& S = Niagara->GetScalabilitySettings();
        Root->SetBoolField(TEXT("distanceCull"), S.bCullByDistance);
        Root->SetNumberField(TEXT("maxDistance"), S.MaxDistance);
        Root->SetBoolField(TEXT("instanceCull"), S.bCullMaxInstanceCount);
        Root->SetNumberField(TEXT("maxInstances"), S.MaxInstances);
    }
    FString Text; FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Text));
    TestTrue(TEXT("Read-only authored graph audit saved"), ProjectJ::Experiments::Save(TEXT("authoring.json"), Text));
    return true;
}
