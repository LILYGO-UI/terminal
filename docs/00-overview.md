# LILYGO UI Terminal 架构总览

本应用基于 LILYGO UI AppKit 和 LVGL，在 CM0 ARM64 Linux 设备上提供可交互
终端，同时保留 SDL2 主机模拟器用于开发和测试。应用通过 PTY 运行 `/bin/sh`，
支持 shell 的管道、交互式输入、程序中断和终端窗口尺寸通知。

项目介绍、依赖安装和构建入口见 [中文 README](../README.zh-CN.md) 或
[English README](../README.md)。运行配置、权限和故障排查见
[终端会话指南（英文）](02-terminal-sessions.md)，页面开发要求见
[UI 开发规范](01-user-interface.md)。

## 数据流

```text
触摸键盘/特殊键/LVGL 键盘事件
              |
              v
TerminalView -> TerminalViewModel -> TerminalModel
                       |
                       v
                TerminalSession <-> PTY <-> /bin/sh
                       |
                       v
                libtmt ANSI 屏幕缓冲
                       |
              LVGL 定时器读取快照
```

PTY 读取线程只更新受 mutex 保护的 ANSI 屏幕，不直接操作 LVGL。页面每 45 ms
在 LVGL 线程中通过 `TerminalViewModel::state()` 获取屏幕副本，并依据 revision
更新行标签和状态。当前终端使用快照轮询，不通过 Subject 发布屏幕内容。

## 模块边界

| 模块 | 责任 |
| --- | --- |
| `src/domain/terminal_emulator.*` | 封装 libtmt，解析 ANSI 序列、光标和终端查询响应 |
| `src/domain/terminal_session.*` | 创建和销毁 PTY/shell、异步读取输出、写入输入、同步行列数 |
| `src/pages/terminal/terminal_model.*` | 字号范围、Ctrl 组合键、特殊键字节和配置持久化 |
| `src/pages/terminal/terminal_view_model.*` | 将页面命令编排为会话操作，并合成展示快照 |
| `src/pages/terminal/terminal_view.*` | 响应式布局、终端行渲染、特殊键区和四态软键盘 |
| `src/app.cpp` | 应用根对象、View 和 ViewModel 的创建与销毁 |

`lilygo_ui_terminal_core` 包含终端后端和页面 Model，只依赖线程库和 libtmt；
`lilygo_ui_terminal_ui` 链接 core 与 AppKit Runtime。核心层不依赖 LVGL，
可独立验证按键、配置、ANSI 解析及 PTY 行为。

## 会话生命周期

页面完成首次布局后先计算可见行列数，再启动 shell。字号或视口变化时，页面根据
实际字体度量重新计算行列数，`TerminalSession` 同时调用 `tmt_resize()` 和
`TIOCSWINSZ`，让 shell 与画面保持一致。

shell 子进程继承 UI 进程的用户和用户组权限。普通用户启动应用时使用该用户
身份，设备 Launcher 以 root 启动应用时则运行 root shell，无需已有 SSH 或控制台
登录会话。

默认启动 `/bin/sh -l`。主目录依次取非空的 `LILYGO_UI_TERMINAL_HOME`、`HOME`、
进程真实 UID 对应的账户目录，最后回退到 `/`。子进程继承原有环境，只覆盖
`TERM`、`SHELL`、`HOME`，以及非空提示符覆盖指定的 `PS1`。shell 启动时进入
该主目录，无法进入则尝试 `/`。`Restart` 回收旧 shell 后创建新的会话。

关闭页面时先删除 LVGL 定时器，再停止 shell：发送 `SIGHUP`、关闭 PTY、等待读取
线程退出并回收子进程。若子进程未及时退出，才使用 `SIGKILL` 完成清理。

## 输入规则

- `Ctrl` 是一次性锁定键，下一次字母输入会转换成控制字符。
- `Esc`、`Tab`、`Enter`、方向键和退格发送标准终端序列。
- 启用 `Ctrl` 后点击特殊键区的 `L`，同时发送换页字符并清空本地屏幕。
- `Clear` 只清空本地 ANSI 缓冲，不终止当前程序。
- `Restart` 完整回收旧 shell 后启动新 PTY 会话。
- 软键盘可切换小写、大写、数字和符号四种映射。

## 字号持久化

可选字号为 14、18 和 22 px，默认 18 px。设置使用临时文件加原子重命名写入：

```text
$XDG_CONFIG_HOME/lilygo-ui-terminal/settings.conf
```

未设置 `XDG_CONFIG_HOME` 时优先使用 `$HOME/.config`，再回退到 UI 进程真实 UID
对应的账户目录，无法获取时使用 `/tmp/.config`。测试可通过
`LILYGO_UI_TERMINAL_CONFIG` 指定隔离路径。配置损坏或超出范围时会回退并限制到
有效字号范围，不影响终端启动。完整路径规则见
[会话指南的字体配置章节](02-terminal-sessions.md#font-preferences)。

## 响应式布局

- 竖屏：终端、特殊键入口、软键盘自上而下排列。
- 横屏：终端和特殊键入口并排，软键盘保持在底部。
- 特殊键入口打开一个独立弹窗，使用更大的按钮选择终端控制键。
- 接入实体键盘时隐藏底部软键盘，特殊键入口仍可打开。
- 终端区始终保留至少 4 行和 16 列，最大 48 行和 120 列。

## 兼容范围

子进程使用 `TERM=ansi`。libtmt 负责 ANSI 解析，当前 View 将屏幕行转换为文本
标签，不呈现每个字符的 ANSI 颜色或样式，也没有滚动历史。全屏程序、复杂字符
宽度和要求 xterm 扩展的程序需要单独验证，不能将其视为完整的桌面终端模拟器。

## 验收

```sh
cmake --preset host-simulator
cmake --build --preset host-simulator --parallel
ctest --preset host-simulator
```

标准验证视口为 `320x568`、`568x1232`、`1232x568` 和 `1024x768`。UI 测试
在 `build/host-simulator/` 生成 `terminal-<viewport>.ppm`，需结合视觉检查确认
按键、状态和输出没有裁切或重叠。测试范围见
[UI 开发规范的验收章节](01-user-interface.md#验收)。涉及工具链、依赖或打包
变更时还需完成 `cm0-cross` 构建和 Debian 打包。
