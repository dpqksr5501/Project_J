// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "MoverTypes.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/StrongObjectPtr.h"
#include "UserDefinedStructSupport.generated.h"

class UUserDefinedStruct;


#define UE_API MOVER_API

/** Wrapper class that's used to add User-Defined Struct instances to Mover Data Collections (input or state).
 * This allows devs to add custom data to inputs and/or state without requiring native code.
 * Note that these are typically less efficient than natively-defined structs, and the logic of operations
 * like interpolation, merging, and serialization may be simplistic for a project's needs.
 * At present:
 * - any differences between any struct contents will trigger reconciliation, even small floating point number differences
 * - only boolean values can be merged
 * - interpolation will take the entire struct instance from the highest weight frame
 */
USTRUCT()
struct FMoverUserDefinedDataStruct : public FMoverDataStructBase
{
	GENERATED_BODY()

private:
	// Keeps the User-Defined Struct type alive while StructInstance holds an instance of it. Copies of Mover data are freed from
	// C++ destructors of many holders, often during the garbage collector's purge, and destroying the instance needs its type.
	// Declared before StructInstance so it is released only after the instance is destroyed. Kept in sync with StructInstance by
	// this struct's own members (and copies of it): code that fills or retypes StructInstance directly must use InitializeAs instead,
	// otherwise that wrapper does not keep its type alive. Only the User-Defined Struct type itself is kept alive, not types
	// held by Instanced Struct or Property Bag members inside it.
	TStrongObjectPtr<const UScriptStruct> StructTypeRef;

public:
	UPROPERTY()
	FInstancedStruct StructInstance;	// this always represents a User-Defined Struct from Blueprint. Create or retype it only with InitializeAs.

	FMoverUserDefinedDataStruct() = default;
	FMoverUserDefinedDataStruct(const FMoverUserDefinedDataStruct& Other)
		: Super(Other)
		, StructTypeRef(Other.StructInstance.GetScriptStruct())
		, StructInstance(Other.StructInstance)
	{
	}
	UE_API FMoverUserDefinedDataStruct& operator=(const FMoverUserDefinedDataStruct& Other);

	// Initializes StructInstance as an instance of InUserDefinedStruct (copying InStructMemory when given) and keeps that type alive
	UE_API void InitializeAs(const UScriptStruct* InUserDefinedStruct, const uint8* InStructMemory = nullptr);

	// Tagged serialization can retype StructInstance
	UE_API void PostSerialize(const FArchive& Ar);

	// Implementation of FMoverDataStructBase
	virtual bool ShouldReconcile(const FMoverDataStructBase& AuthorityState) const override;
	virtual void Interpolate(const FMoverDataStructBase& From, const FMoverDataStructBase& To, float LerpFactor) override;
	virtual void Merge(const FMoverDataStructBase& From) override;
	virtual FMoverDataStructBase* Clone() const override;
	virtual bool NetSerialize(FArchive& Ar, UPackageMap* Map, bool& bOutSuccess) override;
	virtual void ToString(FAnsiStringBuilderBase& Out) const override;

	// Note: this is the FMoverUserDefinedDataStruct type, NOT the User-Defined Struct type
	virtual UScriptStruct* GetScriptStruct() const override { return StaticStruct(); }
	// This returns the User-Defined Struct type
	virtual const UScriptStruct* GetDataScriptStruct() const override;

	static bool NetSerializeUserDefinedStruct(FInstancedStruct& UDSStructInstance, FArchive& Ar, UPackageMap* Map);

private:
	// Points StructTypeRef at StructInstance's current type. Call after every write to StructInstance, so the old type outlives the old instance.
	void UpdateStructTypeRef();
};

template<>
struct TStructOpsTypeTraits<FMoverUserDefinedDataStruct> : public TStructOpsTypeTraitsBase2<FMoverUserDefinedDataStruct>
{
	enum
	{
		WithCopy = true,
		WithPostSerialize = true
	};
};


#undef UE_API
