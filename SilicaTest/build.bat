@echo off
REM Builds SilicaTest.dll (native client mod) with clang-cl against the VS 2022
REM Build Tools on D: and the LeviLamina client SDK headers. Silica is built
REM first so the test always links the current framework.
setlocal enabledelayedexpansion
cd /d "%~dp0"

if not exist "D:\DevTools\VSBuildTools2022\VC\Auxiliary\Build\vcvars64.bat" (
    echo [error] MSVC environment not found at D:\DevTools\VSBuildTools2022
    exit /b 1
)
call "D:\DevTools\VSBuildTools2022\VC\Auxiliary\Build\vcvars64.bat" >nul
set "PATH=D:\DevTools\VSBuildTools2022\VC\Tools\Llvm\x64\bin;%PATH%"

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
