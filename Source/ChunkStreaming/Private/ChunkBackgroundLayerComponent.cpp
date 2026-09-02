#include "ChunkBackgroundLayerComponent.h"

#include "Camera/PlayerCameraManager.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"

void UChunkBackgroundLayerComponent::BeginPlay()
{
	Super::BeginPlay();
	if (bAutoRecordBase && GetOwner())
	{
		BaseLocation = GetOwner()->GetActorLocation();
	}
}

void UChunkBackgroundLayerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	APlayerCameraManager* Cam = UGameplayStatics::GetPlayerCameraManager(World, 0);
	if (Cam)
	{
		ApplyParallax(Cam->GetCameraLocation());
	}
}

void UChunkBackgroundLayerComponent::ApplyParallax(const FVector& CameraLocation)
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}
	const float Follow = 1.f - ParallaxFactor;
	FVector NewLoc;
	NewLoc.X = FMath::Lerp(BaseLocation.X, CameraLocation.X, Follow);
	NewLoc.Y = BaseLocation.Y;
	NewLoc.Z = BaseLocation.Z + ZOffset;
	if (bFollowCameraZ)
	{
		NewLoc.Z = FMath::Lerp(BaseLocation.Z, CameraLocation.Z, Follow) + ZOffset;
	}
	Owner->SetActorLocation(NewLoc);
}
