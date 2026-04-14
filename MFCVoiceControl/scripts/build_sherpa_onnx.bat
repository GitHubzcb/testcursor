@echo off
REM ================================================================
REM  sherpa-onnx Windows x86/x64 编译脚本
REM  用于生成与 MFC 兼容的动态链接库
REM ================================================================

echo ========================================
echo  sherpa-onnx 编译脚本
echo ========================================
echo.

REM 检查 Visual Studio 环境
where cl >nul 2>nul
if errorlevel 1 (
    echo [错误] 未检测到 Visual Studio 编译器。
    echo 请从 "Visual Studio Developer Command Prompt" 运行此脚本。
    echo 或者先运行: "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat" x86
    pause
    exit /b 1
)

REM 配置参数
set ARCH=Win32
set BUILD_TYPE=Release
set INSTALL_DIR=%~dp0..\sherpa-onnx-install

echo 编译架构: %ARCH%
echo 编译类型: %BUILD_TYPE%
echo 安装目录: %INSTALL_DIR%
echo.

REM ================================================================
REM  步骤 1: 克隆 sherpa-onnx
REM ================================================================
set SHERPA_SRC=%~dp0..\sherpa-onnx-src

if exist "%SHERPA_SRC%" (
    echo sherpa-onnx 源码已存在，跳过克隆。
) else (
    echo [1/3] 正在克隆 sherpa-onnx 源码...
    git clone --depth 1 https://github.com/k2-fsa/sherpa-onnx.git "%SHERPA_SRC%"
    if errorlevel 1 (
        echo 克隆失败！请检查网络连接。
        pause
        exit /b 1
    )
)

REM ================================================================
REM  步骤 2: 修改运行时为 /MD（匹配MFC默认配置）
REM ================================================================
echo.
echo [2/3] 配置 CMake 项目...

set BUILD_DIR=%SHERPA_SRC%\build-%ARCH%

if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
cd /d "%BUILD_DIR%"

REM 注意: 如果遇到运行时不匹配错误 (LNK2038)，
REM 需要手动修改 sherpa-onnx/CMakeLists.txt:
REM   将 /MT 替换为 /MD, 将 /MTd 替换为 /MDd
REM
REM 参考: https://www.amd.com/en/developer/resources/technical-articles/2026/
REM        a-practical-approach-to-using-sherpa-onnx-production-ready-on-wi.html

cmake -G "Visual Studio 17 2022" -A %ARCH% ^
    -DCMAKE_BUILD_TYPE=%BUILD_TYPE% ^
    -DBUILD_SHARED_LIBS=ON ^
    -DCMAKE_INSTALL_PREFIX="%INSTALL_DIR%" ^
    -DSHERPA_ONNX_ENABLE_C_API=ON ^
    -DSHERPA_ONNX_ENABLE_BINARY=OFF ^
    "%SHERPA_SRC%"

if errorlevel 1 (
    echo CMake 配置失败！
    pause
    exit /b 1
)

REM ================================================================
REM  步骤 3: 编译和安装
REM ================================================================
echo.
echo [3/3] 正在编译 (可能需要几分钟)...

cmake --build . --config %BUILD_TYPE% --target install -j %NUMBER_OF_PROCESSORS%

if errorlevel 1 (
    echo 编译失败！请查看上方的错误信息。
    pause
    exit /b 1
)

echo.
echo ========================================
echo  编译完成！
echo ========================================
echo.
echo 输出文件:
echo   头文件:  %INSTALL_DIR%\include\
echo   库文件:  %INSTALL_DIR%\lib\
echo   DLL文件: %INSTALL_DIR%\bin\
echo.
echo MFC 项目配置:
echo   附加包含目录: %INSTALL_DIR%\include
echo   附加库目录:   %INSTALL_DIR%\lib
echo   附加依赖项:   sherpa-onnx-c-api.lib; onnxruntime.lib
echo   后期生成事件:  xcopy /Y "%INSTALL_DIR%\bin\*.dll" "$(OutDir)"
echo.
pause
