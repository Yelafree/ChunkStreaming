#include "ChunkStreamingModule.h"

#include "HAL/IConsoleManager.h"
#include "Modules/ModuleManager.h"

#include "ChunkStreamingSubsystem.h"

void FChunkStreamingModule::StartupModule()
{
	DebugCommand = IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("ChunkStream.Debug"),
		TEXT("Toggle chunk streaming debug visualization. Usage: ChunkStream.Debug [0|1]"),
		FConsoleCommandWithArgsDelegate::CreateRaw(this, &FChunkStreamingModule::HandleDebugCommand),
		ECVF_Default);
}

void FChunkStreamingModule::ShutdownModule()
{
	if (DebugCommand)
	{
		IConsoleManager::Get().UnregisterConsoleObject(DebugCommand);
		DebugCommand = nullptr;
	}
}

FChunkStreamingModule& FChunkStreamingModule::Get()
{
	return FModuleManager::LoadModuleChecked<FChunkStreamingModule>(TEXT("ChunkStreaming"));
}

void FChunkStreamingModule::HandleDebugCommand(const TArray<FString>& Args)
{
	UChunkStreamingSubsystem* Sub = UChunkStreamingSubsystem::GetActive();
	if (!Sub)
	{
		return;
	}
	if (Args.Num() > 0)
	{
		const FString& Arg = Args[0];
		if (Arg == TEXT("1") || Arg.Equals(TEXT("on"), ESearchCase::IgnoreCase))
		{
			Sub->SetDebugDraw(true);
			return;
		}
		if (Arg == TEXT("0") || Arg.Equals(TEXT("off"), ESearchCase::IgnoreCase))
		{
			Sub->SetDebugDraw(false);
			return;
		}
	}
	Sub->SetDebugDraw(!Sub->GetDebugDraw());
}

IMPLEMENT_MODULE(FChunkStreamingModule, ChunkStreaming)
