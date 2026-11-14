#include "MBTraversal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "InputKeyEventArgs.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"

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
        Check(TEXT("excessive distance rejected"), !TryTraversal() && LastResult.Contains(TEXT("approach")));
        ResetAttempt();
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
        if (bTraversing) {
            Check(TEXT("valid approach accepted through E binding"), true);
            Check(TEXT("repeat input during traversal rejected"), !TryTraversal() && bTraversing && LastResult.Contains(TEXT("already")));
            VerificationPhase = 2;
        } else if (VerificationTime >= 0.3f) {
            Check(TEXT("valid approach accepted through E binding"), false);
            VerificationPhase = 6;
        }
        return;
    case 2:
        if (bTraversing) {
            const float Fraction = Elapsed/Clip->Duration();
            if (VerificationCapture == 0 && Fraction > 0.40f) {
                const UAnimInstance* Anim = GetMesh()->GetAnimInstance();
                const bool bSynchronized = TraversalMontage && Anim &&
                    FMath::IsNearlyEqual(Anim->Montage_GetPosition(TraversalMontage),Elapsed,0.001f);
                const float SlotWeight = Anim ? Anim->GetSlotMontageGlobalWeight(TEXT("DefaultSlot")) : 0.f;
                UE_LOG(LogTemp, Display, TEXT("MB_VERIFY slot weight %.3f at %.3f s"),SlotWeight,Elapsed);
                Check(TEXT("montage blended and synchronized"), bSynchronized && SlotWeight > 0.9f);
                Capture(TEXT("hand"));
                ++VerificationCapture;
            } else if (VerificationCapture == 1 && Fraction > 0.59f) {
                Capture(TEXT("clearance"));
                ++VerificationCapture;
            }
            return;
        }
        Check(TEXT("first traversal complete"), LastResult.StartsWith(TEXT("Complete")));
        Check(TEXT("normal capsule restored"), FMath::IsNearlyEqual(GetCapsuleComponent()->GetUnscaledCapsuleHalfHeight(),NormalHalfHeight));
        VerificationLanding = GetActorLocation();
        {
            const FVector Expected = VerificationStart+TraversalRotation.RotateVector(Clip->RootOffsets.Last());
            const double Drift = FVector::Dist2D(Expected,VerificationLanding);
            UE_LOG(LogTemp, Display, TEXT("MB_VERIFY planar root drift %.3f cm"), Drift);
            Check(TEXT("planar root drift below 1 cm"), Drift < 1.0);
        }
        Capture(TEXT("landing"));
        VerificationTime = 0.f;
        VerificationPhase = 3;
        return;
    case 3:
        AddMovementInput(GetActorForwardVector(), 1.f);
        if (VerificationTime < 0.55f) return;
        Check(TEXT("walking controlled after landing"), GetCharacterMovement()->IsMovingOnGround() &&
            FVector::Dist2D(VerificationLanding, GetActorLocation()) > 40.0);
        ResetAttempt();
        VerificationTime = 0.f;
        VerificationPhase = 4;
        return;
    case 4:
        if (VerificationTime < 0.15f) return;
        if (TryTraversal()) {
            Check(TEXT("second attempt accepted"), true);
            VerificationPhase = 5;
        } else {
            Check(TEXT("second attempt accepted"), false);
            VerificationPhase = 6;
        }
        return;
    case 5:
        if (bTraversing) return;
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
