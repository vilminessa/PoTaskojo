@echo off
chcp 65001 >nul
setlocal EnableExtensions
cd /d "%~dp0"

rem ============================================================
rem  PoTaskojo - консоль управления проектом
rem  debug.bat            - меню
rem  debug.bat 5          - сразу выполнить пункт, для скриптов
rem  debug.bat 8 путь     - аудит сходства по указанному архиву строк
rem
rem  В текстах меню нет спецсимволов cmd, а метки только латиницей -
rem  так надёжнее при кодировке UTF-8.
rem ============================================================

set "scripted="
set "sel="
if not "%~1"=="" (set "scripted=1" & set "sel=%~1")

set "branch=?"
for /f "delims=" %%b in ('git rev-parse --abbrev-ref HEAD 2^>nul') do set "branch=%%b"
set "dirty=0"
for /f %%c in ('git status --porcelain 2^>nul ^| find /c /v ""') do set "dirty=%%c"

if defined scripted goto dispatch

:menu
cls
echo.
echo  ===============================================================
echo    PoTaskojo - консоль управления проектом
echo  ===============================================================
echo    ветка: %branch%    не закоммичено файлов: %dirty%
echo  ---------------------------------------------------------------
echo    [1] Запустить приложение
echo    [2] Запустить: диалог настроек
echo    [3] Запустить: редактор правила
echo    [4] Запустить: проверка краш-обработчика
echo    [5] Собрать Release со строгими предупреждениями
echo    [6] Собрать и прогнать тесты
echo    [7] Только тесты
echo    [8] Аудит сходства с чужими строками
echo    [9] Последние строки лога
echo  ---------------------------------------------------------------
echo    [A] git: статус и последние коммиты
echo    [B] git: добавить всё, закоммитить, запушить
echo    [C] Остановить процессы PoTaskojo
echo    [D] Открыть папку профиля
echo    [E] Открыть папку сборки
echo    [F] Пересобрать скриншоты для docs
echo  ---------------------------------------------------------------
echo    [0] Выход
echo.
set "sel="
set /p "sel=Выбор: "
if not defined sel goto menu
if "%sel%"=="0" exit /b 0

:dispatch
if "%sel%"=="1" goto op_run
if "%sel%"=="2" goto op_settings
if "%sel%"=="3" goto op_editor
if "%sel%"=="4" goto op_crash
if "%sel%"=="5" goto op_build
if "%sel%"=="6" goto op_build_test
if "%sel%"=="7" goto op_tests
if "%sel%"=="8" goto op_audit
if "%sel%"=="9" goto op_log
if /i "%sel%"=="A" goto op_git_status
if /i "%sel%"=="B" goto op_git_push
if /i "%sel%"=="C" goto op_kill
if /i "%sel%"=="D" goto op_profile
if /i "%sel%"=="E" goto op_builddir
if /i "%sel%"=="F" goto op_shots
if defined scripted (echo Неизвестный пункт: %~1 & exit /b 2)
goto menu

rem ---------------------------------------------------------------
rem  Запуск
rem ---------------------------------------------------------------

:op_run
if not exist build\Release\potaskojo.exe (echo Сначала соберите приложение - пункт 5 & set "rc=1" & goto after)
echo Запуск PoTaskojo...
build\Release\potaskojo.exe
set "rc=%errorlevel%"
goto after

:op_settings
if not exist build\Release\potaskojo.exe (echo Сначала соберите приложение - пункт 5 & set "rc=1" & goto after)
echo Запуск с диалогом настроек...
build\Release\potaskojo.exe --internal-show-settings
set "rc=%errorlevel%"
goto after

:op_editor
if not exist build\Release\potaskojo.exe (echo Сначала соберите приложение - пункт 5 & set "rc=1" & goto after)
echo Запустится на вкладке Rules с открытым редактором первого правила.
echo Если правил нет - откроется пустая вкладка.
build\Release\potaskojo.exe --internal-open-rule-editor
set "rc=%errorlevel%"
goto after

