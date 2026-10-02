#include "EdGraphCompilerUtilities.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "HAL/IConsoleManager.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Logging/TokenizedMessage.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectJGraphDiagnostics, Log, All);

namespace
{
class FProjectJGraphValidationContext : public FGraphCompilerContext
{
public:
	explicit FProjectJGraphValidationContext(FCompilerResultsLog& Log) : FGraphCompilerContext(Log) {}
	void CheckNode(const UEdGraphNode* Node) const { ValidateNode(Node); }
};

void ValidateBlueprintGraph(const TArray<FString>& Args)
{
	if (Args.Num() != 1 || !Args[0].StartsWith(TEXT("/Game/")))
	{
		UE_LOG(LogProjectJGraphDiagnostics, Error, TEXT("Usage: ProjectJ.Animation.ValidateBlueprintGraph /Game/Path/Asset.Asset"));
		return;
	}
	UBlueprint* Blueprint = Cast<UBlueprint>(StaticLoadObject(UBlueprint::StaticClass(), nullptr, *Args[0],
		nullptr, LOAD_NoWarn | LOAD_DisableCompileOnLoad));
	if (!Blueprint)
	{
		UE_LOG(LogProjectJGraphDiagnostics, Error, TEXT("Cannot load Blueprint %s"), *Args[0]);
		return;
	}
	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);
	int32 Errors = 0, Warnings = 0;
	for (const UEdGraph* Graph : Graphs)
	{
		if (!Graph) { continue; }
		for (const UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node) { continue; }
			// Re-run the engine's structural validation without AnimGraph's
			// subsequent Messages.Empty(). Do not annotate nodes or save assets.
			FCompilerResultsLog Log;
			Log.bSilentMode = true;
			Log.bAnnotateMentionedNodes = false;
			FProjectJGraphValidationContext Context(Log);
			Context.CheckNode(Node);
			Errors += Log.NumErrors;
			Warnings += Log.NumWarnings;
			for (const TSharedRef<FTokenizedMessage>& Message : Log.Messages)
			{
				UE_LOG(LogProjectJGraphDiagnostics, Display, TEXT("asset=%s graph=%s node=%s class=%s severity=%d message=%s"),
					*Blueprint->GetPathName(), *Graph->GetName(), *Node->GetName(), *Node->GetClass()->GetName(),
					static_cast<int32>(Message->GetSeverity()), *Message->ToText().ToString());
			}
		}
	}
	UE_LOG(LogProjectJGraphDiagnostics, Display, TEXT("Structural validation finished: asset=%s errors=%d warnings=%d. No asset saved."),
		*Blueprint->GetPathName(), Errors, Warnings);
}

FAutoConsoleCommand ValidateBlueprintGraphCommand(
	TEXT("ProjectJ.Animation.ValidateBlueprintGraph"),
	TEXT("Read-only structural Blueprint graph diagnostics for one /Game asset. Does not save or annotate nodes."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&ValidateBlueprintGraph));
}
