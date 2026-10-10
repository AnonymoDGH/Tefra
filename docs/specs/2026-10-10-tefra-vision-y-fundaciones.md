# Spec 0 — Tefra: visión, decisiones globales y fundaciones (SP0 / M0)

- Fecha: 2026-10-10
- Estado: **aprobado por autonomía**. El 2026-10-10 el usuario concedió autonomía total ("sin preguntas, full automatización") y aprobó el vertical slice. Las decisiones restantes las tomó Claude con criterio y quedan **revisables**: el usuario corrige al ver resultados.
- Alcance de este spec: decisiones que afectan a todo el motor y diseño detallado de **M0**. Cada subproyecto posterior (SP1–SP8) tendrá su propio spec.

---

## 1. Visión

**Tefra** es un motor 3D open source, clean-room, orientado a **mundo abierto fotorrealista** con **rendimiento escalable por tiers**. "Hiperrealismo con rendimiento" se interpreta así:

1. **Realismo físico, no efectista.** Unidades físicas (metros, lux, cd/m², EV100), BRDF energéticamente conservadora, atmósfera física, exposición de cámara física y assets fotogramétricos CC0. El realismo sale de la coherencia física y de buenos datos, no de post-proceso.
2. **Presupuesto antes que efecto.** Toda característica declara su coste en ms por tier y tiene un nivel "off". Sin números del `bench`, no se afirma ninguna mejora.
3. **El tier Low es ciudadano de primera.** El PC del usuario (HD 520) debe verse bien y ser realista a 720p/30 fps. Las técnicas se eligen para que escalen hacia abajo con elegancia (menos resolución, menos muestras), no para que se desactiven.

Referencia estética: oscuro, crudo y realista. Bruma fría, troncos enormes, hierba alta, hojarasca, rocas con musgo, agua oscura y turbia. Prioridades del renderer en este orden: **atmósfera/niebla > iluminación día/noche > follaje > terreno > agua > clima > efectos brillantes**.

## 2. Vertical slice (aprobado)

**"Valle de Ceniza"**: un valle templado y brumoso de **2×2 km**. La arquitectura se dimensiona para 16×16 km.

| Elemento | Contenido |
|---|---|
| Terreno | Heightmap de 2×2 km a 0,5–1 m/texel. Valle con laderas, un río y un lago. Materiales por capas (tierra, hojarasca, roca con musgo, barro). |
| Bosque | Coníferas y caducifolios de tronco grueso. Sotobosque de helechos y hierba alta. Troncos caídos, rocas. Scatter procedural con reglas (pendiente, altura, humedad). |
| Agua | Río y lago oscuros y turbios: absorción y scattering, reflejos de pantalla con fallback a sonda, lluvia sobre la superficie. |
| Asentamiento | Estación forestal o aldea pequeña, con 5–10 edificios. |
| Vehículo | Un 4×4 con física de suspensión, conducible por un camino de tierra y campo a través. |
| Agentes | Más de 100: fauna en manadas (navmesh + steering) y algunos humanos en la aldea. |
| Ambiente | Ciclo día/noche continuo (sol, luna, estrellas, adaptación a la noche) y clima: despejado → nublado → lluvia con niebla volumétrica. Superficies mojadas. |
| Streaming | Por celdas, sin tirones, conduciendo a 80 km/h. |

Criterio global de éxito del slice: en el PC del usuario (tier Low) a **1280×720, p99 ≤ 40 ms**, con capturas que el usuario no califique de "plástico" ni "feo".

## 3. Decisiones globales (ADR)

Formato: decisión, por qué y qué se descartó.

### ADR-002 Lenguaje: C++20 con un subconjunto disciplinado
- **Decisión:** C++20 compilable con **GCC ≥ 13, MinGW-w64 GCC ≥ 13 y Clang ≥ 17**. En los targets del motor: sin excepciones (`-fno-exceptions`), sin RTTI, sin `<iostream>`, sin plantillas recursivas y sin C++ modules. Headers públicos ligeros, con forward declarations y pimpl en las fronteras de módulo.
- **Por qué:** las dependencias clave (Vulkan, VMA, Jolt, ImGui, meshoptimizer, miniaudio) son C/C++ nativas, el toolchain del usuario es MinGW y la experiencia del usuario es C. Rust (`ash`) es viable, pero añade FFI con todo y compila igual de lento en 2 núcleos.
- **Tiempos de build:** módulos pequeños, PCH por módulo, unity build opcional (`TEFRA_UNITY=ON`) y ccache detectado automáticamente. Objetivo medible: **rebuild incremental de un `.cpp` del motor < 10 s en el PC del usuario**.

