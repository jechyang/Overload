# Flappy Bird（uGUI）

基于 ScriptCore 的 uGUI 运行时实现的可玩 Flappy Bird：

- `CircleGraphic`：项目 C# 重写 `OnPopulateMesh` 画圆（小鸟）
- `Image`：天空、地面、管道
- `Text`：分数与提示
- `EventSystem` + `StandaloneInputModule`：Space / 鼠标点击拍翅
- `FlappyBirdGame`：`OnUpdate` 驱动重力、管道滚动、计分与重开

## 打开示例

先构建最新 Debug 引擎，再在仓库根目录执行（需要 Python 3）：

    node Examples/FlappyBird/build.mjs <Python 3 可执行文件的绝对路径>

也可设置 `OV_REFLECTION_PYTHON`。脚本生成 `FlappyBird.local.ovproject`。

用 `OvEditor` 打开 `FlappyBird.local.ovproject`，再打开
`Assets/Scenes/FlappyBird.ovscene`，进入 **Play**：

| 操作 | 效果 |
|---|---|
| Space / 鼠标左键 | 拍翅上升 |
| 撞管或落地 | Game Over |
| 再按 Space / 点击 | 回到 Ready 重开 |

参考分辨率 400×600（`FlappyCanvas`）。

## 代码入口

`Assets/Scripts/FlappyBirdGame.cs`：

- `Build()`：创建背景/地面/管道池/小鸟/分数/结束面板
- `OnUpdate`：Ready 悬停 → Playing 物理+管道+碰撞 → Dead 重力坠落+重开
- 修改脚本后 Build/Reload 可热重载，无需重编 C++。
