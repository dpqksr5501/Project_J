// Copyright Epic Games, Inc. All Rights Reserved.

#include "NetworkPredictionNetSerializers.h"
#include "NetworkPredictionConfig.h"
#include "NetworkPredictionLog.h"
#include "NetworkPredictionProxy.h"
#include "NetworkPredictionReplicationProxy.h"
#include "HAL/IConsoleManager.h"
#include "ProfilingDebugging/CsvProfiler.h"
#include "ProjectJNPBlobWordDelta.inl"
#if !UE_BUILD_SHIPPING
#include <atomic>
#endif
#include "Engine/NetConnection.h"
#include "Iris/ReplicationState/PropertyNetSerializerInfoRegistry.h"
#include "Iris/Serialization/IrisObjectReferencePackageMap.h"
#include "Iris/Serialization/IrisPackageMapExportUtil.h"
#include "Iris/Serialization/NetBitStreamUtil.h"
#include "Iris/Serialization/NetSerializerDelegates.h"
#include "Net/Iris/ReplicationSystem/ReplicationSystemUtil.h"
#include "Net/Iris/ReplicationSystem/EngineReplicationBridge.h"


#include UE_INLINE_GENERATED_CPP_BY_NAME(NetworkPredictionNetSerializers)



namespace UE::Net
{

struct FNetworkPredictionProxyNetSerializer
{
	// Version
	static const uint32 Version = 0;

	// Types
	struct FQuantizedData
	{
		int32 SpawnID;
		FNetworkPredictionInstanceArchetype InstanceArchetype;

		bool IsEqual(const FQuantizedData& Other) const
		{
			return (SpawnID == Other.SpawnID && 
			       InstanceArchetype.TickingMode == Other.InstanceArchetype.TickingMode);
		}
	};

	typedef FNetworkPredictionProxy SourceType;
	typedef FQuantizedData QuantizedType;
	typedef FNetworkPredictionProxyNetSerializerConfig ConfigType;

	inline static const ConfigType DefaultConfig;

	static void Serialize(FNetSerializationContext&, const FNetSerializeArgs&);
	static void Deserialize(FNetSerializationContext&, const FNetDeserializeArgs&);

	static void Quantize(FNetSerializationContext&, const FNetQuantizeArgs&);
	static void Dequantize(FNetSerializationContext&, const FNetDequantizeArgs&);

	static bool IsEqual(FNetSerializationContext&, const FNetIsEqualArgs&);

	// Avoid overwriting some members in the target FNetworkPredictionProxy instance.
	static void Apply(FNetSerializationContext&, const FNetApplyArgs&);

private:
	class FNetSerializerRegistryDelegates final : private UE::Net::FNetSerializerRegistryDelegates
	{
	public:
		virtual ~FNetSerializerRegistryDelegates();

	private:
		virtual void OnPreFreezeNetSerializerRegistry() override;

		bool QuantizedTypeMeetRequirements() const;
		void InitNetSerializer();

		inline static const FName NetworkPredictionProxyNetSerializerName = FName("NetworkPredictionProxy");
		UE_NET_IMPLEMENT_NAMED_STRUCT_NETSERIALIZER_INFO(NetworkPredictionProxyNetSerializerName, FNetworkPredictionProxyNetSerializer);
	};

	inline static FNetworkPredictionProxyNetSerializer::FNetSerializerRegistryDelegates NetSerializerRegistryDelegates;
};

UE_NET_IMPLEMENT_SERIALIZER(FNetworkPredictionProxyNetSerializer);

// FNetworkPredictionProxyNetSerializer implementation
void FNetworkPredictionProxyNetSerializer::Serialize(FNetSerializationContext& Context, const FNetSerializeArgs& Args)
{
	// Write to the network bitstream from the quantized representation
	const QuantizedType& Value = *reinterpret_cast<const QuantizedType*>(Args.Source);

	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();

	WritePackedInt32(Writer, Value.SpawnID);
	Writer->WriteBits((uint8)Value.InstanceArchetype.TickingMode, 8u);
}

void FNetworkPredictionProxyNetSerializer::Deserialize(FNetSerializationContext& Context, const FNetDeserializeArgs& Args)
{
	// Read a quantized representation from the network bitstream
	QuantizedType& Target = *reinterpret_cast<QuantizedType*>(Args.Target);

	FNetBitStreamReader* Reader = Context.GetBitStreamReader();

	Target.SpawnID = ReadPackedInt32(Reader);

	const uint8 TickingModeAsByte = static_cast<uint8>(Reader->ReadBits(8u));
	Target.InstanceArchetype.TickingMode = static_cast<ENetworkPredictionTickingPolicy>(TickingModeAsByte);
}


void FNetworkPredictionProxyNetSerializer::Quantize(FNetSerializationContext& Context, const FNetQuantizeArgs& Args)
{
	// Create a quantized representation from the full instance shadow copy
	SourceType& Source = *reinterpret_cast<SourceType*>(Args.Source);
	QuantizedType& Target = *reinterpret_cast<QuantizedType*>(Args.Target);

	Target.SpawnID = Source.GetID();
	Target.InstanceArchetype.TickingMode = Source.GetCachedArchetype().TickingMode;
}

void FNetworkPredictionProxyNetSerializer::Dequantize(FNetSerializationContext& Context, const FNetDequantizeArgs& Args)
{
	// Write onto a full shadow copy instance from a quantized representation
	const QuantizedType& Source = *reinterpret_cast<const QuantizedType*>(Args.Source);
	SourceType& Target = *reinterpret_cast<SourceType*>(Args.Target);

	Target.GetID() = FNetworkPredictionID(Source.SpawnID, Target.GetID().GetTraceID());
	Target.GetCachedArchetype().TickingMode = Source.InstanceArchetype.TickingMode;
}

bool FNetworkPredictionProxyNetSerializer::IsEqual(FNetSerializationContext& Context, const FNetIsEqualArgs& Args)
{
	if (Args.bStateIsQuantized)
	{
		const QuantizedType& QuantizedValue0 = *reinterpret_cast<const QuantizedType*>(Args.Source0);
		const QuantizedType& QuantizedValue1 = *reinterpret_cast<const QuantizedType*>(Args.Source1);
		return QuantizedValue0.IsEqual(QuantizedValue1);
	}
	else
	{
		const SourceType& Value0 = *reinterpret_cast<SourceType*>(Args.Source0);
		const SourceType& Value1 = *reinterpret_cast<SourceType*>(Args.Source1);

		return Value0.Identical(&Value1, 0);
	}
}


void FNetworkPredictionProxyNetSerializer::Apply(FNetSerializationContext& Context, const FNetApplyArgs& Args)
{
	// Copy selectively from the shadow copy to the real instance that game-level systems see, then let it configure itself
	SourceType& Source = *reinterpret_cast<SourceType*>(Args.Source);
	SourceType& Target = *reinterpret_cast<SourceType*>(Args.Target);

	const int32 NewID = Source.GetID();	// ID will be set on Target inside ConfigureAfterLoading
	Target.GetCachedArchetype().TickingMode = Source.GetCachedArchetype().TickingMode;

	Target.ConfigureAfterLoading(NewID);
}

// FNetSerializerRegistryDelegates
FNetworkPredictionProxyNetSerializer::FNetSerializerRegistryDelegates::~FNetSerializerRegistryDelegates()
{
	UE_NET_UNREGISTER_NETSERIALIZER_INFO(NetworkPredictionProxyNetSerializerName);
}

void FNetworkPredictionProxyNetSerializer::FNetSerializerRegistryDelegates::OnPreFreezeNetSerializerRegistry()
{
	UE_NET_REGISTER_NETSERIALIZER_INFO(NetworkPredictionProxyNetSerializerName);
}


} // end namespace UE::Net