### ADR-003 API gráfica: Vulkan 1.2 baseline, sin capa de abstracción gruesa
- **Decisión:** Vulkan **1.2** con `volk` (carga dinámica, sin enlazar `vulkan-1`), **VMA** (suballocation obligatoria: 4096 allocations en AMD) y Vulkan-Headers fijados a `vulkan-sdk-1.4.341.0`.
- **Baseline** (requerido):
  - `VK_KHR_synchronization2`, `VK_KHR_create_renderpass2`, imageless framebuffer, timeline semaphores.
  - Descriptor indexing (**bindless**), buffer device address, `drawIndirectCount`, `multiDrawIndirect`.
  - Compresión BC, `samplerAnisotropy`.
- **Rutas opcionales** detrás de capacidades: `VK_KHR_dynamic_rendering`, mesh shaders, ray query y VRS.
- **Límites que el diseño respeta:** push constants ≤ 128 B, ≤ 4 descriptor sets en uso (límite real: 8), sin fp16 en shaders y shared memory de compute ≤ 32 KB.
- **RHI:** capa delgada en C++ sobre Vulkan con tipos propios (handles de 32 bits con generación) para poder añadir D3D12 algún día sin reescribir el renderer. No se emula una API genérica: la RHI expone conceptos Vulkan 1.2 simplificados.
- **Descartado:** wgpu (no da control de bindless ni de memoria) y OpenGL.

### ADR-004 Shaders: GLSL → SPIR-V con glslang, permutaciones por `#define`
- **Decisión:** GLSL 4.60 con `GL_EXT_nonuniform_qualifier`, `GL_EXT_buffer_reference` y `GL_EXT_scalar_block_layout`. Se compila en build con `glslangValidator` (Vulkan SDK en Windows, `glslang-tools` en Linux) y se carga desde `shaders/` junto al ejecutable. Las permutaciones son pocas y explícitas (`#define`). El hot-reload (dev) invoca el mismo compilador como subproceso.
- **Por qué no Slang (todavía):** GLSL lo entiende cualquier herramienta (RenderDoc, validation, glslang en CI de Linux y Windows sin el SDK completo). Slang queda como opción futura si las permutaciones explotan.

### ADR-005 Plataformas: Windows (MinGW) y Linux desde el día 1
- **Decisión:** las dos en CI desde M0. Windows es la plataforma de medición (PC del usuario) y Linux la de desarrollo y CI con GPU por software (lavapipe).
- Windows: MinGW-w64 GCC (MSYS2 UCRT64 en CI) + Ninja. **MSVC y clang-cl no se requieren**, aunque no se rompen a propósito.
- Linux: GCC y Clang. X11 y Wayland vía GLFW.

### ADR-006 Licencia: Apache-2.0
- **Por qué:** permisiva y compatible con todas las dependencias previstas (MIT, BSD, zlib, Apache-2.0, CC0), y además incluye **concesión explícita de patentes**, relevante en gráficos. MIT no la tiene, y MPL-2.0 complica la integración en juegos comerciales de terceros.
- `THIRD_PARTY.md` lista cada dependencia con su versión, licencia y ruta.

### ADR-007 Herramientas: sin editor hasta después de 0.1
- **Decisión:** el contenido se crea con **reglas procedurales + datos en texto** (TOML/JSON) + **Blender como DCC** (exporta glTF). En el motor, overlay de depuración con **Dear ImGui** (inspección, tuning en vivo y perfilado) y herramientas CLI (`tefra_pack`, `tefra_bench`, `tefra_info`).
- **Por qué:** un editor de niveles es un proyecto entero. El slice se puede generar proceduralmente, y eso es más realista para un bosque (scatter por reglas) que colocarlo a mano.

### ADR-008 Arquitectura de render: **deferred con G-buffer fino + clustered forward para transparencias**
Análisis para el tier Low, que es el que restringe. La HD 520 tiene 24 EU (~0,4 TFLOPS) y memoria compartida DDR4 (17–34 GB/s según canales, a medir). La R5 M330 tiene ~0,6 TFLOPS y DDR3 de 64 bits (~14 GB/s).

