# Plan M0 — Fundaciones

Spec: `docs/specs/2026-10-10-tefra-vision-y-fundaciones.md` (§8). Ejecución: Claude, de forma autónoma en la nube (autorizado por el usuario).

## Tareas

| # | Tarea | Hecho cuando |
|---|---|---|
| 1 | Scaffold: LICENSE (Apache-2.0), README, THIRD_PARTY.md, .gitignore, .clang-format, CMake raíz, presets, `cmake/` (warnings, shaders, toolchain mingw) | `cmake --preset linux-debug` configura |
| 2 | `third_party/` vendored (Vulkan-Headers 1.4.341, volk 1.4.341, VMA 3.4.0, GLFW 3.5.1, doctest 2.5.3, stb) + `CMakeLists` | compila con `-Werror` en el código propio, sin warnings en el ajeno |
| 3 | `core`: log, assert, time, fs, memory (arena, pool, contabilidad), math, jobs, json writer, image (PNG + compare) | tests unitarios pasan con ASan/UBSan |
| 4 | `platform`: ventana GLFW (opcional, `TEFRA_WITH_WINDOW`) | el viewer abre ventana en Windows y Linux/X11 |
| 5 | `rhi`: instancia + validación con contador, selección de GPU (`--gpu`), capacidades baseline, VMA, frames en vuelo con timeline, swapchain y headless, renderpass2, pipelines, timestamps, readback | test gpu de clear con validación 0 |
| 6 | Shaders: compilación en build (`glslangValidator`), test pattern (cielo analítico + suelo a cuadros de 1 m, cámara a 1,7 m) | SPIR-V generado y validado |
| 7 | Apps: `tefra_info` (capacidades en JSON), `tefra_viewer`, `tefra_bench` (JSON `tefra-bench/1`, `--capture`) | JSON válido y PNG del test pattern |
| 8 | Tests GPU + golden (`tests/golden/testpattern.png`), comparación con tolerancia | `ctest -L gpu` verde con lavapipe |
| 9 | CI: linux-gcc (validación + lavapipe + golden + artifacts), linux-clang, linux-asan, windows-mingw (MSYS2) | CI verde |
| 10 | Cross-build mingw local desde Linux (`cross-mingw-release`) | genera `.exe` |
| 11 | `scripts/setup-cloud.sh` + SessionStart hook para futuras sesiones en la nube | una sesión nueva puede compilar y testear sin pasos manuales |
| 12 | Actualizar CLAUDE.md (estado, comandos, próximos pasos) y push | push en `claude/new-session-ksgl0m` |

## Riesgos de M0
- GLFW en CI de Linux sin display: el viewer no se ejecuta en CI y solo se compila. La captura es headless sin ventana.
- Lavapipe en CI (Ubuntu 24.04, Mesa 25.x): las golden se generan con la misma versión que la nube. Si difiere, se usa la tolerancia de la comparación.
