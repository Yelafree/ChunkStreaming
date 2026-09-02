#pragma once

#include "CoreMinimal.h"

/**
 * 反射收集工具（ChunkEnemyStateComponent / ChunkEnemySpawnerComponent 共用）：
 * 遍历对象上的"可保存数值属性"并打包/解包成字节流。
 * 格式（每条）：FString 完整名, uint32 值长度, 值字节
 * 完整名 = 组件名.属性名（无前缀 = Actor 自身属性）。
 */
namespace ChunkStateReflection
{
	/** 属性是否可保存（数值/布尔/字符串/名字/枚举/白名单结构；排除对象引用/数组/Map/委托/Transient）。 */
	bool IsSaveableProperty(FProperty* Prop);

	/** 把 Obj 上（可选按名字白名单过滤）的可保存属性写入流。返回写入条数。 */
	int32 CaptureObjectProps(FArchive& W, UObject* Obj, const FString& Prefix, const TArray<FName>* NameFilter);

	/** 从流中解包 Count 条并写回（Obj 为 Actor 时支持"组件名.变量名"前缀）。返回成功写回条数。 */
	int32 RestoreObjectProps(FArchive& R, UObject* Obj, int32 Count);
}
