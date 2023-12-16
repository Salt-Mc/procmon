@echo off
REM  --> Check for permissions
>nul 2>&1 "%SYSTEMROOT%\system32\cacls.exe" "%SYSTEMROOT%\system32\config\system"

REM --> If error flag set, we do not have admin.
if '%errorlevel%' NEQ '0' (
    echo Requesting administrative privileges...
    goto UACPrompt
) else ( goto gotAdmin )

:UACPrompt
    echo Set UAC = CreateObject^("Shell.Application"^) > "%temp%\getadmin.vbs"
    echo UAC.ShellExecute "%~s0", "", "", "runas", 1 >> "%temp%\getadmin.vbs"

    "%temp%\getadmin.vbs"
    exit /B

:gotAdmin
    title ContolProcMon
    if exist "%temp%\getadmin.vbs" ( del "%temp%\getadmin.vbs" )
    pushd "%CD%"
    CD /D "%~dp0"

    sc create procmon_t binPath=%~dp0procmon.sys type=kernel
    sc start procmon_t

    start /D %~dp0 cmd /k "@echo off & title ProcMonClient & ProcMonClient.exe & echo ProcMonClient has exited. You may close this window."
    %~dp0nircmdc win setsize ititle "ContolProcMon" 1 1 900 600
    %~dp0nircmdc cmdwait 700 win setsize ititle "ProcMonClient" 900 1 700 600

    set pid=0
    for /f "tokens=2 delims==; " %%a in ('wmic process call create "calc.exe"^,"%~dp0." ^| find "ProcessId"') do set pid=%%a
    taskkill /PID %pid% /f

    cls
    echo.
    echo The ProcMonClient.exe will run for 35 sec, kernel service will be deleted as well
    echo.
    echo Calculator (calc.exe) with process ID %pid% was launched and terminated,
    echo.
    echo Try looking for this PID in ProcMonClient.exe to verify process start and exit are captured correctly
    echo You can also run your own executable to verify that capturing works fine
    timeout /t 35
    taskkill /im ProcMonClient.exe /f

   sc stop procmon_t
   sc delete procmon_t

