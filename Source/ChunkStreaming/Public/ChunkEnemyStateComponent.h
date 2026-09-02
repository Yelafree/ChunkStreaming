#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ChunkSaveable.h"
#include "ChunkEnemyStateComponent.generated.h"

/**
 * 敌人状态组件：挂到敌人/可保存 Actor 上即可自动参与区块状态持久化。
 * 自动保存/恢复：血量、死亡、位置。子类或蓝图可在此基础上扩展。
 */
UCLASS(ClassGroup = (ChunkStreaming), meta = (BlueprintSpawnableComponent))
class CHUNKSTREAMING_API UChunkEnemyStateComponent : public UActorComponent, public IChunkSaveable
{
	GENERATED_BODY()

public:
	/** 稳定唯一 ID；留空则使用所属 Actor 的名字。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunk State")
	FString EnemyId;

	/** 血量（游戏逻辑读写该值，卸载时自动保存）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunk State")
	float Health = 100.f;

	/** 死亡标记（游戏逻辑置 true，卸载时自动保存，重进区块不会复活）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunk State")
	bool bDead = false;

	/** 上次保存时的位置（保存/恢复时自动写入）。 */
	UPROPERTY(BlueprintReadOnly, Category = "Chunk State")
	FTransform SavedTransform = FTransform::Identity;

	/**
	 * 自动保存的组件 Tag 白名单（反射收集）：
	 * 敌人 Actor 自身的数值型变量总是被自动保存；
	 * 挂有这些 Tag 的组件，其数值型变量也会被自动保存（默认空 = 只保存 Actor 自身）。
	 * 自动收集的变量类型：float/double/int/bool/枚举/字符串/名字 + FVector/FRotator/FQuat/FTransform/FLinearColor/FColor/FVector2D/FIntPoint；
	 * 对象引用、数组、Map、委托等自动跳过。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunk State")
	TArray<FName> ComponentTagsToSave;

	/** 手动把当前值写入自定义扩展数据（子类/蓝图可重写 Save/Restore 补全）。 */
	UFUNCTION(BlueprintCallable, Category = "Chunk State")
	void SaveNow();

	/** 手动恢复（一般不需要直接调用）。 */
	UFUNCTION(BlueprintCallable, Category = "Chunk State")
	void RestoreNow(const FChunkEnemySaveData& InData);

	// IChunkSaveable 实现
	virtual void SaveChunkState_Implementation(FChunkEnemySaveData& OutData) override;
	virtual void RestoreChunkState_Implementation(const FChunkEnemySaveData& InData) override;
	virtual FString GetEnemyId_Implementation() override;
};
