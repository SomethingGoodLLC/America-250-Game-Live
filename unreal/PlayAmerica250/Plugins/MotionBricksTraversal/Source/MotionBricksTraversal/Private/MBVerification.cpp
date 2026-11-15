#include "MBTraversal.h"
#include "MBTraversalComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "InputKeyEventArgs.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

void AMBTestCharacter::SampleVerificationContacts(UWorld* World, ELevelTick TickType, float DeltaSeconds) {
    const AMBTestCourse* Course = ActiveCourse.Get();
    if (World != GetWorld() || !Clip || !Course || VerificationPhase != 2 || !Traversal->IsVaulting()) return;
    SampleMusketClearance();
    const float Frame = Traversal->GetElapsed()*Clip->FPS;
    for (int32 Index=0; Index<Clip->Contacts.Num(); ++Index) {
        const FMBCourseContact& Contact = Clip->Contacts[Index];
        if (Frame < Contact.FirstFrame-0.02f || Frame > Contact.LastFrame+0.02f) continue;
        if (GetMesh()->GetBoneIndex(Contact.Bone) == INDEX_NONE) continue;
        const FVector Point = GetMesh()->GetSocketTransform(Contact.Bone).TransformPosition(Contact.BoneLocalPoint);
        const FVector Anchor = Course->GetActorTransform().TransformPosition(Contact.CourseAnchor);
        const UStaticMeshComponent* Surface = Contact.Bone == TEXT("hand_l") ? Course->Wall.Get() : Course->Floor.Get();
        const double SurfaceZ = Surface->Bounds.Origin.Z+Surface->Bounds.BoxExtent.Z;
        FObservedContact& Observed = ObservedContacts[Index];
        ++Observed.Samples;
        Observed.HeightError = FMath::Max(Observed.HeightError,FMath::Abs(Point.Z-SurfaceZ));
        Observed.PlanarError = FMath::Max(Observed.PlanarError,FVector::Dist2D(Point,Anchor));
        UE_LOG(LogTemp, Display, TEXT("MB_VERIFY contact sample %s frame %.2f point %s anchor %s"),
            *Contact.Bone.ToString(),Frame,*Point.ToCompactString(),*Anchor.ToCompactString());
    }
}

