// Copyright Epic Games, Inc. All Rights Reserved.
// Private bounded transport codec. No model precision/cadence changes.
#include "Iris/Serialization/NetBitStreamReader.h"
#include "Iris/Serialization/NetBitStreamWriter.h"

namespace UE::Net::ProjectJNPBlobDelta
{
static constexpr uint32 MaxBlobBits = FReplicationProxy::MaxNumBits;
static constexpr uint32 MaxWords = (MaxBlobBits + 31U) / 32U;
static constexpr uint32 MaxMaskWords = (MaxWords + 31U) / 32U;
static_assert(MaxBlobBits == 16384U);

struct FPlan
{
    uint32 ChangedWordMask[MaxMaskWords];
    uint32 EncodedBits;
};

static uint32 SignificantBitsInWord(uint32 NumBits, uint32 WordIndex)
{
    return FMath::Min(32U, NumBits - WordIndex * 32U);
}

static uint32 SignificantMask(uint32 BitCount)
{
    // BitCount is locally derived in [1,32], never a network-controlled shift.
    return BitCount == 32U ? ~0U : ((1U << BitCount) - 1U);
}

// Current and Prev must each have ceil(NumBits/32) readable words.
// Plan is local scratch, independent of any replication baseline lifetime.
static bool BuildPlan(const uint32* Current, const uint32* Prev, uint32 NumBits, FPlan& Plan)
{
    Plan = FPlan{};
    if (!Current || !Prev || NumBits == 0U || NumBits > MaxBlobBits)
    {
        return false;
    }
    const uint32 WordCount = (NumBits + 31U) / 32U;
    Plan.EncodedBits = WordCount; // One bounded mask bit per data word.
    for (uint32 WordIndex = 0U; WordIndex < WordCount; ++WordIndex)
    {
        const uint32 WordBits = SignificantBitsInWord(NumBits, WordIndex);
        const uint32 Delta = (Current[WordIndex] ^ Prev[WordIndex]) & SignificantMask(WordBits);
        if (Delta != 0U)
        {
            Plan.ChangedWordMask[WordIndex / 32U] |= 1U << (WordIndex % 32U);
            Plan.EncodedBits += WordBits;
        }
    }
    return true;
}

// The caller selects this only if Plan.EncodedBits < NumBits. Otherwise full.
// Length/mode/common exports are outside this body and identical for both modes.
static bool Write(FNetBitStreamWriter& Writer, const uint32* Current, const uint32* Prev,
                  uint32 NumBits, const FPlan& Plan)
{
    if (!Current || !Prev || NumBits == 0U || NumBits > MaxBlobBits)
    {
        return false;
    }
    const uint32 WordCount = (NumBits + 31U) / 32U;
    Writer.WriteBitStream(Plan.ChangedWordMask, 0U, WordCount);
    for (uint32 WordIndex = 0U; WordIndex < WordCount && !Writer.IsOverflown(); ++WordIndex)
    {
        if ((Plan.ChangedWordMask[WordIndex / 32U] & (1U << (WordIndex % 32U))) != 0U)
        {
            const uint32 WordBits = SignificantBitsInWord(NumBits, WordIndex);
            Writer.WriteBits(Current[WordIndex] ^ Prev[WordIndex], WordBits);
        }
    }
    return !Writer.IsOverflown();
}

// Prev is an acked, immutable baseline. Out is caller-owned temporary scratch.
// The caller commits Out to target dynamic storage ONLY after successful decode.
// All malformed/truncated paths leave Prev untouched and return false.
static bool Read(FNetBitStreamReader& Reader, const uint32* Prev, uint32 NumBits, uint32* Out)
{
    if (!Prev || !Out || Out == Prev || NumBits == 0U || NumBits > MaxBlobBits)
    {
        return false;
    }
    const uint32 WordCount = (NumBits + 31U) / 32U;
    uint32 ChangedWordMask[MaxMaskWords] = {};
    Reader.ReadBitStream(ChangedWordMask, WordCount);
    if (Reader.IsOverflown())
    {
        return false;
    }

    uint32 RequiredDataBits = 0U;
    for (uint32 WordIndex = 0U; WordIndex < WordCount; ++WordIndex)
    {
        if ((ChangedWordMask[WordIndex / 32U] & (1U << (WordIndex % 32U))) != 0U)
        {
            RequiredDataBits += SignificantBitsInWord(NumBits, WordIndex);
        }
    }
    if (RequiredDataBits > Reader.GetBitsLeft())
    {
        Reader.DoOverflow();
        return false;
    }

    for (uint32 WordIndex = 0U; WordIndex < WordCount; ++WordIndex)
    {
        const uint32 WordBits = SignificantBitsInWord(NumBits, WordIndex);
        const bool Changed = (ChangedWordMask[WordIndex / 32U] & (1U << (WordIndex % 32U))) != 0U;
        const uint32 Delta = Changed ? Reader.ReadBits(WordBits) : 0U;
        Out[WordIndex] = (Prev[WordIndex] ^ Delta) & SignificantMask(WordBits);
    }
    return !Reader.IsOverflown();
}
} // namespace UE::Net::ProjectJNPBlobDelta
