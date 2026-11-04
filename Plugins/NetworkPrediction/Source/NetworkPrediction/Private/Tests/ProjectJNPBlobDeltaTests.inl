// Copyright Epic Games, Inc. All Rights Reserved.
// Included only by NetworkPredictionNetSerializers.cpp; no exported test API.
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "Async/Async.h"
#include "Misc/ScopeExit.h"
#include "Iris/ReplicationSystem/NetTokenStore.h"
#include "Iris/ReplicationSystem/NameTokenStore.h"
#include "UObject/StrongObjectPtr.h"

namespace UE::Net
{
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNPBlobWordCodecTest,
    "ProjectJ.NetworkPrediction.BlobDelta.BoundedWordCodec",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNPBlobWordCodecTest::RunTest(const FString&)
{
    using namespace ProjectJNPBlobDelta;
    const uint32 Lengths[] = {1U,7U,8U,31U,32U,33U,1023U,1024U,4095U,4096U,4097U,16383U,16384U};
    uint32 Prev[MaxWords], Current[MaxWords], Result[MaxWords];
    uint32 Encoded[(MaxBlobBits + MaxWords + 31U) / 32U];
    for (const uint32 Bits : Lengths)
    {
        const uint32 Words = (Bits + 31U) / 32U;
        for (uint32 Pattern = 0U; Pattern < 4U; ++Pattern)
        {
            for (uint32 I = 0U; I < Words; ++I)
            {
                Prev[I] = 0x9E3779B9U * (I + 1U);
                Current[I] = Prev[I] ^ (Pattern == 0U ? 0U : Pattern == 1U ? (I == 0U ? 1U : 0U) : Pattern == 2U ? (I == Words - 1U ? ~0U : 0U) : (0xA17C39D5U * (I + 11U)));
            }
            const uint32 PreviousFirst = Prev[0];
            const uint32 PreviousLast = Prev[Words - 1U];
            FPlan Plan;
            TestTrue(TEXT("Bounded plan"), BuildPlan(Current, Prev, Bits, Plan));
            FNetBitStreamWriter Writer;
            Writer.InitBytes(Encoded, sizeof(Encoded));
            TestTrue(TEXT("Word codec write"), Write(Writer, Current, Prev, Bits, Plan));
            Writer.CommitWrites();
            TestEqual(TEXT("Exact encoded-bit plan"), Writer.GetPosBits(), Plan.EncodedBits);
            FNetBitStreamReader Reader;
            Reader.InitBits(Encoded, Writer.GetPosBits());
            TestTrue(TEXT("Word codec read"), Read(Reader, Prev, Bits, Result));
            for (uint32 I = 0U; I < Words; ++I)
            {
                TestEqual(TEXT("All significant bits exact, padding canonical"), Result[I], Current[I] & SignificantMask(SignificantBitsInWord(Bits, I)));
            }
            TestEqual(TEXT("Baseline first word immutable"), Prev[0], PreviousFirst);
            TestEqual(TEXT("Baseline tail immutable"), Prev[Words - 1U], PreviousLast);
            // Exercise all truncations of the actual encoded body (bounded <=16896).
            for (uint32 Cut = 0U; Cut < Writer.GetPosBits(); ++Cut)
            {
                FNetBitStreamReader ShortReader;
                ShortReader.InitBits(Encoded, Cut);
                if (Read(ShortReader, Prev, Bits, Result))
                {
                    AddError(TEXT("Truncated word body unexpectedly decoded"));
                    break;
                }
            }
        }
    }
    FPlan Invalid;
    TestFalse(TEXT("Zero length rejected"), BuildPlan(Current, Prev, 0U, Invalid));
    TestFalse(TEXT("Length beyond existing cap rejected"), BuildPlan(Current, Prev, MaxBlobBits + 1U, Invalid));
    TestFalse(TEXT("Null baseline rejected"), BuildPlan(Current, nullptr, 1U, Invalid));
    FNetBitStreamReader EmptyReader;
    EmptyReader.InitBits(Encoded, 0U);
    TestFalse(TEXT("Cannot write to baseline"), Read(EmptyReader, Prev, 32U, Prev));
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNPBlobInlineSerializerTest,
    "ProjectJ.NetworkPrediction.BlobDelta.ActualInlineSerializer",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNPBlobInlineSerializerTest::RunTest(const FString&)
{
    // Dispatch the actual registered serializer. No copied/mock codec or private
    // Iris allocator. Inline<=4096 and empty exports deliberately need no heap.
    const FNetSerializer& Serializer = UE_NET_GET_SERIALIZER(FReplicationProxyNetSerializer);
    FReplicationProxyNetSerializerConfig Config;
    FNetSerializationContext StorageContext;
    IConsoleVariable* EncodeMode = CVarProjectJReplicationBlobDelta.AsVariable();
    const int32 SavedMode = EncodeMode->GetInt();
    ON_SCOPE_EXIT { EncodeMode->SetWithCurrentPriority(SavedMode); };
    using FState = FFReplicationProxyNetSerializerQuantizedType;
    auto SetState = [&](FState& State, uint32 Bits, int32 Frame)
    {
        const uint32 Words = (Bits + 31U) / 32U;
        State.DataBitsStorage.AdjustSize(StorageContext, Words);
        State.NumDataBits = Bits;
        State.ByteCapacity = Words * sizeof(uint32);
        State.CachedPendingFrame = Frame;
        for (uint32 I = 0U; I < Words; ++I) State.DataBitsStorage.GetData()[I] = 0x9E3779B9U * (I + 1U);
        if (Bits % 32U) State.DataBitsStorage.GetData()[Words - 1U] &= (1U << (Bits % 32U)) - 1U;
    };
    auto Free = [&](FState& State)
    {
        const uint64 Before = FReplicationProxy::GetProcessEmptyDecodeCount();
        FNetFreeDynamicStateArgs Args{};
        Args.Version = Serializer.Version;
        Args.NetSerializerConfig = &Config;
        Args.Source = NetSerializerValuePointer(&State);
        Serializer.FreeDynamicState(StorageContext, Args);
        TestEqual(TEXT("Releasing a state is not an empty decode"), FReplicationProxy::GetProcessEmptyDecodeCount(), Before);
    };
    auto EqualBits = [&](const FState& Expected, const FState& Actual)
    {
        bool bExact = Expected.NumDataBits == Actual.NumDataBits;
        const uint32 Words = (Expected.NumDataBits + 31U) / 32U;
        if (bExact && Words) bExact = FMemory::Memcmp(Expected.DataBitsStorage.GetData(), Actual.DataBitsStorage.GetData(), Words * sizeof(uint32)) == 0;
        TestTrue(TEXT("Actual serializer reconstructed every quantized payload bit"), bExact);
    };
    uint32 Packet[2048];
    for (int32 Mode = 0; Mode <= 1; ++Mode)
    {
        EncodeMode->SetWithCurrentPriority(Mode);
        const uint32 Lengths[] = {0U,1U,31U,32U,33U,1023U,4095U,4096U};
        for (uint32 Bits : Lengths)
        {
            FState Prev{}, Current{}, Target{};
            SetState(Prev, Bits, 50);
            SetState(Current, Bits, 51);
            SetState(Target, 63U, 1); // Existing target storage must be reusable.
            if (Bits) Current.DataBitsStorage.GetData()[0] ^= 1U;
            FNetSerializeDeltaArgs WriteArgs{};
            WriteArgs.Version = Serializer.Version;
            WriteArgs.NetSerializerConfig = &Config;
            WriteArgs.Source = NetSerializerValuePointer(&Current);
            WriteArgs.Prev = NetSerializerValuePointer(&Prev);
            FNetDeserializeDeltaArgs ReadArgs{};
            ReadArgs.Version = Serializer.Version;
            ReadArgs.NetSerializerConfig = &Config;
            ReadArgs.Target = NetSerializerValuePointer(&Target);
            ReadArgs.Prev = NetSerializerValuePointer(&Prev);
            // The initial full state uses the unchanged public Serialize path.
            {
                const uint64 EmptyBefore = FReplicationProxy::GetProcessEmptyDecodeCount();
                FNetBitStreamWriter InitialWriter;
                InitialWriter.InitBytes(Packet, sizeof(Packet));
                FNetSerializationContext InitialWriteContext(&InitialWriter);
                Serializer.Serialize(InitialWriteContext, WriteArgs);
                InitialWriter.CommitWrites();
                FNetBitStreamReader InitialReader;
                InitialReader.InitBits(Packet, InitialWriter.GetPosBits());
                FNetSerializationContext InitialReadContext(&InitialReader);
                Serializer.Deserialize(InitialReadContext, ReadArgs);
                TestFalse(TEXT("Actual full initial serializer writes cleanly"), InitialWriteContext.HasErrorOrOverflow());
                TestFalse(TEXT("Actual full initial serializer reads cleanly"), InitialReadContext.HasErrorOrOverflow());
                TestEqual(TEXT("Initial full consumes its complete wire body"), InitialReader.GetPosBits(), InitialWriter.GetPosBits());
                EqualBits(Current, Target);
                TestEqual(TEXT("Initial explicit empty decode evidence"), FReplicationProxy::GetProcessEmptyDecodeCount() - EmptyBefore, Bits == 0U ? uint64(1) : uint64(0));
            }
            auto RoundTrip = [&](const FState& Expected)
            {
                const uint64 EmptyBefore = FReplicationProxy::GetProcessEmptyDecodeCount();
                FNetBitStreamWriter Writer;
                Writer.InitBytes(Packet, sizeof(Packet));
                FNetSerializationContext WriteContext(&Writer);
                Serializer.SerializeDelta(WriteContext, WriteArgs);
                Writer.CommitWrites();
                TestFalse(TEXT("Actual serializer writes without error"), WriteContext.HasErrorOrOverflow());
                FNetBitStreamReader FrameReader;
                FrameReader.InitBits(Packet, Writer.GetPosBits());
                const bool bSameFrame = FrameReader.ReadBool();
                TestEqual(TEXT("Existing frame equality is preserved"), bSameFrame, Current.CachedPendingFrame == Prev.CachedPendingFrame);
                if (bSameFrame)
                {
                    TestEqual(TEXT("Same-value wire body remains one bit"), Writer.GetPosBits(), 1U);
                }
                else
                {
                    TestFalse(TEXT("Current object exports empty in inline-only fixture"), FrameReader.ReadBool());
                    TestFalse(TEXT("Current name exports empty in inline-only fixture"), FrameReader.ReadBool());
                    if (FrameReader.ReadBool())
                    {
                        const uint32 WrittenLength = ReadPackedUint32(&FrameReader);
                        const bool bSparseMode = FrameReader.ReadBool();
                        TestEqual(TEXT("Framed length matches the source blob"), WrittenLength, Current.NumDataBits);
                        ProjectJNPBlobDelta::FPlan Plan;
                        const bool bExpectedSparse = Mode != 0 && WrittenLength == Current.NumDataBits && Current.NumDataBits == Prev.NumDataBits &&
                            ProjectJNPBlobDelta::BuildPlan(Current.DataBitsStorage.GetData(), Prev.DataBitsStorage.GetData(), WrittenLength, Plan) && Plan.EncodedBits < WrittenLength;
                        TestEqual(TEXT("Encoder chooses delta exactly when strictly smaller"), bSparseMode, bExpectedSparse);
                        if (bSparseMode && bExpectedSparse) TestEqual(TEXT("Sparse body uses the exact planned bits"), FrameReader.GetBitsLeft(), Plan.EncodedBits);
                    }
                }
                FNetBitStreamReader Reader;
                Reader.InitBits(Packet, Writer.GetPosBits());
                FNetSerializationContext ReadContext(&Reader);
                Serializer.DeserializeDelta(ReadContext, ReadArgs);
                TestFalse(TEXT("Actual serializer reads without error"), ReadContext.HasErrorOrOverflow());
                TestEqual(TEXT("Actual serializer consumes exact wire bits"), Reader.GetPosBits(), Writer.GetPosBits());
                EqualBits(Expected, Target);
                const bool bExplicitEmpty = Current.NumDataBits == 0U && Current.CachedPendingFrame != Prev.CachedPendingFrame;
                TestEqual(TEXT("Only explicit successful empty decode increments process evidence"), FReplicationProxy::GetProcessEmptyDecodeCount() - EmptyBefore, bExplicitEmpty ? uint64(1) : uint64(0));
            };
            RoundTrip(Current);
            if (Bits)
            {
                const uint32 Before = Prev.DataBitsStorage.GetData()[0];
                Target.DataBitsStorage.GetData()[0] ^= 1U;
                TestEqual(TEXT("Target writes do not reach baseline"), Prev.DataBitsStorage.GetData()[0], Before);
                RoundTrip(Current);
                // Existing same-frame equality uses Prev and must deep-clone it.
                Current.CachedPendingFrame = Prev.CachedPendingFrame;
                RoundTrip(Prev);
                Target.DataBitsStorage.GetData()[0] ^= 1U;
                TestEqual(TEXT("Same-value clone owns independent inline storage"), Prev.DataBitsStorage.GetData()[0], Before);
            }
            // Current state size changes cannot use sparse baseline memory.
            SetState(Current, Bits == 4096U ? 4095U : Bits + 1U, 52);
            RoundTrip(Current);
            SetState(Current, 0U, 53);
            RoundTrip(Current);
            SetState(Current, 256U, 54);
            RoundTrip(Current);
            TestTrue(TEXT("Empty to nonempty restores real storage length"), Target.DataBitsStorage.Num() >= 8U);
            Free(Target); Free(Current); Free(Prev);
        }
    }
    // Actual serializer rejects invalid length/baseline/framing, not a helper.
    for (uint32 BadCase = 0U; BadCase < 8U; ++BadCase)
    {
        FReplicationProxyNetSerializerConfig BadConfig;
        if (BadCase == 5U || BadCase == 6U) BadConfig.MaxQuantizedSizeBits = 20000U;
        FState Prev{}, Target{};
        SetState(Prev, 256U, 10);
        SetState(Target, 64U, 2);
        const uint32 PreviousFirst = Prev.DataBitsStorage.GetData()[0];
        FNetBitStreamWriter Writer;
        Writer.InitBytes(Packet, sizeof(Packet));
        Writer.WriteBool(false); // Different frame.
        Writer.WriteBool(false); Writer.WriteBool(false); // Empty current exports.
        Writer.WriteBool(true);
        WritePackedUint32(&Writer, BadCase == 0U ? 0U : (BadCase == 1U || BadCase == 5U || BadCase == 6U) ? FReplicationProxy::MaxNumBits + 1U : BadCase == 2U ? 255U : 256U);
        Writer.WriteBool(BadCase != 5U && BadCase != 7U);
        Writer.CommitWrites(); // Case3 deliberately omits the mask/data body.
        FNetBitStreamReader Reader;
        Reader.InitBits(Packet, Writer.GetPosBits());
        FNetSerializationContext Context(&Reader);
        FNetDeserializeDeltaArgs Args{};
        Args.Version = Serializer.Version;
        Args.NetSerializerConfig = &BadConfig;
        Args.Target = NetSerializerValuePointer(&Target);
        Args.Prev = NetSerializerValuePointer(&Prev);
        if (BadCase == 4U) Args.Target = Args.Prev;
        const uint64 EmptyBefore = FReplicationProxy::GetProcessEmptyDecodeCount();
        Serializer.DeserializeDelta(Context, Args);
        TestTrue(TEXT("Actual serializer fails closed on malformed data"), Context.HasErrorOrOverflow());
        TestEqual(TEXT("Malformed data is not counted as empty decode"), FReplicationProxy::GetProcessEmptyDecodeCount(), EmptyBefore);
        if (BadCase == 5U || BadCase == 7U)
            TestEqual(TEXT("Config-valid full lengths reach raw-body truncation validation"), Context.GetError(), GNetError_BitStreamOverflow);
        TestEqual(TEXT("Rejected packet leaves baseline intact"), Prev.DataBitsStorage.GetData()[0], PreviousFirst);
        Free(Target); Free(Prev);
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNPBlobNativeResolveResetTest,
    "ProjectJ.NetworkPrediction.BlobDelta.NativeResolveContextReset",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNPBlobNativeResolveResetTest::RunTest(const FString&)
{
    // Actual Dequantize dispatch with a public default context verifies clearing.
    // Positive per-connection context forwarding needs the real Iris cohort.
    const FNetSerializer& Serializer = UE_NET_GET_SERIALIZER(FReplicationProxyNetSerializer);
    FReplicationProxyNetSerializerConfig Config;
    FNetSerializationContext Context;
    FFReplicationProxyNetSerializerQuantizedType Quantized{};
    Quantized.DataBitsStorage.AdjustSize(Context, 1U);
    Quantized.DataBitsStorage.GetData()[0] = 0xA13F027BU;
    Quantized.NumDataBits = 32U;
    Quantized.ByteCapacity = sizeof(uint32);
    FNetTokenStore Store; // A real public store, never an invented pointer/context.
    FReplicationProxy External;
    TestNull(TEXT("Native resolve store defaults to null"), External.PackageMapTokenResolveContext.NetTokenStore);
    TestNull(TEXT("Native remote state defaults to null"), External.PackageMapTokenResolveContext.RemoteNetTokenStoreState);
    FNetDequantizeArgs Args{};
    Args.Version = Serializer.Version;
    Args.NetSerializerConfig = &Config;
    Args.Source = NetSerializerValuePointer(&Quantized);
    Args.Target = NetSerializerValuePointer(&External);
    for (int32 DefaultState = 0; DefaultState < 2; ++DefaultState)
    {
        External.PackageMapTokenResolveContext.NetTokenStore = &Store;
        const FReplicationProxy Copy = External;
        TestTrue(TEXT("Native context copies as borrowed values"), Copy.PackageMapTokenResolveContext.NetTokenStore == &Store);
        Context.SetIsInitializingDefaultState(DefaultState != 0);
        Serializer.Dequantize(Context, Args);
        TestFalse(TEXT("Actual dequantize succeeds for inline empty exports"), Context.HasErrorOrOverflow());
        TestNull(TEXT("Dequantize cannot retain a previous source store"), External.PackageMapTokenResolveContext.NetTokenStore);
        TestNull(TEXT("Dequantize cannot retain a previous remote state"), External.PackageMapTokenResolveContext.RemoteNetTokenStoreState);
        TestEqual(TEXT("Context bookkeeping does not change payload bits"), External.DataBits.Num(), 32);
    }
    FNetFreeDynamicStateArgs FreeArgs{};
    FreeArgs.Version = Serializer.Version;
    FreeArgs.NetSerializerConfig = &Config;
    FreeArgs.Source = NetSerializerValuePointer(&Quantized);
    Serializer.FreeDynamicState(Context, FreeArgs);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNPBlobWriterBudgetTest,
    "ProjectJ.NetworkPrediction.BlobDelta.WriterBudgetClassification",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNPBlobWriterBudgetTest::RunTest(const FString&)
{
    const FNetSerializer& Serializer = UE_NET_GET_SERIALIZER(FReplicationProxyNetSerializer);
    FReplicationProxyNetSerializerConfig Config;
    FNetSerializationContext StorageContext;
    FFReplicationProxyNetSerializerQuantizedType Prev{}, Current{};
    Prev.CachedPendingFrame = 1;
    Current.CachedPendingFrame = 2;
    Current.NumDataBits = 256U;
    Current.ByteCapacity = 32U;
    Current.DataBitsStorage.AdjustSize(StorageContext, 8U);
    for (uint32 I = 0U; I < 8U; ++I) Current.DataBitsStorage.GetData()[I] = 0xA13F027BU + I;
    FNetSerializeDeltaArgs Args{};
    Args.Version = Serializer.Version;
    Args.NetSerializerConfig = &Config;
    Args.Source = NetSerializerValuePointer(&Current);
    Args.Prev = NetSerializerValuePointer(&Prev);
    uint32 TinyPacket[1] = {};
    FNetBitStreamWriter Writer;
    Writer.InitBytes(TinyPacket, sizeof(TinyPacket));
    FNetSerializationContext WriteContext(&Writer);
    Serializer.SerializeDelta(WriteContext, Args);
    TestTrue(TEXT("Actual writer reports insufficient packet space"), Writer.IsOverflown());
    TestFalse(TEXT("Packet-space retry is not a serializer context error"), WriteContext.HasError());
    FNetBitStreamWriter ErrorWriter;
    ErrorWriter.InitBytes(TinyPacket, sizeof(TinyPacket));
    FNetSerializationContext ErrorContext(&ErrorWriter);
    Current.NumDataBits = Config.MaxQuantizedSizeBits + 1U;
    Serializer.SerializeDelta(ErrorContext, Args);
    TestTrue(TEXT("Invalid source is a genuine context error"), ErrorContext.HasError());
    TestEqual(TEXT("Genuine error keeps its specific cause"), ErrorContext.GetError(), GNetError_ArraySizeTooLarge);
    FNetFreeDynamicStateArgs FreeArgs{};
    FreeArgs.Version = Serializer.Version;
    FreeArgs.NetSerializerConfig = &Config;
    FreeArgs.Source = NetSerializerValuePointer(&Current);
    Serializer.FreeDynamicState(StorageContext, FreeArgs);
    return !HasAnyErrors();
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNPBlobWorkerSerializerTest,
    "ProjectJ.NetworkPrediction.BlobDelta.WorkerSerializerInstrumentation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNPBlobWorkerSerializerTest::RunTest(const FString&)
{
    // Run the actual serializer off the game thread, including category-off.
    // Worker output is plain data; assertions and category setup remain on GT.
    const FNetSerializer& Serializer = UE_NET_GET_SERIALIZER(FReplicationProxyNetSerializer);
    FReplicationProxyNetSerializerConfig Config;
#if CSV_PROFILER && !CSV_PROFILER_MINIMAL
    FCsvProfiler* Profiler = FCsvProfiler::Get();
    const uint32 Category = CSV_CATEGORY_INDEX(ProjectJNPBlobDelta);
    const bool SavedCategory = Profiler->IsCategoryEnabled(Category);
    ON_SCOPE_EXIT { Profiler->EnableCategoryByIndex(Category, SavedCategory); };
    const int32 CategoryModes = 2;
#else
    const int32 CategoryModes = 1;
#endif
    struct FWorkerResult
    {
        bool bWorker = false, bFull = false, bDelta = false, bBaselineUnchanged = false;
        bool bBudgetOverflow = false, bExplicitError = false;
    };
    auto Execute = [&Serializer, &Config]()
    {
        FWorkerResult Result;
        Result.bWorker = !IsInGameThread();
        using FState = FFReplicationProxyNetSerializerQuantizedType;
        FNetSerializationContext StorageContext;
        FState Prev{}, Current{}, Target{};
        for (FState* State : {&Prev, &Current})
        {
            State->NumDataBits = 256U; State->ByteCapacity = 32U;
            State->DataBitsStorage.AdjustSize(StorageContext, 8U);
            for (uint32 I = 0U; I < 8U; ++I) State->DataBitsStorage.GetData()[I] = 0xA13F027BU + I;
        }
        Prev.CachedPendingFrame = 1; Current.CachedPendingFrame = 2;
        Current.DataBitsStorage.GetData()[0] ^= 1U;
        FNetSerializeDeltaArgs WriteArgs{};
        WriteArgs.Version = Serializer.Version; WriteArgs.NetSerializerConfig = &Config;
        WriteArgs.Source = NetSerializerValuePointer(&Current); WriteArgs.Prev = NetSerializerValuePointer(&Prev);
        FNetDeserializeDeltaArgs ReadArgs{};
        ReadArgs.Version = Serializer.Version; ReadArgs.NetSerializerConfig = &Config;
        ReadArgs.Target = NetSerializerValuePointer(&Target); ReadArgs.Prev = NetSerializerValuePointer(&Prev);
        auto MatchesCurrent = [&]()
        {
            return Target.NumDataBits == Current.NumDataBits &&
                FMemory::Memcmp(Target.DataBitsStorage.GetData(), Current.DataBitsStorage.GetData(), 32U) == 0;
        };
        uint32 Packet[64] = {};
        FNetBitStreamWriter FullWriter; FullWriter.InitBytes(Packet, sizeof(Packet));
        FNetSerializationContext FullWriteContext(&FullWriter);
        Serializer.Serialize(FullWriteContext, WriteArgs); FullWriter.CommitWrites();
        FNetBitStreamReader FullReader; FullReader.InitBits(Packet, FullWriter.GetPosBits());
        FNetSerializationContext FullReadContext(&FullReader);
        Serializer.Deserialize(FullReadContext, ReadArgs);
        Result.bFull = !FullWriteContext.HasErrorOrOverflow() && !FullReadContext.HasErrorOrOverflow() &&
            FullReader.GetPosBits() == FullWriter.GetPosBits() && MatchesCurrent();
        FNetBitStreamWriter DeltaWriter; DeltaWriter.InitBytes(Packet, sizeof(Packet));
        FNetSerializationContext DeltaWriteContext(&DeltaWriter);
        Serializer.SerializeDelta(DeltaWriteContext, WriteArgs); DeltaWriter.CommitWrites();
        FNetBitStreamReader DeltaReader; DeltaReader.InitBits(Packet, DeltaWriter.GetPosBits());
        FNetSerializationContext DeltaReadContext(&DeltaReader);
        Serializer.DeserializeDelta(DeltaReadContext, ReadArgs);
        Result.bDelta = !DeltaWriteContext.HasErrorOrOverflow() && !DeltaReadContext.HasErrorOrOverflow() &&
            DeltaReader.GetPosBits() == DeltaWriter.GetPosBits() && MatchesCurrent();
        Result.bBaselineUnchanged = true;
        for (uint32 I = 0U; I < 8U; ++I)
            Result.bBaselineUnchanged &= Prev.DataBitsStorage.GetData()[I] == 0xA13F027BU + I;
        // Force the real full-body budget path regardless of the encoder CVar.
        Prev.NumDataBits = 0U;
        uint32 TinyPacket[1] = {};
        FNetBitStreamWriter TinyWriter; TinyWriter.InitBytes(TinyPacket, sizeof(TinyPacket));
        FNetSerializationContext TinyContext(&TinyWriter);
        Serializer.SerializeDelta(TinyContext, WriteArgs);
        Result.bBudgetOverflow = TinyWriter.IsOverflown() && !TinyContext.HasError();
        FNetBitStreamWriter InvalidWriter; InvalidWriter.InitBytes(Packet, sizeof(Packet));
        FNetSerializationContext InvalidContext(&InvalidWriter);
        WriteArgs.Prev = 0;
        Serializer.SerializeDelta(InvalidContext, WriteArgs);
        Result.bExplicitError = InvalidContext.HasError() && InvalidContext.GetError() == GNetError_InvalidValue;
        for (FState* State : {&Prev, &Current, &Target})
        {
            FNetFreeDynamicStateArgs FreeArgs{};
            FreeArgs.Version = Serializer.Version; FreeArgs.NetSerializerConfig = &Config;
            FreeArgs.Source = NetSerializerValuePointer(State);
            Serializer.FreeDynamicState(StorageContext, FreeArgs);
        }
        return Result;
    };
    for (int32 CategoryMode = 0; CategoryMode < CategoryModes; ++CategoryMode)
    {
#if CSV_PROFILER && !CSV_PROFILER_MINIMAL
        Profiler->EnableCategoryByIndex(Category, CategoryMode != 0);
#endif
        const FWorkerResult Result = Async(EAsyncExecution::ThreadPool, Execute).Get();
        TestTrue(TEXT("Actual serializer ran off GT"), Result.bWorker);
        TestTrue(TEXT("Worker full serializer round-trip is exact"), Result.bFull);
        TestTrue(TEXT("Worker delta serializer round-trip is exact"), Result.bDelta);
        TestTrue(TEXT("Worker serializer leaves the baseline unchanged"), Result.bBaselineUnchanged);
        TestTrue(TEXT("Worker packet budget overflow is not a context error"), Result.bBudgetOverflow);
        TestTrue(TEXT("Worker missing baseline is an explicit context error"), Result.bExplicitError);
    }
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNPServerRPCExportEqualityTest,
    "ProjectJ.NetworkPrediction.RPCParameter.ExportEquality",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNPServerRPCExportEqualityTest::RunTest(const FString&)
{
    const FPropertyNetSerializerInfo* Info = FPropertyNetSerializerInfoRegistry::FindStructSerializerInfo(FServerReplicationRPCParameter::StaticStruct()->GetFName());
    if (!TestNotNull(TEXT("RPC parameter has its actual registered serializer"), Info)) return false;
    const FNetSerializer* Registered = Info->GetNetSerializer(nullptr);
    if (!TestTrue(TEXT("Registry dispatches the production RPC parameter serializer"), Registered == &UE_NET_GET_SERIALIZER(FServerReplicationRPCParameterNetSerializer))) return false;
    const FNetSerializer& Serializer = *Registered;
    FServerReplicationRPCParameterNetSerializerConfig Config;
    FNetSerializationContext Context;
    using FState = FServerReplicationRPCParameterNetSerializerQuantizedType;
    FState First{}, Same{}, Changed{}, Decoded{};
    auto Free = [&](FState& State)
    {
        FNetFreeDynamicStateArgs Args{};
        Args.Version = Serializer.Version; Args.NetSerializerConfig = &Config; Args.Source = NetSerializerValuePointer(&State);
        Serializer.FreeDynamicState(Context, Args);
    };
    ON_SCOPE_EXIT { Free(Decoded); Free(Changed); Free(Same); Free(First); };
    IConsoleVariable* NameCapture = IConsoleManager::Get().FindConsoleVariable(TEXT("net.iris.EnableIrisPackageMapNameExports"));
    if (!TestNotNull(TEXT("Installed public Iris name capture policy exists"), NameCapture)) return false;
    const int32 SavedNameCapture = NameCapture->GetInt();
    NameCapture->SetWithCurrentPriority(1);
    ON_SCOPE_EXIT { NameCapture->SetWithCurrentPriority(SavedNameCapture); };
    const TStrongObjectPtr<UIrisObjectReferencePackageMap> PackageMap(NewObject<UIrisObjectReferencePackageMap>());
    FServerReplicationRPCParameter FirstSource, SameSource, ChangedSource;
    auto CaptureNames = [&](FServerReplicationRPCParameter& Source, EName LastName)
    {
        // Use the real packagemap serializer. Both values write indices0,1;
        // only the second metadata entry differs. These hardcoded names avoid
        // any private allocator or fabricated net-token/resolve context.
        FNetBitWriter Body(PackageMap.Get(), 64);
        FIrisObjectReferencePackageMapWriteScope Capture(PackageMap.Get(), &Source.PackageMapExports);
        FName Leading(NAME_None), Trailing(LastName);
        Body << Leading; Body << Trailing;
        Source.DataBits.Init(false, int32(Body.GetNumBits()));
        if (Body.GetNumBytes()) FMemory::Memcpy(Source.DataBits.GetData(), Body.GetData(), Body.GetNumBytes());
        TestFalse(TEXT("Normal packagemap writes without archive error"), Body.IsError());
        TestEqual(TEXT("Capture owns both FName metadata entries"), Source.PackageMapExports.Names.Num(), 2);
    };
    auto Quantize = [&](FServerReplicationRPCParameter& Source, FState& State)
    {
        FNetQuantizeArgs Args{};
        Args.Version = Serializer.Version; Args.NetSerializerConfig = &Config;
        Args.Source = NetSerializerValuePointer(&Source); Args.Target = NetSerializerValuePointer(&State);
        Serializer.Quantize(Context, Args);
        TestFalse(TEXT("Registered production quantization succeeds for inline captured metadata"), Context.HasErrorOrOverflow());
    };
    auto Equal = [&](const FState& Left, const FState& Right)
    {
        FNetIsEqualArgs Args{};
        Args.Version = Serializer.Version; Args.NetSerializerConfig = &Config; Args.bStateIsQuantized = true;
        Args.Source0 = NetSerializerValuePointer(&Left); Args.Source1 = NetSerializerValuePointer(&Right);
        return Serializer.IsEqual(Context, Args);
    };
    auto EqualSource = [&](const FServerReplicationRPCParameter& Left, const FServerReplicationRPCParameter& Right)
    {
        FNetIsEqualArgs Args{};
        Args.Version = Serializer.Version; Args.NetSerializerConfig = &Config; Args.bStateIsQuantized = false;
        Args.Source0 = NetSerializerValuePointer(&Left); Args.Source1 = NetSerializerValuePointer(&Right);
        return Serializer.IsEqual(Context, Args);
    };
    CaptureNames(FirstSource, NAME_ByteProperty);
    CaptureNames(SameSource, NAME_ByteProperty);
    CaptureNames(ChangedSource, NAME_IntProperty);
    TestTrue(TEXT("Different trailing name uses exactly the same captured body indices"), FirstSource.DataBits == ChangedSource.DataBits);
    Quantize(FirstSource, First); Quantize(SameSource, Same); Quantize(ChangedSource, Changed);
    TestTrue(TEXT("Identical captured metadata and payload remain equal"), Equal(First, Same));
    TestTrue(TEXT("Self equality remains reflexive"), Equal(First, First));
    TestFalse(TEXT("A changed second FName cannot be hidden by equal raw body"), Equal(First, Changed));
    TestFalse(TEXT("Trailing FName inequality is symmetric"), Equal(Changed, First));
    TestTrue(TEXT("Source dispatcher preserves identical captured metadata"), EqualSource(FirstSource, SameSource));
    TestTrue(TEXT("Source equality remains reflexive"), EqualSource(FirstSource, FirstSource));
    TestFalse(TEXT("Source body indices cannot hide a changed second FName"), EqualSource(FirstSource, ChangedSource));
    TestFalse(TEXT("Source trailing FName inequality is symmetric"), EqualSource(ChangedSource, FirstSource));
    const uint32 OriginalLimit = Config.MaxQuantizedSizeBits;
    if (TestTrue(TEXT("Captured normal source has a nonempty index body"), FirstSource.DataBits.Num() > 0))
    {
        Config.MaxQuantizedSizeBits = uint32(FirstSource.DataBits.Num()) - 1U;
        TestFalse(TEXT("Source equality rejects a body beyond the active config limit"), EqualSource(FirstSource, SameSource));
        Config.MaxQuantizedSizeBits = OriginalLimit;
    }

    // Confirm that full Serialize/Deserialize still carries the actual changed
    // metadata and body; this comparison fix adds no selector or wire format.
    uint32 Packet[64] = {};
    FNetBitStreamWriter Writer; Writer.InitBytes(Packet, sizeof(Packet));
    FNetSerializationContext WriteContext(&Writer);
    FNetSerializeArgs WriteArgs{};
    WriteArgs.Version = Serializer.Version; WriteArgs.NetSerializerConfig = &Config; WriteArgs.Source = NetSerializerValuePointer(&Changed);
    Serializer.Serialize(WriteContext, WriteArgs); Writer.CommitWrites();
    FNetBitStreamReader Reader; Reader.InitBits(Packet, Writer.GetPosBits());
    FNetSerializationContext ReadContext(&Reader);
    FNetDeserializeArgs ReadArgs{};
    ReadArgs.Version = Serializer.Version; ReadArgs.NetSerializerConfig = &Config; ReadArgs.Target = NetSerializerValuePointer(&Decoded);
    Serializer.Deserialize(ReadContext, ReadArgs);
    TestFalse(TEXT("Existing full export wire writes successfully"), WriteContext.HasErrorOrOverflow());
    TestFalse(TEXT("Existing full export wire reads successfully"), ReadContext.HasErrorOrOverflow());
    TestEqual(TEXT("Full read consumes its exact existing bit span"), Reader.GetPosBits(), Writer.GetPosBits());
    TestTrue(TEXT("Wire roundtrip preserves changed metadata and body"), Equal(Changed, Decoded));
    FServerReplicationRPCParameter DecodedSource;
    FNetDequantizeArgs DequantizeArgs{};
    DequantizeArgs.Version = Serializer.Version; DequantizeArgs.NetSerializerConfig = &Config;
    DequantizeArgs.Source = NetSerializerValuePointer(&Decoded); DequantizeArgs.Target = NetSerializerValuePointer(&DecodedSource);
    Serializer.Dequantize(ReadContext, DequantizeArgs);
    if (TestEqual(TEXT("Normal dequantize retains both captured names"), DecodedSource.PackageMapExports.Names.Num(), 2))
        TestEqual(TEXT("Actual second FName remains the changed semantic value"), DecodedSource.PackageMapExports.Names[1], FName(NAME_IntProperty));

    // Pending-only differences are export obligations, not invented gameplay
    // value changes. Compare every real token conservatively at equal counts.
    FNetTokenStore Store;
    FNetTokenStore::FInitParams StoreParams{FNetToken::ENetTokenAuthority::Authority, 1U};
    Store.Init(StoreParams);
    if (!TestTrue(TEXT("A real public name-token data store registers"), Store.RegisterDataStore(MakeUnique<FNameTokenStore>(Store), FNameTokenStore::GetTokenStoreName()))) return false;
    auto* Names = Store.GetDataStore<FNameTokenStore>();
    const FNetToken TokenA = Names->GetOrCreateToken(FName(TEXT("ProjectJ.ExportEquality.A")));
    const FNetToken TokenB = Names->GetOrCreateToken(FName(TEXT("ProjectJ.ExportEquality.B")));
    if (!TestTrue(TEXT("Real data-store tokens are valid and distinct"), TokenA.IsValid() && TokenB.IsValid() && !(TokenA == TokenB))) return false;
    CaptureNames(ChangedSource, NAME_ByteProperty);
    FirstSource.NetTokensPendingExport = {TokenA}; SameSource.NetTokensPendingExport = {TokenA}; ChangedSource.NetTokensPendingExport = {TokenB};
    Quantize(FirstSource, First); Quantize(SameSource, Same); Quantize(ChangedSource, Changed);
    TestTrue(TEXT("Pending-token variant retains the same normal captured body"), FirstSource.DataBits == ChangedSource.DataBits);
    TestTrue(TEXT("Equal full pending-token lists remain equal"), Equal(First, Same));
    TestFalse(TEXT("Equal token counts cannot hide different pending exports"), Equal(First, Changed));
    TestFalse(TEXT("Pending-export inequality is symmetric"), Equal(Changed, First));
    TestTrue(TEXT("Source equality retains equal complete pending lists"), EqualSource(FirstSource, SameSource));
    TestFalse(TEXT("Source same-count pending obligations compare every token"), EqualSource(FirstSource, ChangedSource));
    TestFalse(TEXT("Source pending-export inequality is symmetric"), EqualSource(ChangedSource, FirstSource));
    return !HasAnyErrors();
}

} // namespace UE::Net
#endif