// FServerReplicationRPCParameterNetSerializer implementation
//  Largely a copy of FCharacterNetworkSerializationPackedBitsNetSerializer to start
namespace UE::Net::Private
{
	static constexpr inline uint32 CalculateRequiredWordCount(uint32 NumBits) { return (NumBits + NumBitsPerDWORD - 1U) / NumBitsPerDWORD; }
}


namespace UE::Net
{

	struct FServerReplicationRPCParameterNetSerializerQuantizedType
	{
		typedef uint32 WordType;
		static constexpr uint32 DefaultReserveNumBits = FServerReplicationRPCParameter::NumReservedBits;
		static constexpr uint32 InlinedWordCount = Private::CalculateRequiredWordCount(DefaultReserveNumBits);

		typedef FNetSerializerArrayStorage<WordType, AllocationPolicies::TInlinedElementAllocationPolicy<InlinedWordCount>> FDataBitsStorage;

		FIrisPackageMapExportsQuantizedType QuantizedExports;
		FDataBitsStorage DataBitsStorage;
		uint32 NumDataBits = 0;
	};

}

template <> struct TIsPODType<UE::Net::FServerReplicationRPCParameterNetSerializerQuantizedType> { enum { Value = true }; };

namespace UE::Net
{

struct FServerReplicationRPCParameterNetSerializer
{
	// Version
	static const uint32 Version = 0;

	// Traits
	static constexpr bool bHasDynamicState = true;
	static constexpr bool bHasCustomNetReference = true;

	typedef uint32 WordType;

	typedef FServerReplicationRPCParameter SourceType;
	typedef FServerReplicationRPCParameterNetSerializerQuantizedType QuantizedType;
	typedef struct FServerReplicationRPCParameterNetSerializerConfig ConfigType;

	static const ConfigType DefaultConfig;

	static void Serialize(FNetSerializationContext&, const FNetSerializeArgs&);
	static void Deserialize(FNetSerializationContext&, const FNetDeserializeArgs&);

	static void Quantize(FNetSerializationContext&, const FNetQuantizeArgs&);
	static void Dequantize(FNetSerializationContext&, const FNetDequantizeArgs&);

	static bool IsEqual(FNetSerializationContext&, const FNetIsEqualArgs&);
	static bool Validate(FNetSerializationContext&, const FNetValidateArgs&);

	static void CloneDynamicState(FNetSerializationContext&, const FNetCloneDynamicStateArgs&);
	static void FreeDynamicState(FNetSerializationContext&, const FNetFreeDynamicStateArgs&);

	static void CollectNetReferences(FNetSerializationContext&, const FNetCollectReferencesArgs&);

private:
	class FNetSerializerRegistryDelegates final : private UE::Net::FNetSerializerRegistryDelegates
	{
	public:
		virtual ~FNetSerializerRegistryDelegates();

	private:
		virtual void OnPreFreezeNetSerializerRegistry() override;
	};

	static void FreeDynamicStateInternal(FNetSerializationContext&, QuantizedType& Value);

