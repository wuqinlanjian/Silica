简体中文 | [English](README.en.md)

# Silica

Silica 是面向 LeviLamina Minecraft 基岩版客户端模组的嵌入式声明式 UI 框架。界面写在 `.sil` 文档里，由 C++ 运行时构建控件树、完成布局，并直接绘制到模组提供的渲染上下文。

```sil
Container {
    layout: vertical;
    width: 320;
    height: 120;
    padding: 12;
    background: #0F1215F2;
    border: 1 solid #353A40;

    Label {
        text: "Silica 已加载";
        height: 18;
        color: #D9F2D9;
    }

    Button {
        text: "点我";
        width: 96;
        height: 24;
        onClick: "hello_click";
    }
}
```

Silica 不是 JSON-UI 的封装，也不是网页视图：控件树由它自己布局和绘制，只复用游戏的字体、纹理与渲染通道。

## 特性

- **声明式界面**：节点加属性描述界面，布局由容器规则计算，不手写像素坐标。
- **响应式绑定**：`@key` 读取 `DataStore`，数据变化后重新求值并刷新控件。
- **事件脚本**：`onClick: { @count = @count + 1; }` 直接在文档里修改变量；复杂逻辑交给 C++ 命名回调。
- **组件复用**：`Component` 定义带参数的子树，`Slot` 接收调用方传入的子节点。
- **样式系统**：`Var` 常量与 `Style`（支持 `extends`、`class`），行内属性优先。
- **列表与装饰**：`ForEach` 由数组生成行，`Part` 在控件盒内叠加装饰层。
- **动画**：`@keyframes`、`transition`，滑块与列表带平滑插值。
- **内置控件**：`Container`、`Panel`、`Label`、`Button`、`Image`、`ScrollView`、`Checkbox`、`Slider`、`TextBox`、`Dropdown`、`ForEach`、`Part`。
- **可诊断加载**：编译错误带文件、行号与列号；文档损坏时回退到内置界面，不打断游戏。

## 仓库结构

```text
Silica/
├── include/silica/        公开头文件（Silica.h、Widget.h、UICompiler.h …）
├── src/                   静态库实现
├── tests/                 宿主测试（解析器、回退加载等）
├── build.bat              构建静态库并运行测试
├── dist/Silica.lib        构建产物
└── SilicaTest/            参考模组：示例界面与宿主接入
```

## 环境要求

| 项目 | 要求 |
| --- | --- |
| 操作系统 | Windows 10 / 11 x64 |
| 编译环境 | Visual Studio 2022 Build Tools + LLVM（clang-cl），MSVC ABI |
| 语言标准 | C++20 |
| 运行库 | 动态运行库（`/MD`） |
| SDK | LeviLamina 客户端 SDK 头文件（`ll/api/...`、`mc/...`），以及脚本使用的 xmake 包缓存 |
| 游戏 | Minecraft 基岩版客户端与可用的 LeviLamina（运行示例模组时需要） |

## 构建

在仓库根目录执行：

```bat
build.bat
```

脚本用 clang-cl 编译框架、归档 `dist\Silica.lib`、运行解析器与文件回退测试，并检查导出符号。看到 `BUILD PASS` 即构建成功。

```bat
SilicaTest\build.bat
```

先构建框架，再编译、链接并打包示例模组到 `SilicaTest\dist\SilicaTest\`。把该目录复制到 `<实例>/mods/SilicaTest/`，进入世界后按 `P` 打开示例面板。

`build.bat` 中写有 Visual Studio、LeviLamina SDK 与 xmake 包缓存的绝对路径；换机器时调整脚本开头的 `INC`、`PKG` 与编译器路径。

Silica 与模组必须使用相同的 MSVC ABI 和运行库设置，否则会在加载或跨边界传递控件指针时崩溃。

## 文档

中英文文档（默认中文，英文位于 `/en/`）维护在 [Silica-docs](https://github.com/wuqinlanjian/Silica-docs) 仓库，覆盖 DSL 教程、控件参考、C++ 集成与故障排查。

## 许可证

MIT，见 [LICENSE](LICENSE)。
