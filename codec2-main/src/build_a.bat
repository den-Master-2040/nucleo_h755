@echo off
REM ===========================================================================
REM  build_a.bat  -  build libcodec2_datamodem.a (FreeDV DATAC3 modem, no speech)
REM  for STM32H755 CM4, using STM32CubeIDE's OWN bundled arm-none-eabi toolchain.
REM  No make, no CMake. Compiles codec2 strictly as C.
REM
REM  EDIT THE TWO PATHS BELOW, then run from this folder:  build_a.bat
REM ===========================================================================
setlocal enabledelayedexpansion

REM --- (1) folder that contains arm-none-eabi-gcc.exe inside CubeIDE ----------
REM  Find it once: build ANY project in CubeIDE and copy the gcc path printed in
REM  the Console, or browse:
REM  <CubeIDE>\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.14.3.rel1.win32_1.0.100.202602081740\tools\bin
set "TOOLS=C:\ST\STM32CubeIDE_1.15.0\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.gnu-tools-for-stm32.14.3.rel1.win32_1.0.100.202602081740\tools\bin"

REM --- (2) path to your codec2 checkout's src\ folder ------------------------
set "CODEC2_SRC=..\codec2\src"

REM ---------------------------------------------------------------------------
set "CC=%TOOLS%\arm-none-eabi-gcc.exe"
set "AR=%TOOLS%\arm-none-eabi-ar.exe"
set "GEN=gen"
set "OUT=build_m4"

REM these MUST match your CubeMX CM4 settings (cortex-m4 / fpv4-sp-d16 / hard)
set "ARCH=-mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard -D__FPU_PRESENT=1"
set "DEFS=-DFREEDV_MODE_EN_DEFAULT=0 -DFREEDV_MODE_DATAC3_EN=1 -DCODEC2_MODE_EN_DEFAULT=0 -DGIT_HASH=\"embedded\""
set "OPT=-std=gnu11 -Os -ffunction-sections -fdata-sections -fsingle-precision-constant"
set "INC=-I%CODEC2_SRC% -I%GEN% -I%GEN%\codec2"
set "CFLAGS=%ARCH% %OPT% %DEFS% %INC%"

if not exist "%CC%" ( echo [ERR] gcc not found: %CC%  & echo Fix TOOLS path. & exit /b 1 )
if not exist "%CODEC2_SRC%\freedv_api.c" ( echo [ERR] codec2 src not found: %CODEC2_SRC% & exit /b 1 )
if not exist "%OUT%" mkdir "%OUT%"

set "CORE=freedv_api ofdm ofdm_mode mpdecode_core phi0 interldpc gp_interleaver filter freedv_700 kiss_fft kiss_fftr modem_stats pack codec2_fifo varicode freedv_data_channel freedv_fsk  fsk  fmfsk  freedv_vhf_framing"
REM LDPC table for DATAC3 (datac0->H_128_256_5  datac1->H_4096_8192_3d):
set "LDPC=H_1024_2048_4f HRA_112_112 HRA_56_56"

set "OBJS="
for %%F in (%CORE% %LDPC%) do (
    echo   CC %%F.c
    "%CC%" %CFLAGS% -c "%CODEC2_SRC%\%%F.c" -o "%OUT%\%%F.o"
    if errorlevel 1 ( echo [ERR] compile failed: %%F.c & exit /b 1 )
    set "OBJS=!OBJS! "%OUT%\%%F.o""
)

echo   CC ldpc_codes_datac3.c  ^(trimmed registry^)
"%CC%" %CFLAGS% -c "ldpc_codes_datac3.c" -o "%OUT%\ldpc_codes.o"
if errorlevel 1 ( echo [ERR] compile failed: ldpc_codes_datac3.c & exit /b 1 )
set "OBJS=!OBJS! "%OUT%\ldpc_codes.o""

echo   AR libcodec2_datamodem.a
"%AR%" rcs "%OUT%\libcodec2_datamodem.a" !OBJS!
if errorlevel 1 ( echo [ERR] ar failed & exit /b 1 )

echo.
echo DONE -^> %OUT%\libcodec2_datamodem.a
endlocal
