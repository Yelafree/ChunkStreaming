#include "ChunkStateReflection.h"

#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "UObject/UnrealType.h"
#include "Serialization/StructuredArchive.h"

namespace ChunkStateReflection
{
	namespace
	{
		/** 递归检查属性是否包含对象引用（对象引用在关卡卸载后失效，不能保存）。 */
		bool ContainsObjectRef(FProperty* Prop)
		{
			if (!Prop)
			{
				return false;
			}
			if (CastField<FObjectPropertyBase>(Prop) || CastField<FWeakObjectProperty>(Prop)
				|| CastField<FSoftObjectProperty>(Prop) || CastField<FInterfaceProperty>(Prop)
				|| CastField<FClassProperty>(Prop) || CastField<FSoftClassProperty>(Prop)
				|| CastField<FFieldPathProperty>(Prop) || CastField<FDelegateProperty>(Prop)
				|| CastField<FMulticastDelegateProperty>(Prop))
			{
				return true;
			}
			if (FArrayProperty* Arr = CastField<FArrayProperty>(Prop))
			{
				return ContainsObjectRef(Arr->Inner);
			}
			if (FMapProperty* Map = CastField<FMapProperty>(Prop))
			{
				return ContainsObjectRef(Map->KeyProp) || ContainsObjectRef(Map->ValueProp);
			}
			if (FSetProperty* Set = CastField<FSetProperty>(Prop))
			{
				return ContainsObjectRef(Set->ElementProp);
			}
			if (FStructProperty* SP = CastField<FStructProperty>(Prop))
			{
				if (SP->Struct)
				{
					for (TFieldIterator<FProperty> It(SP->Struct); It; ++It)
					{
						if (ContainsObjectRef(*It))
						{
							return true;
						}
					}
				}
			}
			return false;
		}
	}

	bool IsSaveableProperty(FProperty* Prop)
	{
		if (!Prop)
		{
			return false;
		}
		// 运行时瞬态与编辑器数据不保存；BlueprintReadOnly 只限制蓝图写访问，值本身可保存
		if (Prop->HasAnyPropertyFlags(CPF_Transient | CPF_EditorOnly))
		{
			return false;
		}
		// 含对象引用的（递归：数组/映射/结构内部的引用也算）不保存
		if (ContainsObjectRef(Prop))
		{
			return false;
		}
		// 其余全部支持：数值/布尔/枚举/字符串/名字/结构/数组/映射/集合（用 FProperty::SerializeItem 通用序列化）
		return true;
	}

	int32 CaptureObjectProps(FArchive& W, UObject* Obj, const FString& Prefix, const TArray<FName>* NameFilter)
	{
		if (!Obj)
		{
			return 0;
		}
		int32 Count = 0;
		for (TFieldIterator<FProperty> It(Obj->GetClass()); It; ++It)
		{
			FProperty* Prop = *It;
			if (!IsSaveableProperty(Prop))
			{
				continue;
			}
			if (NameFilter && NameFilter->Num() > 0)
			{
				if (!NameFilter->Contains(Prop->GetFName()))
				{
					continue;
				}
			}
			FString FullName = Prefix + Prop->GetName();
			void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Obj);

			// 格式：Name, ValueLen, ValueBytes（值用引擎通用序列化）
			W << FullName;
			const int64 ValueLenPos = W.Tell();
			uint32 DummyLen = 0;
			W << DummyLen;
			const int64 ValueStart = W.Tell();
			{
				FStructuredArchiveFromArchive ArchiveWrapper(W);
				Prop->SerializeItem(ArchiveWrapper.GetSlot(), ValuePtr, nullptr);
			}
			uint32 ActualLen = (uint32)(W.Tell() - ValueStart);
			W.Seek(ValueLenPos);
			W << ActualLen;
			W.Seek(ValueStart + ActualLen);
			++Count;
		}
		return Count;
	}

	int32 RestoreObjectProps(FArchive& R, UObject* Obj, int32 Count)
	{
		int32 Restored = 0;
		if (!Obj)
		{
			return Restored;
		}
		for (int32 i = 0; i < Count; ++i)
		{
			FString FullName;
			R << FullName;
			uint32 ValueLen = 0;
			R << ValueLen;

			FString PropName = FullName;
			const int32 Dot = FullName.Find(TEXT("."));
			UObject* Target = Obj;
			if (Dot != INDEX_NONE)
			{
				// 带组件前缀：主对象必须是 Actor
				PropName = FullName.RightChop(Dot + 1);
				Target = nullptr;
				if (AActor* Actor = Cast<AActor>(Obj))
				{
					const FString CompName = FullName.Left(Dot);
					for (UActorComponent* Comp : Actor->GetComponents())
					{
						if (Comp && Comp->GetName() == CompName)
						{
							Target = Comp;
							break;
						}
					}
				}
			}
			if (Target == nullptr)
			{
				R.Seek(R.Tell() + (int64)ValueLen);
				continue;
			}
			if (FProperty* Prop = FindFProperty<FProperty>(Target->GetClass(), FName(*PropName)))
			{
				if (IsSaveableProperty(Prop))
				{
					void* ValuePtr = Prop->ContainerPtrToValuePtr<void>(Target);
					{
						FStructuredArchiveFromArchive ArchiveWrapper(R);
						Prop->SerializeItem(ArchiveWrapper.GetSlot(), ValuePtr, nullptr);
					}
					++Restored;
					continue;
				}
			}
			R.Seek(R.Tell() + (int64)ValueLen);
		}
		return Restored;
	}
}