# postgraduate

Code during graduate studies

## 景区信息显示系统

这是对本科毕业设计《基于 ARM 的景区信息显示系统设计》的重构。系统由管理端和展示端组成，当前版本已在 Windows 上联调，使用 C++17、Qt 5.15.2 Widgets 和 qmake。

- **管理端**支持选择终端、批量发送文件、远程切换展示页面和查看接收回执。同一台终端再次发送时，跳过已发出且大小和修改时间均未变化的文件。新增、失败和修改过的文件仍可发送。
- **展示端**支持视频循环、图片轮播、通知、远程切页和断线重连。收到的文件存到程序目录下的 `RecvFile`，保存成功后逐文件回执。自定义消息和 UTF-8 `.txt` 文件都可更新通知。
- **天气**由展示端通过高德 Web 服务 API 获取。默认查询成都实况，支持演示模式。

## 目录

| 路径 | 内容 |
| --- | --- |
| [gptMake/Server](gptMake/Server) | Windows 管理端、通信协议和服务端测试 |
| [gptMake/Client](gptMake/Client) | 展示端、天气模块、Windows 视频播放及联调测试 |
| [TCPClient](TCPClient) | 旧客户端源码及界面资源，用于兼容性测试 |

## 在 Windows 构建

使用 **Qt 5.15.2 + MinGW 8.1 32 位**套件，安装 Qt Multimedia 和 Qt Test 模块。可以用 Qt Creator 打开 [管理端工程](gptMake/Server/TCPServer.pro) 和 [展示端工程](gptMake/Client/ScenicClient.pro)，选择同一个套件构建。

也可以在仓库根目录打开已配置 `qmake` 和 `mingw32-make` 的 PowerShell，用独立构建目录编译两端。

```powershell
$projectRoot = (Get-Location).Path

function Build-QtProject([string]$Project, [string]$Output) {
    $buildDir = Join-Path $projectRoot $Output
    New-Item -ItemType Directory -Path $buildDir -Force | Out-Null
    Push-Location $buildDir
    try {
        qmake (Join-Path $projectRoot $Project) 'CONFIG+=release'
        if ($LASTEXITCODE -ne 0) { throw "qmake failed for $Project" }
        mingw32-make -j4
        if ($LASTEXITCODE -ne 0) { throw "Build failed for $Project" }
    } finally {
        Pop-Location
    }
}

Build-QtProject 'gptMake/Server/TCPServer.pro' 'build/server'
Build-QtProject 'gptMake/Client/ScenicClient.pro' 'build/client'
```

生成的程序分别位于 `build/server/release/TCPServer.exe` 和 `build/client/release/ScenicClient.exe`。可以在 Qt Creator 或已加载对应 Qt 运行库的终端中启动。

## 本地联调

1. 打开管理端，确认端口 `6666` 显示「监听中」，然后打开展示端。展示端默认连接 `127.0.0.1:6666`。
2. 在管理端选中显示「就绪」的终端，添加视频、图片或 UTF-8 文本文件，点击「发送素材」。在日志里可以看到文件接收回执。
3. 用管理端的四个按钮切换视频、图片、通知和循环模式。可以用「自定义消息」更新展示端通知。
4. 展示端按 **F12** 打开调试面板，修改服务器地址和端口、手动切页或打开素材目录。按 **F11** 切换全屏。

两端同机联调时启动一个展示端实例即可。现有协议在同一端口建立指令和文件两条 TCP 连接，管理端按来源 IP 配对。

### 天气设置

在展示端调试面板填写高德 **Web 服务 Key** 和六位城市编码，然后点击「应用并刷新」。也可在启动前设置环境变量 `AMAP_WEATHER_KEY`。默认城市编码是成都的 `510100`。手头没有 Key 时，勾选「演示天气」即可预览天气栏。Windows 上的 HTTPS 请求由 WinHTTP 在后台执行，无需数据库。 [高德天气 API](https://lbs.amap.com/api/webservice/guide/api/weatherinfo)

## 测试

使用前面的 `Build-QtProject` 函数编译并运行以下测试。

```powershell
Build-QtProject 'gptMake/Server/tests/server-tests.pro' 'build/server-tests'
Build-QtProject 'gptMake/Server/tests/legacy-client-tests.pro' 'build/legacy-client-tests'
Build-QtProject 'gptMake/Client/tests/client-tests.pro' 'build/client-tests'

& .\build\server-tests\release\server-tests.exe
& .\build\legacy-client-tests\release\legacy-client-tests.exe
& .\build\client-tests\release\client-tests.exe
```

测试覆盖按终端增量发送、失败重试、旧客户端双向收发、展示端切页与通知、中文文件名、逐文件回执、断线重连和天气响应解析。设置 `SCENIC_TEST_VIDEO` 为本地测试视频的绝对路径可一并验证实际播放，设置 `SCENIC_PREVIEW_DIR` 可输出预览截图。

目前验证平台为 Windows。高德天气的正常和异常响应已通过测试数据验证，实时天气联调需要有效 Key，ARM 上板验证尚未完成。

## 许可证

参见仓库的 [LICENSE](LICENSE)。
