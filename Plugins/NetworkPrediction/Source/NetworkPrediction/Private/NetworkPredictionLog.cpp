// Copyright Epic Games, Inc. All Rights Reserved.

#include "NetworkPredictionLog.h"

DEFINE_LOG_CATEGORY(LogNetworkPrediction);
#if !NO_LOGGING
#include "HAL/PlatformTime.h"
#include "Math/UnrealMathUtility.h"
#include "ProfilingDebugging/CsvProfiler.h"
namespace UE::Net
{
CSV_DECLARE_CATEGORY_EXTERN(ProjectJNPBlobDelta);
void ReportIndependentGap(FIndependentGapLogTotals& S, const TCHAR* Model, uint32 Count, uint32 Early, uint32 Late, int64 PeakAbsMS, bool bFinal)
{
    if (!Count && !S.Count) return;
    const double Now=FPlatformTime::Seconds();
    if (Count)
    {
        if (!S.Count) S.FirstQPC=Now;
        S.LastQPC=Now; S.Count+=Count; S.Early+=Early; S.Late+=Late;
        S.PeakAbsMS=FMath::Max(S.PeakAbsMS,PeakAbsMS);
        CSV_CUSTOM_STAT(ProjectJNPBlobDelta, IndependentGapSimTicks, int32(Count), ECsvCustomStatOp::Accumulate);
        CSV_CUSTOM_STAT(ProjectJNPBlobDelta, IndependentEarlyGapSimTicks, int32(Early), ECsvCustomStatOp::Accumulate);
        CSV_CUSTOM_STAT(ProjectJNPBlobDelta, IndependentLateGapSimTicks, int32(Late), ECsvCustomStatOp::Accumulate);
        CSV_CUSTOM_STAT(ProjectJNPBlobDelta, IndependentGapPeakAbsMS, double(PeakAbsMS), ECsvCustomStatOp::Max);
    }
    if (bFinal || Now>=S.NextTextQPC)
    {
        UE_LOGF(LogNetworkPrediction, Display, "[Interpolate] Interpolation out of sync. model=%ls sim_ticks=%llu early=%llu late=%llu peak_abs_ms=%lld first_qpc=%.9f last_qpc=%.9f final=%u", Model,S.Count,S.Early,S.Late,S.PeakAbsMS,S.FirstQPC,S.LastQPC,uint32(bFinal));
        S.Count=S.Early=S.Late=0; S.PeakAbsMS=0; S.FirstQPC=S.LastQPC=0.; S.NextTextQPC=Now+1.;
    }
}
}
#endif