	static FServerReplicationRPCParameterNetSerializer::FNetSerializerRegistryDelegates NetSerializerRegistryDelegates;
};

UE_NET_IMPLEMENT_SERIALIZER(FServerReplicationRPCParameterNetSerializer);


const FServerReplicationRPCParameterNetSerializer::ConfigType FServerReplicationRPCParameterNetSerializer::DefaultConfig;
FServerReplicationRPCParameterNetSerializer::FNetSerializerRegistryDelegates FServerReplicationRPCParameterNetSerializer::NetSerializerRegistryDelegates;

void FServerReplicationRPCParameterNetSerializer::Serialize(FNetSerializationContext& Context, const FNetSerializeArgs& Args)
{
	// For now we ignore this in default state hash due to complications with asymmetrically serialized state.
	if (Context.IsInitializingDefaultState())
	{
		return;
	}

	const QuantizedType& Value = *reinterpret_cast<QuantizedType*>(Args.Source);

	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();

	// Serialize captured references and exports
	FIrisPackageMapExportsUtil::Serialize(Context, Value.QuantizedExports);

	// Write data bits
	const uint32 NumDataBits = Value.NumDataBits;
	if (Writer->WriteBool(NumDataBits > 0))
	{
		UE::Net::WritePackedUint32(Writer, NumDataBits);
		Writer->WriteBitStream(Value.DataBitsStorage.GetData(), 0, NumDataBits);
	}
}

// static
void FServerReplicationRPCParameterNetSerializer::FreeDynamicStateInternal(FNetSerializationContext& Context, QuantizedType& Value)
{
	// Free quantized state for captured reference and exports
	FIrisPackageMapExportsUtil::FreeDynamicState(Context, Value.QuantizedExports);

	Value.DataBitsStorage.Free(Context);
	Value.NumDataBits = 0;
}

void FServerReplicationRPCParameterNetSerializer::Deserialize(FNetSerializationContext& Context, const FNetDeserializeArgs& Args)
{
	// For consistency, we should never get here. For now we ignore this in default state hash due to complications with asymmetrically serialized state.
	if (Context.IsInitializingDefaultState())
	{
		return;
	}

	const ConfigType* Config = static_cast<const ConfigType*>(Args.NetSerializerConfig);
	QuantizedType& TargetValue = *reinterpret_cast<QuantizedType*>(Args.Target);

	FNetBitStreamReader* Reader = Context.GetBitStreamReader();

	// Deserialize captured references and exports
	FIrisPackageMapExportsUtil::Deserialize(Context, TargetValue.QuantizedExports);

	const bool bHasDataBits = Reader->ReadBool();
	if (bHasDataBits)
	{
		const uint32 NumDataBits = UE::Net::ReadPackedUint32(Reader);

		const uint32 MaxNumDataBits = Config->MaxQuantizedSizeBits;
		if (NumDataBits > MaxNumDataBits)
		{
			Context.SetError(GNetError_ArraySizeTooLarge);
			UE_LOGF(LogNetworkPrediction, Error, "FServerReplicationRPCParameterNetSerializer::Deserialize: Invalidating RPC param due to NumBits (%u) exceeding allowable limit (%u).", NumDataBits, MaxNumDataBits);
			ensureMsgf(false, TEXT("Invalidating received FServerReplicationRPCParameterRPC due to NumBits exceeding allowable limit"));
			return;
		}

		const uint32 RequiredWordCount = Private::CalculateRequiredWordCount(NumDataBits);
		TargetValue.DataBitsStorage.AdjustSize(Context, RequiredWordCount);

		Reader->ReadBitStream(TargetValue.DataBitsStorage.GetData(), NumDataBits);
		TargetValue.NumDataBits = NumDataBits;
	}
	else
	{
		TargetValue.DataBitsStorage.Free(Context);
		TargetValue.NumDataBits = 0U;
	}
}

void FServerReplicationRPCParameterNetSerializer::Quantize(FNetSerializationContext& Context, const FNetQuantizeArgs& Args)
{
	// Create a quantized representation from a full shadow copy instance
	const SourceType& SourceValue = *reinterpret_cast<const SourceType*>(Args.Source);
	QuantizedType& TargetValue = *reinterpret_cast<QuantizedType*>(Args.Target);

	// Quantize captured references and exports
	FIrisPackageMapExportsUtil::Quantize(Context, SourceValue.PackageMapExports, MakeArrayView(SourceValue.NetTokensPendingExport), TargetValue.QuantizedExports);

	uint32 NumDataBits = SourceValue.DataBits.Num();

	const ConfigType* Config = static_cast<const ConfigType*>(Args.NetSerializerConfig);
	const uint32 MaxNumDataBits = Config->MaxQuantizedSizeBits;

	if (NumDataBits > MaxNumDataBits)
	{
		// This is just to avoid disconnect and instead warn and invalidate the data on the sending side.
		UE_LOGF(LogNetworkPrediction, Error, "FServerReplicationRPCParameterNetSerializer::Quantize: Invalidating RPC param due to NumBits (%u) exceeding allowable limit (%u). See FServerReplicationRPCParameterNetSerializerConfig::DefaultMaxQuantizedSizeBits.", NumDataBits, MaxNumDataBits);
		NumDataBits = 0U;
		ensureMsgf(false, TEXT("Invalidating RPC param due to NumBits exceeding allowable limit"));
	}

	TargetValue.DataBitsStorage.AdjustSize(Context, Private::CalculateRequiredWordCount(NumDataBits));
	if (NumDataBits > 0)
	{
		FMemory::Memcpy(TargetValue.DataBitsStorage.GetData(), SourceValue.DataBits.GetData(), (NumDataBits + 7U) / 8U);
	}
	TargetValue.NumDataBits = NumDataBits;
}

void FServerReplicationRPCParameterNetSerializer::Dequantize(FNetSerializationContext& Context, const FNetDequantizeArgs& Args)
{
	// Write onto a full shadow copy instance from a quantized representation
	const QuantizedType& Source = *reinterpret_cast<const QuantizedType*>(Args.Source);
	SourceType& Target = *reinterpret_cast<SourceType*>(Args.Target);

	// Dequantize captured references and exports and inject into target
	FIrisPackageMapExportsUtil::Dequantize(Context, Source.QuantizedExports, Target.PackageMapExports);

	// DataBits
	Target.DataBits.SetNumUninitialized(Source.NumDataBits);
	Target.DataBits.SetRangeFromRange(0, Source.NumDataBits, Source.DataBitsStorage.GetData());
}

bool FServerReplicationRPCParameterNetSerializer::IsEqual(FNetSerializationContext& Context, const FNetIsEqualArgs& Args)
{
	if (Args.bStateIsQuantized)
	{
		const QuantizedType& Value0 = *reinterpret_cast<const QuantizedType*>(Args.Source0);
		const QuantizedType& Value1 = *reinterpret_cast<const QuantizedType*>(Args.Source1);

		const ConfigType& Config = *static_cast<const ConfigType*>(Args.NetSerializerConfig);
		if (Value0.NumDataBits != Value1.NumDataBits || Value0.NumDataBits > Config.MaxQuantizedSizeBits)
		{
			return false;
		}

		// Captured exports are arrays of serializer storage, not arrays of
		// FName/FNetObjectReference. Compare every element using its public
		// serializer, and include pending token obligations even at equal counts.
		const auto& Exports0 = Value0.QuantizedExports;
		const auto& Exports1 = Value1.QuantizedExports;
		const uint32 ReferenceCount = Exports0.ObjectReferenceStorage.Num();
		const uint32 NameCount = Exports0.NameStorage.Num();
		const uint32 TokenCount = Exports0.NetTokenStorage.Num();
		if (ReferenceCount > FIrisPackageMapExportsUtil::MaxExports || NameCount > FIrisPackageMapExportsUtil::MaxExports
			|| TokenCount > FIrisPackageMapExportsUtil::MaxExports
			|| ReferenceCount != Exports1.ObjectReferenceStorage.Num() || NameCount != Exports1.NameStorage.Num()
			|| TokenCount != Exports1.NetTokenStorage.Num()) return false;
		const FNetSerializer& ObjectSerializer = UE_NET_GET_SERIALIZER(FObjectNetSerializer);
		const FObjectNetSerializerConfig ObjectConfig;
		FNetIsEqualArgs ElementArgs{};
		ElementArgs.bStateIsQuantized = true;
		ElementArgs.Version = ObjectSerializer.Version;
		ElementArgs.NetSerializerConfig = &ObjectConfig;
		for (uint32 ReferenceIndex = 0; ReferenceIndex < ReferenceCount; ++ReferenceIndex)
		{
			ElementArgs.Source0 = NetSerializerValuePointer(Exports0.ObjectReferenceStorage.GetData() + ReferenceIndex);
			ElementArgs.Source1 = NetSerializerValuePointer(Exports1.ObjectReferenceStorage.GetData() + ReferenceIndex);
			if (!ObjectSerializer.IsEqual(Context, ElementArgs)) return false;
		}
		const FNetSerializer& NameSerializer = UE_NET_GET_SERIALIZER(FNameAsNetTokenNetSerializer);
		const FNameAsNetTokenNetSerializerConfig NameConfig;
		ElementArgs.Version = NameSerializer.Version;
		ElementArgs.NetSerializerConfig = &NameConfig;
		for (uint32 NameIndex = 0; NameIndex < NameCount; ++NameIndex)
		{
			ElementArgs.Source0 = NetSerializerValuePointer(&Exports0.NameStorage.GetData()[NameIndex]);
			ElementArgs.Source1 = NetSerializerValuePointer(&Exports1.NameStorage.GetData()[NameIndex]);
			if (!NameSerializer.IsEqual(Context, ElementArgs)) return false;
		}
		for (uint32 TokenIndex = 0; TokenIndex < TokenCount; ++TokenIndex)
			if (!(Exports0.NetTokenStorage.GetData()[TokenIndex] == Exports1.NetTokenStorage.GetData()[TokenIndex])) return false;

		const uint32 RequiredWords = Private::CalculateRequiredWordCount(Value0.NumDataBits);
		if (RequiredWords > Value0.DataBitsStorage.Num() || RequiredWords > Value1.DataBitsStorage.Num()) return false;
		if (RequiredWords > 0 && FMemory::Memcmp(Value0.DataBitsStorage.GetData(), Value1.DataBitsStorage.GetData(), sizeof(WordType) * RequiredWords) != 0)
		{
			return false;
		}
	}
	else
	{
		const SourceType& Value0 = *reinterpret_cast<const SourceType*>(Args.Source0);
		const SourceType& Value1 = *reinterpret_cast<const SourceType*>(Args.Source1);
		const ConfigType& Config = *static_cast<const ConfigType*>(Args.NetSerializerConfig);
		const auto IsWithinBounds = [&Config](const SourceType& Value)
		{
			return uint32(Value.DataBits.Num()) <= Config.MaxQuantizedSizeBits
				&& uint32(Value.PackageMapExports.References.Num()) <= FIrisPackageMapExportsUtil::MaxExports
				&& uint32(Value.PackageMapExports.Names.Num()) <= FIrisPackageMapExportsUtil::MaxExports
				&& uint32(Value.NetTokensPendingExport.Num()) <= FIrisPackageMapExportsUtil::MaxExports;
		};
		if (!IsWithinBounds(Value0) || !IsWithinBounds(Value1)) return false;
		// The legacy body contains export indices. Its typed source metadata and
		// pending export obligations are part of the same comparison contract.
		return Value0.DataBits == Value1.DataBits
			&& Value0.PackageMapExports.References == Value1.PackageMapExports.References
			&& Value0.PackageMapExports.Names == Value1.PackageMapExports.Names
			&& Value0.NetTokensPendingExport == Value1.NetTokensPendingExport;
	}

	return true;
}

bool FServerReplicationRPCParameterNetSerializer::Validate(FNetSerializationContext& Context, const FNetValidateArgs& Args)
{
	const ConfigType* Config = static_cast<const ConfigType*>(Args.NetSerializerConfig);
	const QuantizedType& SourceValue = *reinterpret_cast<const QuantizedType*>(Args.Source);

	const uint32 MaxNumDataBits = Config->MaxQuantizedSizeBits;

	if (SourceValue.NumDataBits > MaxNumDataBits)
	{
		return false;
	}

	if (!FIrisPackageMapExportsUtil::Validate(Context, SourceValue.QuantizedExports))
	{
		return false;
	}

	return true;
}

void FServerReplicationRPCParameterNetSerializer::CloneDynamicState(FNetSerializationContext& Context, const FNetCloneDynamicStateArgs& Args)
{
	const QuantizedType& SourceValue = *reinterpret_cast<const QuantizedType*>(Args.Source);
	QuantizedType& TargetValue = *reinterpret_cast<QuantizedType*>(Args.Target);

	FIrisPackageMapExportsUtil::CloneDynamicState(Context, TargetValue.QuantizedExports, SourceValue.QuantizedExports);

	TargetValue.DataBitsStorage.Clone(Context, SourceValue.DataBitsStorage);
	TargetValue.NumDataBits = SourceValue.NumDataBits;
}

void FServerReplicationRPCParameterNetSerializer::FreeDynamicState(FNetSerializationContext& Context, const FNetFreeDynamicStateArgs& Args)
{
	QuantizedType& Value = *reinterpret_cast<QuantizedType*>(Args.Source);
	FreeDynamicStateInternal(Context, Value);
}

void FServerReplicationRPCParameterNetSerializer::CollectNetReferences(FNetSerializationContext& Context, const FNetCollectReferencesArgs& Args)
{
	const QuantizedType& Value = *reinterpret_cast<const QuantizedType*>(Args.Source);
	FNetReferenceCollector& Collector = *reinterpret_cast<UE::Net::FNetReferenceCollector*>(Args.Collector);

	FIrisPackageMapExportsUtil::CollectNetReferences(Context, Value.QuantizedExports, Args.ChangeMaskInfo, Collector);
}

static const FName PropertyNetSerializerRegistry_NAME_ServerReplicationRPCParameter("ServerReplicationRPCParameter");
UE_NET_IMPLEMENT_NAMED_STRUCT_NETSERIALIZER_INFO(PropertyNetSerializerRegistry_NAME_ServerReplicationRPCParameter, FServerReplicationRPCParameterNetSerializer);


FServerReplicationRPCParameterNetSerializer::FNetSerializerRegistryDelegates::~FNetSerializerRegistryDelegates()
{
	UE_NET_UNREGISTER_NETSERIALIZER_INFO(PropertyNetSerializerRegistry_NAME_ServerReplicationRPCParameter);
}

void FServerReplicationRPCParameterNetSerializer::FNetSerializerRegistryDelegates::OnPreFreezeNetSerializerRegistry()
{
	UE_NET_REGISTER_NETSERIALIZER_INFO(PropertyNetSerializerRegistry_NAME_ServerReplicationRPCParameter);
}

} // end UE::Net


