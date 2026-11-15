#include "MBImportLibrary.h"
#include "MBContactBake.h"
#include "MBTraversal.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

IMPLEMENT_MODULE(FDefaultModuleImpl, MotionBricksTraversalEditor)

namespace {
bool ReadPose(const TSharedPtr<FJsonValue>& Value, FTransform& Out) {
    const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
    if (!Value->TryGetArray(A) || A->Num()!=7) return false;
    double V[7];
    for (int32 I=0; I<7; ++I) if (!(*A)[I]->TryGetNumber(V[I]) || !FMath::IsFinite(V[I])) return false;
    FQuat Q(V[3],V[4],V[5],V[6]);
    if (FMath::Abs(Q.SizeSquared()-1.0)>0.01) return false;
    Q.Normalize();
    Out = FTransform(Q,FVector(V[0],V[1],V[2]));
    return true;
}
bool SaveAsset(UObject* Asset) {
    UPackage* Package = Asset->GetOutermost();
    FAssetRegistryModule::AssetCreated(Asset);
    Package->MarkPackageDirty();
    FSavePackageArgs Args;
    Args.TopLevelFlags = RF_Public|RF_Standalone;
    return UPackage::SavePackage(Package, Asset,
        *FPackageName::LongPackageNameToFilename(Package->GetName(),FPackageName::GetAssetPackageExtension()),Args);
}
}

