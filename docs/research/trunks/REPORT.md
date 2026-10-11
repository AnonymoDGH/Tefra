# Troncos de árbol realistas

Rama: `worktree-agent-a3d10d9ef6c292a50` (base `feabbeb`, la rama `claude/new-session-ksgl0m`).

## Qué cambia

1. **Geometría (`add_trunk`, `make_trunk_style`, `section_radius`)**
   - Sección irregular: 3 armónicos angulares (2, 3 y 4) con amplitud y fase por árbol (±6 %, 4 % y 2,5 % del radio).
   - Contrafuertes: 4–6 por árbol, con ancho de ~0,28 del paso angular, tamaño 0,7–1,05 × radio base y altura 12–16 % de la altura del árbol. Caen con (1 − y/h)².
   - Raíces hundidas: el anillo inferior está en y = −1,2 m, y el ensanchamiento sigue por debajo del suelo.
   - Inclinación y curvatura: el eje se desplaza con `lean` (≤ 0,7°) y `bend` (cuadrático, hasta 1,2 % de la altura). Las ramas y las copas usan el mismo eje (`axis_point`), así que siguen conectadas al tronco.
   - LOD0: 20 segmentos en todo el tronco, con anillos densos en la base (0, 0,25, 0,6, 1,1, 1,8, 2,8 m). Antes eran 12 segmentos con anillos cada 0,9 m.
   - LOD1: sigue con 5 segmentos y anillos cada 4 m (más los dos anillos bajo el suelo y en la base). Usa la misma forma de sección, para evitar un salto de silueta al cambiar de LOD.
   - Normales: calculadas sobre la rejilla (diferencias finitas), porque la sección ya no es un círculo.
   - Semillas: la forma del tronco usa su propio RNG (`seed * 2654435761 + 99`). La disposición de la copa no cambia.
   - Coste: LOD0 pasa de unos 0,8k triángulos por tronco a 1,5k (altura 30 m). LOD1 añade un par de anillos.

2. **Material (`gbuffer.frag`, flag `kFlagBark` = bit 2 de `flags`)**
   - Musgo: cobertura en `pad[0]` (float bits, 0,85). Máscara = altura (base hasta ~1–2,8 m) × orientación (lado norte, mundo −Z, peso mínimo 0,4) × parches (fbm 3D periódico en ángulo, tomado de la normal horizontal, así que no hay costura) × grumos.
   - El musgo baja la rugosidad a 0,95, pone metal a 0, aplana la normal a 60 % y oscurece el albedo hacia verde musgo (lineal ~0,03–0,09 / 0,08–0,15 / 0,02–0,05).
   - Normal map: `normal_scale` 1,0 → 1,8.
   - Escala UV: se mantiene 0,9 m por teselado horizontal y 1,6 m vertical. Con esa escala, las placas del `bark_brown_01` salen de unos 9 cm. Esto supone que el teselado de la textura mide unos 0,9 m, que es una estimación; no lo he comprobado con la metadata de Poly Haven.
   - Lo que no cambia: `pad` es el único campo libre de `MaterialGPU` (sin cambio de tamaño, sigue en 80 B). El `flags` nuevo solo afecta al bark.

3. **Variación por instancia**
   - `veg_vs.glsl` saca `rot_proto.w` como varying flat `v_rand` en location 5 (solo fuera del pase de sombra). `mesh.vert` escribe 0 para no romper el interfaz.
   - Tinte del bark: `base.rgb *= mix(0.86/0.80/0.74, 1.10/1.02/0.94, v_rand)`.
   - Parches de musgo distintos por árbol (desfase del fbm con `v_rand`).

4. **Convención de tangente del tronco**: el tangente sign pasa a −1, para que la bitangente apunte a +Y (+V). Así el canal verde del normal map `_gl` apunta hacia arriba. Medido: la diferencia de render con el signo contrario es mínima (diferencia media 0,29 sobre 765; 3 píxeles por encima de 30). Lo mantengo por corrección, pero no es un efecto visible en esta escena.

5. **Herramienta**: `--camera x,y,z,yaw_deg,pitch_deg` en `tefra_bench` para encuadrar. Sin esto no se pueden hacer las capturas de cerca.

## Capturas (960×540, `--scene valley`, cámara 0.8, 21.8, −116.0, yaw 185°)

- `before_close.png`: antes. Tronco cilíndrico y liso, bark sin musgo.
- `after_close.png`: después, misma cámara. Ensanchamiento en la base, relieve de la corteza más marcado, musgo y tinte distinto por árbol.
- `after_moss_base.png`: detalle de la base (cámara 0.8, 21.8, −114.8, pitch −6°). Parches de musgo en el lado visible.

## Validación

- `tefra_bench --validation` en lavapipe: `errors: 0`, `warnings: 0` (tras quitar un warning de salida no consumida en el pase de sombra).
- Build release con `-Werror`, `ctest --preset linux-release`: unit y gpu pasan. No regeneré goldens y no tengo un golden específico de la escena valle.

## Coste estimado (tier Low, Intel HD 520)

- Triángulos de los troncos LOD0: +~0,7k por árbol visible, y los troncos LOD0 solo se ven hasta 140 m. Es un coste de vértices pequeño frente a la copa, pero **no medido en la HD 520**. Hay que medirlo en el PC del usuario (`scripts/bench-windows.bat`).
- El musgo es un fbm 3D de 3 octavas más un ruido de detalle, en el gbuffer para cada píxel de bark (unas 32 evaluaciones de hash por píxel). Puede pesar en la HD 520. Si pesa, se puede bajar el ruido o calcular el musgo en el vertex shader. No medido.

## Limitaciones y siguientes pasos

- La cobertura de musgo está en el material y es igual para los tres prototipos. Cuando haya variedad de especies, hay que separarla.
- La orientación norte es constante en el mundo. Un viento dominante o la exposición real del terreno darían un patrón más natural.
- Pendiente de medir: coste en Intel HD 520 y revisión visual en un tronco de primer plano (< 2 m) con sol bajo.
- Las copas no cambiaron; el lean/bend se aplica al eje y las ramas siguen su posición.

## Fuentes

- Weber & Penn, "Creation and Rendering of Realistic Trees", SIGGRAPH 1995. Referencia para el ensanchamiento de las raíces y la forma de la sección. No copié código.
- Tokuyoshi & Kaplanyan 2019 (ya usado en `gbuffer.frag`). Sin cambios.
- Textura: Poly Haven `bark_brown_01`, CC0.
- No hice búsqueda web en esta sesión; las referencias son de memoria y conviene verificarlas.
