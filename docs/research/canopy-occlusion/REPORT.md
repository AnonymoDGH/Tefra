# Oclusión del cielo bajo el dosel (canopy occlusion map)

Rama: `worktree-agent-a139afe2832d105be`. Flag: `RenderSettings::canopy_occlusion` (por defecto `false`; la escena
`valley` lo activa; `tefra_bench --no-canopy` lo desactiva).

## Problema

Bajo un bosque denso la luz ambiente del cielo (irradiancia SH + especular IBL) cae un 70-90 %, pero el motor la
aplicaba entera a suelo y troncos. GTAO solo ve la profundidad de pantalla, así que no sabe que hay copas encima de
un píxel que está fuera de su vecindad.

## Técnica elegida: mapa de cobertura de dosel calculado en CPU

Se descartó el pase cenital en GPU (render ortográfico de la vegetación desde arriba) por coste: duplicaría el
dibujado de vegetación en la tier Low (HD 520), y la vegetación es estática. Se usa la vía "más simple" del brief:

1. **Fuente**: cada conífera tiene una huella de copa (`VegetationPrototype::canopy_*`). Las instancias de
   `Vegetation` se recorren en CPU (`Vegetation::collect_canopy`).
2. **Mapa**: 512×512 texels de 1 m (512 m) centrados en la cámara, con esquina mínima en metros enteros, de modo que
   la rejilla de texels es fija en el mundo y no hay parpadeo al reconstruir.
   - Canal R: cobertura `c = 1 - exp(-τ)`, con `τ` = suma de densidad óptica de cada copa (disco de borde suave de 1 m).
     Se desenfoca con gaussiana separable (σ = 2 texels, radio 6), así las copas solapadas suman en τ.
   - Canal G: altura de la copa más alta (máximo dilatado 3 texels), codificada como `h = -100 + g·800` m.
3. **Subida**: una imagen RG16_UNORM por slot de frames en vuelo (2). Solo se re-sube al reconstruir; `sync()`
   graba la copia staging→imagen con barreras de imagen en el command buffer del slot.
4. **Sombreado** (`shaders/lighting.frag`, `canopy_visibility`): para un píxel a altura `y`,
   `above = smoothstep(0, 10 m, top - y)` (las copas tapan todo lo que queda por debajo de su cima; los 10 m superiores
   de la copa se atenúan), y `vis = 1 - strength · fade · c · above`. Se multiplica la irradiancia SH y la especular IBL.
   El sol no se toca (ya lo cubren las cascadas de sombra). El fade del 10 % exterior evita ver el borde de la ventana.

Fuentes: no se consultó ningún paper ni charla en esta sesión. La formulación es de primeros principios (cobertura
Beer-Lambert, vertical) y la sugerencia del propio brief.

## Parámetros

| Parámetro | Valor | Dónde |
|---|---|---|
| Resolución / texel | 512×512, 1 m | `kCanopyRes`, `kCanopyTexelSize` (canopy.h) |
| Reconstrucción | al moverse la cámara 64 m (margen ≥ 192 m) | `kRebuildDistance` (canopy.cpp) |
| Desenfoque cobertura | gaussiana σ = 2 texels, radio 6 | canopy.cpp |
| Dilatación altura | máximo, radio 3 texels | canopy.cpp |
| Codificación altura | base -100 m, rango 800 m | `kCanopyYBase/Range` (debe coincidir con lighting.frag) |
| Huella de copa | 0.27 · (altura - base de copa) (anchura del verticilo inferior) | `make_conifer` |
| Cima de copa | 0.98 · altura del árbol | scenes.cpp |
| Densidad óptica | 1.4 por copa (≈75 % de cobertura en el centro) | scenes.cpp |
| Rampa de cima | 10 m | lighting.frag |
| Fuerza | 0.85 (fracción de luz ambiente retirada bajo cobertura total) | `RenderSettings::canopy_strength` |

## Resultados (valley, suelo, 960×540, 8 frames, validación)

Capturas: `valley_ground_before.png` (sin el pase) y `valley_ground_after.png` (con el pase).

Validación: `errors: 0`, `warnings: 0` (con validation layers, lavapipe). `ctest --preset linux-release`: 2/2
(unit y gpu, incluido el golden). El build compila con `-Werror`.

Luminancia media (antes → después, incluye niebla; ratio después/antes):

| Región | Antes | Después | Ratio |
|---|---|---|---|
| Suelo cercano | 61.0 | 49.5 | 0.81 |
| Suelo medio | 76.8 | 67.1 | 0.87 |
| Base del tronco | 105.0 | 99.2 | 0.95 |
| Copas / cielo | 109.0 | 101.9 | 0.94 |

