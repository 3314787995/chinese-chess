# 中国象棋

这是一个基于 C++17 和 Visual Studio 的中国象棋课程项目。项目采用“规则内核 + 多入口界面”的结构，同一套规则引擎同时服务控制台对局、EasyX 图形界面、局域网联机、人机对战、智能提示、存档和 PGN 回放。

## 功能概览

- 标准 9x10 中国象棋棋盘
- 扩展 11x10 特殊棋盘模式
- 控制台文字对局
- EasyX 图形界面对局
- 局域网主机权威联机
- 房间发现、观战和黑方断线重连
- 悔棋、计时、认输、胜负判定
- 存档、排行榜、PGN 导出和导入回放
- AI 对战和 Hint 智能提示
- 规则与存储相关自测试

## 目录结构

```text
src/
  ai/          AI 搜索与提示
  app/         对局流程、会话状态、输入解析
  common/      公共类型和重复局面判断
  engine/      棋盘、棋子和规则引擎
  net/         WinSock 局域网通信
  storage/     存档、排行榜和 PGN
  tests/       自测试
  ui_console/  控制台入口
  ui_easyx/    EasyX 图形界面
```

根目录中的 `00_答辩文档导航.md`、`01_棋盘_棋子_规则引擎详解.md` 等文档用于答辩讲解和操作说明。

## 编译环境

- Windows 10 / Windows 11
- Visual Studio 2022
- MSVC v143
- EasyX 图形库

使用 Visual Studio 打开 `中国象棋.sln`，选择 `x64` + `Release` 或 `Debug` 后生成即可。

## 运行与测试

生成后运行：

```powershell
.\x64\Release\中国象棋.exe
```

运行自测试：

```powershell
.\x64\Release\中国象棋.exe --selftest
.\x64\Release\中国象棋.exe --smoke
```

## 联机说明

当前联机能力面向局域网直连，不包含公网大厅、账号系统或自动匹配。主机执红并作为权威状态源，客户端执黑；程序通过 TCP 同步走法、悔棋、认输和完整局面快照，并通过 UDP 广播实现房间发现。

## 版本库说明

仓库保留源码、Visual Studio 工程文件和项目文档。编译产物、`.vs` 缓存、运行存档、PGN、排行榜、压缩包和 Word 文档不纳入版本控制。
