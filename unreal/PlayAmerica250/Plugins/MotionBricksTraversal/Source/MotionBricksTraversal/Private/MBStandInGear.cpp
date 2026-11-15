#include "MBTraversal.h"

#include "AnimationRuntime.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"

void AMBTestCharacter::ClearPreviewGear() {
    for (USceneComponent* Part : Gear) {
        if (IsValid(Part)) Part->DestroyComponent();
    }
    Gear.Reset();
    Musket = nullptr;
    GetMesh()->EmptyOverrideMaterials();
}

USceneComponent* AMBTestCharacter::AttachSlungMusket() {
    USkeletalMeshComponent* Body = GetMesh();
    const USkeletalMesh* BodyAsset = Body->GetSkeletalMeshAsset();
    if (!BodyAsset) return nullptr;
    const FName Chest(TEXT("spine_03"));
    const FReferenceSkeleton& Skeleton = BodyAsset->GetRefSkeleton();
    const int32 ChestIndex = Skeleton.FindBoneIndex(Chest);
    if (ChestIndex == INDEX_NONE) return nullptr;
    const FTransform Bind = FAnimationRuntime::GetComponentSpaceTransformRefPose(Skeleton,ChestIndex)*Body->GetRelativeTransform();
    const FVector BarrelAxis = FVector(-0.05,-0.50,0.86).GetSafeNormal();
    const FQuat Orientation = FRotationMatrix::MakeFromXY(BarrelAxis,FVector::ForwardVector).ToQuat();
    USceneComponent* Sling = NewObject<USceneComponent>(this,TEXT("PreviewMusket"));
    AddInstanceComponent(Sling);
    Gear.Add(Sling);
    Sling->SetupAttachment(Body,Chest);
    Sling->SetRelativeTransform(FTransform(Orientation,Bind.GetLocation()+FVector(-23,3,5)).GetRelativeTransform(Bind));
    Sling->RegisterComponent();
    return Sling;
}

void AMBTestCharacter::BuildLicensedMusket() {
    UStaticMesh* Asset = LoadObject<UStaticMesh>(nullptr,
        TEXT("/Game/HS_WeaponPack_01/Meshes/MusketRifle/Meshes/SM_HS_WP01_MusketRifle_X_0.SM_HS_WP01_MusketRifle_X_0"));
    if (!Asset) return;
    Musket = AttachSlungMusket();
    if (!Musket) return;
    const FBox Bounds = Asset->GetBoundingBox();
    const FVector Extent = Bounds.GetExtent();
    MusketHalfLength = Extent.X;
    MusketRadius = float(FVector2D(Extent.Y,Extent.Z).Length());
    UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this);
    AddInstanceComponent(Part);
    Gear.Add(Part);
    Part->SetupAttachment(Musket);
    Part->SetStaticMesh(Asset);
    Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Part->SetRelativeLocation(-Bounds.GetCenter());
    Part->RegisterComponent();
    UE_LOG(LogTemp, Display, TEXT("MB_SOLDIER musket=%s"),*Asset->GetPathName());
}