| | A) Forward+ clustered | B) Deferred G-buffer fino | C) Visibility buffer |
|---|---|---|---|
| Ancho de banda | El más bajo | 16 B/px + depth: a 960×540 ≈ 8,3 MB por escritura, **~0,5 GB/s a 30 fps** (asumible) | Mínimo (4 B/px) |
| Overdraw de follaje | Necesita prepass. El sombreado caro solo se paga una vez con depth EQUAL | Necesita prepass para alpha-test. Las escrituras de overdraw cuestan 16 B | Ideal: cada capa de overdraw cuesta 4 B |
| Mojado por lluvia, decals, GTAO, SSR | Difícil (sin normales ni roughness en pantalla) | **Natural**: se modifica el G-buffer | Posible, más complejo |
| Variedad de materiales (follaje translúcido, agua) | Total | Limitada a un ID de clase (2 bits) | Uber-shader o clasificación por tiles |
| Complejidad | Media | **Media-baja**, la más probada en mundos abiertos | Alta (refetch de vértices, derivadas analíticas) |
| Riesgo en HD 520 | Shaders largos con muchas luces | Bajo | ALU alto en el resolve por píxel |

**Decisión: B.** Para el look objetivo, la clave son el **mojado** (lluvia), GTAO, SSR sobre charcos y agua, y la niebla aplicada a todo. Todo eso es natural en deferred. A 540p internos, el coste en ancho de banda no es el cuello de botella. Las transparencias, el agua y las partículas van en un **pase forward** que reutiliza los mismos clusters de luces y el mismo volumen de niebla. El visibility buffer queda **reservado** como ruta futura para High (follaje denso).

**Layout del G-buffer** (16 B/px + depth):

| Target | Formato | Contenido |
|---|---|---|
| depth | `D32_SFLOAT` | Reverse-Z, far infinito |
| RT0 | `RGBA8_SRGB` | baseColor.rgb, AO de material |
| RT1 | `A2B10G10R10_UNORM` | normal octaédrica (10+10), roughness (10), clase de material (2 bits: lit, follaje, terreno/mojable, sin iluminar) |
| RT2 | `RGBA8_UNORM` | metalness, translucidez/SSS, intensidad emisiva, mojado |
| RT3 | `R16G16_SFLOAT` | velocidad (TAA) |

**Resolución interna + TAAU:** en Low, 960×540 internos escalados a 1280×720 con TAA upsampling propio, que es imprescindible para estabilizar follaje. Más adelante se evalúa FSR 2 (MIT) como alternativa.

### ADR-009 Convenciones
- **Unidades:** 1 unidad = 1 m, tiempo en s, luz en unidades físicas (sol ≈ 100 000 lux, luna llena ≈ 0,25 lux). Exposición en EV100.
- **Ejes:** diestro, **+Y arriba**, −Z adelante (como glTF, para evitar conversiones).
- **Profundidad:** **reverse-Z** con far infinito, en `D32_SFLOAT`.
- **Mundo grande:** posiciones de mundo en `double` en CPU. La GPU recibe posiciones **relativas a la cámara** (float), de modo que la precisión no depende del tamaño del mundo.
- **Color:** trabajo lineal en Rec.709/sRGB primaries. Tonemapping filmico (AgX-like) con salida sRGB. HDR10 en el futuro.

### ADR-010 Dependencias (todas permisivas)

| Dependencia | Uso | Licencia | Cuándo |
|---|---|---|---|
| Vulkan-Headers | API | Apache-2.0 / MIT | M0 |
| volk | loader | MIT | M0 |
| VMA | memoria GPU | MIT | M0 |
| GLFW 3.4 | ventana, entrada, gamepad | zlib | M0 |
| stb_image_write / stb_image | PNG (captura), carga en tools | MIT / dominio público | M0 |
| doctest | tests | MIT | M0 |
| Dear ImGui | debug UI | MIT | M1 |
| cgltf | importador glTF (tool) | MIT | M1 |
| meshoptimizer | LOD, optimización de mallas | MIT | M2 |
| Basis Universal / KTX | compresión de texturas BC | Apache-2.0 | M3 |
| Jolt Physics | física y vehículos | MIT | M4 |
| miniaudio | audio | MIT-0 / dominio público | M6 |
| Tracy | perfilador | BSD-3 | opcional |

Se descartan: GLFW frente a SDL3 (SDL3 compila mucho más lento en 2 núcleos y GLFW cubre ventana y gamepad; el audio va con miniaudio). fastgltf frente a cgltf (fastgltf arrastra simdjson y solo lo usaría el importador offline). GLM se descarta en favor de math propia (plantillas pesadas que alargan el build).

