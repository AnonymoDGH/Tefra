# Luz volumétrica con sombras del sol (god rays)

Tema: niebla volumétrica ray-marched a media resolución, con visibilidad solar del atlas de cascadas.
Escena de prueba: `valley` (vertical slice, sol a 11,5° de elevación).

## Qué se implementa

- **Pase `volumetric`** (`shaders/volumetric.frag`), a media resolución del render interno, RGBA16F.
  - rgb = in-scatter pre-expuesto (multiplicado por `exposure.x`), a = transmitancia hasta min(profundidad, 150 m).
  - Para el cielo el rayo es infinito, pero el marcado se limita igualmente a 150 m.
- **16 pasos** uniformes sobre el rayo. La muestra de cada paso tiene jitter por píxel (IGN) y por frame (`exposure.w` × proporción áurea); el TAA integra el ruido.
- **Densidad**: la misma fórmula que `f.fog` en `shaders/common/fog.glsl`: `dens(h) = fog.x · exp(-fog.y · (h - fog.z))`, albedo 1.
- **Integración por paso**: la transmitancia desde la cámara es la integral exacta de la densidad exponencial. El in-scatter de cada paso es `L(x_i) · (T_i - T_i+1)`, donde `L = ambiente SH + sol · HG(g = fog.w) · visibilidad`. Así, con visibilidad 1, el resultado coincide con la niebla analítica.
- **Visibilidad solar**: `sun_shadow` de `shaders/common/shadow.glsl` con un único tap de comparación hardware (`pcf = false`). La lógica de cascadas se movió desde `lighting.frag` a ese header; lighting sigue usando 9 taps.
- **Composición** en `lighting.frag`: upsample bilateral 2×2 (pesos por profundidad lineal) y, cuando está activo, `color·exp·T + Iv + L·(T_vol - T)`, que sustituye a `apply_fog`. Incluye el cielo. El término `L·(T_vol - T)` cubre el tramo que queda más allá de 150 m con el fog uniforme.
- **Flag**: `RenderSettings::volumetric_fog` (por defecto `false`; `build_valley_scene` lo pone a `true`). No tiene efecto si `fog_density = 0`.
- **CLI** (bench/viewer): `--no-volumetric`, `--yaw DEG`, `--pitch DEG` (para orientar la cámara hacia el sol).

Fuentes: Wronski, "Volumetric Fog: Unified, Compute Shader Based Solution to Atmospheric Scattering" (SIGGRAPH 2014, Advances in Real-Time Rendering). Hillaire, "Physically Based and Unified Volumetric Rendering in Frostbite" (SIGGRAPH 2015, Advances in Real-Time Rendering). Henyey y Greenstein (1941) para la fase. El código está escrito desde cero; no se copió código de ninguna fuente.

## Parámetros

| Parámetro | Valor |
|---|---|
| Resolución del pase | media del render interno (960×540 → 480×270 en Low) |
| Alcance | min(profundidad, 150 m) |
| Pasos | 16 (≈9,4 m por paso sobre 150 m) |
| Formato | RGBA16F (~1 MB a 960×540) |
| Visibilidad solar | 1 tap por cascada, con fundido entre cascadas |
| Fase | Henyey-Greenstein, g = 0,6 (`fog_anisotropy`) |
| Escena valley | fog 0,008 /m a y = altura del terreno, falloff 0,035 /m, 3 cascadas, distancia de sombra 200 m |

## Validación

- `tefra_bench --scene valley --validation`: **0 errores** en las tres vistas (sol de frente, pitch 6° y 14°) y en la vista por defecto. La sync validation está activa (`engine/rhi/src/device.cpp`).
- `ctest --preset linux-release`: 2/2 pasan (unit, gpu con goldens de materials y testpattern).
- `materials_sky` (con niebla, ruta analítica): la salida es **idéntica byte a byte** a la del commit anterior. La refactorización del header de sombras y de la niebla no cambia la ruta existente.
- Prueba de integración: con la visibilidad forzada a 1, el pase volumétrico reproduce el tono y la densidad de la niebla analítica. La integración y la composición son correctas. La diferencia que se ve en las capturas viene de las sombras del dosel.

