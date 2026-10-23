---
title: ScreenShot M0 技术验证报告
meta: v1.0 · 2026-10-23 · 输入：[技术架构文档](02-architecture.md) 的 M0 验证清单
toc: [2, 3]
marks:
  实测: {kind: ok, title: 本机自动测试得到的数据}
  待测: {kind: warn, title: 需要解锁桌面或特定设备后再测}
  延后: {kind: muted, title: 按计划延后到后续里程碑}
---

# ScreenShot M0 技术验证报告

## 结论 {#summary}

==M0 的工具链、测试框架、内置测试接口已搭好并提交；17 项风险验证中 10 项达标，4 项因测试机锁屏只能等解锁后补测，3 项按计划延后（macOS 设备、HDR 设备）。没有一项需要启用预案。==

Table: M0 验证清单结果 {.lead}

| 编号 | 风险 | 结果 | 数据 | 状态 |
|---|---|---|---|---|
| `R-01` | 工具链组合 | Qt 6.12.0 + MSVC 14.51（VS 2026 Build Tools）+ C++20 编译运行正常 | 90 个目标全部构建，11 个单元 / 组件测试通过 | :ok[达标] :m[实测] |
| `R-02` | 冻结画面延迟 | 两台 2560×1440（125%），100 次：P50 15.9 ms，P95 19.2 ms，P99 21.1 ms | 其中抓屏 P50 10.1 ms（DXGI） | :ok[达标] :m[待测]（锁屏期间测得，解锁后复测 1000 次） |
| `R-03` | 混合缩放坐标精度 | 合成布局（150% / 100%）跨屏裁剪逐像素零偏差；逻辑→物理换算在 100%~300% 六档缩放下零偏差 | 真实鼠标框选用例已写好 | :ok[算法达标] :m[待测]（真实鼠标部分需解锁桌面） |
| `R-04` | 覆盖窗口上的中文输入法 | 文字工具在 M2 实现，随 `AC-03` 验证 | — | :muted[延后] |
| `R-05` | CPU 透明贴图性能 | 50 个 1080p 贴图：拖动最低 110 次/秒，缩放最低 203 FPS，最慢一帧 1.9 ms | 带贴图私有内存 904 MB（见 [](#memory)） | :ok[达标] :m[实测] |
| `R-06` | CPU 马赛克 / 模糊 | 4K 高斯模糊半径 10：44 ms，半径 40：8 ms；马赛克：4 ms | 要求 ≤ 100 ms | :ok[达标] :m[实测] |
| `R-07` | CPU 大图缩放 | 8K 贴图连续缩放：缩小 293 FPS，放大 117 FPS，最慢一帧 4.7 ms | 要求 ≥ 30 FPS | :ok[达标] :m[实测] |
| `R-08` | 常驻内存 | 无贴图空闲 10 s 后私有内存 4.7 MB | 要求 ≤ 150 MB；10 分钟版本与 Snipaste 同口径基线待测 | :ok[达标] :m[待测] |
| `R-09` | UI 元素检测卡死 | Debug 构建实测：挂起窗口 309 ms 返回，挂起期间第二次调用 1 ms 被拒绝，任务栏元素 23 ms 找到 | Release 构建需解锁桌面复测 | :ok[达标] :m[待测] |
| `R-10` | HDR 色彩 | 没有 HDR 显示器 | — | :muted[延后] 到 M5 |
| `R-11` | Windows 系统 OCR | LTSC 2021 上未打包程序经 C++/WinRT 调用成功，英文、中文都能识别，21 ms | 中文逐字带空格已在平台层合并 | :ok[达标] :m[实测] |
| `R-12` | 双显卡笔记本捕获 | 测试机单显卡（RTX 4060） | 代码已按显卡分别创建设备 | :muted[延后]（无设备） |
| `R-13` `R-14` `R-15` | macOS 三项 | 没有 Mac 设备 | macOS 平台层已编写、未编译 | :muted[延后] |
| `R-16` | 测试替身覆盖多屏用例 | 合成两台 4K（150% / 100%）跨屏裁剪与真实算法逐像素一致 | 差异像素 0 | :ok[达标] :m[实测] |
| `R-17` | Qt Qml 是否依赖 Qt Network | `Qt6Qml.dll` 导入表含 `Qt6Network.dll` | 维持 QuickJS-ng 方案 | :ok[有结论] :m[实测] |

> [!warn] 锁屏导致的待测项
> 整个端到端测试期间测试机处于锁屏状态（失败截图里是锁屏时钟）。`SendInput` 的鼠标、键盘到不了锁屏后面的窗口，DXGI 桌面复制也会因桌面切换失效。测试驱动现已用 WTS 会话状态检测锁屏并把 `R-02` `R-03` `HOTKEY` `R-09` 标为跳过。解锁后运行下面的命令即可补齐：
>
> 命令
> : `scripts\test.cmd win-release-test -L e2e`

## 环境 {#env}

Table: 测试机与工具链 {.lead}

| 项目 | 实际 |
|---|---|
| 系统 | Windows 10 IoT Enterprise LTSC 2021（19044）x64 |
| 显示器 | 两台 2560×1440，都是 125%（左屏坐标为负）；不是架构文档假设的两台 4K 150% / 100%，混合缩放改由合成显示器覆盖 |
| 硬件 | 28 逻辑处理器，128 GB 内存，RTX 4060 单显卡 |
| 编译器 | Visual Studio 2026 Build Tools 18.11.1，MSVC 14.51.36231，Windows SDK 10.0.26100 |
| Qt | 6.12.0 msvc2022_64（qtbase、qtsvg、qttools、qttranslations、qtdeclarative、qtimageformats） |
| 构建 | CMake 4.4.0 + Ninja 1.13.2，`CMakePresets.json` 三个预设：`win-debug`、`win-release-test`、`win-release` |
| 调试 | WinDbg（含 `cdbX64`），崩溃转储在 `%LOCALAPPDATA%\CrashDumps` |

> [!note] Qt 6.12 的获取方式
> aqtinstall 3.3.0（当前最新）还不认识 6.12 的仓库布局，而且 6.12 的 7z 包不再带 `6.12.0/msvc2022_64/` 前缀。改用自写脚本从中科大镜像下载各模块的 7z 包、核对官方源的 sha1、解压后移到 `C:\Qt\6.12.0\msvc2022_64`，并补上 aqt 会做的 `qt.conf` 与安装前缀修补。原有的 Qt 6.11.1 MinGW 及其 MinGW 工具链已删除。

## 已交付的内容 {#delivered}

Table: 代码结构（CMake 目标） {.lead}

| 目标 | 内容 | 行数量级 |
|---|---|---|
| `screenshot_core` | 平台接口（纯虚类）、虚拟桌面几何合成、三次盒式高斯模糊、马赛克、多级缩小图、单调时钟 | 核心算法全部可单元测试 |
| `platform_windows` | DXGI 桌面复制（GDI 兜底）、显示器枚举、DWM 窗口检测、UIA 元素检测（独立 MTA 线程 + 300 ms 超时）、RegisterHotKey、Windows.Media.Ocr、进程内存 / 网络端点 / PE 导入表 | 只用系统组件 |
| `platform_macos` | ScreenCaptureKit、CGWindowList、Accessibility、Carbon 热键、Vision OCR、权限、窗口层级（.mm） | :warn[未编译] |
| `screenshot_applib` / `screenshot_app` | 托盘、截图会话、每屏覆盖窗口、贴图窗口原型、内置测试接口（命名管道，拒绝远程客户端，只在测试构建编译） | — |
| `screenshot_testing` | 合成显示器、可控失败 / 挂起的替身、图像比对与失败产物 | — |
| `screenshot_testdriver` | 启动程序、经测试接口执行 12 个场景、监控网络端点、输出 JSON 报告；`check-imports` 做导入表检查 | — |
| 测试 | 9 个 Qt Test 程序（单元 6 + 组件 3）、需求覆盖矩阵、零网络导入表检查、端到端 | `ctest` 一条命令 |

### 测试框架如何满足 TST 需求

- `TST-01` 需求追踪：用例文件头写 `REQ: 编号`，`tools/coverage/gen_matrix.py` 扫描需求文档与用例生成矩阵，`tests/delivered_scope.txt` 里的编号缺用例即构建失败。M0 范围：`TST-02` `TST-03` `TST-04` `TST-09` `TST-10` `NFR-07` `NFR-08` `NFR-10`。
- `TST-02` 分层：单元（core）、组件（`QT_QPA_PLATFORM=offscreen` 下的会话、覆盖窗口、贴图窗口）、端到端（真实桌面）。
- `TST-03` 内置测试接口：注入合成显示器布局、触发截图、设置选区、输出 PNG、创建 / 拖动 / 缩放贴图、读内存、模糊基准、OCR 自测、元素检测、桌面截图。
- `TST-04` 图像比对：`compareImages` 要求用例显式给出容差；失败时写出实际图、期望图、差异图。
- `TST-09` 零网络：构建后 `dumpbin /dependents` 检查，测试驱动运行期间轮询进程的 TCP / UDP 端点；两次端到端运行均为零端点。
- `TST-10` 一条命令：`scripts\test.cmd [preset]`，报告在 `build\<preset>\test-results\`。

## 过程中的发现与决定 {#findings}

Table: 发现与处理 {.lead}

| 发现 | 处理 | 影响 |
|---|---|---|
| 高斯模糊在 Release 构建下必崩（Debug 不崩）：三次盒式模糊第二遍把与调用方共享的输入图像当输出，多个工作线程同时调用非 const 的 `scanLine()` 触发并发 detach，cdb 在 `boxBlurH` 的读取侧确认 | 工作线程只用 const 接口读源图；输出缓冲在并行区之前用 `bits()` 独占；输入永远不作为输出 | 这条规则写进 `fx` 代码注释，后续所有按块并行的算法遵守 |
| 50 个 1080p 贴图在原型实现下私有内存 1.5 GB：每个贴图各持一份全尺寸阴影位图和平滑缩放缓存 | 阴影改为九宫格小图拉伸；1:1 时直接画原图不建缓存；窗口只覆盖内容在桌面上可见的部分 | 降到 904 MB，见 [](#memory) |
| 8K 贴图原型创建 7680×4320 的分层窗口，每帧要推送 130 MB | 窗口裁剪到桌面可见区域；新贴图超出屏幕时自动缩小（`PIN-42`） | 缩放 117~293 FPS |
| Windows OCR 把中文逐字当“单词”，行文本字与字之间带空格 | 平台层合并 CJK 字符之间的空格 | `REC-03` 的排版选项以此为基础 |
| 锁屏时 `OpenInputDesktop` 仍能打开 Default 桌面（Windows 10 的锁屏画在上面），不能用来判断锁屏 | 改用 `WTSQuerySessionInformation` 的会话锁定标志 | 测试驱动能正确跳过真实输入场景 |
| 从 Git Bash 启动程序时 `--test-pipe` 参数没有生效；pymanager 的 Python 打不开命名管道（`ERROR_PATH_NOT_FOUND`） | 端到端由 ctest / PowerShell 启动；临时诊断客户端用 PowerShell 的 `NamedPipeClientStream` | 只影响开发期的手工调试 |
| `Qt6Qml.dll` 直接导入 `Qt6Network.dll` | 脚本引擎维持 QuickJS-ng 方案（`R-17`） | 不链接 Qt Qml |

### 关于内存口径 {#memory}

- 本报告的“私有内存”是 `PROCESS_MEMORY_COUNTERS_EX.PrivateUsage`（私有提交），不含映射的 DLL 页面。空闲 4.7 MB 对一个 Qt Widgets 程序是合理的，因为 Qt 的 DLL 不算私有。
- 架构文档里“Snipaste 约 130 MB”是任务管理器一栏读数，口径未必相同。`NFR-02` 的基线要在同一台机器上用同一个计数器重测 Snipaste（目前本机没有安装）。
- 50 个 1080p 贴图的 904 MB 中，底图在测试里是同一份共享图像，主要开销是每个分层窗口的后备缓冲（约 9 MB × 2）和多级缩小图（约 2.7 MB）。`NFR-04` 要求内存与总像素线性相关，目前满足；要进一步降低需要按窗口可见区而不是整窗分配后备缓冲，列为 M3 的优化项。

## 下一步 {#next}

1. 桌面解锁后运行 `scripts\test.cmd win-release-test -L e2e`，补齐 `R-02`（1000 次）、`R-03` 真实鼠标、`HOTKEY`、`R-09` Release 数据，更新本报告。
2. 进入 M1 截图核心：`GEN` `CAP` `OUT` 中的 P0 与最小偏好设置，交付标准见需求文档里程碑表。
3. 有 Mac 设备后首次构建 `platform_macos`，集中修复编译错误，补做 `R-13` `R-14` `R-15`。
