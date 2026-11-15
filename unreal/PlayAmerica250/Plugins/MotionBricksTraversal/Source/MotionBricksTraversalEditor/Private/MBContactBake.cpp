#include "MBContactBake.h"

#include "MBTraversal.h"
#include "ReferenceSkeleton.h"

namespace {
constexpr double MeshFloorOffset = 2.0;
constexpr double SoleClearance = 0.2;
const FQuat MeshYaw = FRotator(0,-90,0).Quaternion();

void BuildComponentPose(const FReferenceSkeleton& Skeleton, const TArray<FTransform>& Local, TArray<FTransform>& Component) {
    Component.SetNumUninitialized(Local.Num());
    for (int32 Bone=0; Bone<Local.Num(); ++Bone) {
        const int32 Parent = Skeleton.GetParentIndex(Bone);
        Component[Bone] = Parent < 0 ? Local[Bone] : Local[Bone]*Component[Parent];
    }
}

void SetComponentRotation(const FReferenceSkeleton& Skeleton, TArray<FTransform>& Local, TArray<FTransform>& Component,
    int32 Bone, const FQuat& Rotation) {
    const int32 Parent = Skeleton.GetParentIndex(Bone);
    Local[Bone].SetRotation((Component[Parent].GetRotation().Inverse()*Rotation).GetNormalized());
    BuildComponentPose(Skeleton, Local, Component);
}

double Smooth(double T) {
    T = FMath::Clamp(T,0.0,1.0);
    return T*T*(3.0-2.0*T);
}

struct FBakeContact {
    FMBCourseContact Data;
    int32 Proximal;
    int32 Middle;
    int32 End;
    int32 Toe;
    int32 BlendInStart;
    int32 BlendOutEnd;
    FQuat Rotation;
};

// Keep the original bend plane and both segment lengths while reaching for the point.
bool SolveLimb(const FReferenceSkeleton& Skeleton, TArray<FTransform>& Local, TArray<FTransform>& Pose,
    const FBakeContact& Contact, const FVector& Target, const FQuat& EndRotation) {
    const FVector Start = Pose[Contact.Proximal].GetLocation();
    const FVector Middle = Pose[Contact.Middle].GetLocation();
    const FVector End = Pose[Contact.End].GetLocation();
    const double UpperLength = FVector::Distance(Start,Middle);
    const double LowerLength = FVector::Distance(Middle,End);
    const FVector ToGoal = Target-Start;
    const double RequestedLength = ToGoal.Length();
    if (RequestedLength < UE_SMALL_NUMBER || UpperLength < 1.0 || LowerLength < 1.0) return false;
    const double Length = FMath::Clamp(RequestedLength, FMath::Abs(UpperLength-LowerLength)+0.01, UpperLength+LowerLength-0.01);
    const FVector Direction = ToGoal/RequestedLength;
    FVector Pole = FVector::VectorPlaneProject(Middle-Start,Direction).GetSafeNormal();
    if (Pole.IsNearlyZero()) {
        FVector Unused;
        Direction.FindBestAxisVectors(Pole,Unused);
    }
    const double Along = (UpperLength*UpperLength-LowerLength*LowerLength+Length*Length)/(2.0*Length);
    const FVector NewMiddle = Start + Direction*Along + Pole*FMath::Sqrt(FMath::Max(0.0,UpperLength*UpperLength-Along*Along));
    const FQuat UpperSwing = FQuat::FindBetweenVectors(Middle-Start,NewMiddle-Start);
    SetComponentRotation(Skeleton,Local,Pose,Contact.Proximal,UpperSwing*Pose[Contact.Proximal].GetRotation());
    const FVector ActualMiddle = Pose[Contact.Middle].GetLocation();
    const FQuat LowerSwing = FQuat::FindBetweenVectors(Pose[Contact.End].GetLocation()-ActualMiddle,Start+Direction*Length-ActualMiddle);
    SetComponentRotation(Skeleton,Local,Pose,Contact.Middle,LowerSwing*Pose[Contact.Middle].GetRotation());
    SetComponentRotation(Skeleton,Local,Pose,Contact.End,EndRotation);
    return true;
}

void LeanToReach(const FReferenceSkeleton& Skeleton, TArray<FTransform>& Local, TArray<FTransform>& Pose,
    int32 Spine, int32 Shoulder, const FVector& Wrist, double Reach) {
    const FVector Pivot = Pose[Spine].GetLocation();
    const FVector ShoulderOffset = Pose[Shoulder].GetLocation()-Pivot;
    if (FVector::Distance(Pivot+ShoulderOffset,Wrist) <= Reach) return;
    const FQuat TowardWrist = FQuat::FindBetweenVectors(ShoulderOffset,Wrist-Pivot);
    // Use the smallest lean that makes contact reachable, preserving the source torso where possible.
    double Min = 0.0, Max = 1.0;
    for (int32 Iteration=0; Iteration<12; ++Iteration) {
        const double Mid = (Min+Max)/2.0;
        const FVector Candidate = Pivot+FQuat::Slerp(FQuat::Identity,TowardWrist,Mid).RotateVector(ShoulderOffset);
        if (FVector::Distance(Candidate,Wrist) > Reach) Min = Mid;
        else Max = Mid;
    }
    SetComponentRotation(Skeleton,Local,Pose,Spine,FQuat::Slerp(FQuat::Identity,TowardWrist,Max)*Pose[Spine].GetRotation());
}
}

