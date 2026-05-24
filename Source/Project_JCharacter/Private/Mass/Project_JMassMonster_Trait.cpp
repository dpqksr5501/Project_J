#include "Mass/Project_JMassMonster_Trait.h"
#include "MassEntityTemplateRegistry.h"

void UProject_JMassMonster_Trait::BuildTemplate(FMassEntityTemplateBuildContext& BuildContext, const UWorld& World) const
{
	// Tell the Mass Framework that this entity requires the Stats Fragment
	BuildContext.AddFragment<FProject_JMassMonsterStatsFragment>();
}
