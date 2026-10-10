@echo off
rem Tefra bench on the reference PC. Measures both GPUs and writes JSON reports next to this script.
rem Usage: double-click, or run from cmd. Send back info.json, bench-*.json and the PNG captures.
setlocal
cd /d "%~dp0"
tefra_info.exe --out info.json
echo GPUs written to info.json
if not exist assets\cache\textures (
  echo Downloading CC0 demo assets from Poly Haven ^(about 35 MB^)...
  python scripts\fetch_assets.py --res 1k
)
for %%G in (0 1) do (
  echo === GPU %%G: test pattern, headless 1280x720 ===
  tefra_bench.exe --gpu %%G --frames 300 --warmup 30 --out bench-gpu%%G-testpattern.json
  echo === GPU %%G: valley, tier low ^(960x540 + TAAU to 1280x720^), headless ===
  tefra_bench.exe --gpu %%G --scene valley --tier low --frames 600 --warmup 120 --out bench-gpu%%G-valley-low.json --capture valley-gpu%%G.png
  echo === GPU %%G: valley, tier low, window, present immediate ===
  tefra_bench.exe --gpu %%G --scene valley --tier low --window --present immediate --frames 600 --warmup 120 --out bench-gpu%%G-valley-low-window.json
)
echo Done. Send info.json, bench-*.json and valley-*.png
pause