bool BakeVaultContacts(const FReferenceSkeleton& Skeleton, const TArray<FTransform>& Reference,
    TArray<TArray<FTransform>>& LocalFrames, UMBGeneratedClip& Clip) {
    auto Bone = [&Skeleton](const TCHAR* Name) { return Skeleton.FindBoneIndex(Name); };
    const int32 Spine = Bone(TEXT("spine_01"));
    const int32 MiddleFinger = Bone(TEXT("middle_01_l"));
    const int32 IndexFinger = Bone(TEXT("index_01_l"));
    const int32 Pinky = Bone(TEXT("pinky_01_l"));
    FBakeContact Palm{{TEXT("hand_l"),{},FVector(-5,-24,Clip.WallHeight+0.15),16,19},
        Bone(TEXT("upperarm_l")),Bone(TEXT("lowerarm_l")),Bone(TEXT("hand_l")),INDEX_NONE,13,24,FQuat::Identity};
    FBakeContact Left{{TEXT("foot_l"),{},FVector::ZeroVector,32,34},
        Bone(TEXT("thigh_l")),Bone(TEXT("calf_l")),Bone(TEXT("foot_l")),Bone(TEXT("ball_l")),29,37,FQuat::Identity};
    FBakeContact Right{{TEXT("foot_r"),{},FVector::ZeroVector,38,40},
        Bone(TEXT("thigh_r")),Bone(TEXT("calf_r")),Bone(TEXT("foot_r")),Bone(TEXT("ball_r")),35,41,FQuat::Identity};
    for (int32 Required : {Spine,MiddleFinger,IndexFinger,Pinky,Palm.Proximal,Palm.Middle,Palm.End,
        Left.Proximal,Left.Middle,Left.End,Left.Toe,Right.Proximal,Right.Middle,Right.End,Right.Toe}) {
        if (Required < 0) return false;
    }
    const FTransform& RefHand = Reference[Palm.End];
    const FVector Across = Reference[IndexFinger].GetLocation()-Reference[Pinky].GetLocation();
    const FVector Along = Reference[MiddleFinger].GetLocation()-RefHand.GetLocation();
    const FQuat RefPalmBasis = FRotationMatrix::MakeFromXY(Along,Across).ToQuat();
    const FVector RefPad = RefHand.GetLocation()+0.62*Along-1.25*RefPalmBasis.GetAxisZ();
    Palm.Data.BoneLocalPoint = RefHand.InverseTransformPosition(RefPad);
    // Fingers point across the top toward the exit; the left thumb faces inward.
    Palm.Rotation = MeshYaw.Inverse()*RefPalmBasis.Inverse()*RefHand.GetRotation();

    auto MeshToCourse = [&Clip](int32 Index) {
        return FTransform(MeshYaw,FVector(-Clip.WallDistance,0,MeshFloorOffset)+Clip.RootOffsets[Index]);
    };
    TArray<FTransform> Pose;
    for (FBakeContact* Foot : {&Left,&Right}) {
        const FTransform& RefFoot = Reference[Foot->End];
        FVector RefSole = Reference[Foot->Toe].GetLocation();
        RefSole.Z = 0.0;
        Foot->Data.BoneLocalPoint = RefFoot.InverseTransformPosition(RefSole);
        const int32 Plant = Foot->Data.FirstFrame;
        BuildComponentPose(Skeleton,LocalFrames[Plant],Pose);
        Foot->Data.CourseAnchor = MeshToCourse(Plant).TransformPosition(Pose[Foot->End].TransformPosition(Foot->Data.BoneLocalPoint));
        Foot->Data.CourseAnchor.Z = SoleClearance;
        const FVector AuthoredHeading = Pose[Foot->Toe].GetLocation()-Pose[Foot->End].GetLocation();
        const FVector RefHeading = Reference[Foot->Toe].GetLocation()-RefFoot.GetLocation();
        const double YawAdjustment = FMath::FindDeltaAngleRadians(FMath::Atan2(RefHeading.Y,RefHeading.X),
            FMath::Atan2(AuthoredHeading.Y,AuthoredHeading.X));
        Foot->Rotation = FQuat(FVector::UpVector,YawAdjustment)*RefFoot.GetRotation();
    }

    // Meet the standing capsule height over the exit step instead of jumping up on Finish().
    const double ExitZ = Clip.RootOffsets.Last().Z;
    for (int32 Frame=36; Frame<LocalFrames.Num(); ++Frame) {
        Clip.RootOffsets[Frame].Z -= ExitZ*Smooth(double(Frame-36)/(LocalFrames.Num()-1-36));
    }
    for (FBakeContact* Contact : {&Palm,&Left,&Right}) {
        double Before = 0.0, After = 0.0;
        for (int32 Frame=Contact->BlendInStart+1; Frame<FMath::Min(Contact->BlendOutEnd,LocalFrames.Num()); ++Frame) {
            const double In = Smooth(double(Frame-Contact->BlendInStart)/(Contact->Data.FirstFrame-Contact->BlendInStart));
            const double Out = Frame <= Contact->Data.LastFrame ? 1.0 :
                Smooth(double(Contact->BlendOutEnd-Frame)/(Contact->BlendOutEnd-Contact->Data.LastFrame));
            const double Weight = In*Out;
            TArray<FTransform>& Local = LocalFrames[Frame];
            BuildComponentPose(Skeleton,Local,Pose);
            const FTransform Basis = MeshToCourse(Frame);
            const FVector TargetPoint = Basis.InverseTransformPosition(Contact->Data.CourseAnchor);
            const FTransform Original = Pose[Contact->End];
            const FVector OriginalPoint = Original.TransformPosition(Contact->Data.BoneLocalPoint);
            const FQuat Rotation = FQuat::Slerp(Original.GetRotation(),Contact->Rotation,Weight);
            const FVector Target = FMath::Lerp(OriginalPoint,TargetPoint,Weight)-Rotation.RotateVector(Contact->Data.BoneLocalPoint);
            if (Contact == &Palm) {
                const double Reach = FVector::Distance(Pose[Palm.Proximal].GetLocation(),Pose[Palm.Middle].GetLocation())+
                    FVector::Distance(Pose[Palm.Middle].GetLocation(),Pose[Palm.End].GetLocation())-1.5;
                LeanToReach(Skeleton,Local,Pose,Spine,Palm.Proximal,Target,Reach);
            }
            if (!SolveLimb(Skeleton,Local,Pose,*Contact,Target,Rotation)) return false;
            if (Contact->Toe != INDEX_NONE) {
                Local[Contact->Toe].SetRotation(FQuat::Slerp(Local[Contact->Toe].GetRotation(),
                    Skeleton.GetRefBonePose()[Contact->Toe].GetRotation(),Weight).GetNormalized());
            }
            if (Frame >= Contact->Data.FirstFrame && Frame <= Contact->Data.LastFrame) {
                Before = FMath::Max(Before,FVector::Distance(OriginalPoint,TargetPoint));
                After = FMath::Max(After,FVector::Distance(Pose[Contact->End].TransformPosition(Contact->Data.BoneLocalPoint),TargetPoint));
            }
        }
        UE_LOG(LogTemp, Display, TEXT("MB_CONTACT_BAKE %s frames %d-%d max error %.2f -> %.2f cm"),
            *Contact->Data.Bone.ToString(),Contact->Data.FirstFrame,Contact->Data.LastFrame,Before,After);
        if (After > 0.75) return false;
        Clip.Contacts.Add(Contact->Data);
    }
    return true;
}
