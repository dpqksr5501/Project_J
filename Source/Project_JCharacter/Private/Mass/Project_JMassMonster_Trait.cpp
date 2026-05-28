#include "Mass/Project_JMassMonster_Trait.h"
#include "MassEntityTemplateRegistry.h"

void UProject_JMassMonster_Trait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
	// Add the Stats Fragment and initialize it with DefaultStats configured in the editor.
	FProject_JMassMonsterStatsFragment& Stats = BuildContext.AddFragment_GetRef<FProject_JMassMonsterStatsFragment>();
	Stats = DefaultStats;
}
