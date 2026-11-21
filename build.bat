@echo off
REM Builds Silica.lib (static) with clang-cl (MSVC ABI) against Visual Studio
REM 2022 C++ build tools and the LeviLamina client SDK headers.
setlocal enabledelayedexpansion
cd /d "%~dp0"

REM Optional machine-specific overrides, not tracked by git. Create
REM local.build.bat next to this script and set:
REM   SILICA_VS_ROOT   Visual Studio 2022 root (contains VC\Auxiliary\Build\vcvars64.bat)
REM   SILICA_LLVM_BIN  folder containing clang-cl.exe (default: under SILICA_VS_ROOT)
if exist "%~dp0local.build.bat" call "%~dp0local.build.bat"

set "PF86=%ProgramFiles(x86)%"
set "VSWHERE=%PF86%\Microsoft Visual Studio\Installer\vswhere.exe"

if not defined SILICA_VS_ROOT if defined VCINSTALLDIR set "SILICA_VS_ROOT=%VCINSTALLDIR%.."
if not defined SILICA_VS_ROOT if defined VSINSTALLDIR set "SILICA_VS_ROOT=%VSINSTALLDIR%"
if not defined SILICA_VS_ROOT if exist "%VSWHERE%" for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "SILICA_VS_ROOT=%%i"
if not defined SILICA_VS_ROOT if exist "%PF86%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" set "SILICA_VS_ROOT=%PF86%\Microsoft Visual Studio\2022\BuildTools"
if not defined SILICA_VS_ROOT if exist "%PF86%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" set "SILICA_VS_ROOT=%PF86%\Microsoft Visual Studio\2022\Community"
if not defined SILICA_VS_ROOT if exist "%PF86%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" set "SILICA_VS_ROOT=%PF86%\Microsoft Visual Studio\2022\Professional"
if not defined SILICA_VS_ROOT if exist "%PF86%\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" set "SILICA_VS_ROOT=%PF86%\Microsoft Visual Studio\2022\Enterprise"
if not defined SILICA_VS_ROOT if exist "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" set "SILICA_VS_ROOT=%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools"
if not defined SILICA_VS_ROOT if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" set "SILICA_VS_ROOT=%ProgramFiles%\Microsoft Visual Studio\2022\Community"

if not defined SILICA_VS_ROOT (
    echo [error] Visual Studio 2022 C++ build tools not found
    echo [error] Set SILICA_VS_ROOT or create local.build.bat with the installation root
    exit /b 1
)
if not exist "%SILICA_VS_ROOT%\VC\Auxiliary\Build\vcvars64.bat" (
    echo [error] vcvars64.bat not found under "%SILICA_VS_ROOT%"
    exit /b 1
)
call "%SILICA_VS_ROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul

if not defined SILICA_LLVM_BIN if exist "%SILICA_VS_ROOT%\VC\Tools\Llvm\x64\bin\clang-cl.exe" set "SILICA_LLVM_BIN=%SILICA_VS_ROOT%\VC\Tools\Llvm\x64\bin"
if not defined SILICA_LLVM_BIN for /f "delims=" %%i in ('where clang-cl 2^>nul') do if not defined SILICA_LLVM_BIN set "SILICA_LLVM_BIN=%%~dpi"
if not defined SILICA_LLVM_BIN (
    echo [error] clang-cl.exe not found
    echo [error] Set SILICA_LLVM_BIN or create local.build.bat with the LLVM bin folder
    exit /b 1
)
if not exist "%SILICA_LLVM_BIN%\clang-cl.exe" (
    echo [error] clang-cl.exe not found in "%SILICA_LLVM_BIN%"
    exit /b 1
)
set "PATH=%SILICA_LLVM_BIN%;%PATH%"

set "OUT=build"
set "DIST=dist"
if not exist "%OUT%" mkdir "%OUT%"
if not exist "%DIST%" mkdir "%DIST%"