namespace UE::Net
{

	struct FFReplicationProxyNetSerializerQuantizedType
	{
		typedef uint32 WordType;
		static constexpr uint32 DefaultReserveNumBits = FReplicationProxy::NumReservedBits;
		static constexpr uint32 InlinedWordCount = Private::CalculateRequiredWordCount(DefaultReserveNumBits);

		typedef FNetSerializerArrayStorage<WordType, AllocationPolicies::TInlinedElementAllocationPolicy<InlinedWordCount>> FDataBitsStorage;

		FIrisPackageMapExportsQuantizedType QuantizedExports;
		// How many bytes the current allocation can hold.
		uint32 ByteCapacity = 0;
		FDataBitsStorage DataBitsStorage;
		uint32 NumDataBits = 0;

		int32 CachedPendingFrame = INDEX_NONE;
	};

}

template <> struct TIsPODType<UE::Net::FFReplicationProxyNetSerializerQuantizedType> { enum { Value = true }; };


namespace UE::Net
{

static TAutoConsoleVariable<int32> CVarProjectJReplicationBlobDelta(
	TEXT("np.ProjectJ.ReplicationBlobDelta"), 0,
	TEXT("Encode lossless word deltas for NP Iris blobs when strictly smaller. Decoder always supports both modes."),
	ECVF_Default);

CSV_DEFINE_CATEGORY(ProjectJNPBlobDelta, false);

#if !UE_BUILD_SHIPPING
static std::atomic<uint64> GProjectJNPBlobEmptyDecodeCount{0};
static void RecordProjectJNPBlobEmptyDecode()
{
	// Process-wide, monotonic read-only evidence. Never part of a model or baseline.
	GProjectJNPBlobEmptyDecodeCount.fetch_add(1U, std::memory_order_relaxed);
	CSV_CUSTOM_STAT(ProjectJNPBlobDelta, EmptyDecodeCount, 1, ECsvCustomStatOp::Accumulate);
}
#endif

// Attempts are codec work, not sent/acked bytes: Iris may retry or roll back.
struct FProjectJNPBlobWriteScope
{
#if CSV_PROFILER && !CSV_PROFILER_MINIMAL
	FNetSerializationContext& Context;
	const uint32 Begin;
	const bool bDeltaEntry;
#endif
	uint32 InlineExportBits = 0U;
	uint32 PayloadBits = 0U;
	FProjectJNPBlobWriteScope(FNetSerializationContext& InContext, bool bInDeltaEntry)
#if CSV_PROFILER && !CSV_PROFILER_MINIMAL
		: Context(InContext), Begin(InContext.GetBitStreamWriter()->GetPosBits()), bDeltaEntry(bInDeltaEntry)
#endif
	{
		// Iris may serialize on workers. Native CSV recording supplies its own
		// capture/category gating and per-thread storage; IsCapturing is GT-only.
		CSV_CUSTOM_STAT(ProjectJNPBlobDelta, SerializerAttemptCount, 1, ECsvCustomStatOp::Accumulate);
		if (bInDeltaEntry) { CSV_CUSTOM_STAT(ProjectJNPBlobDelta, DeltaAttemptCount, 1, ECsvCustomStatOp::Accumulate); }
	}
	~FProjectJNPBlobWriteScope()
	{
#if CSV_PROFILER && !CSV_PROFILER_MINIMAL
		if (Context.HasErrorOrOverflow())
		{
			CSV_CUSTOM_STAT(ProjectJNPBlobDelta, SerializerErrorOrOverflowCount, 1, ECsvCustomStatOp::Accumulate);
			// Iris can retry/split a valid object after running out of packet space.
			// Explicit context errors remain a separate failure class.
			if (Context.HasError())
			{
				CSV_CUSTOM_STAT(ProjectJNPBlobDelta, SerializerContextErrorCount, 1, ECsvCustomStatOp::Accumulate);
			}
			else
			{
				CSV_CUSTOM_STAT(ProjectJNPBlobDelta, SerializerWriterOverflowAttemptCount, 1, ECsvCustomStatOp::Accumulate);
			}
			return;
		}
		const int32 Written = int32(Context.GetBitStreamWriter()->GetPosBits() - Begin);
		CSV_CUSTOM_STAT(ProjectJNPBlobDelta, SerializerSuccessfulAttemptBits, Written, ECsvCustomStatOp::Accumulate);
		CSV_CUSTOM_STAT(ProjectJNPBlobDelta, WrapperWrittenBits, Written - int32(InlineExportBits) - int32(PayloadBits), ECsvCustomStatOp::Accumulate);
		if (bDeltaEntry) { CSV_CUSTOM_STAT(ProjectJNPBlobDelta, DeltaSuccessfulAttemptBits, Written, ECsvCustomStatOp::Accumulate); }
		else { CSV_CUSTOM_STAT(ProjectJNPBlobDelta, FullSerializeSuccessfulAttemptBits, Written, ECsvCustomStatOp::Accumulate); }
#endif
	}
};

struct FReplicationProxyNetSerializer
{
	// Version
	static const uint32 Version = 1;

