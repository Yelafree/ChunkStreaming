#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ChunkBackgroundLayerComponent.generated.h"

/**
 * 背景视差组件：挂到背景块内的根 Actor 上，数据驱动视差滚动。
 * ParallaxFactor = 0 完全跟随相机；= 1 完全静止（最远层）。
 */
UCLASS(ClassGroup = (ChunkStreaming), meta = (BlueprintSpawnableComponent))
class CHUNKSTREAMING_API UChunkBackgroundLayerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** 视差系数 0~1（0=跟随相机，1=静止）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parallax")
	float ParallaxFactor = 0.5f;

	/** 垂直偏移（相对基点的 Z 抬升）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parallax")
	float ZOffset = 0.f;

	/** 是否沿 Z 轴也做视差跟随。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parallax")
	bool bFollowCameraZ = true;

	/** 自动在 BeginPlay 记录当前摆放位置作为视差基点。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parallax")
	bool bAutoRecordBase = true;

	/** 视差基点（自动记录或手动指定）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Parallax")
	FVector BaseLocation = FVector::ZeroVector;

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** 用相机位置计算并应用视差偏移。 */
	UFUNCTION(BlueprintCallable, Category = "Parallax")
	void ApplyParallax(const FVector& CameraLocation);
};
