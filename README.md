# ChunkStreaming

图驱动的关卡流送插件（UE 5.4），为横板 3D 超长连续世界设计。

## 模块

| 模块 | 类型 | 内容 |
|---|---|---|
| `ChunkStreaming` | Runtime | `UChunkGraphAsset`（连接图资产）、`UChunkStreamingSubsystem`（流送管理器）、`UChunkStateStore`（敌人状态库）、`UChunkEnemyStateComponent`（状态组件）、`IChunkSaveable`（状态接口）、`UChunkBackgroundLayerComponent`（视差组件）、`UChunkStreamingSettings`（项目设置） |
| `ChunkStreamingEditor` | Editor | `Window → Chunk Streaming → Graph Editor` 面板：自动建图、校验、视口可视化、区块隔离编辑 |

## 核心机制

- **图驱动流送**：只加载"当前区块 + 邻居"，相机带 LookAhead 预加载，身后按 KeepBehind 保留
- **半开区间** `[MinX, MaxX)`：边界点只属于右侧区块
- **双判定分离**：流送焦点跟相机、玩家事件跟玩家坐标
- **背景块 OR 加载**：任一引用它的玩法块加载则加载，跨块无缝
- **瞬移节点**：`Teleport To Chunk`，过渡模式 + 代际号防加载不同步
- **敌人状态持久化**：卸载前保存、加载完成后恢复（GameInstance 级状态库，可并入存档）

## 使用

详见项目文档 `Docs/ChunkStreaming使用说明.md`（快速开始 5 步、编辑器工具、蓝图 API、调试命令）。

## 依赖

- UE 5.4（项目 EngineAssociation 5.4）
- 无第三方依赖