Política: las librerías pequeñas van **vendored** en `third_party/` con la versión fijada. Las grandes (Jolt) se traen con `FetchContent` fijado a un tag y con caché.

## 4. Arquitectura de módulos

```
apps/      tefra_viewer, tefra_bench, tefra_info, (tefra_pack)
  │
engine/
  world/      celdas, streaming, HLOD                 (SP4)
  sim/        ECS, física, vehículo, anim, IA, audio  (SP5–SP7)
  renderer/   frame, materiales, pases, tiers         (SP2–SP3, SP6)
  rendergraph/ grafo de frame, barreras, aliasing     (SP1)
  rhi/        Vulkan: device, memoria, swapchain, comandos, pipelines, queries (SP0–SP1)
  platform/   ventana y entrada (GLFW)                (SP0)
  core/       log, assert, memoria, math, jobs, ficheros, tiempo, json, imagen (SP0)
```

Reglas:
- Dependencias solo hacia abajo. `core` no depende de nada y `rhi` no conoce el renderer.
- Ningún subsistema depende de ImGui. Solo existe un módulo `debugui` opcional.
- Cada módulo es una librería estática CMake con `include/tefra/<modulo>/` público y `src/` privado.

**Hilos:**
- Hilo principal: juego + construcción del frame.
- N−1 workers de jobs, con N = hilos HW. En el PC del usuario son 3 workers, así que todo debe escalar bien con 2–4 hilos.
- 1 hilo de E/S para streaming.

**Una sola cola gráfica en Low.** Async compute es opcional.

## 5. Tiers de calidad y presupuestos

El escalado se define con una **tabla por característica** (datos, no código duplicado). Cada característica tiene parámetros por tier.

| Característica | Low (HD 520) | Medium (GTX 1060) | High |
|---|---|---|---|
| Resolución interna → salida | 960×540 → 720p (TAAU) | 1080p nativo + TAA | 1440p + TAA |
| Sombras del sol | 2 cascadas de 1024², PCF 3×3 + contact shadows | 3 × 2048², PCSS ligero | 4 × 2048² |
| AO | GTAO a media resolución, 1 slice | GTAO a resolución completa | GTAO + bent normals |
| Niebla volumétrica | froxels 160×90×48 | 240×135×64 | 320×180×96 |
| Atmósfera | LUTs Hillaire (actualización amortizada) | igual, cada frame | + multi-scattering completo |
| Nubes | capa 2D/plana raymarch barato | volumétricas a 1/4 res | volumétricas a 1/2 res |
| GI | sonda de cielo + AO, sin GI dinámico | probes irradiance (DDGI-like) | probes + SSR |
| Vegetación | densidad 40 %, impostores > 150 m | 70 %, > 300 m | 100 %, > 500 m |
| Agua | SSR + sonda, absorción | + refracción | + cáusticas |

**Presupuesto GPU Low (hipótesis, 33,3 ms a 30 fps):**

| Pase | ms |
|---|---|
| prepass de profundidad (follaje + terreno) | 3,0 |
| G-buffer | 7,0 |
| sombras | 4,0 |
| GTAO | 1,5 |
| iluminación deferred | 3,0 |
| atmósfera | 0,5 |
| niebla volumétrica | 2,0 |
| forward (transparentes, agua, partículas) | 3,0 |
| TAAU + post | 2,5 |
| UI | 0,5 |
| **total** | **27** |
| margen | 6 |

Se revisa con el primer `bench` real.

**Otros presupuestos de Low:**
- **CPU:** hilo principal ≤ 12 ms.
- **Memoria:** proceso ≤ 2,5 GB de RAM; GPU ≤ 1 GB en la HD 520 (memoria compartida) y ≤ 1,5 GB en la R5 M330.
- **Streaming:** p99 ≤ 40 ms y ningún frame > 50 ms conduciendo a 80 km/h.

**Medium y High:** 60 fps (16,6 ms) en su resolución. Ultra se reserva para RT.

## 6. Verificación y calidad

