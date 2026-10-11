# Sombras realistas: PCSS y sombras de contacto

Estado: implementado en el motor, activo por defecto, validación Vulkan con 0 errores. Coste en la HD 520 **estimado, no medido**.

## Qué se ha hecho

1. **Penumbra física (PCSS) en las cascadas de sombra** (`shaders/lighting.frag`, `shadow_cascade`, `sun_shadow`).
   - Blocker search: 16 muestras Vogel (disco de Fernando, distribución uniforme) rotadas por píxel con ruido de interleaved gradient (IGN) que cambia por frame. Usa el sampler sin comparación `SMP_NEAREST_CLAMP` sobre el atlas de profundidad. El radio de búsqueda es `tan(sol) · distancia_al_plano_cercano`, acotado a [1,5, 32] texeles.
   - Penumbra en metros: `w = d_bloqueador · tan(radio angular del sol)`, con el radio del sol tomado de `sun_color.w` (0,00465 rad ≈ 0,53° de diámetro). Se convierte a texeles de la cascada y se acota a [0,75, 24] texeles.
   - PCF: 16 muestras Vogel de radio variable, rotadas por píxel y por frame, con comparación hardware (`SMP_SHADOW`, bilineal 2×2).
   - Penumbra física de referencia: a 75 m de distancia (copa sobre el suelo con sol a 11,5°) la semiamplitud es ≈ 0,35 m; a 15 m (pie del tronco) ≈ 7 cm, que es lo que da las sombras de copas blandas y de troncos nítidas.
   - **Sesgo por plano receptor**: cada muestra PCF compara contra la profundidad que tendría la superficie receptora en ese desplazamiento, `rz + dot(grad, (a,b))` con `grad = (n·R, n·U) / (n·L · zrange)`. Sin esto, un kernel ancho sobre el suelo casi horizontal (sol rasante) se auto-sombrea. Se mantienen el sesgo de un texel y el offset de normal de 1,5 texeles.
   - Modo de respaldo `--no-pcss`: PCF 3×3 idéntico al original (ver validación).

2. **Sombras de contacto en espacio de pantalla** (`contact_shadow`).
   - Marcha de 10 pasos hacia el sol, 0,75 m (configurable), desde el punto con offset de normal. Proyección con `view_proj`, linealización de profundidad reverse-Z con `proj[3][2] / depth`.
   - Un muestreo cuenta como ocluido si queda detrás de la superficie visible, a menos de 0,15 m (grosor). Factor de visibilidad `1 - 0,9 · min(aciertos/3, 1)`, multiplicado sobre el término del sol.
   - Solo afecta al sol directo. El ruido de inicio por píxel y frame lo promedia el TAA.

3. **Sesgo y acné en el terreno**: revisado.
   - Un sesgo constante cuadruplicado (4 texeles) no cambia la imagen del valle, así que el acné no es el factor limitante en esta escena.
   - El moteado que se veía en el suelo resultó ser de la propia diagnóstica (píxeles con N·L ≤ 0 por el normal map del suelo), no de sombra. Se corrigió la diagnóstica.

## Parámetros y ajustes

| Ajuste (`RenderSettings`) | Por defecto | CLI |
|---|---|---|
| `pcss_shadows` | true | `--no-pcss` |
| `contact_shadows` | true | `--no-contact-shadows` |
| `penumbra_scale` | 1,0 (multiplicador artístico) | — |
| `contact_length` | 0,75 m | — |
| `debug_sun_shadow` | false (diagnóstico: muestra solo la visibilidad del sol) | `--debug-sun-shadow` |

Otros cambios de herramientas: `--camera-yaw` y `--camera-pitch` en el bench, para encuadres de inspección.

Cambio de ABI GPU: `FrameConstants` ⇄ `Frame` (GLSL) reutilizan los tres `pad` por `shadow_flags`, `penumbra_scale`, `contact_length`. Tamaño y orden verificados (`static_assert` existente).

## Resultados

- Validación: `tefra_bench --scene valley --validation` con 0 errores en: escena por defecto (6 frames), diagnóstico de sol y ruta de respaldo (`--no-pcss --no-contact-shadows`).
- `ctest --preset linux-release`: unit y gpu en verde. El golden `materials` se regeneró (ver abajo).
- Capturas (1280×720):
  - `valley_before.png` (base, TAA), `valley_after.png` (con PCSS + contacto, TAA).
  - `sun_visibility_before_after.png`: visibilidad del sol antes/después (izquierda/derecha) y mapa de diferencias ×8 (abajo). Es la prueba más clara porque es determinista.

