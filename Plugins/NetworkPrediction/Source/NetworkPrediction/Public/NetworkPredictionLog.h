// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Logging/LogMacros.h"

NETWORKPREDICTION_API DECLARE_LOG_CATEGORY_EXTERN(LogNetworkPrediction, Log, All);

#if !NO_LOGGING
namespace UE::Net
{
// Service-lifetime text/CSV totals only; not simulation, per-instance or wire state.
struct FIndependentGapLogTotals
{
    uint64 Count=0, Early=0, Late=0;
    int64 PeakAbsMS=0;
    double FirstQPC=0., LastQPC=0., NextTextQPC=0.;
};
NETWORKPREDICTION_API void ReportIndependentGap(FIndependentGapLogTotals&, const TCHAR* Model, uint32 Count, uint32 Early, uint32 Late, int64 PeakAbsMS, bool bFinal=false);
}
#endif
