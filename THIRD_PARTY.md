# Dependencias de terceros

Todas permisivas y compatibles con Apache-2.0. Cada una conserva su licencia en su carpeta.

| Dependencia | Versión | Licencia | Ruta | Uso |
|---|---|---|---|---|
| Vulkan-Headers | vulkan-sdk-1.4.341.0 | Apache-2.0 / MIT | `third_party/vulkan-headers` | API Vulkan (solo headers C) |
| volk | vulkan-sdk-1.4.341.0 | MIT | `third_party/volk` | Carga dinámica de Vulkan |
| Vulkan Memory Allocator | 3.4.0 | MIT | `third_party/vma` | Suballocation de memoria GPU |
| GLFW | 3.5.1 | zlib | `third_party/glfw` | Ventana, entrada, gamepad |
| doctest | 2.5.3 | MIT | `third_party/doctest` | Tests |
| Dear ImGui (docking) | commit 778992f | MIT | `third_party/imgui` | Interfaz de Tefra Studio (backends GLFW + Vulkan vía volk) |
| ImGuizmo | commit 105e4fd | MIT | `third_party/imguizmo` | Gizmos de mover/rotar/escalar en Studio |
| stb_image / stb_image_write | commit 2c980bb (2026-08-01) | MIT o dominio público | `third_party/stb` | PNG (captura, golden images) |
| zstd (decompresor) | 1.5.7 (tag v1.5.7, commit f8745da) | BSD-3-Clause (zstd es BSD o GPLv2 a elección; se usa la BSD) | `third_party/zstd` (`zstddeclib.c` amalgamado con `build/single_file_libs/combine.py -x legacy/zstd_legacy.h`, `zstd.h`, `zstd_errors.h`, `LICENSE`) | Descompresión de los chunks zstd de `.rbxl`/`.rbxm` en `engine/rbx` (ADR-017). Solo el decodificador, sin formatos legacy. |
| Jolt Physics | 5.6.0 (tag v5.6.0, 2026-07-11) | MIT | `third_party/jolt` (solo `Jolt/` y `LICENSE`) | Física de cuerpos, personaje (`CharacterVirtual`) y raycasts en `engine/physics` (ADR-014). Integración en `third_party/CMakeLists.txt` con `Jolt/Jolt.cmake`: sin Vulkan/DX12/Metal, sin profiler ni debug renderer, SIMD hasta SSE4.2 (x86-64-v2), sin `-Werror` y con includes de sistema. |
| Microsoft.CodeAnalysis.CSharp (Roslyn) | 4.8.0 (NuGet) | MIT | No vendored: `dotnet build` la restaura de NuGet para `managed/Tefra.Engine` y la copia a `bin/managed` | Compilación en memoria de los scripts C# (M3, ADR-013) |
| .NET 8 runtime (hostfxr, CoreCLR) | 8.0 | MIT | No redistribuido: lo carga el host en tiempo de ejecución desde la instalación del sistema (`engine/scripting`) | Scripting C# |

Herramientas de build (no se redistribuyen): glslang (BSD-3 / Apache-2.0), CMake, Ninja, .NET 8 SDK (MIT).

## Assets

Política (spec §4): solo CC0, CC-BY, OFL (fuentes) o propios, con la procedencia registrada.

| Asset | Versión | Licencia | Ruta | Uso |
|---|---|---|---|---|
| Inter (Regular, SemiBold, Bold) | 4.0 (rsms/inter) | SIL OFL 1.1 | `apps/studio/resources/fonts/Inter-*.ttf` | Fuente de la interfaz de Studio y del player |
| JetBrains Mono (Regular) | 2.304 | SIL OFL 1.1 | `apps/studio/resources/fonts/JetBrainsMono-Regular.ttf` | Editor de scripts y salida |
| Miniaturas de plantillas | propias | CC0 | `apps/studio/resources/templates/*.png` | Renderizadas por Tefra Studio (`--render-template-thumbnails`) |
