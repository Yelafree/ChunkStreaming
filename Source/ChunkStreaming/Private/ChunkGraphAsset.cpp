#include "ChunkGraphAsset.h"

FName UChunkGraphAsset::FindChunkAtX(float X) const
{
	FName Best = NAME_None;
	float BestSpan = TNumericLimits<float>::Max();
	for (const FChunkInfo& Info : Chunks)
	{
		if (Info.Category != EChunkCategory::Gameplay)
		{
			continue; // 背景/常驻块不参与玩家进入判定
		}
		if (X >= Info.XRange.X && X < Info.XRange.Y)
		{
			// 范围重叠时取"范围最小"的块（最具体），例如站中站：站内判定为站
			const float Span = Info.XRange.Y - Info.XRange.X;
			if (Span < BestSpan)
			{
				BestSpan = Span;
				Best = Info.LevelName;
			}
		}
	}
	return Best;
}

FName UChunkGraphAsset::FindChunkAtLocation(const FVector& Location) const
{
	return FindChunkAtLocationSmart(Location, NAME_None);
}

namespace
{
	/** 从候选集合中按"当前块保持 > 与当前相连 > 范围最小"选择。 */
	template <typename TSpanFn>
	FName PickFromCandidates(const UChunkGraphAsset* Asset, const TArray<FName>& Candidates, FName Cur, TSpanFn SpanOf)
	{
		if (!Asset || Candidates.Num() == 0)
		{
			return NAME_None;
		}
		// 1) 当前所在区块：保持稳定（重叠区不抖动、不规则形状相邻不误判）
		if (!Cur.IsNone() && Candidates.Contains(Cur))
		{
			return Cur;
		}
		// 2) 与当前区块相连的候选：取其中范围最小的（沿连接进入重叠区）
		if (!Cur.IsNone())
		{
			FName Best = NAME_None;
			float BestSpan = TNumericLimits<float>::Max();
			for (const FName& C : Candidates)
			{
				if (!Asset->HasConnection(Cur, C))
				{
					continue;
				}
				const FChunkInfo* Info = Asset->FindChunkInfoPtr(C);
				if (!Info)
				{
					continue;
				}
				const float Span = SpanOf(*Info);
				if (Span < BestSpan)
				{
					BestSpan = Span;
					Best = C;
				}
			}
			if (!Best.IsNone())
			{
				return Best;
			}
		}
		// 3) 范围最小（站中站/传送房等不相连重叠，进入更具体的块）
		FName Best = NAME_None;
		float BestSpan = TNumericLimits<float>::Max();
		for (const FName& C : Candidates)
		{
			const FChunkInfo* Info = Asset->FindChunkInfoPtr(C);
			if (!Info)
			{
				continue;
			}
			const float Span = SpanOf(*Info);
			if (Span < BestSpan)
			{
				BestSpan = Span;
				Best = C;
			}
		}
		return Best;
	}
}

FName UChunkGraphAsset::FindChunkAtLocationSmart(const FVector& Location, FName CurrentChunk) const
{
	// 收集 3D 包含候选
	TArray<FName> Contained;
	for (const FChunkInfo& Info : Chunks)
	{
		if (Info.Category != EChunkCategory::Gameplay)
		{
			continue;
		}
		if (Info.WorldBounds.IsInside(Location))
		{
			Contained.Add(Info.LevelName);
		}
	}
	if (Contained.Num() > 0)
	{
		return PickFromCandidates(this, Contained, CurrentChunk,
			[](const FChunkInfo& I) { return I.XRange.Y - I.XRange.X; });
	}

	// 3D 未命中：回退纯轴判定（同规则）
	const float X = GetAxisCoord(Location);
	TArray<FName> OnAxis;
	for (const FChunkInfo& Info : Chunks)
	{
		if (Info.Category != EChunkCategory::Gameplay)
		{
			continue;
		}
		if (X >= Info.XRange.X && X < Info.XRange.Y)
		{
			OnAxis.Add(Info.LevelName);
		}
	}
	return PickFromCandidates(this, OnAxis, CurrentChunk,
		[](const FChunkInfo& I) { return I.XRange.Y - I.XRange.X; });
}

bool UChunkGraphAsset::FindChunkInfo(FName LevelName, FChunkInfo& OutInfo) const
{
	const FChunkInfo* Found = FindChunkInfoPtr(LevelName);
	if (Found)
	{
		OutInfo = *Found;
		return true;
	}
	return false;
}

const FChunkInfo* UChunkGraphAsset::FindChunkInfoPtr(FName LevelName) const
{
	for (const FChunkInfo& Info : Chunks)
	{
		if (Info.LevelName == LevelName)
		{
			return &Info;
		}
	}
	return nullptr;
}

void UChunkGraphAsset::GetNeighbors(FName LevelName, TArray<FName>& OutNeighbors) const
{
	OutNeighbors.Reset();
	for (const FChunkConnection& Conn : Connections)
	{
		if (Conn.FromLevel == LevelName)
		{
			OutNeighbors.AddUnique(Conn.ToLevel);
		}
		else if (Conn.ToLevel == LevelName)
		{
			OutNeighbors.AddUnique(Conn.FromLevel);
		}
	}
}

bool UChunkGraphAsset::HasConnection(FName LevelA, FName LevelB) const
{
	for (const FChunkConnection& Conn : Connections)
	{
		if ((Conn.FromLevel == LevelA && Conn.ToLevel == LevelB) ||
			(Conn.FromLevel == LevelB && Conn.ToLevel == LevelA))
		{
			return true;
		}
	}
	return false;
}

float UChunkGraphAsset::GetAxisCoord(const FVector& V) const
{
	return (MovementAxis == EAxis::Y) ? V.Y : V.X;
}
