#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "ChunkEnemySpawnerComponent.generated.h"

class UChunkEnemyManager;

/** 敌人被"再次加载"（回收后重新生成）时广播；蓝图可绑定做自定义初始化。 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FChunkEnemyRespawnedDelegate);


/**
 * 敌人占位收编组件：挂到敌人父类（如 BP_EnemiesFather）上即可。
 * 编辑器里照常摆放敌人；游戏开始时：
 * - 占位模式（普通摆放的敌人）：把自己注册进 UChunkEnemyManager 后自我销毁，
 *   之后由管理器按玩家距离生成"活体"（Spawn 到持久层，可跨区块追击）。
 * - 活体模式（管理器生成的实例，带 Tag "ChunkEnemyActive"）：不注册不销毁；
 *   玩家击杀（活体被销毁）时自动上报管理器 → 记永久死亡，永不复活。
 * 死亡上报也可手动调用 MarkAsDead（用于不销毁的死亡表现）。
 */
UCLASS(ClassGroup = (ChunkStreaming), meta = (BlueprintSpawnableComponent))
class CHUNKSTREAMING_API UChunkEnemySpawnerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UChunkEnemySpawnerComponent();

	/** 稳定唯一 Key（默认 = 敌人 Actor 名字；若不同区块存在同名敌人请手动设置区分）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunk Enemy")
	FString EnemyKey;

	/**
	 * 变量名白名单：活体被回收/重新生成时，这些变量会被保存并写回（标记"再次加载时重新赋值"）。
	 * 支持的类型：float/double/int/bool/枚举/字符串/名字 + FVector/FRotator/FQuat/FTransform/FLinearColor/FColor/FVector2D/FIntPoint。
	 * 留空 = 自动保存全部可保存数值变量。
	 * 组件变量用"组件名.变量名"（如 BPC_Attribution.Health），Actor 变量直接写变量名。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunk Enemy")
	TArray<FString> VariablesToSave;

	/**
	 * 【推荐】直接选择敌人身上已挂载的组件：点 + 后在下拉里选组件（标准组件选择器，
	 * 只列出本敌人身上的组件，如 BPC_AttributionManager）。所选组件的一切数值型变量
	 * 都会在活体回收时自动保存、重生后写回。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunk Enemy")
	TArray<FComponentReference> ComponentsToSave;

	/** 备选：按组件类指定（该类的所有实例全变量保存）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunk Enemy")
	TArray<TSubclassOf<UActorComponent>> ComponentClassesToSave;

	/** 备选：按组件 Tag 指定（挂该 Tag 的组件全变量保存）。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Chunk Enemy")
	TArray<FName> ComponentTagsToSave;

	/** 敌人被再次加载（回收后重新生成且状态已写回）时触发。蓝图绑定方式：Event BeginPlay 里 Assign/Add Event。 */
	UPROPERTY(BlueprintAssignable, Category = "Chunk Enemy")
	FChunkEnemyRespawnedDelegate OnEnemyRespawned;

	/** 手动标记永久死亡（敌人死亡逻辑中调用；死亡后管理器不再生成该敌人）。 */
	UFUNCTION(BlueprintCallable, Category = "Chunk Enemy")
	void MarkAsDead();

	/** 管理器在重生并写回状态后调用：广播 OnEnemyRespawned。 */
	void NotifyRespawned();

	/** 管理器生成活体后调用：继承占位注册的 Key（否则死亡上报找不到记录，敌人会"复活"）。 */
	void SetSpawnKey(const FString& Key);

	/** 把当前标记变量打包（管理器回收活体时自动调用；蓝图也可手动调用）。 */
	UFUNCTION(BlueprintCallable, Category = "Chunk Enemy")
	void CaptureState(UPARAM(ref) TArray<uint8>& OutBytes);

	/** 把打包数据写回标记变量（管理器生成活体后自动调用）。 */
	UFUNCTION(BlueprintCallable, Category = "Chunk Enemy")
	void ApplyState(const TArray<uint8>& InBytes);

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/** BeginPlay 后一帧执行（等玩家 Possess 完成再识别玩家/活体/占位）。 */
	void DeferredBeginPlay();

	UFUNCTION()

	UChunkEnemyManager* GetManager() const;
	FString GetResolvedKey() const;

	bool bActiveEnemy = false;
	FString CachedKey;
};