:op_crash
if not exist build\Release\potaskojo.exe (echo Сначала соберите приложение - пункт 5 & set "rc=1" & goto after)
echo Проверка краш-обработчика: намеренный сбой, будет дамп и crash-report.log.
echo Подтвердите окно с сообщением - оно часть проверки.
build\Release\potaskojo.exe --internal-selftest-crash
set "rc=%errorlevel%"
goto after

rem ---------------------------------------------------------------
rem  Сборка и тесты
rem ---------------------------------------------------------------

:op_build
echo Конфигурация...
cmake -S . -B build -DPOTASKOJO_WERROR=ON
set "rc=%errorlevel%"
if not "%rc%"=="0" goto after
echo Сборка Release...
cmake --build build --config Release --parallel
set "rc=%errorlevel%"
goto after

:op_build_test
echo Конфигурация...
cmake -S . -B build -DPOTASKOJO_WERROR=ON
set "rc=%errorlevel%"
if not "%rc%"=="0" goto after
echo Сборка Release...
cmake --build build --config Release --parallel
set "rc=%errorlevel%"
if not "%rc%"=="0" goto after
echo Тесты...
ctest --test-dir build -C Release --output-on-failure
set "rc=%errorlevel%"
goto after

:op_tests
echo Тесты...
ctest --test-dir build -C Release --output-on-failure
set "rc=%errorlevel%"
goto after

:op_audit
echo Аудит сходства: наши строки против чужого дампа.
echo Нужен архив строк - задайте RE_NOTES или передайте путь вторым аргументом.
if not "%~2"=="" (
  python tools\similarity_audit.py --re-notes "%~2"
) else (
  python tools\similarity_audit.py
)
set "rc=%errorlevel%"
goto after

:op_log
echo Последние строки лога приложения...
powershell -NoProfile -Command "Get-Content (Join-Path $env:LOCALAPPDATA 'PoTaskojo\logs\app.log') -Tail 40 -ErrorAction SilentlyContinue"
set "rc=0"
goto after

rem ---------------------------------------------------------------
rem  Git
rem ---------------------------------------------------------------

:op_git_status
git status -sb
echo ---------------------------------------------------------------
git log --oneline -6
set "rc=%errorlevel%"
goto after

:op_git_push
echo Текущие изменения:
git status -sb
echo.
set "msg="
set /p "msg=Сообщение коммита (пусто = отмена): "
if not defined msg goto menu
echo.
git add -A
git commit -m "%msg%"
set "rc=%errorlevel%"
if not "%rc%"=="0" goto after
git push origin main
set "rc=%errorlevel%"
if not "%rc%"=="0" goto after
git push github main
set "rc=%errorlevel%"
goto after

rem ---------------------------------------------------------------
rem  Процессы, папки, скриншоты
rem ---------------------------------------------------------------

:op_kill
echo Ищу процессы potaskojo...
powershell -NoProfile -Command "Get-Process potaskojo -ErrorAction SilentlyContinue | ForEach-Object { Write-Host ('kill ' + $_.Id); Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue }"
set "rc=0"
goto after

:op_profile
echo Папка профиля: %LOCALAPPDATA%\PoTaskojo
if exist "%LOCALAPPDATA%\PoTaskojo" (explorer "%LOCALAPPDATA%\PoTaskojo") else (echo Профиля ещё нет - запустите приложение)
set "rc=0"
goto after

:op_builddir
if exist build\Release (explorer build\Release) else (echo Папки сборки ещё нет - соберите, пункт 5)
set "rc=0"
goto after

:op_shots
echo Пересобрать скриншоты в docs\screenshots...
powershell -NoProfile -ExecutionPolicy Bypass -File tools\screenshots.ps1
set "rc=%errorlevel%"
goto after

rem ---------------------------------------------------------------
rem  Возврат в меню
rem ---------------------------------------------------------------

:after
if defined scripted exit /b %rc%
echo.
echo Код возврата: %rc%
pause
goto menu
