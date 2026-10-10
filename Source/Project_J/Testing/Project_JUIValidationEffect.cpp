#include "Testing/Project_JUIValidationEffect.h"
#include "UI/Project_JStatusEffects.h"
UProject_JUIValidationEffect::UProject_JUIValidationEffect()
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;
	DurationMagnitude = FScalableFloat(30.f);
	// UE 5.8's setter is editor-only; native CDO construction also runs in cooked builds.
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	StackingType = EGameplayEffectStackingType::AggregateByTarget;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	StackLimitCount = 3;
	auto *UI = CreateDefaultSubobject<UProject_JStatusEffectUIData>(TEXT("StatusUI"));
	GEComponents.Add(UI);
	UI->DisplayName = NSLOCTEXT("ProjectJUI", "ValidationEffect", "상태 검증");
	UI->Description = NSLOCTEXT("ProjectJUI", "ValidationEffectDescription", "검증용 표시 효과 · 게임 속성은 변경하지 않습니다.");
}
