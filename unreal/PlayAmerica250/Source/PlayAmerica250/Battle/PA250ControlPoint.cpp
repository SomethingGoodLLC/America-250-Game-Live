#include "PA250ControlPoint.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "RFCharacter.h"
#include "UObject/ConstructorHelpers.h"

APA250ControlPoint::APA250ControlPoint()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.2f;

	Zone = CreateDefaultSubobject<USphereComponent>(TEXT("Zone"));
	Zone->InitSphereRadius(Radius);
	Zone->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	Zone->SetGenerateOverlapEvents(true);
	RootComponent = Zone;

	Disc = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Disc"));
	Disc->SetupAttachment(RootComponent);
	Disc->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Disc->SetRelativeLocation(FVector(0.f, 0.f, -Radius + 8.f));
	Disc->SetRelativeScale3D(FVector(Radius / 50.f, Radius / 50.f, 0.05f));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cyl(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (Cyl.Succeeded())
	{
		Disc->SetStaticMesh(Cyl.Object);
	}
}

void APA250ControlPoint::BeginPlay()
{
	Super::BeginPlay();
	Zone->SetSphereRadius(Radius);
	UpdateColor();
}

void APA250ControlPoint::UpdateColor()
{
	if (UMaterialInstanceDynamic* MID = Disc->CreateAndSetMaterialInstanceDynamic(0))
	{
		const FLinearColor C = Holder == TEXT("british") ? FLinearColor(0.8f, 0.05f, 0.05f)
		                     : Holder == TEXT("continental") ? FLinearColor(0.1f, 0.2f, 0.85f)
		                                                     : FLinearColor(0.8f, 0.8f, 0.8f);
		MID->SetVectorParameterValue(TEXT("Color"), C);
	}
}

void APA250ControlPoint::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	Presence.Reset();
	TArray<AActor*> Overlapping;
	Zone->GetOverlappingActors(Overlapping, ARFCharacter::StaticClass());
	for (AActor* A : Overlapping)
	{
		const ARFCharacter* C = Cast<ARFCharacter>(A);
		if (C && !C->IsDead() && !C->FactionId.IsNone())
		{
			Presence.FindOrAdd(C->FactionId)++;
		}
	}

	const int32 Brit = GetPresence(TEXT("british"));
	const int32 Cont = GetPresence(TEXT("continental"));
	FName Sole = NAME_None;
	if (Brit > 0 && Cont == 0) Sole = TEXT("british");
	else if (Cont > 0 && Brit == 0) Sole = TEXT("continental");

	if (Sole != NAME_None)
	{
		if (Sole == Holder)
		{
			CaptureProgress = 0.f;
			Capturer = NAME_None;
		}
		else
		{
			if (Capturer != Sole)
			{
				Capturer = Sole;
				CaptureProgress = 0.f;
			}
			CaptureProgress += DeltaSeconds;
			if (CaptureProgress >= CaptureSeconds)
			{
				Holder = Sole;
				HoldSeconds = 0.f;
				CaptureProgress = 0.f;
				Capturer = NAME_None;
				UpdateColor();
			}
		}
	}
	// Contested or empty: progress pauses; the holder keeps the point.
	if (Holder != NAME_None)
	{
		HoldSeconds += DeltaSeconds;
	}
}
