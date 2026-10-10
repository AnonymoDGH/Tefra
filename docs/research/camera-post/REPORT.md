# Post-proceso fotográfico (cámara real)

Tema: bloom físico, auto-exposición y efectos de lente/película antes del tonemap (AgX). Todo implementado en el
motor, activable por `RenderSettings`. Validación Vulkan: 0 errores (lavapipe, `--validation`). `ctest`: unit y gpu pasan.

## Qué hay

| Flag (`RenderSettings`) | Por defecto | Pases añadidos | Qué hace |
|---|---|---|---|
| `bloom` / `bloom_intensity` | on / 0,04 | 6 down + 5 up (raster) | Dual filter sin umbral; mezcla `lerp(hdr, bloom, 0,04)` en el tonemap |
| `auto_exposure` | off | 6 reducciones (raster, R16F) + 1 compute | Log-promedio → compensación EV → adaptación temporal → buffer GPU por slot |
| `film_effects` | on | ninguno (dentro de `tonemap.frag`) | Viñeta cos⁴, aberración cromática lateral ≤ 1 px en esquinas, grano por luminancia animado |

CLI (bench/viewer): `--no-bloom`, `--auto-exposure`, `--no-film`.

Archivos: `shaders/bloom_down.frag`, `shaders/bloom_up.frag`, `shaders/exposure_first.frag`,
`shaders/exposure_reduce.frag`, `shaders/exposure_adapt.comp`, `shaders/tonemap.frag` (reescrito),
`engine/renderer/src/renderer.cpp` (`PostPush`, `ExposureState`, pases), `engine/renderer/include/tefra/renderer/renderer.h`.

### 1. Bloom

- **Downsample**: 13 taps de Jimenez (SIGGRAPH 2014). Cinco grupos 2×2 (centro 0,5; cuatro esquinas 0,125 cada uno)
  suman 1, así que la energía media se conserva. Solo en el primer nivel se aplica el promedio Karis
  (peso `1/(1+luma)` por grupo) para amortiguar píxeles aislados muy brillantes. Ver "Desviaciones".
- **Upsample**: tent 3×3 del nivel más grueso, mezclado 50/50 con el downsample del mismo nivel
  (`up_i = ½·down_i + ½·tent(up_{i+1})`). Una imagen uniforme sigue uniforme en todos los niveles.
- **Mezcla**: `c = lerp(hdr, bloom, 0,04)` antes de AgX. Con `lerp` (y no suma) la energía media no cambia.
- **Niveles**: 6 (de 1/2 a 1/64 de la resolución de salida). Textura HDR (`B10G11R11`), transitoria del grafo.

### 2. Auto-exposición

- **Medida**: el HDR resuelto (salida, después de TAA) se reduce a 1×1 con `exposure_first` (16 taps puntuales,
  media de log2 de luminancia por bloque 4×4) y después `exposure_reduce` (4 taps bilineales en los límites
  interiores, que promedian exactamente un bloque 4×4). Para 1280×720: 320×180 → 80×45 → 20×12 → 5×3 → 2×1 → 1×1.
- **Objetivo**: `ev_obj = log2(0,18) − mean_log2`, con `0,18` la clave de Reinhard et al. 2002 (log-promedio).
  Se limita a ±2 EV.
- **Adaptación**: `exposure_adapt.comp` (un hilo) hace un paso exponencial hacia el objetivo. Tau 0,4 s si la escena
  se volvió más brillante (la compensación baja rápido) y 1,2 s si se volvió más oscura. Se expresa en frames a
  60 Hz nominal (ver limitaciones).
- **Estado**: `ExposureState {ev, last_frame, valid, pad}`, 16 B, un buffer **por slot de frames en vuelo** (así lo
  fenza el grafo igual que a los demás). Primer uso del slot: `valid = 0` y se fija el valor medido sin transición.
- **Uso**: el tonemap multiplica HDR y bloom por `2^ev`. Barrera compute→fragment dentro del frame.

### 3. Efectos de película (`tonemap.frag`)

- **Viñeta**: `cos⁴θ` del objetivo de cámara leído de la proyección (`1/(1+|tan|²)²`), mezclado al 35 %
  (`kVignette`). Aplicada en lineal, antes de AgX.
- **Aberración cromática**: R y B muestrean desplazados radialmente, `shift = dir · px · r²`, con `px = 1` en las
  esquinas (`r` normalizado). Cero en el centro. Modelo lateral simple, no datos de lente.
- **Grano**: ruido triangular [−1, 1] por píxel y frame (semilla `frame_index`). Amplitud relativa 0,05 en sombras y
  0,015 en altas luces, multiplicada por `sqrt(c + 0,02)`, sobre el valor ya en AgX.

## Parámetros (valores fijados)

- Clave auto-exposición 0,18; límite ±2 EV; tau 0,4 s / 1,2 s (a 60 Hz nominal: 24 / 72 frames).
- Bloom: 6 niveles, `bloom_intensity` 0,04, mezcla 50/50 por nivel.
- Viñeta: `kVignette` 0,35. CA: `kCaPixels` 1,0. Grano: 0,05 sombras / 0,015 luces.
- Push de tonemap: 32 B (`PostPush`, verificado con `static_assert`); push de adaptación: 32 B.

## Capturas (valle, 960×540, 8 frames, lavapipe)

- `valley_before.png`: motor sin estas funciones (original).
- `valley_after.png`: por defecto (bloom + film, auto-exposición off).
- `valley_after_auto_exposure.png`: los tres activos.