1. **Validation layers** siempre activas en Debug, con un contador de errores y warnings vía `VK_EXT_debug_utils`. **Cualquier error de validación hace fallar los tests** (exit code ≠ 0).
2. **Golden images.** Cada test visual renderiza headless con lavapipe a PNG y lo compara con `tests/golden/*.png` (métrica: RMSE por canal y % de píxeles con diferencia > umbral). Las imágenes de referencia se regeneran con `--update-golden` y el cambio se revisa visualmente.
3. **Perfil baseline en CI.** Lavapipe expone Vulkan 1.4, así que el motor **solo habilita** features y extensiones del baseline (ADR-003) y pide `apiVersion` 1.2, y la validación detecta cualquier uso fuera de lo habilitado. Cuando el usuario aporte `vulkaninfoSDK --json` de sus GPU, se añadirá el Vulkan Profiles layer para emular también sus límites.
4. **Sanitizers.** ASan + UBSan en un job de Linux.
5. **Tests unitarios** (doctest) para core, math, jobs y, más adelante, streaming. **Tests de integración GPU** con la etiqueta `gpu` en CTest.
6. **Bench.** `tefra_bench` escribe JSON (schema `tefra-bench/1`, §8.4). Los números de rendimiento **solo** valen si vienen del PC del usuario. Los de lavapipe se marcan `"software": true` y no cuentan.
7. **Capturas.** Todo cambio visual se acompaña de capturas PNG, revisando la escala real con cámara a nivel de suelo (1,7 m).

## 7. Hitos (revisados)

| Hito | Contenido | Criterio de salida medible |
|---|---|---|
| **M0** | build reproducible, core, RHI mínimo, ventana, captura headless, `bench` JSON, CI | CI verde (Linux gcc+clang+ASan, Windows MinGW); 0 errores de validación; golden del test pattern; `bench` produce JSON válido en el PC del usuario |
| M1 | render graph mínimo, glTF + PBR + IBL + CSM, ImGui | golden de escenas de prueba; esfera de materiales correcta frente a referencia; validación 0 |
| M2 | GPU-driven (instancing, culling frustum + Hi-Z, MDI), LOD, terreno, **atmósfera + niebla básica**, HDR/TAAU/exposición | 100k instancias de vegetación; captura del valle con bruma; bench Low ≤ 33 ms en escena de prueba |
| M3 | formato pak, pipeline de assets, streaming por celdas | p99 ≤ 40 ms en recorrido grabado; 0 frames > 50 ms |
| M4 | física (Jolt), 4×4, cámara, entrada (teclado + gamepad) | conducir el recorrido completo; test de determinismo |
| M5 | día/noche, clima (lluvia, mojado, niebla volumétrica), agua | capturas a 4 horas del día × 3 climas, aprobadas por el usuario |
| M6 | animación, multitud de más de 100 agentes, IA, audio espacial | 100+ agentes dentro del presupuesto de CPU |
| M7 | herramientas mínimas, documentación, ejemplos, release 0.1 | README, guía de build, slice jugable |

Diferencias con el brief original: niebla y atmósfera básicas en **M2** (validan el look pronto), y el formato de assets se diseña junto con M2–M3.

## 8. Diseño detallado de M0

### 8.1 Estructura del repo

```
CMakeLists.txt  CMakePresets.json  LICENSE  README.md  THIRD_PARTY.md  CLAUDE.md
cmake/          toolchains (mingw-cross), utilidades (shaders, warnings)
third_party/    vulkan-headers, volk, vma, glfw, stb, doctest
engine/core/    include/tefra/core/*.h, src/*.cpp
engine/platform/
engine/rhi/
shaders/        *.vert/*.frag/*.comp + common/*.glsl
apps/           viewer/, bench/, info/
tests/          unit/, gpu/, golden/
docs/           specs/, plans/, adr/ (si un ADR crece)
.github/workflows/ci.yml
scripts/        setup-cloud.sh, compare_png.py
```

### 8.2 core (API mínima)
- `log`: niveles, salida a stderr y a archivo, formato printf con chequeo de formato. Sin iostreams.
- `assert`: `TEFRA_ASSERT`, `TEFRA_VERIFY` y `TEFRA_PANIC` con mensaje y ubicación.
- `memory`: `LinearArena` (por frame) y `PoolAllocator`. Seguimiento de bytes por etiqueta para el bench.
- `math`: `vec2/3/4`, `mat3/4`, `quat`, `dvec3`, perspectiva reverse-Z infinita y `look_at`. Header-only y simple, con tests de cada operación.
- `jobs`: pool de workers con cola global y `parallel_for` y `TaskGroup` con espera cooperativa (el hilo que espera también trabaja). Work-stealing real queda para cuando el perfilado lo pida.
- `time`: reloj monotónico de alta resolución.
- `fs`: leer y escribir archivo completo, ruta del ejecutable.
- `json`: writer en streaming (sin DOM).
- `image`: escritura de PNG (stb) y comparación de imágenes para golden.