	// Traits
	static constexpr bool bHasDynamicState = true;
	static constexpr bool bHasCustomNetReference = true;

	// Types

	typedef FReplicationProxy SourceType;
	typedef FFReplicationProxyNetSerializerQuantizedType QuantizedType;
	typedef FReplicationProxyNetSerializerConfig ConfigType;

	inline static const ConfigType DefaultConfig;

	static void Serialize(FNetSerializationContext&, const FNetSerializeArgs& Args);
	static void Deserialize(FNetSerializationContext&, const FNetDeserializeArgs& Args);
	static void SerializeDelta(FNetSerializationContext&, const FNetSerializeDeltaArgs& Args);
	static void DeserializeDelta(FNetSerializationContext&, const FNetDeserializeDeltaArgs& Args);

	static void Quantize(FNetSerializationContext&, const FNetQuantizeArgs& Args);
	static void Dequantize(FNetSerializationContext&, const FNetDequantizeArgs& Args);

	static bool IsEqual(FNetSerializationContext&, const FNetIsEqualArgs& Args);
	
	static void Apply(FNetSerializationContext&, const FNetApplyArgs& Args);

	static void CloneDynamicState(FNetSerializationContext&, const FNetCloneDynamicStateArgs&);
	static void FreeDynamicState(FNetSerializationContext&, const FNetFreeDynamicStateArgs&);

	static void CollectNetReferences(FNetSerializationContext&, const FNetCollectReferencesArgs&);

private:
	static constexpr uint32 AllocationAlignment = 4U;
	static uint32 GetWordCount(uint32 Bits) { return Bits / 32U + uint32((Bits % 32U) != 0U); }
	static uint32 SerializeCurrentExports(FNetSerializationContext&, const QuantizedType&);
	static void DeserializeCurrentExports(FNetSerializationContext&, QuantizedType&);

	static void FreeDynamicStateInternal(FNetSerializationContext&, QuantizedType& Value);
	static void GrowDynamicStateInternal(FNetSerializationContext&, QuantizedType& Value, uint32 NewBitCount);
	static void AdjustStorageSize(FNetSerializationContext&, QuantizedType& Value, uint32 NewBitCount);

private:
	class FNetSerializerRegistryDelegates final : private UE::Net::FNetSerializerRegistryDelegates
	{
	public:
		virtual ~FNetSerializerRegistryDelegates();

	private:
		virtual void OnPreFreezeNetSerializerRegistry() override;
	};