Medidas: `valley_before` → `valley_after`: luminancia p50 0,361 → 0,329 (−9 %). La caída viene
de la viñeta (la media de bloom es neutra y el grano tiene media cero). Bloom solo, sin viñeta:
media 0,3788 → 0,3788 (energía conservada). El `diff` del bloom está en los bordes de cielo brillante y las copas.
Auto-exposición en el valle: la compensación converge a **+1,42 EV** (comprobado leyendo el buffer de estado de cada
slot; estable entre slots). Sube la luminancia p50 de 0,36 a 0,57 (+58 %), medido con bloom y sin film.

## Decisiones

- **Auto-exposición apagada por defecto.** La escena del valle fija `ev100 = 12,5`, y su log-promedio está unos
  1,4 EV por debajo de la clave 0,18. Encenderla da un valle más brillante y aéreo (+1,42 EV). Es un
  cambio de look que no conviene aplicar sin decisión del equipo. El valle se puede ver con `--auto-exposure`.
- **Bloom y film encendidos por defecto.** Son cambios de look pequeños (ver medidas) y la mezcla de bloom conserva
  energía.
- **Test de materials**: el golden `materials.png` se generó sin post. El test desactiva `no_bloom` y `no_film`, así el
  golden no cambia. Nuevo test `valley: bloom, auto exposure and film effects ...`: solo valida (0 errores), sin golden.
- **Estado por slot** y no un buffer único: el orden entre submits en la misma cola no está garantizado sin
  semáforos. Por slot el grafo ya fenza el uso.
- **Tiempo en frames y no en segundos reales**: `render()` recibe el tiempo de animación (`frame/60` solo con
  `--animate`), así que la adaptación usa frames a 60 Hz nominal, constante `kNominalFps`.

## Desviaciones frente al brief

- **Karis en el primer nivel** del downsample: es un promedio ponderado, así que no conserva la energía de píxeles
  aislados. Es lo que recomienda Jimenez y solo afecta al primer nivel. Para estricta conservación basta con
  `karis = 0` en `renderer.cpp`.
- **Exposición por reducción de textura**, no por histograma ni compute sobre texturas: el grafo no tiene acceso de
  escritura a imagen para compute, así que se usan pases raster a 1×1 como permite el brief.

## Coste estimado (Intel HD 520, tier Low)

**No medido en el hardware objetivo.** Los números de lavapipe no sirven para rendimiento. Estimación por ancho de
banda (34 GB/s compartidos) para 720p de salida:

- Bloom: ~11 pases, lecturas de 13 taps sobre mips de 1/2 a 1/64. Estimación **0,5–1 ms**.
- Auto-exposición: 6 pases pequeños (el primero: 16 taps en 1/4 de resolución) y un hilo de adaptación. Estimación
  **< 0,1 ms**.
- Film: 3 lecturas extra de HDR (solo con CA) y ~30 instrucciones por píxel en `tonemap.frag`. Estimación **< 0,1 ms**.
- Memoria: mips de bloom en HDR (~1 MB a 720p) y 16 B por slot para la exposición.

Confirmar con `tefra_bench --tier low --out ...` en el PC del usuario (`pass_ms` por pase).

## Limitaciones

- La adaptación usa frames a 60 Hz nominal. A 30 fps reales tarda el doble en segundos reales.
- El bloom con 4 % tiene poco efecto visible en el valle, porque el sol no está en cuadro. Se notará con sol o
  cielo brillante, o con fuentes emisivas.
- CA y viñeta son modelos ópticos simples, no calibrados con lente real.
- El grano es ruido por píxel: no tiene estructura de película. Un blur de 1 px o una textura de grano sería el
  siguiente paso si se quiere un aspecto más orgánico.
- La reducción de exposición usa la clave media logarítmica. Escenas con mucho cielo pueden sesgar la medida.

## Bug encontrado y arreglado (aprendizaje)

Con `auto_exposure` apagado, el tonemap leía `pc.exposure.ev` mediante un ternario. glslang lo emite como
`OpSelect` (ambos brazos evaluados), y lavapipe hacía un load con dirección 0 y fallaba con SIGSEGV en la JIT.
Corregido con dos medidas: la dirección del buffer es **siempre válida** (el buffer existe por slot) y la lectura
va dentro de un `if`. Regla para el equipo: un puntero de BDA nunca debe ser 0 si el shader puede leerlo en un
ternario o en una expresión que no se ejecuta.

## Fuentes

- Jimenez, "Next Generation Post Processing in Call of Duty: Advanced Warfare", SIGGRAPH 2014 (13 taps, tent
  upsample, promedio Karis).
- Reinhard, Stark, Shirley, Ferwerda, "Photographic Tone Reproduction for Digital Images", SIGGRAPH 2002
  (log-promedio de luminancia y clave ~0,18).
- Cos⁴ de iluminación de lente: óptica geométrica estándar (no requiere fuente de código).
- Todo el código es propio, escrito a partir de estas técnicas. No se copió código de terceros ni de juegos
  comerciales (ADR-001).

## Siguientes pasos

1. Medir `pass_ms` del bloom y la exposición en el PC del usuario (tier Low) y ajustar mips si pesa.
2. Decidir la clave de exposición del valle (o bajar `ev100` 1,4 EV) y así activar auto-exposición por defecto.
3. Escena con sol visible para validar el bloom visualmente; posible calibración de intensidad (0,04 → 0,03–0,06).
4. Grano con estructura (textura de grano animada) y CA con datos de lente si se quiere más realismo.
