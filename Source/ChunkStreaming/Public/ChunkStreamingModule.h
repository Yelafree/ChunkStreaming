#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"
#include "HAL/IConsoleManager.h"

/**
 * ChunkStreaming 运行时模块：负责注册/注销控制台命令
 * （放在模块层而不是 WorldSubsystem 层，避免引擎关闭时全局析构顺序问题）。
 */
class CHUNKSTREAMING_API FChunkStreamingModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static FChunkStreamingModule& Get();

private:
	void HandleDebugCommand(const TArray<FString>& Args);

	IConsoleCommand* DebugCommand = nullptr;
};
