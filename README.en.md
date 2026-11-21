[简体中文](README.md) | English

# Silica

Silica is an embedded declarative UI framework for LeviLamina Minecraft Bedrock Edition client mods. An interface is described in a small `.sil` document, and a C++ runtime builds, lays out, and draws the widget tree directly into the render context your mod provides.

```sil
Container {
    layout: vertical;
    width: 320;
    height: 120;
    padding: 12;
    background: #0F1215F2;
    border: 1 solid #353A40;

    Label {
        text: "Silica loaded";
        height: 18;
        color: #D9F2D9;
    }

    Button {
        text: "Click me";
        width: 96;
        height: 24;
        onClick: "hello_click";
    }
}
```

Silica is not a wrapper around the game's JSON-UI and not a web view: it lays out and draws the tree itself, reusing only the game's fonts, textures, and render passes.

## Features

- **Declarative interfaces** — nodes and properties; layout comes from container rules, no hand-written pixel coordinates.
- **Reactive bindings** — `@key` reads from the `DataStore`, and a change re-evaluates the bound property.
- **Event scripts** — `onClick: { @count = @count + 1; }` runs in the document; complex logic goes to named C++ callbacks.
- **Components** — parameterized subtrees with `Slot`, plus `Var` constants and `Style` blocks with `extends` / `class`.
- **Lists and decoration** — `ForEach` renders arrays, `Part` overlays decoration layers inside a widget box.
- **Animation** — `@keyframes`, `transition`, and smooth interpolation for sliders and lists.
- **Built-in widgets** — `Container`, `Panel`, `Label`, `Button`, `Image`, `ScrollView`, `Checkbox`, `Slider`, `TextBox`, `Dropdown`, `ForEach`, `Part`.
- **Diagnosable loading** — compile errors carry file, line, and column; a broken document falls back to an embedded interface instead of interrupting the game.

## Repository layout

```text
Silica/
├── include/silica/        public headers (Silica.h, Widget.h, UICompiler.h ...)
├── src/                   static library implementation
├── tests/                 host tests (parser, fallback loading, etc.)
├── build.bat              build the static library and run the tests
├── dist/Silica.lib        build output
└── SilicaTest/            reference mod: sample interface and host integration
```

## Requirements

| Item | Requirement |
| --- | --- |
| OS | Windows 10 / 11 x64 |
| Build environment | Visual Studio 2022 Build Tools + LLVM (clang-cl), MSVC ABI |
| Language standard | C++20 |
| Runtime library | Dynamic (`/MD`) |
| SDK | LeviLamina client SDK headers (`ll/api/...`, `mc/...`) and the xmake package cache used by the scripts |
| Game | Minecraft Bedrock client with a working LeviLamina installation (to run the sample mod) |

## Build

From the repository root:

```bat
build.bat
```

The script compiles the framework with clang-cl, archives `dist\Silica.lib`, runs the parser and file-fallback host tests, and checks the exported symbols. `BUILD PASS` means the build succeeded.

```bat
SilicaTest\build.bat
```

builds the framework first, then compiles, links, and packages the sample mod into `SilicaTest\dist\SilicaTest\`. Copy that directory into `<instance>/mods/SilicaTest/`, start the game, and press `P` in a world to open the panel.

`build.bat` contains absolute paths for the Visual Studio installation, the LeviLamina SDK, and the xmake package cache. On another machine, adjust `INC`, `PKG`, and the compiler paths at the top of the script.

The MSVC ABI and the runtime library settings must match between Silica and your mod; a mismatch crashes at load time or when widget pointers cross the boundary.

## Documentation

The bilingual documentation (Chinese by default, English under `/en/`) is maintained in the [Silica-docs](https://github.com/wuqinlanjian/Silica-docs) repository. It covers the DSL tutorial, the widget reference, C++ integration, and troubleshooting.

## License

MIT. See [LICENSE](LICENSE).