set "PKG=C:\Users\zzh\AppData\Local\.xmake\packages"
set "INC=-Iinclude -ID:\LeviLamina\LeviLamina-main\src -ID:\LeviLamina\LeviLamina-main\src-client -ID:\LeviLamina\LlSdkBuild\include"
set "INC=%INC% -I%PKG%\e\entt\v4.0.0\0d58f676872d486790799bfc6f016197\include"
set "INC=%INC% -I%PKG%\f\fmt\11.2.0\ee1902dd7cff4195a287949f80dbce70\include"
set "INC=%INC% -I%PKG%\g\gsl\v4.2.0\cdcbe579518f475dbbe3954d5b394a71\include"
set "INC=%INC% -I%PKG%\g\glm\1.0.1\18615f0387cd4e2ab2e9ea9965afc784\include"
set "INC=%INC% -I%PKG%\m\magic_enum\v0.9.7\03e2990032db4ea08102d17ff8a57da7\include"
set "INC=%INC% -I%PKG%\n\nlohmann_json\v3.12.0\ab8414047c424750bdc9d7a0ff7afdde\include"
set "INC=%INC% -I%PKG%\r\rapidjson\2025.02.05\5aec2755925a45d9974d95d2cb7038b1\include"
set "INC=%INC% -I%PKG%\t\type_safe\v0.2.4\4b33a9a5c8154bd58b7d4007150f50b7\include"
set "INC=%INC% -I%PKG%\e\expected-lite\v0.8.0\36ca3aab48a04473a0b2d34e01875b34\include"
set "INC=%INC% -I%PKG%\p\parallel-hashmap\v2.0.0\01789a062bfd4c27ba236d537d9ff7c9\include"
set "INC=%INC% -I%PKG%\c\concurrentqueue\v1.0.4\0cd923cdfb6a4256954a994422b17de9\include -I%PKG%\c\concurrentqueue\v1.0.4\0cd923cdfb6a4256954a994422b17de9\include\concurrentqueue\moodycamel"
set "INC=%INC% -I%PKG%\s\stb\2025.03.14\5add428b704648039c039d4d74bed865\include -I%PKG%\s\stb\2025.03.14\5add428b704648039c039d4d74bed865\include\stb"
set "INC=%INC% -I%PKG%\c\ctre\3.8.1\354f59ffe01146af8f95691d1df61dab\include"
set "INC=%INC% -I%PKG%\d\debug_assert\v1.3.4\e3515434506a445792c1c83600bcf899\include"
set "INC=%INC% -I%PKG%\l\leveldb\1.23\2532d000dc9d47cbb618c80ba0e6d806\include"
set "INC=%INC% -I%PKG%\p\pfr\2.1.1\45e5102591a649fda279e67eaf92482a\include"
set "INC=%INC% -I%PKG%\p\pcg_cpp\v1.0.0\6c550fbfc1bb49fbbed872829f6090b0\include"

set "DEFS=/DNOMINMAX /DUNICODE /D_HAS_CXX23=1 /DENTT_PACKED_PAGE=128 /DENTT_SPARSE_PAGE=4096 /DENTT_NO_MIXIN /DFMT_USE_FULL_CACHE_DRAGONBOX=1 /DWIN32_LEAN_AND_MEAN /D_AMD64_ /DLL_PLAT_C"
set "FLAGS=-nologo -O2 -MD -std:c++20 -EHa -utf-8 /wd4265 /wd4289 /wd4296 /wd5263 /wd4738 /wd5204 /wd4996 -Wno-invalid-offsetof -Wno-c++2b-extensions -Wno-microsoft-include"

