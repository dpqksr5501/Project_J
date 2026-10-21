#include "Animation/Project_JPlayerLocomotionLibrary.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/Skeleton.h"
#include "Hash/Blake3.h"
#include "Serialization/BufferArchive.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "UObject/UnrealType.h"

FString UProject_JPlayerLocomotionLibrary::GetAnimationMotionFingerprint(UAnimSequence* Sequence)
{
 if(!Sequence || !Sequence->GetDataModel() || !Sequence->GetSkeleton())return FString();
 const IAnimationDataModel* Model=Sequence->GetDataModel();
 FBufferArchive Bytes;
 FObjectAndNameAsStringProxyArchive Ar(Bytes,false);
 FString Version=TEXT("ProjectJ.AnimationMotion.v1");Ar<<Version;
 // A matching bone name list alone cannot establish compatible bind poses.
 const FReferenceSkeleton& Skeleton=Sequence->GetSkeleton()->GetReferenceSkeleton();
 int32 Bones=Skeleton.GetNum();Ar<<Bones;
 for(int32 Index=0;Index<Bones;++Index)
 {
  FString Name=Skeleton.GetBoneName(Index).ToString().ToLower();Ar<<Name;
  int32 Parent=Skeleton.GetParentIndex(Index);Ar<<Parent;
  FTransform Pose=Skeleton.GetRefBonePose()[Index];Ar<<Pose;
 }
 FFrameRate Rate=Model->GetFrameRate();Ar<<Rate.Numerator<<Rate.Denominator;
 int32 Frames=Model->GetNumberOfFrames();Ar<<Frames;
 TArray<FName> Tracks;Model->GetBoneTrackNames(Tracks);
 Tracks.Sort([](FName A,FName B){return A.LexicalLess(B);});
 int32 TrackCount=Tracks.Num();Ar<<TrackCount;
 for(FName Track:Tracks)
 {
  FString Name=Track.ToString().ToLower();Ar<<Name;
  TArray<FTransform> Transforms;Model->GetBoneTrackTransforms(Track,Transforms);
  Ar<<Transforms;
 }
 // Serialize reflected curve/attribute data, not compressed caches, model GUIDs,
 // sequence outers or import filenames. Object/name values use stable strings.
 FAnimationCurveData Curves=Model->GetCurveData();
 FAnimationCurveData::StaticStruct()->SerializeItem(Ar,&Curves,nullptr);
 int32 Attributes=Model->GetAttributes().Num();Ar<<Attributes;
 for(const FAnimatedBoneAttribute& Attribute:Model->GetAttributes())
 {
  FAnimatedBoneAttribute Copy=Attribute;
  FAnimatedBoneAttribute::StaticStruct()->SerializeItem(Ar,&Copy,nullptr);
 }
 if(Ar.IsError())return FString();
 return LexToString(FBlake3::HashBuffer(Bytes.GetData(),Bytes.Num()));
}

FString UProject_JPlayerLocomotionLibrary::GetAnimationNotifyFingerprint(UAnimSequence* Sequence)
{
 if(!Sequence)return FString();
 FBufferArchive Bytes;
 FString Version=TEXT("ProjectJ.AnimationNotifies.v1");Bytes<<Version;
 auto AddText=[&](FString Text)
 {
  Text.ReplaceInline(*Sequence->GetPathName(),TEXT("$Sequence"));
  Bytes<<Text;
 };
 for(const auto& Event:Sequence->Notifies)
 {
  FAnimNotifyEvent Copy=Event;Copy.Guid.Invalidate();Copy.Notify=nullptr;Copy.NotifyStateClass=nullptr;
  FString Text;FAnimNotifyEvent::StaticStruct()->ExportText(Text,&Copy,nullptr,nullptr,PPF_None,nullptr);AddText(Text);
  for(UObject* Notify:{static_cast<UObject*>(Event.Notify.Get()),static_cast<UObject*>(Event.NotifyStateClass.Get())})
  {
   AddText(Notify?Notify->GetClass()->GetPathName():FString());
   if(!Notify)continue;
   for(TFieldIterator<FProperty> Property(Notify->GetClass());Property;++Property)
   {
    if(Property->HasAnyPropertyFlags(CPF_Transient|CPF_Deprecated))continue;
    AddText(Property->GetName());Text.Reset();
    Property->ExportTextItem_Direct(Text,Property->ContainerPtrToValuePtr<void>(Notify),nullptr,Notify,PPF_None);
    AddText(Text);
   }
  }
 }
 return LexToString(FBlake3::HashBuffer(Bytes.GetData(),Bytes.Num()));
}
