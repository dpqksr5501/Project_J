// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Trace/DataProcessors/ChaosVDDataProcessorBase.h"

/**
 * Data processor implementation that is able to deserialize traced Mover data
 */
class FMoverCVDSimDataProcessor final : public FChaosVDDataProcessorBase
{
public:
	explicit FMoverCVDSimDataProcessor();
	
	virtual bool ProcessRawData(const TArray<uint8>& InData) override;
};
