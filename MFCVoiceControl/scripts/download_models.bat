@echo off
REM ================================================================
REM  MFC 语音控制系统 - 模型下载脚本
REM  运行此脚本下载 ASR 和 TTS 模型文件
REM ================================================================

echo ========================================
echo  MFC 语音控制系统 - 模型下载
echo ========================================
echo.

set MODELS_DIR=%~dp0..\models
if not exist "%MODELS_DIR%" mkdir "%MODELS_DIR%"

cd /d "%MODELS_DIR%"

REM ================================================================
REM  1. 下载 ASR 模型: SenseVoice-Small (sherpa-onnx 格式)
REM ================================================================
echo [1/2] 正在下载 ASR 模型 (SenseVoice-Small)...
echo.

set ASR_MODEL=sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17
set ASR_URL=https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/%ASR_MODEL%.tar.bz2

if exist "%ASR_MODEL%" (
    echo ASR模型已存在，跳过下载。
) else (
    echo 下载地址: %ASR_URL%
    echo 文件大小: 约 200MB
    echo.

    REM 使用 curl 下载（Windows 10+ 内置）
    curl -L -o "%ASR_MODEL%.tar.bz2" "%ASR_URL%"
    if errorlevel 1 (
        echo 下载失败！请手动下载以下文件：
        echo %ASR_URL%
        echo 并解压到 %MODELS_DIR% 目录
        goto :TTS
    )

    REM 解压（需要 7-Zip 或 tar）
    echo 正在解压...
    tar -xjf "%ASR_MODEL%.tar.bz2" 2>nul
    if errorlevel 1 (
        echo tar 解压失败，尝试使用 7z...
        where 7z >nul 2>nul
        if errorlevel 1 (
            echo 请安装 7-Zip 或手动解压 %ASR_MODEL%.tar.bz2
        ) else (
            7z x "%ASR_MODEL%.tar.bz2" -so | 7z x -si -ttar
        )
    )
    del /q "%ASR_MODEL%.tar.bz2" 2>nul
    echo ASR模型下载完成!
)

:TTS
echo.

REM ================================================================
REM  2. 下载 TTS 模型: VITS 中文模型 (sherpa-onnx 格式)
REM ================================================================
echo [2/2] 正在下载 TTS 模型 (VITS 中文)...
echo.

set TTS_MODEL=vits-zh-hf-fanchen-C
set TTS_URL=https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/%TTS_MODEL%.tar.bz2

if exist "%TTS_MODEL%" (
    echo TTS模型已存在，跳过下载。
) else (
    echo 下载地址: %TTS_URL%
    echo 文件大小: 约 100MB
    echo.

    curl -L -o "%TTS_MODEL%.tar.bz2" "%TTS_URL%"
    if errorlevel 1 (
        echo 下载失败！请手动下载以下文件：
        echo %TTS_URL%
        echo 并解压到 %MODELS_DIR% 目录
        goto :DONE
    )

    echo 正在解压...
    tar -xjf "%TTS_MODEL%.tar.bz2" 2>nul
    if errorlevel 1 (
        where 7z >nul 2>nul
        if errorlevel 1 (
            echo 请安装 7-Zip 或手动解压 %TTS_MODEL%.tar.bz2
        ) else (
            7z x "%TTS_MODEL%.tar.bz2" -so | 7z x -si -ttar
        )
    )
    del /q "%TTS_MODEL%.tar.bz2" 2>nul
    echo TTS模型下载完成!
)

:DONE
echo.
echo ========================================
echo  模型下载完毕！
echo  模型目录: %MODELS_DIR%
echo ========================================
echo.

REM 检查模型文件
echo 检查模型文件:
if exist "%ASR_MODEL%\model.int8.onnx" (
    echo   [OK] ASR: %ASR_MODEL%\model.int8.onnx
) else if exist "%ASR_MODEL%\model.onnx" (
    echo   [OK] ASR: %ASR_MODEL%\model.onnx
) else (
    echo   [!!] ASR模型文件未找到
)

if exist "%TTS_MODEL%\%TTS_MODEL%.onnx" (
    echo   [OK] TTS: %TTS_MODEL%\%TTS_MODEL%.onnx
) else (
    echo   [!!] TTS模型文件未找到
)

echo.
pause