void AMBTestCharacter::BuildStandInGear(bool bManny) {
    USkeletalMeshComponent* Body = GetMesh();
    const USkeletalMesh* BodyAsset = Body->GetSkeletalMeshAsset();
    if (!BodyAsset) return;
    const FReferenceSkeleton& Skeleton = BodyAsset->GetRefSkeleton();
    const FTransform Basis = Body->GetRelativeTransform();
    auto Bind = [&](FName Bone) {
        const int32 Index = Skeleton.FindBoneIndex(Bone);
        return Index >= 0 ? FAnimationRuntime::GetComponentSpaceTransformRefPose(Skeleton,Index)*Basis : FTransform::Identity;
    };
    const FName Chest = TEXT("spine_03");
    const FName Head = TEXT("head");
    const FName Hips = TEXT("pelvis");
    const FVector ChestCenter = Bind(Chest).GetLocation();
    const FVector HeadCenter = Bind(Head).GetLocation();
    const FVector HipCenter = Bind(Hips).GetLocation();

    UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr,TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    auto Tint = [&](FLinearColor Color) {
        UMaterialInstanceDynamic* Material = Base ? UMaterialInstanceDynamic::Create(Base,this) : nullptr;
        if (Material) Material->SetVectorParameterValue(TEXT("Color"),Color);
        return Material;
    };
    UMaterialInstanceDynamic* Cloth = Tint(bManny ? FLinearColor(0.035f,0.10f,0.23f) : FLinearColor(0.25f,0.20f,0.10f));
    UMaterialInstanceDynamic* Leather = Tint(FLinearColor(0.075f,0.039f,0.017f));
    UMaterialInstanceDynamic* Felt = Tint(FLinearColor(0.026f,0.020f,0.017f));
    UMaterialInstanceDynamic* Trim = Tint(bManny ? FLinearColor(0.48f,0.035f,0.025f) : FLinearColor(0.23f,0.30f,0.14f));
    UMaterialInstanceDynamic* Webbing = Tint(FLinearColor(0.75f,0.62f,0.39f));
    UMaterialInstanceDynamic* Metal = Tint(FLinearColor(0.20f,0.22f,0.24f));
    UMaterialInstanceDynamic* Brass = Tint(FLinearColor(0.62f,0.38f,0.07f));
    UMaterialInstanceDynamic* Wood = Tint(FLinearColor(0.24f,0.089f,0.032f));
    if (Cloth) Body->SetMaterial(0,Cloth);
    if (Trim && Body->GetNumMaterials() > 1) Body->SetMaterial(1,Trim);

    UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));
    UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    auto Shape = [&](USceneComponent* Parent, FName Bone, UStaticMesh* Asset, UMaterialInterface* Material,
        const FTransform& Relative, FVector Centimeters) {
        UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this);
        AddInstanceComponent(Part);
        Gear.Add(Part);
        Part->SetupAttachment(Parent,Bone);
        Part->SetStaticMesh(Asset);
        Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        if (Material) Part->SetMaterial(0,Material);
        Part->SetRelativeTransform(Relative);
        Part->SetRelativeScale3D(Centimeters/100.0);
        Part->RegisterComponent();
    };
    auto OnBone = [&](FName Bone, UStaticMesh* Asset, UMaterialInterface* Material, FVector At,
        FVector Size, FRotator Rotation = FRotator::ZeroRotator) {
        Shape(Body,Bone,Asset,Material,FTransform(Rotation,At).GetRelativeTransform(Bind(Bone)),Size);
    };

    // Tricorne silhouette, shoulder facing and belt kit distinguish the two stand-ins.
    const FVector Hat = HeadCenter+FVector(0,0,17);
    OnBone(Head,Cylinder,Felt,Hat,FVector(28,27,2));
    OnBone(Head,Cylinder,Felt,Hat+FVector(-1,0,3),FVector(18,17,7));
    for (double Yaw : {0.0,120.0,240.0}) {
        const FRotator Rotation(0,Yaw,0);
        OnBone(Head,Cube,Felt,Hat+Rotation.RotateVector(FVector(0,10.5,3.0)),FVector(23,2.0,5.0),Rotation);
    }
    OnBone(Chest,Cube,Webbing,ChestCenter+FVector(12,0,1),FVector(1.6,5.5,39),FRotator(0,0,bManny ? 32 : -32));
    OnBone(Hips,Sphere,Leather,HipCenter+FVector(0,0,10),FVector(36,39,5));
    OnBone(Hips,Cube,Brass,HipCenter+FVector(17,0,10),FVector(2,6,5));
    OnBone(Hips,Cube,Leather,HipCenter+FVector(-3,bManny ? 20 : -20,2),FVector(13,10,17));
    if (!bManny) return;

    // This local-only prop is slung across the back, leaving the vault hand free.
    Musket = AttachSlungMusket();
    if (!Musket) return;
    MusketHalfLength = 70.0;
    MusketRadius = 6.f;
    auto Prop = [&](UStaticMesh* Asset, UMaterialInterface* Material, double X, FVector Size, double Roll = 0.0) {
        Shape(Musket,NAME_None,Asset,Material,FTransform(FRotator(Roll,0,0),FVector(X,0,0)),Size);
    };
    Prop(Sphere,Wood,-44,FVector(44,8,12));
    Prop(Cube,Wood,-5,FVector(65,5,6));
    Prop(Cylinder,Metal,25,FVector(3.0,3.0,89),90);
    for (double Band : {-16.0,7.0,29.0}) Prop(Cube,Brass,Band,FVector(3.5,6.2,7));
    Prop(Cube,Metal,-27,FVector(15,6,5));
}

void AMBTestCharacter::SampleMusketClearance() {
    const AMBTestCourse* Course = ActiveCourse.Get();
    if (!Musket || !Course) return;
    const FTransform Pose = Musket->GetComponentTransform();
    const FVector Start = Pose.TransformPosition(FVector(-MusketHalfLength,0,0));
    const FVector End = Pose.TransformPosition(FVector(MusketHalfLength,0,0));
    ++MusketChecks;
    FHitResult Hit;
    if (GetWorld()->SweepSingleByChannel(Hit,Start,End,FQuat::Identity,ECC_Visibility,
        FCollisionShape::MakeSphere(MusketRadius),FCollisionQueryParams(SCENE_QUERY_STAT(MBMusket),false,this))) {
        ++MusketWallHits;
    }
    const double FloorZ = Course->Floor->Bounds.Origin.Z+Course->Floor->Bounds.BoxExtent.Z;
    MusketMinFloorClearance = FMath::Min(MusketMinFloorClearance,FMath::Min(Start.Z,End.Z)-MusketRadius-FloorZ);
}