Lectura de los números: la escena es casi toda luz ambiente (con el término ambiental a 0 el suelo baja a 0,41 de su
brillo, así que el ambiente es ~59 % del suelo). La cobertura medida en la ventana es 0,74 de media y 0,79 en la
cámara, así que el mapa es correcto. Pero la cámara del valle está en un claro junto al río (no hay árboles a
menos de 11 m del río en el generador), y ahí la cobertura local en el encuadre es ~0,4-0,6. Eso explica que el
ambiente baje un 32 % en el suelo cercano, un 42 % en el medio y un 54 % en la base del tronco. En bosque denso
(cobertura ~0,8) el teórico es 0,85 × 0,8 ≈ 68 % menos de ambiente, dentro del rango 70-90 % del brief, pero **no
está medido en una vista de bosque denso**: el bench no permite mover la cámara. Pendiente.

Las copas bajan ~6 % en la zona del cielo. Es la autosombra de la copa: las agujas de arriba tapan a las de abajo.
Es intencionado, aunque quizá algo fuerte; la rampa de 10 m es un parámetro a ajustar con capturas en bosque denso.

## Coste estimado

- **GPU**: una muestra bilineal y ~20 ALU por píxel iluminado. Estimado < 0,1 ms en HD 520 a 720p. No medido en
  hardware real.
- **CPU**: la reconstrucción cuesta ~6,5 ms en este Xeon compartido (medido con otros agentes en paralelo; en reposo y en el i5-6200U no está medido). Ocurre una vez cada 64 m recorridos, no por frame. Antes de reescribir el desenfoque (filas con padding, sin clamp por muestra) el coste era ~13 ms; el desglose previo era recogida 1-1,4 ms, splat 3,2-3,7 ms, desenfoque 6,4 ms, codificación 2,9 ms.
- **Subida**: 512 KB de staging y una copia por slot tras cada reconstrucción.
- **Memoria**: 2 imágenes de 1 MB + 2 staging de 1 MB (frames en vuelo = 2) + ~3,5 MB de CPU de trabajo (tau, top, tmp de 1 MB cada una, más el mapa de 512 KB).

## Limitaciones

- Solo cobertura vertical: no modela el cielo que entra lateralmente (horizonte). Bajo un tronco el valor es
  razonable; en claros o bordes del bosque la oclusión puede estar algo sobrestimada.
- Solo coníferas dan dosel. El sotobosque (helechos, hierba) no entra: su altura es pequeña.
- La niebla ambiente no se atenúa (opcional del brief, no hecho). La niebla es una parte no despreciable del suelo
  lejano.
- Vegetación estática: el mapa se reconstruye al cambiar el puntero `Vegetation`, no al modificar instancias in situ.
- Fuera de la ventana de 512 m no hay oclusión: los árboles a más de ~256 m de la cámara no se ven en el mapa.
  Un punto cercano bajo un árbol lejano puede quedar sin oclusión; aceptable con la niebla actual, no es general.
- Precisión: la altura se codifica en 16 bits sobre 800 m (~1,2 cm por paso). Requiere `R16G16_UNORM` muestreable
  con filtro lineal y transfer destino; si el dispositivo no lo tiene, el pase queda desactivado
  (`CanopyMap::supported()`).

## Siguientes pasos

1. Vista de bosque denso (cámara controlable en el bench) para medir la atenuación real frente al 70-90 %.
2. Atenuar también la niebla ambiental con el mismo factor (in-scatter bajo el dosel).
3. Si hace falta cobertura direccional: pase cenital en GPU con la profundidad de la copa (solo para High).
4. Sincronizar la reconstrucción en un hilo de trabajo y repartirla en varios frames si el coste en la i5 supera
   lo aceptable.

## Archivos

- `engine/renderer/include/tefra/renderer/canopy.h`, `engine/renderer/src/canopy.cpp` (mapa, subida, sync)
- `engine/renderer/include/tefra/renderer/vegetation.h`, `engine/renderer/src/vegetation.cpp` (huella de copa, `collect_canopy`)
- `engine/renderer/include/tefra/renderer/renderer.h`, `engine/renderer/src/renderer.cpp` (ajustes, integración)
- `engine/renderer/include/tefra/renderer/gpu_types.h`, `shaders/common/gpu_types.glsl` (campos `canopy_map`, `canopy`)
- `shaders/lighting.frag` (`canopy_visibility`)
- `apps/common/scenes.cpp` (huellas de coníferas, activación en valley), `apps/common/options.cpp|h`, `runner.cpp|h` (`--no-canopy`)
