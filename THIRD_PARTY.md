# Dependencias de terceros

Todas permisivas y compatibles con Apache-2.0. Cada una conserva su licencia en su carpeta.

| Dependencia | Versión | Licencia | Ruta | Uso |
|---|---|---|---|---|
| Vulkan-Headers | vulkan-sdk-1.4.341.0 | Apache-2.0 / MIT | `third_party/vulkan-headers` | API Vulkan (solo headers C) |
| volk | vulkan-sdk-1.4.341.0 | MIT | `third_party/volk` | Carga dinámica de Vulkan |
| Vulkan Memory Allocator | 3.4.0 | MIT | `third_party/vma` | Suballocation de memoria GPU |
| GLFW | 3.5.1 | zlib | `third_party/glfw` | Ventana, entrada, gamepad |
| meshoptimizer | 0.22 (solo `simplifier.cpp` y `allocator.cpp`) | MIT | `third_party/meshoptimizer` | Simplificación de mallas: LODs de los escaneos pesados (rocas de la costa) |
| ufbx | 0.23.1 (tag v0.23.1, https://github.com/ufbx/ufbx) | MIT o dominio público (a elegir; se usa la MIT) | `third_party/ufbx` (`ufbx.c`, `ufbx.h`, `LICENSE`) | Lectura de FBX (binario y ASCII) y OBJ + MTL en la importación de modelos (`engine/renderer/src/model_import.cpp`, Studio: File > Import 3D Model) |
| doctest | 2.5.3 | MIT | `third_party/doctest` | Tests |
| Dear ImGui (docking) | commit 778992f | MIT | `third_party/imgui` | Interfaz de Tefra Studio (backends GLFW + Vulkan vía volk) |
| ImGuizmo | commit 105e4fd | MIT | `third_party/imguizmo` | Gizmos de mover/rotar/escalar en Studio |
| stb_image / stb_image_write | commit 2c980bb (2026-08-01) | MIT o dominio público | `third_party/stb` | PNG (captura, golden images) |
| zstd (decompresor) | 1.5.7 (tag v1.5.7, commit f8745da) | BSD-3-Clause (zstd es BSD o GPLv2 a elección; se usa la BSD) | `third_party/zstd` (`zstddeclib.c` amalgamado con `build/single_file_libs/combine.py -x legacy/zstd_legacy.h`, `zstd.h`, `zstd_errors.h`, `LICENSE`) | Descompresión de los chunks zstd de `.rbxl`/`.rbxm` en `engine/rbx` (ADR-017). Solo el decodificador, sin formatos legacy. |
| miniaudio | 0.11.25 | MIT-0 o dominio público (a elegir) | `third_party/miniaudio` (`miniaudio.h`, `LICENSE`, `miniaudio_impl.c`) | Audio de juego: salida por WASAPI/DirectSound/ALSA/PulseAudio (carga en tiempo de ejecución), decodificación WAV, FLAC y MP3, mezcla y espacialización 3D (`engine/audio`) |
| stb_vorbis | v1.22 | Dominio público / MIT (a elegir) | `third_party/stb_vorbis` (`stb_vorbis.c`, `LICENSE`) | Decodificador Ogg Vorbis que usa miniaudio (`STB_VORBIS_INCLUDE_STB_VORBIS_H`) |
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
| Efectos de sonido integrados | propios (generados) | CC0 | `resources/sfx/*.wav` (`scripts/gen_sfx.py`) | Sonidos `builtin:<nombre>` del motor de audio y de los personajes; ver `resources/sfx/README.md` |
| Fox (`Models/Fox`, Khronos glTF-Sample-Assets) | main (descargado por `scripts/fetch_assets.py`) | modelo CC0 1.0 (PixelMannen); rig y animaciones CC-BY 4.0 (tomkranis); conversión glTF CC-BY 4.0 (@AsoboStudio, @scurest) | `assets/cache/models/characters/fox.glb` (no versionado) | Pruebas de skinning y animación (M3b); personaje importable de ejemplo. **Atribución obligatoria** (CC-BY 4.0) |
| CesiumMan (`Models/CesiumMan`, Khronos glTF-Sample-Assets) | main (descargado por `scripts/fetch_assets.py`) | CC-BY 4.0 (Cesium) | `assets/cache/models/characters/cesium_man.glb` (no versionado) | Prueba de skinning con un solo clip (M3b). **Atribución obligatoria** (CC-BY 4.0) |
| Kits de fachada modular `modular_urban_apartments_facade` y `modular_factory_facade` (Poly Haven, colección "Hidden Alley") | 1k, descargados en 2026-10 | CC0 1.0 | `apps/studio/resources/models/city/buildings/` (montados por `tools/blender/make_city_buildings.py`) | Edificios de la plantilla Rainy City. El mapa de alturas del ladrillo es el `brick_disp` de Poly Haven; el del enlucido se deriva de su AO |
| Mobiliario urbano de Poly Haven (`fire_hydrant`, `metal_trash_can`, `modular_street_seating`, `exterior_aircon_unit`, `modular_fire_escape`, `water_manhole_cover`, `street_lamp_01/02`, `security_light`, `utility_box_02`, `concrete_road_barrier`, `covered_car`, `barrel_stove`, `cardboard_box_01`, `wooden_crate_02`, `plastic_crate_01`, `planter_box_01`, `old_tyre`, `WetFloorSign_01`, `rollershutter_door`) | 1k, descargados en 2026-10 | CC0 1.0 | `apps/studio/resources/models/city/*.glb` (`tools/blender/make_city_ph_props.py`) | Props de la ciudad, reducidos y con texturas de 256 a 1024 px |
| Coches, semáforos, neones, toldos, bolardos, parquímetros, atlas de habitaciones | propios (bpy) | CC0 | `apps/studio/resources/models/city/` (`make_city_cars.py`, `make_city_props.py`, `make_city_buildings.py`) | Las letras de los neones son contornos de la fuente integrada de Blender (Inter, SIL OFL 1.1) |