### 8.3 rhi (alcance M0)
- Instancia con validación opcional y debug messenger que cuenta errores.
- Selección de dispositivo por puntuación. Override con `--gpu <índice>` o `TEFRA_GPU` (el usuario tiene 2 GPU y queremos medir las dos).
- Informe de capacidades: features y límites del baseline. El dispositivo se rechaza con un mensaje claro si falta algo requerido.
- VMA con `VK_EXT_memory_budget` si existe.
- Frames en vuelo (2) con timeline semaphore.
- Swapchain (ventana) o **target headless** (imagen offscreen + readback).
- Render pass vía `vkCreateRenderPass2` y framebuffers imageless.
- Pipelines gráficos desde SPIR-V.
- Timestamps de GPU por pase con nombre.

### 8.4 Bench JSON `tefra-bench/1`
```json
{
  "schema": "tefra-bench/1",
  "engine": {"version": "0.0.1", "git": "<sha>", "build": "Release", "compiler": "GCC 15.2"},
  "system": {"os": "Windows 10", "cpu": "...", "threads": 4, "ram_mb": 8096},
  "device": {"name": "...", "vendor_id": 32902, "driver": "...", "api": "1.2.203", "type": "integrated", "software": false},
  "config": {"scene": "testpattern", "tier": "low", "width": 1280, "height": 720, "frames": 600, "warmup": 60, "present": "fifo|immediate|headless"},
  "frame_ms": {"mean": 0, "p50": 0, "p95": 0, "p99": 0, "max": 0},
  "cpu_ms":   {"mean": 0, "p50": 0, "p95": 0, "p99": 0, "max": 0},
  "gpu_ms":   {"total": {"mean": 0, "p95": 0, "p99": 0}, "passes": {"<nombre>": {"mean": 0, "p95": 0, "p99": 0}}},
  "memory": {"cpu_rss_mb": 0, "gpu_used_mb": 0, "gpu_budget_mb": 0},
  "draw_calls": 0,
  "validation": {"enabled": false, "errors": 0, "warnings": 0}
}
```

### 8.5 Escena de M0: test pattern físico
Un pase fullscreen que dibuja un cielo analítico (gradiente de Rayleigh aproximado + disco solar) sobre un suelo a cuadros de 1 m, con cámara a 1,7 m. Sirve para validar el pipeline completo (shader → pipeline → render pass → readback → PNG → golden) y la convención de ejes y escala desde el primer día. Es un placeholder: la atmósfera real llega en M2.

### 8.6 Build y CI
- Presets:
  - `linux-debug`, `linux-release` y `linux-asan` (Ninja, GCC/Clang).
  - `windows-mingw-debug` y `windows-mingw-release` (MinGW + Ninja).
  - `cross-mingw-release` (Linux → Windows, toolchain `cmake/toolchains/mingw-w64.cmake`).
- CI (GitHub Actions):
  1. `linux-gcc` Debug con validación + lavapipe: unit + gpu + golden, y sube los PNG como artifact.
  2. `linux-clang` Release.
  3. `linux-asan`.
  4. `windows-mingw` (MSYS2 UCRT64): build + tests unitarios. Sin GPU, los tests `gpu` se saltan.
- Mínimo CMake 3.25 (el usuario tiene 4.0.2 y la nube 3.28).

## 9. Riesgos principales

| Riesgo | Mitigación |
|---|---|
| El Low no llega a 30 fps con follaje denso | TAAU desde 540p, densidad por tier, impostores pronto, medir en M2 |
| Lavapipe difiere del hardware real | Lavapipe solo valida corrección; el rendimiento y los bugs de driver se cazan con el `bench` del usuario. Probar Intel **y** AMD |
| Assets CC0 insuficientes para el bosque | Poly Haven (accesible desde la nube) + generación procedural de árboles (L-system/space colonization) como plan B |
| Build lento en 2 núcleos | ADR-002, unity build, ccache, deps precompiladas en caché |
| Alcance | El slice manda: lo que no sirva al slice se pospone |

## 10. Fuera de alcance (hasta 0.1)

Editor de niveles, red/multijugador, consolas, D3D12, ray tracing, scripting, localización, mods.
