@echo off
rem Tefra bench on the reference PC. Runs both GPUs and writes JSON reports next to this script.
rem Usage: double-click, or run from cmd. Send back the bench-*.json files.
setlocal
cd /d "%~dp0"
tefra_info.exe --out info.json
echo GPUs written to info.json
for %%G in (0 1) do (
  echo === GPU %%G: headless 1280x720 ===
  tefra_bench.exe --gpu %%G --frames 600 --warmup 60 --width 1280 --height 720 --out bench-gpu%%G-headless.json --capture capture-gpu%%G.png
  echo === GPU %%G: window, present immediate ===
  tefra_bench.exe --gpu %%G --window --present immediate --frames 600 --warmup 60 --width 1280 --height 720 --out bench-gpu%%G-window.json
)
echo Done. Send info.json, bench-*.json and capture-*.png
pause