	static FReplicationProxyNetSerializer::FNetSerializerRegistryDelegates NetSerializerRegistryDelegates;
};


UE_NET_IMPLEMENT_SERIALIZER(FReplicationProxyNetSerializer);


FReplicationProxyNetSerializer::FNetSerializerRegistryDelegates FReplicationProxyNetSerializer::NetSerializerRegistryDelegates;


void FReplicationProxyNetSerializer::Serialize(FNetSerializationContext& Context, const FNetSerializeArgs& Args)
{
	if (Context.IsInitializingDefaultState())
	{
		return;
	}

	const ConfigType* Config = static_cast<const ConfigType*>(Args.NetSerializerConfig);
	const QuantizedType& Value = *reinterpret_cast<const QuantizedType*>(Args.Source);
	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();

	FProjectJNPBlobWriteScope WriteScope(Context, false);
	// Current exports preserve their existing reference/net-token semantics.
	WriteScope.InlineExportBits = SerializeCurrentExports(Context, Value);

	// Write data.
	const uint32 NumDataBits = Value.NumDataBits;
	if (Writer->WriteBool(NumDataBits > 0))
	{
		UE::Net::WritePackedUint32(Writer, NumDataBits);
		const uint32 BodyBegin = Writer->GetPosBits();
		Writer->WriteBitStream(Value.DataBitsStorage.GetData(), 0, NumDataBits);
		WriteScope.PayloadBits = Writer->GetPosBits() - BodyBegin;
	}
}

void FReplicationProxyNetSerializer::Deserialize(FNetSerializationContext& Context, const FNetDeserializeArgs& Args)
{
	// For consistency, we should never get here. For now we ignore this in default state hash due to complications with asymmetrically serialized state.
	if (Context.IsInitializingDefaultState())
	{
		return;
	}

	const ConfigType* Config = static_cast<const ConfigType*>(Args.NetSerializerConfig);
	QuantizedType& Value = *reinterpret_cast<QuantizedType*>(Args.Target);
	const uint32 CurrentBitCount = Value.NumDataBits;

	FNetBitStreamReader* Reader = Context.GetBitStreamReader();

	// Read current exports using the same ownership path as delta.
	DeserializeCurrentExports(Context, Value);
	if (Context.HasErrorOrOverflow()) return;

	const bool bHasDataBits = Reader->ReadBool();
	if (Reader->IsOverflown())
	{
		Context.SetError(GNetError_BitStreamOverflow);
		return;
	}

	if (bHasDataBits)
	{
		// Read the data
		const uint32 NewBitCount = ReadPackedUint32(Reader);
		if (Reader->IsOverflown() || NewBitCount == 0U)
		{
			Context.SetError(Reader->IsOverflown() ? GNetError_BitStreamOverflow : GNetError_InvalidValue);
			return;
		}

		if (!ensureMsgf(NewBitCount <= Config->MaxQuantizedSizeBits,
			TEXT("FReplicationProxyNetSerializer::Deserialize data size of %u bits exceeds maximum of %u bits."),
			NewBitCount, Config->MaxQuantizedSizeBits))
		{
			Context.SetError(GNetError_ArraySizeTooLarge);
			return;
		}

		if (NewBitCount > Reader->GetBitsLeft())
		{
			Context.SetError(GNetError_BitStreamOverflow);
			return;
		}
		AdjustStorageSize(Context, Value, NewBitCount);

		Reader->ReadBitStream(static_cast<uint32*>(Value.DataBitsStorage.GetData()), NewBitCount);
	}
	else
	{
		Value.DataBitsStorage.Free(Context);
		Value.NumDataBits = 0U;
		Value.ByteCapacity = 0U;
#if !UE_BUILD_SHIPPING
		RecordProjectJNPBlobEmptyDecode();
#endif
	}
}

uint32 FReplicationProxyNetSerializer::SerializeCurrentExports(FNetSerializationContext& Context, const QuantizedType& Value)
{
	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();
	const uint32 Before = Writer->GetPosBits();
	// Keep the current refs/names and pending net-token export side effects.
	FIrisPackageMapExportsUtil::Serialize(Context, Value.QuantizedExports);
	if (!Context.HasErrorOrOverflow())
	{
		CSV_CUSTOM_STAT(ProjectJNPBlobDelta, CurrentExportInlineAttemptBits, int32(Writer->GetPosBits() - Before), ECsvCustomStatOp::Accumulate);
	}
	return Writer->GetPosBits() - Before;
}

void FReplicationProxyNetSerializer::DeserializeCurrentExports(FNetSerializationContext& Context, QuantizedType& Value)
{
	FIrisPackageMapExportsUtil::Deserialize(Context, Value.QuantizedExports);
}

void FReplicationProxyNetSerializer::SerializeDelta(FNetSerializationContext& Context, const FNetSerializeDeltaArgs& Args)
{
	if (Context.IsInitializingDefaultState()) return;
	CSV_SCOPED_TIMING_STAT(ProjectJNPBlobDelta, DeltaEncodeCPU);
	FProjectJNPBlobWriteScope WriteScope(Context, true);
	const ConfigType& Config = *static_cast<const ConfigType*>(Args.NetSerializerConfig);
	const QuantizedType& Value = *reinterpret_cast<const QuantizedType*>(Args.Source);
	if (!Args.Prev)
	{
		Context.SetError(GNetError_InvalidValue);
		return;
	}
	const QuantizedType& Prev = *reinterpret_cast<const QuantizedType*>(Args.Prev);
	FNetBitStreamWriter* Writer = Context.GetBitStreamWriter();
	FNetIsEqualArgs EqualArgs{};
	EqualArgs.Version = Args.Version;
	EqualArgs.NetSerializerConfig = Args.NetSerializerConfig;
	EqualArgs.Source0 = Args.Source;
	EqualArgs.Source1 = Args.Prev;
	EqualArgs.bStateIsQuantized = true;
	if (Writer->WriteBool(IsEqual(Context, EqualArgs)))
	{
		CSV_CUSTOM_STAT(ProjectJNPBlobDelta, SameValueCount, 1, ECsvCustomStatOp::Accumulate);
		return;
	}

	const uint32 NumBits = Value.NumDataBits;
	if (NumBits > Config.MaxQuantizedSizeBits)
	{
		Context.SetError(GNetError_ArraySizeTooLarge);
		return;
	}
	const uint32 WordCount = GetWordCount(NumBits);
	if (WordCount > Value.DataBitsStorage.Num())
	{
		Context.SetError(GNetError_InvalidValue);
		return;
	}
	WriteScope.InlineExportBits = SerializeCurrentExports(Context, Value);
	if (Context.HasErrorOrOverflow()) return;
	if (!Writer->WriteBool(NumBits != 0U)) return;
	WritePackedUint32(Writer, NumBits);

	ProjectJNPBlobDelta::FPlan Plan;
	bool bWordDelta = false;
	const bool bEncodeEnabled = CVarProjectJReplicationBlobDelta.GetValueOnAnyThread() != 0;
	if (bEncodeEnabled &&
		NumBits <= ProjectJNPBlobDelta::MaxBlobBits && NumBits == Prev.NumDataBits &&
		WordCount <= Prev.DataBitsStorage.Num())
	{
		CSV_SCOPED_TIMING_STAT(ProjectJNPBlobDelta, WordPlanCPU);
		bWordDelta = ProjectJNPBlobDelta::BuildPlan(Value.DataBitsStorage.GetData(), Prev.DataBitsStorage.GetData(), NumBits, Plan) && Plan.EncodedBits < NumBits;
	}
	if (!bEncodeEnabled) { CSV_CUSTOM_STAT(ProjectJNPBlobDelta, EncoderOffFullCount, 1, ECsvCustomStatOp::Accumulate); }
	else if (NumBits != Prev.NumDataBits) { CSV_CUSTOM_STAT(ProjectJNPBlobDelta, SizeChangeFullCount, 1, ECsvCustomStatOp::Accumulate); }
	else if (!bWordDelta) { CSV_CUSTOM_STAT(ProjectJNPBlobDelta, NoSavingsOrUnavailableFullCount, 1, ECsvCustomStatOp::Accumulate); }
	Writer->WriteBool(bWordDelta);
	const uint32 PayloadBegin = Writer->GetPosBits();
	if (bWordDelta)
	{
		ProjectJNPBlobDelta::Write(*Writer, Value.DataBitsStorage.GetData(), Prev.DataBitsStorage.GetData(), NumBits, Plan);
	}
	else
	{
		Writer->WriteBitStream(Value.DataBitsStorage.GetData(), 0U, NumBits);
	}
	if (!Context.HasErrorOrOverflow())
	{
		WriteScope.PayloadBits = Writer->GetPosBits() - PayloadBegin;
		CSV_CUSTOM_STAT(ProjectJNPBlobDelta, ChangedRawEquivalentBits, int32(NumBits), ECsvCustomStatOp::Accumulate);
		if (bWordDelta)
		{
			CSV_CUSTOM_STAT(ProjectJNPBlobDelta, WordDeltaCount, 1, ECsvCustomStatOp::Accumulate);
			CSV_CUSTOM_STAT(ProjectJNPBlobDelta, WordDeltaBodyBits, int32(Writer->GetPosBits() - PayloadBegin), ECsvCustomStatOp::Accumulate);
		}
		else
		{
			CSV_CUSTOM_STAT(ProjectJNPBlobDelta, FullChangedCount, 1, ECsvCustomStatOp::Accumulate);
			CSV_CUSTOM_STAT(ProjectJNPBlobDelta, FullChangedBodyBits, int32(Writer->GetPosBits() - PayloadBegin), ECsvCustomStatOp::Accumulate);
		}
	}
}

void FReplicationProxyNetSerializer::DeserializeDelta(FNetSerializationContext& Context, const FNetDeserializeDeltaArgs& Args)
{
	if (Context.IsInitializingDefaultState()) return;
	CSV_SCOPED_TIMING_STAT(ProjectJNPBlobDelta, DeltaDecodeCPU);
	// Iris owns the acked baseline separately from the receive-state target.
	if (!Args.Prev || Args.Target == Args.Prev)
	{
		Context.SetError(GNetError_InvalidValue);
		return;
	}
	const ConfigType& Config = *static_cast<const ConfigType*>(Args.NetSerializerConfig);
	const QuantizedType& Prev = *reinterpret_cast<const QuantizedType*>(Args.Prev);
	QuantizedType& Value = *reinterpret_cast<QuantizedType*>(Args.Target);
	FNetBitStreamReader* Reader = Context.GetBitStreamReader();
	const bool bSameValue = Reader->ReadBool();
	if (Reader->IsOverflown())
	{
		Context.SetError(GNetError_BitStreamOverflow);
		return;
	}
	if (bSameValue)
	{
		// Match the engine default deep-clone path; never alias Prev's allocation.
		FreeDynamicStateInternal(Context, Value);
		FMemory::Memcpy(&Value, &Prev, sizeof(QuantizedType));
		FNetCloneDynamicStateArgs CloneArgs{};
		CloneArgs.Version = Args.Version;
		CloneArgs.NetSerializerConfig = Args.NetSerializerConfig;
		CloneArgs.Source = Args.Prev;
		CloneArgs.Target = Args.Target;
		CloneDynamicState(Context, CloneArgs);
		return;
	}
	DeserializeCurrentExports(Context, Value);
	if (Context.HasErrorOrOverflow()) return;
	const bool bHasData = Reader->ReadBool();
	if (Reader->IsOverflown())
	{
		Context.SetError(GNetError_BitStreamOverflow);
		return;
	}
	if (!bHasData)
	{
		Value.DataBitsStorage.Free(Context);
		Value.NumDataBits = 0U;
		Value.ByteCapacity = 0U;
#if !UE_BUILD_SHIPPING
		RecordProjectJNPBlobEmptyDecode();
#endif
		return;
	}
	const uint32 NumBits = ReadPackedUint32(Reader);
	if (Reader->IsOverflown())
	{
		Context.SetError(GNetError_BitStreamOverflow);
		return;
	}
	if (NumBits == 0U || NumBits > Config.MaxQuantizedSizeBits)
	{
		Context.SetError(NumBits == 0U ? GNetError_InvalidValue : GNetError_ArraySizeTooLarge);
		return;
	}
	const bool bWordDelta = Reader->ReadBool();
	if (Reader->IsOverflown())
	{
		Context.SetError(GNetError_BitStreamOverflow);
		return;
	}
	const uint32 WordCount = GetWordCount(NumBits);
	if (bWordDelta)
	{
		if (NumBits > ProjectJNPBlobDelta::MaxBlobBits || NumBits != Prev.NumDataBits || WordCount > Prev.DataBitsStorage.Num())
		{
			Context.SetError(GNetError_InvalidValue);
			return;
		}
		uint32 Decoded[ProjectJNPBlobDelta::MaxWords];
		if (!ProjectJNPBlobDelta::Read(*Reader, Prev.DataBitsStorage.GetData(), NumBits, Decoded))
		{
			Context.SetError(Reader->IsOverflown() ? GNetError_BitStreamOverflow : GNetError_InvalidValue);
			return;
		}
		AdjustStorageSize(Context, Value, NumBits);
		FMemory::Memcpy(Value.DataBitsStorage.GetData(), Decoded, WordCount * sizeof(uint32));
		if (!Context.HasErrorOrOverflow())
		{
			// Successful quantized reconstruction; model Apply is verified separately.
			CSV_CUSTOM_STAT(ProjectJNPBlobDelta, WordDeltaDecodeCount, 1, ECsvCustomStatOp::Accumulate);
		}
	}
	else
	{
		if (NumBits > Reader->GetBitsLeft())
		{
			Context.SetError(GNetError_BitStreamOverflow);
			return;
		}
		AdjustStorageSize(Context, Value, NumBits);
		Reader->ReadBitStream(Value.DataBitsStorage.GetData(), NumBits);
		const uint32 TailBits = NumBits % 32U;
		if (TailBits) Value.DataBitsStorage.GetData()[WordCount - 1U] &= (1U << TailBits) - 1U;
	}
}

void FReplicationProxyNetSerializer::Quantize(FNetSerializationContext& Context, const FNetQuantizeArgs& Args)
{
	CSV_SCOPED_TIMING_STAT(ProjectJNPBlobDelta, QuantizeCPU);
	const ConfigType* Config = static_cast<const ConfigType*>(Args.NetSerializerConfig);
	SourceType& Source = *reinterpret_cast<SourceType*>(Args.Source);
	QuantizedType& Value = *reinterpret_cast<QuantizedType*>(Args.Target);

	Value.CachedPendingFrame = Source.GetCachedPendingFrame();

	UIrisObjectReferencePackageMap* PackageMap = nullptr; 

	if (const AActor* ReplicatingActor = Source.OwnerActor.Get())
	{
		if (UEngineReplicationBridge* ReplicationBridge = UE::Net::FReplicationSystemUtil::GetActorReplicationBridge(ReplicatingActor))
		{
			PackageMap = ReplicationBridge->GetObjectReferencePackageMap();
		}
	}

	// Since this struct uses custom serialization path we need to explicitly capture exports in order to forward them to iris
	UE::Net::FIrisPackageMapExports PackageMapExports;
	UE::Net::FNetTokenExportContext::FNetTokenExports NetTokensPendingExport;

	if (PackageMap)
	{
		PackageMap->InitForWrite(&PackageMapExports);
	}

	// Use the Property serialization and store as binary blob.
	FNetBitWriter Archive(PackageMap, FReplicationProxy::NumReservedBits);
	FNetTokenExportScope NetTokenExportScope(Archive, Context.GetNetTokenStore(), NetTokensPendingExport);

	if (!Context.IsInitializingDefaultState())
	{
		bool bOutSuccess = true;
		Source.NetSerialize(Archive, PackageMap, bOutSuccess);
	}

	const uint64 BitCount = Archive.GetNumBits();

	if (!ensureMsgf(BitCount <= Config->MaxQuantizedSizeBits,
		TEXT("FReplicationProxyNetSerializer::Quantize: data size of %llu bits exceeds maximum of %u bits."),
		BitCount, Config->MaxQuantizedSizeBits))
	{
		Context.SetError(GNetError_ArraySizeTooLarge);
		return;
	}

	if (!ensureMsgf(!Archive.IsError(), TEXT("FReplicationProxyNetSerializer::Quantize: NetBitWriter archive error in NetSerialize")))
	{
		Context.SetError(GNetError_InternalError);
		return;
	}

	if (!Context.IsInitializingDefaultState())
	{
		CSV_CUSTOM_STAT(ProjectJNPBlobDelta, QuantizeBlobCount, 1, ECsvCustomStatOp::Accumulate);
		CSV_CUSTOM_STAT(ProjectJNPBlobDelta, QuantizeBlobBits, int32(BitCount), ECsvCustomStatOp::Accumulate);
		CSV_CUSTOM_STAT(ProjectJNPBlobDelta, QuantizeBlobBitsMin, int32(BitCount), ECsvCustomStatOp::Min);
		CSV_CUSTOM_STAT(ProjectJNPBlobDelta, QuantizeBlobBitsMax, int32(BitCount), ECsvCustomStatOp::Max);
	}
	// Quantize captured exports
	FIrisPackageMapExportsUtil::Quantize(Context, PackageMapExports, NetTokensPendingExport, Value.QuantizedExports);

	// Deal with serialized data
	AdjustStorageSize(Context, Value, static_cast<uint32>(BitCount));
	if (BitCount > 0)
	{
		FMemory::Memcpy(Value.DataBitsStorage.GetData(), Archive.GetData(), (BitCount + 7U) / 8U);
	}
}

void FReplicationProxyNetSerializer::Dequantize(FNetSerializationContext& Context, const FNetDequantizeArgs& Args)
{
	const QuantizedType& Source = *reinterpret_cast<const QuantizedType*>(Args.Source);
	SourceType& Target = *reinterpret_cast<SourceType*>(Args.Target);

	// Property Apply is invoked with a fresh empty context by Iris. Carry the
	// actual receive context with this temporary external state, never globally.
	Target.PackageMapTokenResolveContext = {};
	if (!Context.IsInitializingDefaultState())
	{
		Target.PackageMapTokenResolveContext.NetTokenStore = Context.GetNetTokenStore();
		Target.PackageMapTokenResolveContext.RemoteNetTokenStoreState = Context.GetRemoteNetTokenStoreState();
	}

	// Dequantize captured references and exports and inject into target
	FIrisPackageMapExportsUtil::Dequantize(Context, Source.QuantizedExports, Target.PackageMapExports);

	// DataBits
	Target.DataBits.SetNumUninitialized(Source.NumDataBits);
	Target.DataBits.SetRangeFromRange(0, Source.NumDataBits, Source.DataBitsStorage.GetData());
}

bool FReplicationProxyNetSerializer::IsEqual(FNetSerializationContext& Context, const FNetIsEqualArgs& Args)
{
	if (Args.bStateIsQuantized)
	{
		const QuantizedType& Value0 = *reinterpret_cast<const QuantizedType*>(Args.Source0);
		const QuantizedType& Value1 = *reinterpret_cast<const QuantizedType*>(Args.Source1);

		return (Value0.CachedPendingFrame == Value1.CachedPendingFrame);
	}
	else
	{
		const SourceType& Value0 = *reinterpret_cast<SourceType*>(Args.Source0);
		const SourceType& Value1 = *reinterpret_cast<SourceType*>(Args.Source1);
		return Value0.Identical(&Value1, 0);
	}
}

void FReplicationProxyNetSerializer::Apply(FNetSerializationContext& Context, const FNetApplyArgs& Args)
{
	const SourceType& Source = *reinterpret_cast<const SourceType*>(Args.Source);
	SourceType& Target = *reinterpret_cast<SourceType*>(Args.Target);

	UIrisObjectReferencePackageMap* PackageMap = nullptr;

	if (const AActor* ReplicatingActor = Target.OwnerActor.Get())
	{
		if (UEngineReplicationBridge* ReplicationBridge = UE::Net::FReplicationSystemUtil::GetActorReplicationBridge(ReplicatingActor))
		{
			PackageMap = ReplicationBridge->GetObjectReferencePackageMap();
		}
	}

	// The public scope sets the actual source exports/resolve context for this
	// Apply and invalidates the current read exports on exit. It does not
	// restore an outer nested read. Do not retain this context on live Target.
	FIrisObjectReferencePackageMapReadScope ReadScope(PackageMap, &Source.PackageMapExports, &Source.PackageMapTokenResolveContext);

	uint32 NumDataBits = Source.DataBits.Num();

	if (NumDataBits > 0)
	{
		// Note that we are reading from Source's data bits and net serializing directly from them, 
		// without copying the bits to Target. This avoids an unnecessary memcpy.
		FNetBitReader Archive(PackageMap, static_cast<uint8*>((void*)Source.DataBits.GetData()), NumDataBits);
		bool bOutSuccess = true;
		Target.NetSerialize(Archive, PackageMap, bOutSuccess);
	}
}


void FReplicationProxyNetSerializer::CloneDynamicState(FNetSerializationContext& Context, const FNetCloneDynamicStateArgs& Args)
{
	const QuantizedType& SourceValue = *reinterpret_cast<const QuantizedType*>(Args.Source);
	QuantizedType& TargetValue = *reinterpret_cast<QuantizedType*>(Args.Target);

	FIrisPackageMapExportsUtil::CloneDynamicState(Context, TargetValue.QuantizedExports, SourceValue.QuantizedExports);
	TargetValue.DataBitsStorage.Clone(Context, SourceValue.DataBitsStorage);
	TargetValue.NumDataBits = SourceValue.NumDataBits;
	TargetValue.ByteCapacity = SourceValue.ByteCapacity;
	TargetValue.CachedPendingFrame = SourceValue.CachedPendingFrame;
}

void FReplicationProxyNetSerializer::FreeDynamicState(FNetSerializationContext& Context, const FNetFreeDynamicStateArgs& Args)
{
	FreeDynamicStateInternal(Context, *reinterpret_cast<QuantizedType*>(Args.Source));
}

void FReplicationProxyNetSerializer::FreeDynamicStateInternal(FNetSerializationContext& Context, QuantizedType& Value)
{
	// Free quantized state for captured reference and exports
	FIrisPackageMapExportsUtil::FreeDynamicState(Context, Value.QuantizedExports);

	Value.DataBitsStorage.Free(Context);
	Value.NumDataBits = 0;
	Value.ByteCapacity = 0;
}

void FReplicationProxyNetSerializer::GrowDynamicStateInternal(FNetSerializationContext& Context, QuantizedType& Value, uint32 NewBitCount)
{
	checkSlow(NewBitCount > Value.NumDataBits);

	const uint32 ByteCount = GetWordCount(NewBitCount) * sizeof(uint32);

	const uint32 RequiredWordCount = GetWordCount(NewBitCount);
	Value.DataBitsStorage.AdjustSize(Context, RequiredWordCount);

	// Clear the last word to support IsEqual Memcmp optimization.
	const uint32 LastWordIndex = ByteCount / 4U - 1U;
	static_cast<uint32*>(Value.DataBitsStorage.GetData())[LastWordIndex] = 0U;

	Value.ByteCapacity = ByteCount;
	Value.NumDataBits = NewBitCount;
}


void FReplicationProxyNetSerializer::AdjustStorageSize(FNetSerializationContext& Context, QuantizedType& Value, uint32 NewBitCount)
{
	const uint32 NewByteCapacity = GetWordCount(NewBitCount) * sizeof(uint32);
	if (NewByteCapacity == 0)
	{
		// Free everything
		FreeDynamicStateInternal(Context, Value);
	}
	else if (NewByteCapacity > Value.ByteCapacity)
	{
		GrowDynamicStateInternal(Context, Value, NewBitCount);
	}
	// If byte capacity is within the allocated capacity we just update the bit count and clear the last word
	else
	{
		Value.NumDataBits = NewBitCount;

		// Clear the last word to support IsEqual Memcmp optimization.
		const uint32 LastWordIndex = NewByteCapacity / 4U - 1U;
		static_cast<uint32*>(Value.DataBitsStorage.GetData())[LastWordIndex] = 0U;
	}
}

void FReplicationProxyNetSerializer::CollectNetReferences(FNetSerializationContext& Context, const FNetCollectReferencesArgs& Args)
{
	const ConfigType& Config = *static_cast<const ConfigType*>(Args.NetSerializerConfig);
	const QuantizedType& Value = *reinterpret_cast<const QuantizedType*>(Args.Source);
	FNetReferenceCollector& Collector = *reinterpret_cast<UE::Net::FNetReferenceCollector*>(Args.Collector);

	FIrisPackageMapExportsUtil::CollectNetReferences(Context, Value.QuantizedExports, Args.ChangeMaskInfo, Collector);
}

static const FName PropertyNetSerializerRegistry_NAME_ReplicationProxy("ReplicationProxy");
UE_NET_IMPLEMENT_NAMED_STRUCT_NETSERIALIZER_INFO(PropertyNetSerializerRegistry_NAME_ReplicationProxy, FReplicationProxyNetSerializer);


FReplicationProxyNetSerializer::FNetSerializerRegistryDelegates::~FNetSerializerRegistryDelegates()
{
	UE_NET_UNREGISTER_NETSERIALIZER_INFO(PropertyNetSerializerRegistry_NAME_ReplicationProxy);
}

void FReplicationProxyNetSerializer::FNetSerializerRegistryDelegates::OnPreFreezeNetSerializerRegistry()
{
	UE_NET_REGISTER_NETSERIALIZER_INFO(PropertyNetSerializerRegistry_NAME_ReplicationProxy);
}



} // end of UE::Net






#if !UE_BUILD_SHIPPING
uint64 FReplicationProxy::GetProcessEmptyDecodeCount()
{
    return UE::Net::GProjectJNPBlobEmptyDecodeCount.load(std::memory_order_relaxed);
}
#endif

#include "Tests/ProjectJNPBlobDeltaTests.inl"
