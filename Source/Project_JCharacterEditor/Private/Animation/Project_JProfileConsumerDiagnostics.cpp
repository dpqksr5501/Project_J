#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "Engine/Blueprint.h"
#include "HAL/IConsoleManager.h"
#include "K2Node_CallFunction.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

namespace
{
/** Read existing graph data only. No compilation, asset mutation or package save. */
void AuditProfileConsumers(const TArray<FString> &Args)
{
	if (Args.Num() != 1)
	{
		UE_LOG(LogTemp, Error, TEXT("Usage: ProjectJ.Diagnostics.ProfileConsumers <Saved-output.json>"));
		return;
	}
	const FString Output = FPaths::ConvertRelativePathToFull(Args[0]);
	if (!FPaths::IsUnderDirectory(Output, FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Saved"))))
		return;
	auto &Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	Registry.WaitForCompletion();
	FARFilter Filter;
	Filter.ClassPaths.Add(UBlueprint::StaticClass()->GetClassPathName());
	Filter.bRecursiveClasses = true;
	Filter.bRecursivePaths = true;
	for (const TCHAR *Path : {TEXT("/Game/Character_BPs"), TEXT("/Game/UI"), TEXT("/Game/ThirdPerson")})
		Filter.PackagePaths.Add(Path);
	TArray<FAssetData> Assets;
	Registry.GetAssets(Filter, Assets);
	const TSet<FName> Targets = {TEXT("InitializeCharacterClassById"),
								 TEXT("ApplyAdvancementById"),
								 TEXT("InitializeCharacterClassDefinition"),
								 TEXT("ApplyAdvancementDefinition"),
								 TEXT("SetCharacterLevel"),
								 TEXT("TrySetCharacterLevel"),
								 TEXT("RequestUseItem"),
								 TEXT("RequestEquipmentChange"),
								 TEXT("NewRequestId")};
	TArray<TSharedPtr<FJsonValue>> Rows, Hits, Failures;
	int32 GraphCount = 0, NodeCount = 0;
	for (const auto &Data : Assets)
	{
		auto *Blueprint = Cast<UBlueprint>(Data.GetAsset());
		if (!Blueprint)
		{
			Failures.Add(MakeShared<FJsonValueString>(Data.PackageName.ToString()));
			continue;
		}
		TArray<UEdGraph *> Graphs;
		Blueprint->GetAllGraphs(Graphs);
		GraphCount += Graphs.Num();
		int32 Calls = 0;
		for (const auto *Graph : Graphs)
		{
			if (!Graph)
			{
				Failures.Add(MakeShared<FJsonValueString>(Blueprint->GetPathName()));
				continue;
			}
			for (const UEdGraphNode *Node : Graph->Nodes)
				if (const auto *Call = Cast<UK2Node_CallFunction>(Node))
				{
					++Calls;
					++NodeCount;
					const FName Name = Call->FunctionReference.GetMemberName();
					if (!Targets.Contains(Name))
						continue;
					auto Hit = MakeShared<FJsonObject>();
					Hit->SetStringField(TEXT("asset"), Blueprint->GetPathName());
					Hit->SetStringField(TEXT("graph"), Graph->GetName());
					Hit->SetStringField(TEXT("function"), Name.ToString());
					if (const auto *Function = Call->GetTargetFunction())
						Hit->SetStringField(TEXT("owner"), Function->GetOuterUClass()->GetPathName());
					Hits.Add(MakeShared<FJsonValueObject>(Hit));
				}
		}
		auto Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("asset"), Blueprint->GetPathName());
		Row->SetNumberField(TEXT("graphs"), Graphs.Num());
		Row->SetNumberField(TEXT("functionCalls"), Calls);
		Rows.Add(MakeShared<FJsonValueObject>(Row));
	}
	auto Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("complete"), Failures.IsEmpty() && !Assets.IsEmpty());
	Result->SetNumberField(TEXT("assets"), Assets.Num());
	Result->SetNumberField(TEXT("graphs"), GraphCount);
	Result->SetNumberField(TEXT("functionNodes"), NodeCount);
	Result->SetNumberField(TEXT("saves"), 0);
	Result->SetArrayField(TEXT("rows"), Rows);
	Result->SetArrayField(TEXT("hits"), Hits);
	Result->SetArrayField(TEXT("unreadable"), Failures);
	FString Json;
	auto Writer = TJsonWriterFactory<>::Create(&Json);
	if (FJsonSerializer::Serialize(Result, Writer))
		FFileHelper::SaveStringToFile(Json, *Output);
	UE_LOG(LogTemp, Display,
		   TEXT("Profile consumers: assets=%d graphs=%d functionNodes=%d hits=%d failures=%d saves=0"), Assets.Num(),
		   GraphCount, NodeCount, Hits.Num(), Failures.Num());
}
FAutoConsoleCommand ProfileConsumers(
	TEXT("ProjectJ.Diagnostics.ProfileConsumers"),
	TEXT("Read-only scoped character/UI Blueprint call audit; writes JSON evidence inside Saved."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&AuditProfileConsumers));
} // namespace