**Lectura honesta de las capturas**: en la vista por defecto del valle el cambio es mínimo. El sol está a 11,5° y el suelo está en sombra de dosel y de la pared del valle. Lo que queda iluminado son copas lejanas y el camino, y ahí la penumbra solo se nota en los bordes. Cuantitativamente, el mapa de diferencias de visibilidad del sol cambia ~110 píxeles por encima de 3/255 (un punto en la copa lejana). En la imagen final el cambio también es pequeño (unos cientos de píxeles por encima de 8/255), aunque esa pasada tiene ruido de ejecución propio.

Golden `materials`: al activar PCSS, 0,225 % de los píxeles superaron el umbral de 0,2 % (máx. 29, solo en bordes de sombra de las esferas). Se revisó la imagen y se regeneró `tests/golden/materials.png`. `testpattern` no cambió.

## Hallazgos y limitaciones

- **La imagen beauty del valle no es determinista entre ejecuciones**: el propio binario base difiere de sí mismo hasta 164/255 entre dos ejecuciones idénticas de 6 frames. Por eso las comparaciones píxel a píxel de la pasada final son poco concluyentes, y la prueba fiable es la diagnóstica con `--no-taa`. Conviene investigar la fuente (¿viento o tiempo de pared?) antes de usar capturas beauty como regresión.
- **Ruta de respaldo**: `--no-pcss` reproduce el original bit a bit en la diagnóstica sin TAA. En la pasada beauty, la diferencia con el base queda dentro del ruido de ejecución de arriba.
- **El promedio del bloqueador se sesga hacia los bloqueadores lejanos** dentro de la ventana de búsqueda. En un bosque denso casi siempre satura el límite de 24 texeles (≈0,5 m en la cascada 0). Alternativa a probar: usar el bloqueador más cercano o un percentil.
- Búsqueda acotada a 32 texeles: los bloqueadores fuera de esa ventana no cuentan.
- Las sombras de contacto no ven geometría oculta a la cámara ni geometría sin grosor en profundidad.
- Lavapipe no sirve para medir rendimiento.

## Coste estimado para Intel HD 520 (tier Low, 960×540 interno)

- Por píxel con sol visible: PCSS 16 (búsqueda, sin comparación) + 16 (PCF hardware) + contacto 10 (texelFetch). Antes: 9 de PCF.
- Las bandas de transición entre cascadas duplican el coste en esa franja.
- Estimación: ~3 a 4× las lecturas de sombra de la pasada de iluminación. En HD 520 a 960×540 esto podría costar del orden de 1 a 2 ms. **No medido**; medir en el PC del usuario con `--no-pcss` y `--no-contact-shadows` como línea base.
- Si resulta caro en Low: bajar la búsqueda a 8 muestras (el mínimo del brief) y el PCF a 12, o desactivar contacto (bit 2).

## Fuentes y decisiones

- Fernando, R. (2005). *Percentage-Closer Soft Shadows*. SIGGRAPH Sketches. Base de PCSS.
- Vogel, H. (1979). *A better way to construct the sunflower head*. Disco de muestreo uniforme.
- Jimenez, J. (2014). *Next Generation Post Processing in Call of Duty: Advanced Warfare* (SIGGRAPH course). Interleaved gradient noise.
- Isidoro, J. (GDC 2006). *Shadow Mapping: GPU-based Tips*. Sesgo de profundidad; cita hecha de memoria, pendiente de verificar.
- Sombras de contacto en espacio de pantalla: técnica estándar. Implementación propia, sin código de ningún motor.
- Sol: diámetro angular 0,53° (dato astronómico estándar).

Decisiones: PCSS y contacto activos por defecto (coste pendiente de medir en la HD 520). Penumbra física sin multiplicador artístico. Sesgo de plano receptor porque el kernel ancho lo necesita.

## Siguientes pasos

1. Medir en el PC del usuario: `tefra_bench --tier low` con y sin `--no-pcss`, `--no-contact-shadows`.
2. Investigar la falta de determinismo de la pasada beauty del valle (ver arriba).
3. Probar el bloqueador más cercano frente al promedio, para evitar la saturación.
4. Añadir una vista de inspección con sol más alto o una toma rasante a un tronco, para validar visualmente la penumbra corta en la base de los troncos.
