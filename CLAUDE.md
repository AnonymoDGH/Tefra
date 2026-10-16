# Tefra (motor 3D): memoria del proyecto

Las sesiones no recuerdan nada: este archivo es la memoria. Actualízalo al final de cada sesión.

## Estado actual (2026-10-16)

- **M3b integrado:**
  - Página de inicio estilo Roblox Studio: `apps/studio/src/home.cpp`, `projects.cpp`.
  - Plantillas en `templates.cpp`: Baseplate, Classic, Flat Terrain, Obby (juego completo), Beach, Meadow y Valley.
  - `tefra_player` + Export Game (`export.cpp`), `engine/runtime` (GameRuntime) y `engine/rbx` (importador Roblox, ADR-017; `tefra_import`).
  - `engine/ui` (GUI en juego), `engine/audio` (miniaudio), skinning glTF y LocalScript/ModuleScript/eventos.
  - Terreno Plains con hierba de pradera y viento.
- **Materiales de Part procedurales** (`shaders/common/surface.glsl`, flag bit4 + `pad[1]` = Material): patrones world-space con bump por gradiente de superficie y antialias por `fwidth`. Captura `docs/captures/part-materials.png`.
- **ParticleEmitter** (`engine/world/particles.cpp` CPU + `shaders/particle.*`, pase tras el agua con máscara reactiva para el TAA); eventos Humanoid Jumping/FreeFalling/Landed/StateChanged (`physics/character.cpp`).
- **API C# ampliada (5 subagentes haiku):** TweenService (`Tween.cs`), CFrame/UDim2/secuencias (`MathTypes.cs`), atributos + CollectionService (DataModel + ABI), Bindable/Remote events, Debris, DataStoreService local (`TEFRA_DATASTORE_DIR`), HttpService JSON, consultas espaciales y ApplyImpulse/GetMass (`physics/spatial.cpp`). ABI de scripting v4.
- **AMD legacy (R5 M330, VK 1.2.170):** validación limpia pero sin vegetación y océano negro. Arreglos: culling de vegetación en CPU automático (`TEFRA_GPU_CULL=0/1` fuerza), protección NaN en océano y TAA, framebuffers clásicos. Intel HD 520 ya se ve bien.
- **2026-10-12 (4 subagentes haiku):** cámara de Studio con menos latencia (wait_next antes de leer input, mailbox); importador Roblox completo (placeholders con atributos `RobloxClass`/`rbx.*`, Value/Decal/Attachment/Weld/GUI, stubs C#; `docs/research/rbx-import/REPORT.md`); cielo configurable (`Sky`/`Atmosphere`/`Clouds`, estrellas, luna, HDRI/cubemap; `engine/world/sky.cpp`); importar FBX/OBJ/STL/PLY → GLB (`model_import.cpp`, ufbx); carga 2-3× más rápida con caché en disco de terreno y glTF (`docs/research/perf-2026-10-12/REPORT.md`; subir `kFieldCacheVersion`/`kGltfCacheVersion` al cambiar generadores).
- **Luces locales reales** (PointLight/SpotLight → `Scene::lights()`, 64 más cercanas en `lighting.frag`), Neon emisivo display-referred, Lighting interior (SunBrightness, EnvironmentDiffuseScale, IndoorAmbient), materiales Wallpaper/Carpet/CeilingTile, plantilla **Backrooms** con props hechos en Blender por script (`tools/blender/make_backrooms_props.py`; bpy instalable con `pip install bpy` en Python 3.13; `MeshId = builtin:models/...`).
- **Ciudad lluviosa (2026-10-13/16):** plantilla `city_rain`. Motor:
  - clase `Rain`: mojado satinado (no espejo), charcos espejo con ondas, gotas en `rain.vert/frag`;
  - luces por tiles (`light_cull.comp`, 128–1024 según el preset);
  - SSR solo para rugosidad < 0,22 (off en Low);
  - halos volumétricos/analíticos.
  - Modelos (todos por bpy, ver `apps/studio/resources/models/city/README.md`):
    - edificios con kits CC0 de Poly Haven (`make_city_buildings.py`, texturas compartidas entre .gltf vía `Scene::find_texture`);
    - props de Poly Haven (`make_city_ph_props.py`);
    - coches propios (`make_city_cars.py`);
    - semáforos, neones con texto (`make_city_props.py`).
  - Kits en `build/polyhaven` (no versionado).
  - Coste sin medir en la HD 520 (edificios de 55–186 k triángulos).
- **POM + interior mapping** (`gbuffer.frag`, flags 32/64, extras glTF `tefra_parallax`/`tefra_height`/`tefra_interior`; `docs/research/parallax/REPORT.md`).
- **Cámara y personaje en juego:** interpolación de física (`character_post_step` con alpha), inercia/giro suave/coyote time/buffer de salto/salto variable/sprint (`physics/character.cpp`); cámara con cursor bloqueado, shift-lock (Ctrl), primera persona, zoom exponencial, colisión con 3 rayos (`GameRuntime::update_camera`).
- **Nubes volumétricas opcionales** (`Clouds.Volumetric`; `shaders/clouds_vol.frag`, pase `clouds` a 1/2-1/4 de resolución solo en píxeles de cielo, compuesto en `lighting.frag` `sky_with_clouds`; presets en `limit_quality`). Pendiente: sombras de nubes sobre el suelo, reproyección temporal propia.
- **MCP para agentes:** `engine_guide`, `create_instances` (lote), `terrain_height`, `raycast`, `new_place_from_template`, `import_model`, `import_roblox`, `save_project`. MeshId relativo al proyecto (`SceneSync::set_asset_root`).
- **Release:** `.github/workflows/release.yml` publica el instalador Inno Setup (`installer/tefra.iss`) y un zip portable. Se dispara con un commit cuyo mensaje lleva `[release]`, porque el proxy no deja empujar tags. Primer release: v0.1.0-alpha.1.
- Subagentes: usar `model: 'haiku'` (petición del usuario, por el límite de uso).
- Siguiente (`docs/research/gap-analysis/REPORT.md`), M4 UI avanzada:
  - tipos UDim2/ColorSequence;
  - TweenService;
  - renderer de UI SDF con fuentes y rich text;
  - editor visual de UI en Studio.
- Pendiente de la pradera: el borde del disco de hierba (~60 m) aún se nota; afinar el tono del terreno lejano.

- **M3 en curso: Tefra Studio** (petición del usuario: hacer juegos de verdad). Spec: `docs/specs/2026-10-10-m3-studio.md` (ADR-011..016).
  - Hecho: `JsonValue`; `engine/world` (DataModel reflejado, `.tefra`, `SceneSync`); `apps/studio` (ImGui docking: Explorer, Properties, Viewport+ImGuizmo, Output, editor C#, undo/redo, Play/Stop; `tefra_studio_core` = session+history testeable); `engine/scripting` + `managed/Tefra.Engine` (.NET 8 hostfxr, Roslyn); `engine/physics` (Jolt 5.6, CharacterController); `engine/anim` (avatar R15, 7 clips, Animator); `engine/mcp` (Streamable HTTP, 15 herramientas + 11 de Studio). Play = scripts → personaje → física → animación; cámara 3ª persona. Guía: `docs/STUDIO.md`. Informes: `docs/research/m3-*/REPORT.md`.
  - Studio en la nube: `xvfb-run -a ./build/linux-release/bin/tefra_studio --demo [--play] --capture out.png --frames N`.
  - Pendiente: skinning glTF, LocalScript/ModuleScript, plantillas (playa/valle como Terrain), juego de ejemplo "Obby", exportar standalone, UI en juego (ScreenGui), sonido.
- Escena playa (`--scene coast`): océano Gerstner + surf guiado por la distancia a la orilla, refracción con absorción por canal, cáusticas, nubes, cocoteros procedurales. Capturas `docs/captures/coast-*`.

- **M2 en curso (valle visible):** atmósfera Hillaire + niebla de altura, terreno procedural 2×2 km (clipmap, capas CC0), vegetación GPU-driven, agua, TAA/TAAU + CAS, GTAO. `tefra_bench --scene valley`.
- **Noche de fotorrealismo (2026-10-10):** 9 subagentes en worktrees; informes en `docs/research/<tema>/REPORT.md`. Integrados los 9: terrain-detail (POM, height blend, stochastic), trunks (contrafuertes, raíces, musgo), volumetric-light (raymarch media res.), camera-post (bloom, auto-exposición, viñeta/CA/grano), canopy-occlusion (mapa CPU de cobertura), grass (briznas en vertex shader; densidad sigue al dosel), shadows (PCSS + contacto; código en `shaders/common/shadow.glsl`), foliage (atlas de agujas horneado; copa densificada en la integración, LOD0 < 4500 tris), gradación pre-AgX (calibración). Capturas finales: `docs/captures/m2-valley-v8-*.png` y `m2-before-after.png`.
- Siguientes ideas visuales: impostores de copa, material propio de madera muerta, compactación GPU de hierba, velocidad de viento para el TAA, niebla menos lechosa, SSR en el agua. Cada técnica con flag en `RenderSettings` y `--no-*` en CLI. Brief para agentes: `docs/research/AGENT_BRIEF.md`.
- Costes de todo lo nuevo **sin medir en la HD 520**; la hierba (3–8 ms estimados) y la volumétrica (2–3 ms) son los riesgos.
- **M0 y M1 implementados.** M1: render graph (barreras automáticas), bindless, deferred PBR, IBL (HDR o cielo procedural), cascadas de sombra, AgX, glTF (cgltf). Spec M1: `docs/specs/2026-10-10-m1-render-base.md`. Capturas en `docs/captures/`.
- Assets CC0 de demo: `python3 scripts/fetch_assets.py` → `assets/cache/` (no versionado). Ejemplo: `tefra_bench --gltf assets/cache/models/dead_tree_trunk/dead_tree_trunk.gltf --hdr assets/cache/hdri/dark_autumn_forest.hdr --capture out.png`.
- El usuario concedió **autonomía total** (2026-10-10): sin preguntas, push libre, decisiones con criterio. Corrige al ver resultados.
- Spec vigente: `docs/specs/2026-10-10-tefra-vision-y-fundaciones.md`. Plan M0: `docs/plans/2026-10-10-m0-fundaciones.md`.
- Repo `AnonymoDGH/Tefra` (público; el usuario autorizó el push). Rama de trabajo: `claude/new-session-ksgl0m`.
- Pendiente del usuario: ejecutar `scripts/bench-windows.bat` en su PC (build en el artifact `tefra-windows` de CI) y devolver `info.json` y `bench-*.json`.

## Decisiones (ADR cortos; detalle en el spec §3)

- ADR-000: nombre **Tefra**. Nada de "RAGE", "estilo GTA" ni marcas de terceros.
- ADR-001: clean-room estricto (nada de Rockstar/Take-Two ni de otros juegos comerciales, tampoco filtraciones).
- ADR-002: C++20 sin excepciones ni RTTI en el motor. GCC/MinGW ≥ 13 y Clang ≥ 17.
- ADR-003: Vulkan 1.2 baseline (sync2, renderpass2, imageless FB, bindless, BDA, timeline, drawIndirectCount) con volk + VMA. Límites: push constants ≤ 128 B, ≤ 4 sets en uso, sin fp16, shared memory ≤ 32 KB.
- ADR-004: shaders GLSL → SPIR-V con glslangValidator en build (`#extension GL_GOOGLE_include_directive`).
- ADR-005: Windows (MinGW) y Linux, ambos en CI.
- ADR-006: licencia Apache-2.0.
- ADR-007: (sustituido por ADR-011) editor Tefra Studio con ImGui docking.
- ADR-011..016 (M3): Studio ImGui; DataModel reflejado; C# .NET 8 vía hostfxr + Roslyn; Jolt; MCP Streamable HTTP en 127.0.0.1:44755/mcp; avatar R15 + animación esquelética.
- ADR-008: **deferred con G-buffer fino** (16 B/px) + clustered forward para transparencias. TAAU de 540p a 720p en Low. El visibility buffer queda reservado para High.
- ADR-009: metros, +Y arriba, −Z adelante, reverse-Z con far infinito y D32F, render relativo a cámara (posiciones double en CPU).
- ADR-010: dependencias vendored en `third_party/` (ver THIRD_PARTY.md).
- Vertical slice: "Valle de Ceniza", valle de bosque brumoso de 2×2 km (spec §2).

## Hardware objetivo (tier Low = PC del usuario)

- i5-6200U (2C/4T, AVX2, **sin AVX-512**), 7,9 GB de RAM, Windows 10.
- Intel HD 520 (VK 1.2.203) y AMD R5 M330 de 2 GB (VK 1.2.170). Sin VK 1.3, dynamic rendering, mesh shaders, RT ni VRS.
- Toolchain: MinGW-w64 g++ 15.2, CMake 4.0.2, Ninja 1.13 y Vulkan SDK 1.4.341.1. Sin MSVC ni clang.
- **Todo número de rendimiento sale de su PC.** Lavapipe solo vale para comprobar corrección.

## Entorno de la nube

- Xeon @ 2,80 GHz (4 vCPU con AVX-512), 15 GiB de RAM, ~31 GB de disco, Ubuntu 24.04. gcc 13.3, clang 18, CMake 3.28.
- Hook SessionStart (`.claude/hooks/session-start.sh`): instala lavapipe, validation layers, glslang, mingw, ccache y las cabeceras X11/Wayland, y configura `build/linux-debug`.
- Lavapipe expone VK 1.4. El motor pide `apiVersion` 1.2 y solo habilita el baseline; los tests GPU usan `strict_baseline`.
- Nunca `-march=native` (se usa x86-64-v2).
- Poly Haven (CC0) es accesible desde la nube. ambientCG no.

## Comandos

```sh
cmake --preset linux-debug && cmake --build --preset linux-debug && ctest --preset linux-debug
cmake --preset linux-asan  && cmake --build --preset linux-asan  && ctest --preset linux-asan
cmake --preset cross-mingw-release && cmake --build --preset cross-mingw-release   # .exe para Windows
TEFRA_UPDATE_GOLDEN=1 ./build/linux-debug/bin/tefra_gpu_tests   # regenerar golden (revisar la imagen)
./build/linux-debug/bin/tefra_bench --frames 5 --capture out.png --out bench.json
clang-format -i $(git ls-files 'engine/*.cpp' 'engine/*.h' 'apps/*.cpp' 'apps/*.h' 'tests/*.cpp')
```

## Estructura

`engine/{core,platform,rhi,rendergraph,renderer}`, `apps/{common(runner),info,viewer,bench}`, `shaders/`, `tests/{unit,gpu,golden}`, `third_party/` y `docs/{specs,plans}`.

## Próximos pasos

1. M2 (el look): terreno con LOD y materiales por capas, atmósfera física (Hillaire), niebla de altura + volumétrica (froxels), TAA/TAAU, GTAO, auto-exposición, vegetación instanciada GPU-driven (culling + MDI).
2. Pendiente de M1: ImGui (debug overlay).
