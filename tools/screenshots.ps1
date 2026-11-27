# Regenerates docs/screenshots from the current build.
#
#   powershell -File tools\screenshots.ps1
#
# ImGui ignores synthetic clicks in this environment, so every view is opened
# with an internal flag or by writing the setting it reads first. ASCII-only
# messages: a PowerShell 5.1 script without a BOM is read as ANSI, and Russian
# text would come out as mojibake.
param([string]$OutDir = "")

$ErrorActionPreference = "Stop"

if (-not $OutDir) { $OutDir = (Resolve-Path (Join-Path $PSScriptRoot "..\docs\screenshots")).Path }
$exe = (Resolve-Path (Join-Path $PSScriptRoot "..\build\Release\potaskojo.exe")).Path
# Tell the program this is a scripted run: the daily update check must not pop
# a dialog over a frame that is about to be captured.
$env:POTASKOJO_SCRIPTED = "1"
$ini = Join-Path $env:LOCALAPPDATA "PoTaskojo\settings.ini"
$rules = Join-Path $env:LOCALAPPDATA "PoTaskojo\rules.tsv"
$rulesBackup = "$rules.bak-shot"

if (-not (Test-Path $exe)) {
  Write-Output "build not found: $exe"
  Write-Output "build first: cmake --build build --config Release"
  exit 1
}

Add-Type -AssemblyName System.Windows.Forms, System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Shot32 {
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
  public struct RECT { public int Left, Top; public int Right, Bottom; }
}
"@

function Grab([string]$path, [string]$argStr, [int]$scale, [int]$tab, [bool]$light) {
  Get-Process potaskojo -EA SilentlyContinue | Stop-Process -Force
  Start-Sleep -Milliseconds 800

  $lines = @("[Window]", "StartupTab=$tab")
  if ($scale -gt 0) { $lines += "UIScalePct=$scale" }
  $lines += "[Theme]"
  $lines += "LightMode=$(if ($light) {1} else {0})"
  # The profile folder may not exist yet on a fresh machine; Set-Content does
  # not create directories and would abort the whole run.
  New-Item -ItemType Directory -Force -Path (Split-Path -Parent $ini) | Out-Null
  Set-Content $ini ($lines -join "`r`n") -Encoding Unicode

  if ($argStr) { $p = Start-Process $exe -ArgumentList $argStr -PassThru }
  else { $p = Start-Process $exe -PassThru }
  Start-Sleep -Seconds 6

  $h = 0
  if (-not $p.HasExited) { $h = (Get-Process -Id $p.Id -EA SilentlyContinue).MainWindowHandle }
  if ($h) {
    $r = New-Object Shot32+RECT
    [Shot32]::GetWindowRect($h, [ref]$r) | Out-Null
    $w = $r.Right - $r.Left
    $ht = $r.Bottom - $r.Top

    # PrintWindow returns a black bitmap for this renderer, so the window is
    # lifted above everything and the screen region is copied instead. Raising
    # it is what makes the copy ours rather than whatever happens to overlap.
    [Shot32]::SetWindowPos([IntPtr]$h, [IntPtr](-1), 0, 0, 0, 0, 0x0043) | Out-Null
    Start-Sleep -Milliseconds 400

    $bmp = New-Object System.Drawing.Bitmap($w, $ht)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($r.Left, $r.Top, 0, 0, (New-Object System.Drawing.Size($w, $ht)))
    $g.Dispose()

    [Shot32]::SetWindowPos([IntPtr]$h, [IntPtr](-2), 0, 0, 0, 0, 0x0043) | Out-Null

    # A blank capture must fail loudly: writing one would silently replace a
    # good screenshot in the repository with a black rectangle.
    $distinct = @{}
    for ($y = 0; $y -lt $ht; $y += 8) {
      for ($x = 0; $x -lt $w; $x += 8) {
        $c = $bmp.GetPixel($x, $y)
        $distinct["{0:X2}{1:X2}{2:X2}" -f $c.R, $c.G, $c.B] = 1
      }
    }
    if ($distinct.Count -lt 12) {
      $bmp.Dispose()
      Stop-Process -Id $p.Id -Force -EA SilentlyContinue
      throw "capture of $path has only $($distinct.Count) distinct colours - it looks blank"
    }

    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bmp.Dispose()
  }
  Stop-Process -Id $p.Id -Force -EA SilentlyContinue
  Start-Sleep -Milliseconds 600
  Write-Output ("  {0,-26} hwnd={1}" -f (Split-Path $path -Leaf), $h)
}

# --- ordinary views -------------------------------------------------------
Write-Output "views:"
Grab (Join-Path $OutDir "scale_auto.png")    ""                       0   0 $false
Grab (Join-Path $OutDir "scale_150.png")     ""                     150   0 $false
Grab (Join-Path $OutDir "scale_200.png")     ""                     200   0 $false
Grab (Join-Path $OutDir "theme_light.png")   ""                       0   0 $true
Grab (Join-Path $OutDir "settings_100.png")  "--internal-show-settings" 0 0 $false
Grab (Join-Path $OutDir "settings_scaled.png") "--internal-show-settings" 150 0 $false
Grab (Join-Path $OutDir "perf_100.png")      ""                       0   1 $false
Grab (Join-Path $OutDir "perf_150.png")      ""                     150   1 $false
Grab (Join-Path $OutDir "startup_100.png")   ""                       0   3 $false

# --- rules: editor dialog and the event journal ---------------------------
# The demo rules are written, photographed and then removed: the editor is
# reached by right-clicking a row, which cannot be synthesised here, and the
# journal needs monitors that fire on the first snapshot. Their response is
# Alert, so nothing on the machine is touched.
$demo = @(
  "TSRULES 2",
  "rule 1 browser.exe",
  "prio 5 io 0 start 1",
  "aff 1 0 010101010101000000000000",
  "wd 0 90.5 30 0 0 5",
  "wd 2 1.04858e+07 10 6 1 15 C:\Tools\watch.exe --once",
  "end",
  "rule 1 explorer.exe",
  "prio 3 io 0 start 0",
  "aff 0 0",
  "wd 1 1 0 0 0 5",
  "end",
  "rule 1 dwm.exe",
  "prio 3 io 0 start 0",
  "aff 0 0",
  "wd 1 1 0 0 0 5",
  "end",
  "rule 0 notepad.exe",
  "prio 3 io 0 start 0",
  "aff 0 0",
  "end",
  ""
) -join "`r`n"

Write-Output "rules:"
$hadRules = Test-Path $rules
if ($hadRules) { Move-Item $rules $rulesBackup -Force }
[System.IO.File]::WriteAllText($rules, $demo, [System.Text.Encoding]::ASCII)
try {
  Grab (Join-Path $OutDir "rules_editor_100.png") "--internal-open-rule-editor" 0 6 $false
  Grab (Join-Path $OutDir "rules_editor_150.png") "--internal-open-rule-editor" 150 6 $false
  Grab (Join-Path $OutDir "rules_events_100.png") "" 0 6 $false
}
finally {
  Get-Process potaskojo -EA SilentlyContinue | Stop-Process -Force
  Start-Sleep -Milliseconds 500
  if ($hadRules) { Move-Item $rulesBackup $rules -Force }
  elseif (Test-Path $rules) { Remove-Item $rules -Force }
  if (Test-Path $rulesBackup) { Remove-Item $rulesBackup -Force }
}
Write-Output "rules.tsv restored"
