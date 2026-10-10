# Brief común para subagentes: Tefra engine (fotorrealismo)

Vas a trabajar en **Tefra**, un motor 3D open source (C++20, Vulkan 1.2, deferred PBR). El objetivo de esta noche es el **fotorrealismo** de un bosque de coníferas brumoso (referencia estética: el videojuego "The Isle": bruma fría, troncos enormes, sotobosque denso, luz filtrada). Trabajas en tu **propio worktree aislado** de git. NO hagas `git push`. Haz commit en tu rama local al terminar.

## Reglas innegociables
- Clean-room: nada de código/assets/formatos de Rockstar ni de otros juegos comerciales. Fuentes permitidas: papers, charlas GDC/SIGGRAPH, documentación, código open source con licencia permisiva (atribuir en THIRD_PARTY.md).
- Assets solo CC0 (Poly Haven) o generados proceduralmente.
- Compila con `-Werror`. Validación Vulkan con **0 errores** (incluida sync validation).
- Push constants ≤ 128 B. **Cuidado con std430**: en GLSL un `vec4` en un bloque `push_constant` se alinea a 16 bytes; en C++ añade padding y `static_assert(offsetof(...) == ...)`.
- Vulkan 1.2 baseline (sin dynamic rendering, sin mesh shaders, sin RT, sin fp16).
- No uses `-march=native`.

## Preparación del worktree (haz esto primero)
```sh
cd <tu worktree>
ln -s /home/user/Tefra/assets assets 2>/dev/null || true   # assets CC0 ya descargados (gitignored)
export CCACHE_BASEDIR=/                                     # ccache compartido entre worktrees
cmake --preset linux-release && cmake --build --preset linux-release -j2
```
(4 vCPU compartidas por ~10 agentes: usa `-j2`.)

## Mapa del código
- `engine/rhi/`: Vulkan (device, bindless heap con `add_texture`, upload, pipelines, `GraphicsPipelineDesc`).
- `engine/rendergraph/graph.{h,cpp}`: frame graph. `graph_.add_pass(name, PassType::Raster|Compute, setup(PassBuilder&), exec(PassContext&))`. En setup: `b.create({fmt,w,h,name})`, `b.read(h, Access::Sampled)`, `b.color(h, Load)`, `b.depth(h, Load, clear, write)`. En exec: `ctx.sampled(h)` da el índice bindless. Las barreras se generan solas. Los buffers no los rastrea: usa `rhi::cmd_memory_barrier` a mano dentro de passes Compute.
- `engine/renderer/renderer.cpp`: construye el frame. Orden: veg_cull → shadows (atlas de cascadas) → gbuffer (terreno + vegetación + mallas) → gtao → skyview_lut → lighting → water → taa → tonemap. Las pipelines se crean en `create_pipelines()` con `compat_pass(...)`. Constantes del frame en `FrameConstants` (`gpu_types.h` ⇄ `shaders/common/gpu_types.glsl`; deben coincidir byte a byte).
- `engine/renderer/{terrain,vegetation,water,atmosphere,environment,scene,mesh,gltf}.cpp`: subsistemas.
- `apps/common/scenes.cpp`: `build_valley_scene` (escena del vertical slice). `apps/common/runner.cpp`: bucle de frames.
- `shaders/`: GLSL 4.60 con `#extension GL_GOOGLE_include_directive : require`. Para añadir un shader, regístralo en `shaders/CMakeLists.txt`. Headers comunes en `shaders/common/` (`bindless.glsl`: `TEX2D(idx, SMP_*)`; `gpu_types.glsl`; `brdf.glsl`; `fog.glsl`; `atmosphere.glsl`).
- G-buffer: RT0 albedo + AO (sRGB), RT1 normal oct + roughness + clase (0 = lit, 1/3 = follaje, 2/3 = terreno), RT2 metal/translucencia/emisivo/mojado, RT3 velocidad, depth D32 reverse-Z (1 = cerca, 0 = infinito).
- El HDR que sale del lighting está **pre-expuesto** (multiplicado por `f.exposure.x`).
- Convenciones: metros, +Y arriba, −Z adelante, posiciones relativas a la cámara en GPU (`f.camera_pos` = posición de mundo).

## Cómo probar (lavapipe, GPU por software: lento pero correcto)
```sh
./build/linux-release/bin/tefra_bench --scene valley --frames 8 --warmup 0 --validation --capture out.png --out out.json
grep '"errors"' out.json                    # debe ser 0
```
Un frame del valle tarda ~4 s en lavapipe (más con otros agentes en paralelo). `--width 640 --height 360` para iterar rápido. Mira las capturas tú mismo (lee el PNG) y compáralas con el antes. Los tests: `ctest --preset linux-release`.

## Entregables
1. Código integrado en el motor (no un prototipo aparte), activable por un flag de `RenderSettings` si tiene coste.
2. `docs/research/<tu-tema>/REPORT.md`: qué técnica, fuentes (papers/charlas), decisiones, parámetros, coste estimado para una Intel HD 520 (tier Low), limitaciones y siguientes pasos.
3. Capturas antes/después en `docs/research/<tu-tema>/` (PNG, máximo 3, a 1280×720 o 960×540).
4. Commit en tu rama con un mensaje claro. Al final, responde con: rama, hash del commit, resumen de 5 líneas, y si la validación dio 0 errores.

Si algo no funciona, simplifícalo hasta que funcione bien antes que entregar algo roto. Un resultado pequeño y correcto vale más que uno ambicioso y roto.