echo [1/6] compile framework
clang-cl %FLAGS% %DEFS% %INC% /c src\Style.cpp /Fo:%OUT%\Style.obj || goto :fail
clang-cl %FLAGS% %DEFS% %INC% /c src\Animation.cpp /Fo:%OUT%\Animation.obj || goto :fail
clang-cl %FLAGS% %DEFS% %INC% /c src\Property.cpp /Fo:%OUT%\Property.obj || goto :fail
clang-cl %FLAGS% %DEFS% %INC% /c src\DataStore.cpp /Fo:%OUT%\DataStore.obj || goto :fail
clang-cl %FLAGS% %DEFS% %INC% /c src\Expression.cpp /Fo:%OUT%\Expression.obj || goto :fail
clang-cl %FLAGS% %DEFS% %INC% /c src\Script.cpp /Fo:%OUT%\Script.obj || goto :fail
clang-cl %FLAGS% %DEFS% %INC% /c src\Renderer.cpp /Fo:%OUT%\Renderer.obj || goto :fail
clang-cl %FLAGS% %DEFS% %INC% /c src\Texture.cpp /Fo:%OUT%\Texture.obj || goto :fail
clang-cl %FLAGS% %DEFS% %INC% /c src\ImagePack.cpp /Fo:%OUT%\ImagePack.obj || goto :fail
clang-cl %FLAGS% %DEFS% %INC% /c src\Widget.cpp /Fo:%OUT%\Widget.obj || goto :fail
clang-cl %FLAGS% %DEFS% %INC% /c src\Part.cpp /Fo:%OUT%\Part.obj || goto :fail
clang-cl %FLAGS% %DEFS% %INC% /c src\Container.cpp /Fo:%OUT%\Container.obj || goto :fail
clang-cl %FLAGS% %DEFS% %INC% /c src\ForEach.cpp /Fo:%OUT%\ForEach.obj || goto :fail
clang-cl %FLAGS% %DEFS% %INC% /c src\Panel.cpp /Fo:%OUT%\Panel.obj || goto :fail
clang-cl %FLAGS% %DEFS% %INC% /c src\Label.cpp /Fo:%OUT%\Label.obj || goto :fail
clang-cl %FLAGS% %DEFS% %INC% /c src\Button.cpp /Fo:%OUT%\Button.obj || goto :fail
clang-cl %FLAGS% %DEFS% %INC% /c src\Image.cpp /Fo:%OUT%\Image.obj || goto :fail
clang-cl %FLAGS% %DEFS% %INC% /c src\ScrollView.cpp /Fo:%OUT%\ScrollView.obj || goto :fail
clang-cl %FLAGS% %DEFS% %INC% /c src\Checkbox.cpp /Fo:%OUT%\Checkbox.obj || goto :fail
clang-cl %FLAGS% %DEFS% %INC% /c src\Slider.cpp /Fo:%OUT%\Slider.obj || goto :fail
clang-cl %FLAGS% %DEFS% %INC% /c src\TextBox.cpp /Fo:%OUT%\TextBox.obj || goto :fail
clang-cl %FLAGS% %DEFS% %INC% /c src\Dropdown.cpp /Fo:%OUT%\Dropdown.obj || goto :fail
clang-cl %FLAGS% %DEFS% %INC% /c src\Handlers.cpp /Fo:%OUT%\Handlers.obj || goto :fail
clang-cl %FLAGS% %DEFS% %INC% /c src\SilParser.cpp /Fo:%OUT%\SilParser.obj || goto :fail
clang-cl %FLAGS% %DEFS% %INC% /c src\WidgetFactory.cpp /Fo:%OUT%\WidgetFactory.obj || goto :fail
clang-cl %FLAGS% %DEFS% %INC% /c src\UICompiler.cpp /Fo:%OUT%\UICompiler.obj || goto :fail
clang-cl %FLAGS% %DEFS% %INC% /c src\Runtime.cpp /Fo:%OUT%\Runtime.obj || goto :fail
clang-cl %FLAGS% %DEFS% %INC% /c src\Silica.cpp /Fo:%OUT%\Silica.obj || goto :fail
clang-cl %FLAGS% %DEFS% %INC% /c src\Tree.cpp /Fo:%OUT%\Tree.obj || goto :fail
clang-cl %FLAGS% %DEFS% %INC% /c src\Input.cpp /Fo:%OUT%\Input.obj || goto :fail

echo [2/6] compile api smoke test
clang-cl %FLAGS% %DEFS% %INC% /c tests\Smoke.cpp /Fo:%OUT%\Smoke.obj || goto :fail