## Capturas (960×540, valley, cámara mirando al sol: yaw −39°)

- `before_sun.png`: niebla analítica (`--no-volumetric`), pitch 6°. Halo cálido y uniforme: la niebla brilla incluso dentro del bosque, donde el sol no llega.
- `after_sun.png`: volumétrico, mismo encuadre. El dosel tapa la mayor parte del aire del bosque: los árboles de 24–36 m con el sol a 11,5° proyectan sombras de unos 120–180 m. La niebla queda sombreada y sólo se ilumina a través de huecos.
- `after_sun_high.png`: volumétrico, pitch 14°. Entra algo de luz por el hueco del dosel superior, pero los rayos siguen siendo débiles.

**Conclusión honesta sobre los god rays**: en esta escena los rayos son débiles porque el dosel es denso y el sol es bajo. El efecto principal es físicamente correcto: la niebla sólo recibe sol donde hay un hueco, así que el bosque deja de brillar de forma uniforme. Para unos rayos marcados hacen falta claros o huecos en el dosel, o más dispersión en la niebla de altura.

## Coste estimado en Intel HD 520 (tier Low)

**Estimación analítica, no medida.** Las cifras de lavapipe no son representativas y no se usan.

- Muestras por frame: 480×270 × 16 ≈ 2,1 M. Cada muestra cuesta ~80–100 operaciones (2 exp de transmitancia, transformada de vista, 1–2 taps de sombra). Son ~170–210 M operaciones por frame.
- Con ~100 GFLOPS efectivos (pico ~400), el pase cuesta **~1,5–2,5 ms**.
- Upsample en lighting: 4 taps de color y 4 de profundidad por píxel a 960×540, **~0,2–0,3 ms**.
- Total previsto: **~2–3 ms por frame** en Low. Hay que medirlo en el PC del usuario con el timer de GPU (`--tier low`, con y sin `--no-volumetric`).
- Si es caro, las opciones son: 8 pasos, visibilidad solar cada 2 pasos, o reproyección temporal del volumen.

## Limitaciones

- El jitter de 16 pasos produce ruido que sólo desaparece con TAA. Sin TAA se verá grano.
- Pasos uniformes: cerca de la cámara la integración de la fase y de la sombra es gruesa. La transmitancia sí es exacta.
- Sin reproyección temporal ni froxels.
- Agua: la superficie del agua sigue usando la niebla analítica de `water.frag`, así que no recibe el término volumétrico.
- El upsample bilateral 2×2 puede dejar halos finos en bordes de hojas donde la profundidad cambia deprisa.
- Ambiente: se usa el SH medio del fog (sin variación direccional).

## Siguientes pasos

1. Medir el coste en la HD 520 (8/12/16 pasos) y decidir el número de pasos.
2. Pasar a froxels con reproyección temporal (Wronski, Hillaire) para compartir el coste entre frames y estabilizar el ruido.
3. Una escena con claros o huecos en el dosel para ver rayos marcados.
4. Aplicar el término volumétrico también a la superficie del agua.

## Archivos

- `shaders/volumetric.frag` (nuevo): el pase.
- `shaders/common/shadow.glsl` (nuevo): cascadas de sombra compartidas, con modo de 1 tap o PCF.
- `shaders/common/fog.glsl`: helpers de transmitancia, ambiente y fase (la ruta analítica no cambia).
- `shaders/lighting.frag`: upsample y composición volumétrica.
- `shaders/CMakeLists.txt`: registro del shader.
- `engine/renderer/include/tefra/renderer/renderer.h`: `RenderSettings::volumetric_fog`.
- `engine/renderer/src/renderer.cpp`: pipeline y pase `volumetric`.
- `apps/common/scenes.cpp`: valley con volumetría activa.
- `apps/common/options.cpp`, `apps/common/runner.{h,cpp}`: `--no-volumetric`, `--yaw`, `--pitch`.
- `docs/research/volumetric-light/`: este informe y las tres capturas.
