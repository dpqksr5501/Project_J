// Fill out your copyright notice in the Description page of Project Settings.

#include "UI/Project_JCharacterViewModel.h"

void UProject_JCharacterViewModel::SetHealth(float NewHealth)
{
	if (Health != NewHealth)
	{
		Health = NewHealth;
		UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(Health);
	}
}

void UProject_JCharacterViewModel::SetMaxHealth(float NewMaxHealth)
{
	if (MaxHealth != NewMaxHealth)
	{
		MaxHealth = NewMaxHealth;
		UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(MaxHealth);
	}
}

void UProject_JCharacterViewModel::SetMana(float NewMana)
{
	if (Mana != NewMana)
	{
		Mana = NewMana;
		UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(Mana);
	}
}

void UProject_JCharacterViewModel::SetMaxMana(float NewMaxMana)
{
	if (MaxMana != NewMaxMana)
	{
		MaxMana = NewMaxMana;
		UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(MaxMana);
	}
}

void UProject_JCharacterViewModel::SetLevel(int32 NewLevel)
{
	if (Level != NewLevel)
	{
		Level = NewLevel;
		UE_MVVM_BROADCAST_FIELD_VALUE_CHANGED(Level);
	}
}
