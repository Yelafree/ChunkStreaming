# ChunkStreaming — 图驱动的关卡流送插件

为 **3D 横板 / 横版动作游戏**设计的 UE 关卡流送方案：把超长连续世界拆成区块，用一张**可编辑的连接图**（动画状态机式）描述区块关系，运行时只加载"当前区块 + 相连区块"，并内置**魂类式敌人管理系统**。

> 支持 UE 5.0 / 5.4 / 5.5 / 5.6（Win64），无第三方依赖。

---

## 核心特性

### 关卡流送
- **图驱动加载**：只加载"当前区块 + 邻居（连接图）+ 前进方向预加载"，线性推进不卡顿
- **无向连接**：连接不区分方向/进/出，只存在"相连/不相连"；连接图编辑器为**动画状态机式**——按住节点边缘拖到另一区块即建立连接，线自动取节点边缘最短路径
- <img width="1279" height="761" alt="屏幕截图 2026-09-03 064627" src="https://github.com/user-attachments/assets/771352d2-b939-43d9-be25-f7bcf36b76aa"/>

- **连通分量隔离**：一组相连区块与另一组不相连的区块**永不共载**；隔离区块（无连接）只能通过传送进入
- **智能玩家判定**：重叠区按"当前块保持 > 相连块 > 范围最小"选择（不规则地形的 AABB 重叠不再误判）
- **Z 轴高度层检测**（可选）：开启后玩家与区块高度差超阈值会延迟/立即卸载，回到高度层自动加载
- 边界半开区间、迟滞窗口、KeepBehind 身后保留等细节均为横板手感调校

### 敌人管理系统（魂类风格）
- **占位收编**：编辑器里直接摆放敌人（直观），运行时自动注册为"Spawner 记录"并自我销毁——敌人资源不常驻
- **按距离生成/回收**：玩家靠近"家"生成活体（生成到持久层，**可跨区块追击**）；追丢/脱战回收（**不记死亡**，回来重新生成）；击杀 = **永久死亡**（GameInstance 级，跨 Open Level 保留）
- **位置与数值连续**：活体最后位置被记录，回收后回到原位置继续；`ComponentsToSave` 直接选择组件，其数值变量（含 TMap/TArray/结构体）自动保存恢复
- **`Reset Enemy States` 蓝图节点**：玩家死亡 / 坐火等关卡重启流程调用——敌人以全新状态重新加载（冻结语义：在场敌人保持现状至关卡重启，不突兀）
- **`OnEnemyRespawned` 事件**：敌人被再次加载时蓝图可绑定做自定义处理

### 敌人状态持久化（卸载 ↔ 重载）
- 挂 `ChunkEnemyStateComponent`（或让组件实现 `IChunkSaveable`）即自动保存：血量 / 死亡 / 位置 + **反射自动收集**的数值变量
- 跨区块、跨 Open Level 保留（GameInstance 级状态库）
<img width="349" height="204" alt="屏幕截图 2026-09-03 064657" src="https://github.com/user-attachments/assets/9e64c447-621e-4811-9cba-6129f7b821cf" />


---

## 安装

### 方式一：Release 包（推荐）
从 [Releases](https://github.com/Yelafree/ChunkStreaming/releases) 下载对应引擎版本的 zip（含编辑器二进制），解压到项目 `Plugins/ChunkStreaming`，重启编辑器即可。

### 方式二：源码编译
克隆本仓库放入项目 `Plugins/ChunkStreaming`，重启编辑器由引擎自动编译（支持 5.0/5.4/5.5/5.6）。

## 快速开始（5 步）

1. 把超长关卡按"玩法区域"拆成多个子关卡（Levels 面板 Add 到主关卡，作为流送子关卡）
2. 主关卡打开 **Chunk Graph** 面板 → 资产框选/新建图资产 → **Refresh Bounds**（自动计算每个子关卡的可碰撞包围盒）
3. **Auto Graph** 自动连接相邻区块；再按住节点边缘手动修正（拖到目标区块相连；Alt+点击线断开）
4. `Project Settings → Chunk Streaming`：指定图资产，按需调 LookAhead/KeepBehind 等参数
5. **Play**——只有你所在的区块和相连区块会加载；走远回收、回来重载

> 详细说明（含区块类型、蓝图 API、敌人状态、调试命令、常见问题）：**[使用说明文档](Docs/ChunkStreaming使用说明.md)**

---

## 蓝图 API 摘要

| 节点 | 说明 |
|---|---|
| `Get Chunk Streaming Subsystem` | 查询当前区块 / 玩家区块 / 加载状态 |
| `Get Current Chunk Name` | 静态节点（右键直接搜索即可用）：查询玩家当前所在区块 |
| `ChunkPlayerComponent`（挂玩家 Pawn） | 自带 `On Player Entered Chunk` 事件（进入新区块，可直接 Add Event）+ 查询节点 |
| `Teleport To Chunk` | 异步预加载目标区块并瞬移 |
| `Preload Teleport To Location` | 延迟式预加载传送（类似 Delay） |
| `Reset Enemy States` | 重置全部敌人状态（死亡/数值/位置），关卡重启用 |
| `ChunkEnemySpawnerComponent → OnEnemyRespawned` | 敌人被再次加载事件 |
| `ChunkStateStore` | 查询/清除区块敌人存档 |
| `ChunkStream.Debug 1`（控制台） | 视口显示区块包围盒与连接线 |

## 模块

| 模块 | 内容 |
|---|---|
| `ChunkStreaming` (Runtime) | 图资产、流送管理器、敌人管理器、状态库、状态组件、占位收编组件、背景视差组件、项目设置 |
| `ChunkStreamingEditor` (Editor) | Chunk Graph 节点图编辑器（无向连接/校验/布局）、视口可视化、工具栏入口 |

## License

[MIT](LICENSE)
