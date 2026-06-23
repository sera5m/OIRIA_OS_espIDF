@echo off
setlocal enabledelayedexpansion

echo ========================================
echo  Counting Lines of Code
echo ========================================
echo.

set TOTAL=0
set C_FILES=0
set H_FILES=0

echo Scanning .c and .h files...
echo.

for /R %%f in (*.c *.h *.cpp *.hpp *.ino) do (
    if exist "%%f" (
        for /F %%i in ('type "%%f" ^| find /C /V ""') do (
            set /a TOTAL+=%%i
            set /a C_FILES+=1
            echo %%~nxf: %%i lines
        )
    )
)

echo.
echo ========================================
echo  Summary
echo ========================================
echo Total files: %C_FILES%
echo Total LOC: %TOTAL%
echo.
pause