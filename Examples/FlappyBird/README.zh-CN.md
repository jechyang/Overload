# Flappy Bird（uGUI）

使用真实 ScriptCore uGUI、原生输入快照和引擎渲染管线运行。FlappyBirdGame : Graphic 绘制场地和小鸟；FlappyScore / FlappyHint / FlappyResult : Text 显示分数、提示和最高分。

## 打开示例

先构建当前 Debug 原生引擎，再在仓库根目录运行：

    node Examples/FlappyBird/build.mjs

脚本自动寻找 Python 3.10+，也会复用已有 FlappyBird.local.ovproject 中的 Python 路径。首次运行若没有找到，可指定绝对路径或设置 OV_REFLECTION_PYTHON：

    node Examples/FlappyBird/build.mjs "C:/path/to/python.exe"

构建成功会生成 FlappyBird.local.ovproject，指向本次构建的托管宿主和游戏程序集。修改引擎 ManagedHost 后，需要退出原来的编辑器进程，再用 OvEditor 打开这个本机项目。

也可从仓库根目录直接启动（显式传入本机项目文件）：

    & .\Build\Debug\OvEditor.exe (Resolve-Path .\Examples\FlappyBird\FlappyBird.local.ovproject).Path

打开 Assets/Scenes/FlappyBird.ovscene，进入 **Play**，在 **Game View** 中操作：

| 操作 | 效果 |
|---|---|
| Space / Enter / 鼠标左键 | 开始、拍翅 |
| 撞管或碰到上下边界 | Game Over |
| Game Over 后 Space / 左键 | 回到 Ready，清零分数，保留本次运行最高分 |
| 失去 Game View 焦点 | 暂停模拟，恢复焦点后继续 |

编辑模式可预览，预览不会创建运行时 EventSystem。固定 400×600 逻辑场地，CanvasScaler Expand 适配窗口，横屏保留两侧背景。首根管道留有反应距离，完全越过管道后才计分。热重载后回到 Ready，保留最高分。

start_scene 相对 Assets 配置为 Scenes/FlappyBird.ovscene，供独立 Game 加载。

## 回归验证

构建匹配当前头文件的 Debug 原生库及 OvEditor/OvGame 后运行：

    node Tests/UI/run-flappy-validation.mjs --apps

不加 --apps 时运行原生运行时场景测试；加上时还链接产品 Editor/Game 对象，验证 Play/Pause/Stop/再次 Play 和独立游戏启动。

测试加载真实场景与脚本，通过 Window → InputManager 注入键鼠事件，覆盖首次显示、连续六次过管及管道复用、计分、死亡、重开、焦点、窗口比例和热重载。普通模式与 RenderThread 分别执行，比较 Ready/Playing/Game Over 的 GPU 像素。

每次证据保存在 Build/FlappyValidation/flappy-*/：result.json、各步骤日志及 preview.png、play.png、dead.png。产品测试在独立目录运行，不写入用户的编辑器配置。
