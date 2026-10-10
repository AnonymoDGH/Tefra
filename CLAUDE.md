# Tefra (motor 3D): memoria del proyecto

Las sesiones no recuerdan nada: este archivo es la memoria. Actualízalo al final de cada sesión.

## Estado actual (2026-10-10)

- **M2 en curso (valle visible):** atmósfera Hillaire + niebla de altura, terreno procedural 2×2 km (clipmap, capas CC0), vegetación GPU-driven (coníferas procedurales + sotobosque CC0, culling compute + indirect), agua de río/lago. `tefra_bench --scene valley`. Falta: TAA/TAAU, GTAO, auto-exposición, niebla volumétrica, SSR, impostores.
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
- ADR-007: sin editor hasta 0.1. Procedural + datos de texto + Blender, ImGui y herramientas CLI.
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