echo [3/6] archive
if exist "%DIST%\Silica.lib" del /q "%DIST%\Silica.lib"
lib /nologo /OUT:"%DIST%\Silica.lib" "%OUT%\Style.obj" "%OUT%\Animation.obj" "%OUT%\Property.obj" ^
    "%OUT%\DataStore.obj" "%OUT%\Expression.obj" "%OUT%\Script.obj" "%OUT%\Renderer.obj" ^
    "%OUT%\Texture.obj" "%OUT%\ImagePack.obj" "%OUT%\Widget.obj" "%OUT%\Part.obj" "%OUT%\Container.obj" "%OUT%\ForEach.obj" "%OUT%\Panel.obj" "%OUT%\Label.obj" ^
    "%OUT%\Button.obj" "%OUT%\Image.obj" "%OUT%\ScrollView.obj" "%OUT%\Checkbox.obj" ^
    "%OUT%\Slider.obj" "%OUT%\TextBox.obj" "%OUT%\Dropdown.obj" ^
    "%OUT%\Handlers.obj" "%OUT%\SilParser.obj" "%OUT%\WidgetFactory.obj" "%OUT%\UICompiler.obj" ^
    "%OUT%\Runtime.obj" "%OUT%\Silica.obj" "%OUT%\Tree.obj" "%OUT%\Input.obj" || goto :fail

echo [4/6] parser test
clang-cl %FLAGS% %DEFS% %INC% /c tests\ParserTest.cpp /Fo:%OUT%\ParserTest.obj || goto :fail
clang-cl /nologo /O2 /MD /Fe:"%OUT%\sil_parser_test.exe" "%OUT%\ParserTest.obj" "%OUT%\SilParser.obj" "%OUT%\Property.obj" || goto :fail
"%OUT%\sil_parser_test.exe" || goto :fail

echo [5/6] file fallback test
clang-cl %FLAGS% %DEFS% %INC% /c tests\FileFallbackTest.cpp /Fo:%OUT%\FileFallbackTest.obj || goto :fail
clang-cl /nologo /O2 /MD /Fe:"%OUT%\sil_file_test.exe" "%OUT%\FileFallbackTest.obj" ^
    "%OUT%\UICompiler.obj" "%OUT%\WidgetFactory.obj" "%OUT%\Widget.obj" "%OUT%\Part.obj" "%OUT%\Container.obj" "%OUT%\ForEach.obj" ^
    "%OUT%\Panel.obj" "%OUT%\Label.obj" "%OUT%\Button.obj" "%OUT%\Image.obj" "%OUT%\ScrollView.obj" ^
    "%OUT%\Checkbox.obj" "%OUT%\Slider.obj" "%OUT%\TextBox.obj" "%OUT%\Dropdown.obj" ^
    "%OUT%\Handlers.obj" "%OUT%\SilParser.obj" ^
    "%OUT%\Property.obj" "%OUT%\Style.obj" "%OUT%\Animation.obj" "%OUT%\Runtime.obj" "%OUT%\Silica.obj" ^
    "%OUT%\DataStore.obj" "%OUT%\Expression.obj" "%OUT%\Script.obj" ^
    "%OUT%\Tree.obj" "%OUT%\Input.obj" || goto :fail
"%OUT%\sil_file_test.exe" || goto :fail

echo [6/6] verify
dumpbin /nologo /symbols "%DIST%\Silica.lib" | findstr /i "SilicaWidget@silica SilicaPart@silica SilicaLabel@silica SilicaButton@silica SilicaContainer@silica SilicaForEach@silica SilicaImage@silica SilicaScrollView@silica SilicaCheckbox@silica SilicaSlider@silica SilicaTextBox@silica SilicaDropdown@silica SilParser@silica UICompiler@silica WidgetFactory@silica SilicaHandlers@silica SetRootPath@Silica@silica ReloadUI@Silica@silica ResolvePath@runtime@silica FocusRing@silica PointerRouter@silica FocusManager@silica FindByName@silica HitTest@silica DrawTexture@silica PushClip@silica PopClip@silica Resolve@imagepack@silica DataStore@silica Expression@silica Script@silica" >nul || goto :fail

echo BUILD PASS
echo Library: %DIST%\Silica.lib
exit /b 0

:fail
echo [error] build failed
exit /b 1