void AMBTestCharacter::TickVerification(float DeltaSeconds) {
    auto Check = [this](const TCHAR* Label, bool bPassed) {
        bVerificationPassed &= bPassed;
        UE_LOG(LogTemp, Display, TEXT("MB_VERIFY %s %s"), bPassed ? TEXT("PASS") : TEXT("FAIL"), Label);
    };
    auto Capture = [](const TCHAR* Label) {
        if (GIsRunning && GEngine && GEngine->GameViewport) {
            FScreenshotRequest::RequestScreenshot(
                FPaths::ScreenShotDir()/FString::Printf(TEXT("MotionBricks_%s.png"),Label), false, false);
        }
    };
    VerificationTime += DeltaSeconds;
    switch (VerificationPhase) {
    case 0: {
        if (VerificationTime < 10.f) return;
        if (!Clip || !Clip->Animation) {
            Check(TEXT("generated data loaded"), false);
            VerificationPhase = 6;
            return;
        }
        Check(TEXT("generated data loaded"), true);
        if (bExpectMusket) Check(TEXT("slung musket asset loaded"), IsValid(Musket));
        Check(TEXT("excessive distance rejected"), !TryTraversal() && LastResult.Contains(TEXT("approach")));
        ResetAttempt();
        for (TActorIterator<AMBTestCourse> It(GetWorld()); It; ++It) {
            FMBVaultTarget Target = It->GetVaultTarget();
            const TCHAR* OtherRig = Musket ? TEXT("Quinn") : TEXT("Manny");
            const FString OtherPath = FString::Printf(TEXT("/Game/Developers/MotionBricksEvaluation/DA_GeneratedTraversal_%s"),OtherRig);
            UMBGeneratedClip* OtherClip = LoadObject<UMBGeneratedClip>(nullptr,*OtherPath);
            Check(TEXT("other rig's animation rejected"), OtherClip &&
                Traversal->TryStart(OtherClip,Target) == EMBVaultStart::IncompatibleMesh);
            Target.CourseFrame.AddToTranslation(FVector(0,0,12));
            Check(TEXT("mismatched course frame rejected"), Traversal->TryStart(Clip,Target) == EMBVaultStart::WrongObstacle);
            break;
        }
        AddActorWorldRotation(FRotator(0,90,0));
        Check(TEXT("wrong direction rejected"), !TryTraversal() && LastResult.Contains(TEXT("approach")));
        ResetAttempt();
        ToggleBlockedLanding();
        Check(TEXT("blocked landing rejected"), !TryTraversal() && LastResult == TEXT("Rejected: blocked landing"));
        ToggleBlockedLanding();
        VerificationStart = GetActorLocation();
        Capture(TEXT("approach"));
        VerificationTime = 0.f;
        VerificationPhase = 1;
        return;
    }
    case 1:
        if (VerificationTime < 0.3f) return;
        if (APlayerController* Player = Cast<APlayerController>(Controller)) {
            Player->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::E, IE_Pressed, 1.0f));
            Player->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::E, IE_Released, 0.0f));
            VerificationTime = 0.f;
            VerificationPhase = 8;
            return;
        }
        Check(TEXT("player controller available"), false);
        VerificationPhase = 6;
        return;
    case 8:
        if (Traversal->IsVaulting()) {
            Check(TEXT("valid approach accepted through E binding"), true);
            Check(TEXT("repeat input during traversal rejected"), !TryTraversal() && Traversal->IsVaulting() && LastResult.Contains(TEXT("already")));
            VerificationPhase = 2;
        } else if (VerificationTime >= 0.3f) {
            Check(TEXT("valid approach accepted through E binding"), false);
            VerificationPhase = 6;
        }
        return;
    case 2:
        if (Traversal->IsVaulting()) {
            const float Fraction = Traversal->GetElapsed()/Clip->Duration();
            if (VerificationCapture == 0 && Fraction > 0.40f) {
                Check(TEXT("montage blended and synchronized"),Traversal->IsPoseSynchronized());
                ++VerificationCapture;
            }
            return;
        }
        Check(TEXT("first traversal complete"), LastResult.StartsWith(TEXT("Complete")));
        Check(TEXT("normal capsule restored"), FMath::IsNearlyEqual(GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight(),NormalHalfHeight));
        if (Musket) {
            UE_LOG(LogTemp, Display, TEXT("MB_VERIFY musket wall/floor hits %d, lowest floor clearance %.2f cm on %d frames"),
                MusketWallHits,MusketMinFloorClearance,MusketChecks);
            Check(TEXT("slung musket clears wall and floor"),MusketChecks >= 20 && MusketWallHits == 0 && MusketMinFloorClearance > 0.0);
        }
        Check(TEXT("three baked contacts available"), Clip->Contacts.Num() == 3);
        for (int32 Index=0; Index<ObservedContacts.Num(); ++Index) {
            const FObservedContact& Observed = ObservedContacts[Index];
            const FName Bone = Clip->Contacts[Index].Bone;
            UE_LOG(LogTemp, Display, TEXT("MB_VERIFY contact %s max height error %.3f cm planar error %.3f cm (%d samples)"),
                *Bone.ToString(),Observed.HeightError,Observed.PlanarError,Observed.Samples);
            const FString Label = FString::Printf(TEXT("%s contact within 1.5 cm of surface and anchor"),*Bone.ToString());
            Check(*Label,Observed.Samples >= 2 && Observed.HeightError < 1.5 && Observed.PlanarError < 1.5);
        }
        VerificationLanding = GetActorLocation();
        if (const AMBTestCourse* Course = ActiveCourse.Get()) {
            const FVector Expected = VerificationStart+Course->GetActorQuat().RotateVector(Clip->RootOffsets.Last());
            const double Drift = FVector::Dist2D(Expected,VerificationLanding);
            UE_LOG(LogTemp, Display, TEXT("MB_VERIFY planar root drift %.3f cm"), Drift);
            Check(TEXT("planar root drift below 1 cm"), Drift < 1.0);
        }
        VerificationTime = 0.f;
        VerificationPhase = 3;
        return;
    case 3:
        if (VerificationTime < 0.55f) {
            AddMovementInput(GetActorForwardVector(),1.f);
            return;
        }
        Check(TEXT("walking controlled after landing"), GetCharacterMovement()->IsMovingOnGround() &&
            FVector::Dist2D(VerificationLanding, GetActorLocation()) > 40.0);
        ResetAttempt();
        VerificationTime = 0.f;
        VerificationPhase = 4;
        return;
    case 4:
        if (VerificationTime < 0.15f) return;
        if (ActiveCourse.IsValid() && Traversal->TryStart(Clip,ActiveCourse->GetVaultTarget()) == EMBVaultStart::Started) {
            Check(TEXT("second attempt accepted through component API"), true);
            VerificationCapture = 0;
            VerificationPhase = 5;
        } else {
            Check(TEXT("second attempt accepted"), false);
            VerificationPhase = 6;
        }
        return;
    case 5:
        if (Traversal->IsVaulting()) {
            // Recording is separate from the first attempt's contact samples.
            static const float CaptureFrames[] = {17.0f, 24.0f, 33.0f, 39.0f};
            static const TCHAR* const CaptureLabels[] = {TEXT("hand"),TEXT("clearance"),TEXT("left_foot"),TEXT("right_foot")};
            if (VerificationCapture < UE_ARRAY_COUNT(CaptureFrames) &&
                Traversal->GetElapsed()*Clip->FPS >= CaptureFrames[VerificationCapture]-0.05f) {
                Capture(CaptureLabels[VerificationCapture++]);
            }
            return;
        }
        Capture(TEXT("landing"));
        Check(TEXT("second traversal complete"), LastResult.StartsWith(TEXT("Complete")));
        VerificationPhase = 6;
        return;
    case 6:
        if (!TraversalFrameTimes.IsEmpty()) {
            TraversalFrameTimes.Sort();
            const int32 Count = TraversalFrameTimes.Num();
            UE_LOG(LogTemp, Display, TEXT("MB_VERIFY game frame ms p50 %.3f p95 %.3f (%d samples)"),
                TraversalFrameTimes[(Count-1)/2], TraversalFrameTimes[FMath::FloorToInt((Count-1)*0.95f)], Count);
        }
        UE_LOG(LogTemp, Display, TEXT("MB_VERIFY RESULT %s"), bVerificationPassed ? TEXT("PASS") : TEXT("FAIL"));
        VerificationPhase = 7;
        FPlatformMisc::RequestExitWithStatus(false, bVerificationPassed ? 0 : 1);
        return;
    default: return;
    }
}
