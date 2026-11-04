#include "Testing/Project_JRenderedCrowdFixture.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "ImageWriteQueue.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "UnrealClient.h"
#include "UObject/StrongObjectPtr.h"

bool UProject_JRenderedCrowdFixture::PrepareAsyncScreenshots(FString& Failure)
{
    if (!NPCCombatCount) return true;
    check(IsInGameThread());
    auto* Viewport=GEngine?GEngine->GameViewport.Get():nullptr;
    const auto* DelegateEnabled=IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenshotDelegate"));
    if (!IsValid(Viewport) || Viewport->GetWorld()!=GetWorld() || !DelegateEnabled || !DelegateEnabled->GetInt() ||
        UGameViewportClient::OnScreenshotCaptured().IsBound() || FScreenshotRequest::IsScreenshotRequested())
    { Failure=TEXT("Exclusive ordinary viewport screenshot capture is unavailable"); return false; }
    // Load the supported writer during setup, outside the measured phases.
    AsyncScreenshotQueue=&FModuleManager::LoadModuleChecked<IImageWriteQueueModule>(TEXT("ImageWriteQueue")).GetWriteQueue();
    ScreenshotViewport=Viewport;
    return true;
}

bool UProject_JRenderedCrowdFixture::PollAsyncScreenshot(bool& Ready, FString& Failure)
{
    check(IsInGameThread());
    Ready=false;
    if (!NPCCombatCount) { Ready=true; return true; }
    if (!ScreenshotFailure.IsEmpty()) { Failure=ScreenshotFailure; return false; }
    if (ScreenshotWrite.IsValid() && ScreenshotWrite.IsReady())
    {
        const bool bWritten=ScreenshotWrite.Get();
        ScreenshotWrite=TFuture<bool>();
        if (!bWritten)
        { ScreenshotFailure=Failure=TEXT("Asynchronous PNG write failed"); return false; }
        ++ScreenshotWritesCompleted; ScreenshotCompletedMask|=1u<<ScreenshotPhase;
        ScreenshotFilename.Reset(); ScreenshotPhase=0;
    }
    Ready=!bAwaitingScreenshotPixels && !ScreenshotWrite.IsValid();
    return true;
}

bool UProject_JRenderedCrowdFixture::RequestFixtureScreenshot(const FString& Filename, FString& Failure)
{
    check(IsInGameThread());
    if (!NPCCombatCount) { FScreenshotRequest::RequestScreenshot(Filename,false,false); return true; }
    bool Ready=false;
    if (!PollAsyncScreenshot(Ready,Failure)) return false;
    const auto* DelegateEnabled=IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenshotDelegate"));
    if (!Ready || !AsyncScreenshotQueue || !GEngine || !ScreenshotViewport.IsValid() ||
        GEngine->GameViewport.Get()!=ScreenshotViewport.Get() || ScreenshotViewport->GetWorld()!=GetWorld() ||
        !DelegateEnabled || !DelegateEnabled->GetInt() || UGameViewportClient::OnScreenshotCaptured().IsBound() ||
        FScreenshotRequest::IsScreenshotRequested() || GIsHighResScreenshot || GIsDumpingMovie || Phase<2 || Phase>6)
    { Failure=TEXT("Screenshot capture ownership or single-write capacity unavailable"); return false; }
    ScreenshotFilename=FPaths::ConvertRelativePathToFull(Filename); ScreenshotPhase=Phase;
    bAwaitingScreenshotPixels=true;
    ScreenshotCaptureHandle=UGameViewportClient::OnScreenshotCaptured().AddUObject(this,&ThisClass::ReceiveScreenshot);
    FScreenshotRequest::RequestScreenshot(ScreenshotFilename,false,false);
    return true;
}

void UProject_JRenderedCrowdFixture::ReceiveScreenshot(int32 Width, int32 Height, const TArray<FColor>& Pixels)
{
    check(IsInGameThread());
    const TStrongObjectPtr<ThisClass> KeepSelf(this);
    // This global engine delegate replaces the stock synchronous encoder only
    // for our one exact request. Never clear other listeners or reset requests.
    const bool bOwnRequest=bAwaitingScreenshotPixels && !bFinished && ScreenshotPhase>=2 && ScreenshotPhase<=6 &&
        GEngine && ScreenshotViewport.IsValid() && GEngine->GameViewport.Get()==ScreenshotViewport.Get() && ScreenshotViewport->GetWorld()==GetWorld() &&
        FPaths::IsSamePath(FScreenshotRequest::GetFilename(),ScreenshotFilename);
    ReleaseAsyncScreenshotCapture();
    constexpr int64 MaxOwnedPixels=16ll*1024*1024; // At most64MiB owned raw BGRA, one write.
    const int64 Count=int64(Width)*int64(Height);
    if (!bOwnRequest || !AsyncScreenshotQueue || ScreenshotWrite.IsValid() || Width<=0 || Height<=0 ||
        Count>MaxOwnedPixels || Count!=Pixels.Num())
    { ScreenshotFailure=TEXT("Screenshot callback identity, dimensions or capacity mismatch"); return; }
    TArray64<FColor> OwnedPixels; OwnedPixels.Append(Pixels.GetData(),Pixels.Num());
    auto Task=MakeUnique<FImageWriteTask>();
    Task->Filename=ScreenshotFilename; Task->Format=EImageFormat::PNG;
    Task->CompressionQuality=0; Task->bOverwriteFile=false;
    // Engine screenshot pixels already have opaque alpha. Preserve their BGRA8
    // sRGB values, dimensions and frame; no worker callback references a UObject.
    Task->PixelData=MakeUnique<TImagePixelData<FColor>>(FIntPoint(Width,Height),MoveTemp(OwnedPixels));
    ScreenshotWrite=AsyncScreenshotQueue->Enqueue(MoveTemp(Task),false);
    if (!ScreenshotWrite.IsValid()) { ScreenshotFailure=TEXT("Image writer queue refused the bounded screenshot"); return; }
    ++ScreenshotWritesStarted;
}

void UProject_JRenderedCrowdFixture::ReleaseAsyncScreenshotCapture()
{
    check(IsInGameThread());
    if (ScreenshotCaptureHandle.IsValid()) UGameViewportClient::OnScreenshotCaptured().Remove(ScreenshotCaptureHandle);
    ScreenshotCaptureHandle.Reset(); bAwaitingScreenshotPixels=false;
    // The engine queue owns already-enqueued pixels/filename until completion or
    // module shutdown. Teardown neither waits on the GT nor changes a failed run.
}
