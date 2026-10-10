# Spec 1: render base (SP1 + SP2 → M1)

- Fecha: 2026-10-10. Estado: aprobado por autonomía (ver spec 0). Depende de `2026-10-10-tefra-vision-y-fundaciones.md`.
- Objetivo: el primer render PBR físicamente correcto. Render graph con barreras automáticas, recursos bindless, mallas por vertex pulling, deferred con G-buffer fino (ADR-008), sol con sombras en cascada, IBL desde HDRI, exposición física y tonemapping filmico.

## 1. Bindless (rhi)

- **Set 0 global** (update-after-bind, partially bound):
  - binding 0: `texture2D[]`, hasta 16384.
  - binding 1: `textureCube[]`, hasta 256.
  - binding 2: `sampler[]`, un set fijo de 8 samplers.
- Los **buffers** van por buffer device address (`GL_EXT_buffer_reference`). Los push constants (≤ 128 B) llevan direcciones de 64 bits e índices.
- **Storage images** de compute: set 1 por pase, asignado de un pool por frame con el formato explícito en GLSL (Intel no tiene `shaderStorageImageReadWithoutFormat`).
- Handles de textura: `u32` (índice). El índice 0 es una textura de error (magenta y negro) para detectar handles inválidos.

## 2. Render graph (`engine/rendergraph`)

- API de dos fases por frame: **setup** (declarar pases y recursos) y **execute**.
- Recursos:
  - Texturas transitorias por descriptor: se cachean entre frames por (formato, tamaño, uso), así que no se reserva memoria cada frame.
  - Importadas: salida/swapchain e historiales TAA.
- Usos declarados: `ColorWrite`, `DepthWrite`, `DepthRead`, `Sampled`, `StorageRead`, `StorageWrite`, `TransferSrc` y `TransferDst`. Con eso el grafo deduce layout, stage y access (sync2) e inserta las barreras mínimas.
- Orden de ejecución: el de declaración. El culling de pases que no contribuyen a la salida queda para M2.
- **Pases raster:** render pass + framebuffer imageless cacheados por clave.
- Cada pase abre un scope de GPU timer y un label de debug.
- **Aliasing de memoria:** se pospone a M2. Se mide primero cuánto ocupan los transitorios.

## 3. Geometría

- **Vertex pulling:** un buffer de vértices y uno de índices globales por escena, con BDA.
- Vértice "fat" de 32 B:
  - posición: `f32×3`
  - normal: octaédrica `snorm16×2`
  - tangente: octaédrica `snorm16×2` (el signo va en el bit bajo)
  - UV: `f32×2`
- Instancias: matriz 3×4 relativa a la cámara, que se calcula en CPU cada frame desde las posiciones `double`, más el índice de material.
- **Fuentes:**
  - Mallas procedurales (esfera, plano, caja) para los tests golden sin assets.
  - glTF 2.0 vía **cgltf** (MIT) con texturas PNG/JPG (stb_image).
- Mips generados en GPU con blit. La compresión BC llega en M3 (pak).

## 4. Materiales

- PBR metal-rough (glTF):
  - texturas: baseColor (sRGB), normal (tangent space) y ORM (R = occlusion, G = roughness, B = metalness)
  - factores: baseColor, roughness, metalness, emissive y alpha cutoff
- Tabla de materiales en un buffer GPU (BDA), indexada por instancia.
- **Specular anti-aliasing:** roughness ajustada con la varianza de la normal (Kaplanyan/Tokuyoshi) para que el follaje y el agua no "chispeen".

## 5. Frame (deferred)

1. **shadow_cascades**: depth only, 2 cascadas en Low y 4 en High. Ajuste estable a texel (sin shimmering) y reverse-Z.
2. **gbuffer**: depth D32F + RT0–RT3 (spec 0, ADR-008).
3. **lighting** (fullscreen):
   - sol: GGX + Smith correlado + Lambert/Burley, con PCF 3×3 de la cascada y blend entre cascadas
   - IBL: split-sum, con prefiltrado GGX en mips de un cubemap e irradiancia SH9
4. **sky**: cielo de fondo donde no hay geometría, usando el mismo entorno que el IBL.
5. **tonemap**: exposición física (EV100), **AgX** (Sobotka; aproximación polinómica de Wrensch) y salida sRGB. Dither por ruido de valor triangular.

Formato HDR intermedio: `B10G11R11_UFLOAT` (4 B/px) en Low y `R16G16B16A16_SFLOAT` en High. Se decide por tier.

## 6. IBL

- Entrada: HDRI equirectangular `.hdr` (stb_image), o un **cielo procedural** cuando no hay asset (golden tests).
- Precomputo en compute al cargar:
  - cubemap de 256² con mips
  - prefiltrado GGX (importance sampling filtrado por pdf)
  - irradiancia SH9
  - BRDF LUT de 128² (RG16F)
- Presupuesto en Low: < 50 ms una sola vez al cargar. No se recalcula por frame en M1; el cielo dinámico llega en M2.

## 7. Unidades físicas

- Sol: iluminancia en lux.
- Cielo/HDRI: escalado para que su luminancia media cuadre con la iluminancia elegida.
- Cámara: EV100 manual. Auto-exposición por histograma en M2.

## 8. Escenas de prueba

- `testpattern`: la de M0, se mantiene.
- `materials`: una rejilla de 7×5 esferas de 0,5 m (roughness de 0 a 1 por columna; filas: dieléctrico, metal, dieléctricos de colores) sobre un plano a cuadros, con sol + IBL procedural. **Golden.**
- `gltf`: carga un `.gltf` por argumento, normalmente un asset CC0 de Poly Haven bajado con `scripts/fetch_assets.py`. Solo capturas, no golden: los assets no viven en el repo.

## 9. Verificación

- **Golden** de `materials` en CI (lavapipe, validación + sync validation, 0 errores).
- Tests unitarios:
  - codificación octaédrica
  - SH9 de una constante
  - integral de la BRDF LUT en puntos conocidos (por ejemplo, 1−F0 en roughness 0)
  - ajuste de cascadas: cobertura del frustum
- Capturas revisadas a ojo: escala real (esferas de 0,5 m frente a cámara a 1,7 m), sin acné de sombras ni peter-panning visible, y metales con reflejos coherentes con el entorno.

## 10. Fuera de M1

GPU-driven culling, LOD, terreno, atmósfera física, niebla, TAA, GTAO, auto-exposición y clustered lights: todo eso es M2. El pak, BC y streaming son M3.
