# PoTaskojo — прогресс

Системный монитор для Windows. Стек: **C++20 + CMake + MSVC, Dear ImGui
(docking) + Win32 + D3D11**.

## Статус фаз

| Фаза | Статус | Что сделано |
|---|---|---|
| **C1. Каркас** | ✅ | Win32-окно, D3D11+ImGui (docking), 7 вкладок-навигация, settings/log/paths, single-instance. **Первый кадр: 78–110 мс** |
| **C2. Processes** | ✅ | Сборщик через `NtQuerySystemInformation` (1 syscall: CPU/RAM/I/O/priority), поиск, сортировка (ImGui sort specs), ListClipper, контекстное меню: End/Suspend/Resume/Priority/Open location/Copy path, защита системных процессов, отображение `Base N` для динамических приоритетов |
| **C3. Performance** | ✅ | PDH: CPU total + per-core wildcard-массивы, память (GlobalMemoryStatusEx), диск (PDH), сеть (PDH wildcard), графики ImGui::PlotLines + ProgressBar |
| **C4. Services/Startup/Connections/Information** | ✅ | SCM-перечисление + start/stop/pause/restart; Run-ключи HKCU/HKLM + StartupApproved + папки + enable/disable/delete; TCP/UDP IPv4+IPv6 через `GetExtendedTcpTable/UdpTable` c PID→имя; system info (RtlGetVersion, RAM, CPU brand, uptime, elevation) |
| **C5. Rules** | ✅ | Модель правил (priority/IO/affinity/start-action/watchdog), версионированное хранилище `rules.tsv` (лимиты: 128 правил, 4 watchdog), движок: матч по exe → применение к **каждому PID** (новые экземпляры не пропускаются), safety-breaker (авто-пауза при >20 действий/мин), защита системных процессов, пауза/резюме, UI вкладки. **Проверено вживую**: правило `notepad.exe → High` применилось (`prio=High`) |
| **C6. История + защита** | ✅ | Crash handler: `SetUnhandledExceptionFilter` → `crash.log` + minidump (`MiniDumpWriteDump`), **проверено вживую** (0xC0000005 → 95.5 КБ дамп); low-memory watcher (GlobalMemoryStatusEx, порог <256 МБ или <3%, колбэк на смену состояния). Снапшоты-история (XPRESS) — отложена, не критична для MVP |
| **C7. Продукт** | 🔶 | **Tray + close-to-tray** (Shell_NotifyIcon, WM_APP, окно скрывается — проверено: процесс жив); **autorun** (Run-ключ + StartupApproved); **темы dark/light** (persist, проверено: bg 0.94/0.95/0.96); **диалог Settings** (окно/autorun/тема); **IFEO-замена Task Manager** (модуль `platform/ifeo.*`: детекция режима запуска + запуск оригинального taskmgr + тумблер в настройках с проверкой admin; требует admin для записи в HKLM — код готов, проверка записи нужна под elevation). Осталось: апдейтер с подписанным манифестом, инсталлятер |

## Сборка

```powershell
cmake -G "Visual Studio 18 2026" -A x64 -S . -B build
cmake --build build --config Release
build\Release\potaskojo.exe
```

Данные: `%LOCALAPPDATA%\PoTaskojo\` (`settings.ini`, `logs\app.log`).
Для portable-режима создайте папку `PoTaskojoData\` рядом с exe.

CI: `.github/workflows/release.yml` — `verify` (сборка со `/WX` + сверка
`VERSION` с тегом) → `build` (артефакт + sha256) → `publish` (на тег `v*`) →
SLSA-подпись → `gh attestation verify`.

## Ключевые решения

- **Сбор данных**: worker-поток → staging → `shared_ptr` swap; UI-поток не
  блокируется на опросе.
- **Первый кадр**: окно не показывается, пока collectors и иконки не готовы —
  иначе видно «мигание» пустого окна.
- **Предохранители правил** (C5) — обязательны: авто-пауза при >N действий/мин,
  лимит дампов/час, отказ от protected/critical процессов.
- **Обновления** (C7): домен-разделённая подпись (domain separation) +
  canonical-строка, SHA-256 payload, затем Authenticode-проверка издателя.

## Известные ограничения

- Нет GPU-графиков (DXCore-счётчики) — в Information пока только базовые метрики.
- Нет per-process сетевых скоростей (нужен ETW) — только соединения.
- Startup: packaged-приложения (AppX) не перечисляются, только Run/папки.
- Сетевые графики: PDH `\Network Interface(*)` суммирует все NIC.
- **Снапшоты-история** (XPRESS) не реализованы — отложены.
- **Share-коды** правил/тем через буфер обмена не реализованы.
- GPU-графики (DXCore), ETW per-process network — не реализованы.