UMBGeneratedClip* UMBImportLibrary::ImportGeneratedMotion(const FString& Filename, USkeletalMesh* Mesh) {
    auto Fail = [](const TCHAR* Message)->UMBGeneratedClip* { UE_LOG(LogTemp,Error,TEXT("MotionBricks import: %s"),Message); return nullptr; };
    if (!Mesh || !Mesh->GetSkeleton()) return Fail(TEXT("Missing target mesh/skeleton"));
    FString Text;
    TSharedPtr<FJsonObject> Json;
    if (!FFileHelper::LoadFileToString(Text,*Filename) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Json) || !Json)
        return Fail(TEXT("Cannot read generated JSON"));
    FString Schema, Coordinates;
    bool bEvaluation = false;
    double FPS = 0;
    if (!Json->TryGetStringField(TEXT("schema"),Schema) || Schema!=TEXT("pa250.motionbricks.global.v1") ||
        !Json->TryGetBoolField(TEXT("evaluation_only"),bEvaluation) || !bEvaluation ||
        !Json->TryGetStringField(TEXT("coordinates"),Coordinates) || Coordinates!=TEXT("Unreal LH Z-up +X-forward") ||
        !Json->TryGetNumberField(TEXT("fps"),FPS) || FPS != 30)
        return Fail(TEXT("Unsupported schema, evaluation flag, coordinates or sample rate"));
    const TArray<TSharedPtr<FJsonValue>> *Names, *Rest, *Frames;
    if (!Json->TryGetArrayField(TEXT("joint_names"),Names) || !Json->TryGetArrayField(TEXT("rest_global"),Rest) ||
        !Json->TryGetArrayField(TEXT("frames_global"),Frames) || Names->Num()!=Rest->Num() || Frames->Num()<4 || Frames->Num()>18000)
        return Fail(TEXT("Malformed arrays"));
    TArray<FString> SourceNames;
    TArray<FTransform> SourceRest;
    for (int32 I=0; I<Names->Num(); ++I) {
        FString Name; FTransform Pose;
        if (!(*Names)[I]->TryGetString(Name) || !ReadPose((*Rest)[I],Pose)) return Fail(TEXT("Invalid rest pose"));
        SourceNames.Add(Name); SourceRest.Add(Pose);
    }
    const int32 Hips = SourceNames.Find(TEXT("Hips"));
    if (Hips != 0) return Fail(TEXT("Expected SOMA Hips at joint zero"));
    TArray<TArray<FTransform>> Motion;
    for (const TSharedPtr<FJsonValue>& Frame : *Frames) {
        const TArray<TSharedPtr<FJsonValue>>* Values;
        if (!Frame->TryGetArray(Values) || Values->Num()!=Names->Num()) return Fail(TEXT("Invalid frame joint count"));
        TArray<FTransform> Poses;
        for (const auto& Value : *Values) { FTransform Pose; if (!ReadPose(Value,Pose)) return Fail(TEXT("Invalid generated transform")); Poses.Add(Pose); }
        Motion.Add(MoveTemp(Poses));
    }
    const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
    const int32 Pelvis = Ref.FindBoneIndex(TEXT("pelvis"));
    if (Pelvis<0 || Ref.FindBoneIndex(TEXT("root"))!=0) return Fail(TEXT("Only UE mannequin root/pelvis mapping is implemented"));
    TArray<FTransform> RefGlobal;
    for (int32 B=0; B<Ref.GetNum(); ++B) {
        const int32 Parent = Ref.GetParentIndex(B);
        RefGlobal.Add(Parent<0 ? Ref.GetRefBonePose()[B] : Ref.GetRefBonePose()[B]*RefGlobal[Parent]);
    }
    const float SourceHeight = SourceRest[Hips].GetLocation().Z;
    if (SourceHeight < 20.f) return Fail(TEXT("Invalid source height"));
    const float Scale = RefGlobal[Pelvis].GetLocation().Z/SourceHeight;
    if (!FMath::IsFinite(Scale) || Scale < 0.5f || Scale > 1.5f)
        return Fail(TEXT("Unexpected mannequin/SOMA height ratio"));
    const TMap<FName,FString> Mapping = {
        {TEXT("pelvis"),TEXT("Hips")},{TEXT("spine_01"),TEXT("Spine1")},
        {TEXT("spine_03"),TEXT("Spine2")},{TEXT("spine_05"),TEXT("Chest")},
        {TEXT("neck_01"),TEXT("Neck1")},{TEXT("neck_02"),TEXT("Neck2")},{TEXT("head"),TEXT("Head")},
        {TEXT("clavicle_l"),TEXT("LeftShoulder")},{TEXT("upperarm_l"),TEXT("LeftArm")},
        {TEXT("lowerarm_l"),TEXT("LeftForeArm")},{TEXT("hand_l"),TEXT("LeftHand")},
        {TEXT("clavicle_r"),TEXT("RightShoulder")},{TEXT("upperarm_r"),TEXT("RightArm")},
        {TEXT("lowerarm_r"),TEXT("RightForeArm")},{TEXT("hand_r"),TEXT("RightHand")},
        {TEXT("thigh_l"),TEXT("LeftLeg")},{TEXT("calf_l"),TEXT("LeftShin")},
        {TEXT("foot_l"),TEXT("LeftFoot")},{TEXT("ball_l"),TEXT("LeftToeBase")},
        {TEXT("thigh_r"),TEXT("RightLeg")},{TEXT("calf_r"),TEXT("RightShin")},
        {TEXT("foot_r"),TEXT("RightFoot")},{TEXT("ball_r"),TEXT("RightToeBase")}
    };
    // SOMA's T-pose and Quinn's A-pose differ noticeably in the arms. Match
    // anatomical segment directions after rotation retargeting, retaining twist.
    const TMap<FName,TPair<FName,FString>> LimbEnds = {
        {TEXT("upperarm_l"),{TEXT("lowerarm_l"),TEXT("LeftForeArm")}},
        {TEXT("lowerarm_l"),{TEXT("hand_l"),TEXT("LeftHand")}},
        {TEXT("hand_l"),{TEXT("middle_01_l"),TEXT("LeftHandMiddle1")}},
        {TEXT("upperarm_r"),{TEXT("lowerarm_r"),TEXT("RightForeArm")}},
        {TEXT("lowerarm_r"),{TEXT("hand_r"),TEXT("RightHand")}},
        {TEXT("hand_r"),{TEXT("middle_01_r"),TEXT("RightHandMiddle1")}},
        {TEXT("thigh_l"),{TEXT("calf_l"),TEXT("LeftShin")}},
        {TEXT("calf_l"),{TEXT("foot_l"),TEXT("LeftFoot")}},
        {TEXT("foot_l"),{TEXT("ball_l"),TEXT("LeftToeBase")}},
        {TEXT("thigh_r"),{TEXT("calf_r"),TEXT("RightShin")}},
        {TEXT("calf_r"),{TEXT("foot_r"),TEXT("RightFoot")}},
        {TEXT("foot_r"),{TEXT("ball_r"),TEXT("RightToeBase")}}
    };
    const TPair<FName,FName> IKFeet[] = {
        {TEXT("ik_foot_l"),TEXT("foot_l")},
        {TEXT("ik_foot_r"),TEXT("foot_r")}
    };
    const FString Folder = TEXT("/Game/Developers/MotionBricksEvaluation/");
    const FString AnimPath = Folder+TEXT("A_GeneratedTraversal");
    const FString ClipPath = Folder+TEXT("DA_GeneratedTraversal");
    if (FPackageName::DoesPackageExist(AnimPath) || FPackageName::DoesPackageExist(ClipPath))
        return Fail(TEXT("Refusing to overwrite existing assets; use a fresh evaluation host"));
    UAnimSequence* Animation = NewObject<UAnimSequence>(CreatePackage(*AnimPath),TEXT("A_GeneratedTraversal"),RF_Public|RF_Standalone);
    Animation->SetSkeleton(Mesh->GetSkeleton());
    Animation->SetPreviewMesh(Mesh);
    // The tracks already use this mesh's proportions. The shared skeleton's
    // default reference would apply another translation retarget at playback.
    Animation->SetRetargetSourceAsset(Mesh);
    Animation->UpdateRetargetSourceAssetData();
    Animation->bEnableRootMotion = false; // Capsule consumes the separately stored root curve.
    IAnimationDataController& Controller = Animation->GetController();
    Controller.InitializeModel();
    Controller.OpenBracket(FText::FromString(TEXT("Import generated MotionBricks evaluation")),false);
    Controller.SetFrameRate(FFrameRate(30,1),false);
    Controller.SetNumberOfFrames(FFrameNumber(Motion.Num()-1),false);
    TArray<TArray<FTransform>> LocalFrames;
    const FQuat MeshBasis = FRotator(0,90,0).Quaternion();
    // The USD bind faces -X after basis conversion. Generated motion crosses toward +X.
    const FQuat SourceBindFacing = FRotator(0,180,0).Quaternion();
    const FVector First = Motion[0][Hips].GetLocation();
    for (const auto& Frame : Motion) {
        TArray<FTransform> Local, Global;
        for (int32 B=0; B<Ref.GetNum(); ++B) {
            const int32 Parent = Ref.GetParentIndex(B);
            FTransform Bone = Ref.GetRefBonePose()[B];
            if (const FString* Source = Mapping.Find(Ref.GetBoneName(B))) {
                const int32 S = SourceNames.Find(*Source);
                if (S<0) return Fail(TEXT("Mapping source joint missing"));
                const FQuat SourceAnimated = MeshBasis*Frame[S].GetRotation();
                const FQuat SourceReference = MeshBasis*SourceBindFacing*SourceRest[S].GetRotation();
                FQuat DesiredGlobal = SourceAnimated*SourceReference.Inverse()*RefGlobal[B].GetRotation();
                if (const auto* End = LimbEnds.Find(Ref.GetBoneName(B))) {
                    const int32 RefEnd = Ref.FindBoneIndex(End->Key);
                    const int32 SourceEnd = SourceNames.Find(End->Value);
                    if (RefEnd >= 0 && SourceEnd >= 0) {
                        const FVector BindDirection = RefGlobal[RefEnd].GetLocation()-RefGlobal[B].GetLocation();
                        const FVector LocalDirection = RefGlobal[B].GetRotation().UnrotateVector(BindDirection);
                        const FVector Predicted = DesiredGlobal.RotateVector(LocalDirection).GetSafeNormal();
                        const FVector Desired = MeshBasis.RotateVector(Frame[SourceEnd].GetLocation()-Frame[S].GetLocation()).GetSafeNormal();
                        if (!Predicted.IsNearlyZero() && !Desired.IsNearlyZero())
                            DesiredGlobal = FQuat::FindBetweenNormals(Predicted,Desired)*DesiredGlobal;
                    }
                }
                Bone.SetRotation((Parent<0 ? DesiredGlobal : Global[Parent].GetRotation().Inverse()*DesiredGlobal).GetNormalized());
                if (B == Pelvis) {
                    FVector Position = Bone.GetLocation();
                    Position.Z += First.Z * Scale - RefGlobal[Pelvis].GetLocation().Z;
                    Bone.SetLocation(Position);
                }
            }
            // The rotation retarget retains target bone lengths before contact correction.
            Local.Add(Bone);
            Global.Add(Parent<0 ? Bone : Bone*Global[Parent]);
        }
        LocalFrames.Add(MoveTemp(Local));
    }
    UMBGeneratedClip* Clip = NewObject<UMBGeneratedClip>(CreatePackage(*ClipPath),TEXT("DA_GeneratedTraversal"),RF_Public|RF_Standalone);
    Clip->Animation = Animation;
    Clip->FPS = 30.f;
    for (const auto& Frame : Motion) Clip->RootOffsets.Add((Frame[Hips].GetLocation()-First)*Scale);
    Clip->WallDistance = -First.X*Scale;
    const FString Primitive = Json->GetStringField(TEXT("primitive"));
    const FString Sequence = Json->GetStringField(TEXT("sequence"));
    Clip->WallHeight = (Primitive == TEXT("jump_0_75m") ? 78.f : 50.f) * Scale;
    if (Primitive == TEXT("jump_0_75m") && Sequence == TEXT("OneHandedJump0") && LocalFrames.Num() == 41) {
        if (!BakeVaultContacts(Ref,RefGlobal,LocalFrames,*Clip)) return Fail(TEXT("Contact bake failed on the evaluation vault"));
    }

    for (auto& Local : LocalFrames) {
        TArray<FTransform> Global;
        for (int32 Bone=0; Bone<Local.Num(); ++Bone) {
            const int32 Parent = Ref.GetParentIndex(Bone);
            Global.Add(Parent<0 ? Local[Bone] : Local[Bone]*Global[Parent]);
        }
        // The locomotion graph consumes these IK goals even while playing a
        // slot montage. Keep them with the authored feet through the vault.
        for (const auto& Foot : IKFeet) {
            const int32 Goal = Ref.FindBoneIndex(Foot.Key);
            const int32 Ankle = Ref.FindBoneIndex(Foot.Value);
            if (Goal < 0 || Ankle < 0) continue;
            const int32 GoalParent = Ref.GetParentIndex(Goal);
            const FTransform Offset = RefGlobal[Goal].GetRelativeTransform(RefGlobal[Ankle]);
            const FTransform GoalGlobal = Offset*Global[Ankle];
            Local[Goal] = GoalGlobal.GetRelativeTransform(Global[GoalParent]);
            Local[Goal].NormalizeRotation();
        }
    }
    for (int32 B=0; B<Ref.GetNum(); ++B) {
        const FName Name = Ref.GetBoneName(B);
        TArray<FVector3f> Positions,Scales; TArray<FQuat4f> Rotations;
        for (const auto& Frame : LocalFrames) {
            Positions.Add(FVector3f(Frame[B].GetLocation()));
            Rotations.Add(FQuat4f(Frame[B].GetRotation()));
            Scales.Add(FVector3f::OneVector);
        }
        if (!Controller.AddBoneCurve(Name,false) || !Controller.SetBoneTrackKeys(Name,Positions,Rotations,Scales,false))
            return Fail(TEXT("Failed writing animation track"));
    }
    Controller.NotifyPopulated();
    Controller.CloseBracket(false);
    Animation->PostEditChange();
    Clip->Provenance = FString::Printf(TEXT("MotionBricks %s; %s/%s; evaluation-only sample targets; source %s"),
        *Json->GetStringField(TEXT("motionbricks_commit")),*Primitive,*Sequence,*Filename);
    if (!SaveAsset(Animation) || !SaveAsset(Clip)) return Fail(TEXT("Could not save assets"));
    UE_LOG(LogTemp,Display,TEXT("MotionBricks import: %d frames, %d bones, scale %.5f, wall %.2f cm, duration %.3f s"),Motion.Num(),Ref.GetNum(),Scale,Clip->WallDistance,Clip->Duration());
    return Clip;
}
