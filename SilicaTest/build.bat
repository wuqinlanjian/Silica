@echo off
REM Builds SilicaTest.dll (native client mod) with clang-cl against a local
REM Visual Studio 2022 installation and the LeviLamina client SDK headers.
REM Silica is built first so the test always links the current framework.
setlocal enabledelayedexpansion
cd /d "%~dp0"

REM Optional machine-specific overrides, not tracked by git. Create
REM local.build.bat here or in the repository root and set:
REM   SILICA_VS_ROOT   Visual Studio 2022 root (contains VC\Auxiliary\Build\vcvars64.bat)
REM   SILICA_LLVM_BIN  folder containing clang-cl.exe (default: under SILICA_VS_ROOT)
if exist "%~dp0local.build.bat" call "%~dp0local.build.bat"
if exist "%~dp0..\local.build.bat" call "%~dp0..\local.build.bat"

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
set "DIST=dist\SilicaTest"
if not exist "%OUT%" mkdir "%OUT%"
if not exist "%DIST%" mkdir "%DIST%"

echo [0/4] build silica framework
pushd ..
call build.bat || (popd & goto :fail)
popd

set "PKG=C:\Users\zzh\AppData\Local\.xmake\packages"
set "INC=-I..\include -Isrc -ID:\LeviLamina\LeviLamina-main\src -ID:\LeviLamina\LeviLamina-main\src-client -ID:\LeviLamina\LlSdkBuild\include"
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

echo [1/4] compile
clang-cl %FLAGS% %DEFS% %INC% /c src\TestOverlay.cpp /Fo:%OUT%\TestOverlay.obj || goto :fail
clang-cl %FLAGS% %DEFS% %INC% /c src\MemoryOperators.cpp /Fo:%OUT%\MemoryOperators.obj || goto :fail
clang-cl %FLAGS% %DEFS% %INC% /c src\main.cpp /Fo:%OUT%\main.obj || goto :fail

echo [2/4] link
set "FMTLIB=%PKG%\f\fmt\11.2.0\ee1902dd7cff4195a287949f80dbce70\lib\fmt.lib"
set "SYSLIBS=kernel32.lib user32.lib shell32.lib ole32.lib advapi32.lib runtimeobject.lib Version.lib DbgHelp.lib"
clang-cl /nologo /LD /MD /O2 /Fe:"%DIST%\SilicaTest.dll" "%OUT%\TestOverlay.obj" "%OUT%\MemoryOperators.obj" "%OUT%\main.obj" ^
    /link /INCREMENTAL:NO /DEBUG:NONE /IGNORE:4217 ^
    "..\dist\Silica.lib" "D:\LeviLamina\LlSdkBuild\LeviLamina.lib" "%FMTLIB%" %SYSLIBS% || goto :fail

if exist "%DIST%\SilicaTest.lib" del /q "%DIST%\SilicaTest.lib"
if exist "%DIST%\SilicaTest.exp" del /q "%DIST%\SilicaTest.exp"

echo [3/4] package
copy /y manifest.json "%DIST%\manifest.json" >nul || goto :fail
xcopy /e /i /y /q ui "%DIST%\ui" >nul || goto :fail

echo [4/4] verify
dumpbin /nologo /exports "%DIST%\SilicaTest.dll" | findstr /i "ll_mod_load ll_mod_enable ll_mod_disable ll_memory_operator_overrided" >nul || goto :fail
if not exist "%DIST%\manifest.json" goto :fail
if not exist "%DIST%\ui\panel.sil" goto :fail

echo BUILD PASS
echo Package verified: %DIST%\SilicaTest.dll
exit /b 0

:fail
echo [error] build failed
exit /b 1
