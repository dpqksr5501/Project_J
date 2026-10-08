#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Audio/Project_JFoleyTypes.h"
#include "Project_JFoleySubsystem.generated.h"

class UProject_JFoleyComponent;
class UProject_JFoleyAudioProfile;
class USoundAttenuation;
class USoundConcurrency;
struct FStreamableHandle;

struct FProject_JFoleyStats
{
	uint64 Submitted = 0, Played = 0, DistanceRejected = 0, NotReady = 0;
	uint64 DuplicateRejected = 0, CapacityRejected = 0, Expired = 0, BudgetDropped = 0, SurfaceTraces = 0;
	uint64 Unmapped = 0, EventFallbacks = 0, LoopRejected = 0;
	int32 Pending = 0;
	double LastMilliseconds = 0.0;
};

/** Event-driven, listener-aware client scheduler. Post-actor batching never builds an audio backlog. */
UCLASS()
class PROJECT_JCHARACTER_API UProject_JFoleySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldEndPlay(UWorld& World) override;
	bool Submit(UProject_JFoleyComponent* Component, const FProject_JFoleyEvent& Event);
	bool PrepareLocalAudio(UProject_JFoleyComponent* Component);
	void Cancel(UProject_JFoleyComponent* Component);
	const FProject_JFoleyStats& GetStats() const { return Stats; }
protected:
	virtual bool DoesSupportWorldType(EWorldType::Type Type) const override;
private:
	friend class FProjectJFoleyAdmissionTest;
	friend class FProjectJFoleyLifetimeTest;
	struct FRequest
	{
		TWeakObjectPtr<UProject_JFoleyComponent> Component;
		FProject_JFoleyEvent Event;
		FSoftObjectPath Profile;
		double SubmittedAt = 0;
		float Score = 0;
		bool bLocal = false;
		uint64 Order = 0;
	};
	struct FResources
	{
		TSharedPtr<FStreamableHandle> ProfileLoad;
		TSharedPtr<FStreamableHandle> SoundsLoad;
		double LastUsed = 0;
		bool bPrepared = false;
	};
	static constexpr int32 MaxPending = 128;
	static constexpr int32 MaxProfiles = 32;
	static bool ValidateEvent(const FProject_JFoleyEvent& Event, float MaxAge);
	static float ScoreEvent(float Importance, float DistanceSquared, float Radius);
	static bool HigherPriority(const FRequest& A, const FRequest& B);
	static TArray<FRequest> SelectBatch(TArray<FRequest>& Requests, double Now, float MaxAge,
		int32 LocalLimit, int32 RemoteLimit, uint64& OutExpired, uint64& OutDropped);
	void OnPostActorTick(UWorld* World, ELevelTick TickType, float DeltaTime);
	void RefreshListeners();
	bool GetListenerDistanceSquared(const FVector& Location, float& OutDistanceSquared);
	UProject_JFoleyAudioProfile* ResolveReadyProfile(const FSoftObjectPath& Path);
	void PrepareProfile(const FSoftObjectPath& Path);
	void Execute(const FRequest& Request);
	void Stop();
	TArray<FRequest> Pending;
	TArray<FVector> ListenerPositions;
	TMap<FSoftObjectPath, FResources> Resources;
	UPROPERTY(Transient)
	TMap<FSoftObjectPath, TObjectPtr<UProject_JFoleyAudioProfile>> LoadedProfiles;
	UPROPERTY(Transient)
	TMap<FSoftObjectPath, TObjectPtr<USoundAttenuation>> FallbackAttenuation;
	UPROPERTY(Transient)
	TObjectPtr<USoundConcurrency> LocalConcurrency;
	UPROPERTY(Transient)
	TObjectPtr<USoundConcurrency> RemoteConcurrency;
	FDelegateHandle PostActorTickHandle;
	uint64 ListenerFrame = MAX_uint64;
	uint64 NextOrder = 0;
	double RemoteTokens = 12.0;
	double LastTokenTime = -1.0;
	bool bStopped = false;
	FProject_JFoleyStats Stats;
};
