# Calibración por estadística de imagen: Tefra valle (M2) frente a fotos CC0 de bosque brumoso

Fecha: 2026-10-10. Autor: subagente de calibración. Alcance: solo análisis cuantitativo. No se ha tocado el código del motor.

## Resumen

El valle M2 está **más oscuro (≈ −1,2 EV en mediana), con menos contraste (≈ 45 % de la referencia), menos croma (≈ 50 %), menos calidez en medios tonos y casi sin detalle fino** (energía en 2,5–5 px ≈ 1/4 de la referencia en v5). El render de prueba `m2-atmosphere-fog` ya está en el rango de tono de las referencias, así que el problema viene sobre todo de la iluminación y el exposure del valle, no de la curva base. Las cinco recomendaciones numéricas más importantes:

1. **Exposición +1,2 EV** en el valle antes del tonemapping (P50 de L* 39 → ~57; mediana de luminancia Y 0,108 → 0,252 lineal).
2. **Curva de gradación** en espacio de visualización: negro P1 de 0,031 a 0,018 lineal, blanco P95 de 0,30 a 0,86 (ganancia ≈ 3, lift −0,09). Rango P1–P99 de 3,4 a ~6 stops (referencia pinar 5,8).
3. **Croma y tinte**: saturación ×1,35 (en lineal, respecto a la luma) y balance cálido RGB ×(1,03; 1,00; 0,97). C* global de 5,4 a ~10 (referencia 4,5–12,8) y b* de medios de 2,5 a ~9 (referencia 6,9–14,9).
4. **Detalle fino**: energía en 256–512 ciclos/imagen (periodos de 2,5–5 px) de 0,0015 a ≥ 0,0050 (mediana de referencias 0,0056). Un enfoque 3x3 de amplitud 1,0 tras el TAA lo alcanza en la prueba what-if, pero hay que comprobar halos. Además, el TAA de v1 a v4 reduce esa energía ×0,18.
5. **Bruma visible**: fracción de píxeles de cielo o bruma (textura 9x9 de L* < 1) de 0,38 a **0,15–0.25**, bajando la densidad de niebla en la mitad lejana o añadiendo detalle de suelo y sotobosque. Este objetivo se ha medido en el render pero **no está validado en el motor**.

## 1. Fuentes

Siete panoramas CC0 de Poly Haven, con MD5 verificado (`refs/`, `PROVENANCE.md`). Se extraen 14 vistas rectilíneas 1280x720 con HFOV 91,5° (mismo FOV que la cámara del valle, fovy 60° en 16:9, `apps/common/scenes.cpp:370`) y supermuestreo 2x para evitar aliasing. Se asume pitch 0 en los renders.

Subconjuntos de referencia, usados para comparar:
- **Pinar**: misty_pines (yaw 0, 270) y phalzer_forest_01 (0, 270). Coníferas en niebla o luz difusa, el objetivo más directo.
- **Arbolado en luz matinal**: je_gray_02 (90, 270; eucaliptos y sol), spruit_dawn (0, 270) y misty_dawn (0, 270).
- **Bruma sin arbolado**: kloofendal_misty_morning y kloofendal_28d_misty (0, 270). Sirve de referencia de niebla.

Renders: m2-valley-v1 a v5 (v4 = TAA, v5 = TAAU low) y m2-atmosphere-fog. Todos son 1280x720 PNG; el alfa se descarta.

## 2. Método

Todo en sRGB de visualización decodificado a lineal. Luminancia Y (Rec.709) y L* (CIELAB D65).

- Histograma: percentiles 1/5/50/95/99 de L* y de Y lineal. Rango dinámico en stops = log2(Y_P99 / Y_P1).
- Contraste local: desviación típica de L* en bloques 32x32 no solapados (mediana, media y P90).
- Saturación: croma CIE C* por rango de L* (0–20, 20–40, …, 80–100) y fracción de píxeles.
- Tono por región: a* y b* medios en sombras (L* < 30), medios (30–70) y luces (> 70).
- Cielo y bruma: textura muy baja, desviación típica en ventana 9x9 de L* < 1,0. Es una heurística. Depende del contraste, así que sus cambios por curva no cuentan como cambio de niebla.
- Nitidez: |Laplaciano| de L*.
- Densidad de detalle: energía del espectro de potencia (ventana Hann, sin DC) en bandas de octava, en ciclos por 1280 px. Pendiente log-log del espectro radial entre 4 y 256 ciclos/imagen (natural ≈ −2,0 a −2,4).

Scripts: `calib_lib.py` (utilidades), `extract_views.py`, `contact_sheet.py` (elección de encuadres), `measure.py` (métricas, `results.json`), `tables.py` (`tables.md`), `grade_fit.py` (`grade_fit.json`, `grade_fit.md`), `whatif.py` (`whatif.json`, `whatif.md`).

## 3. Resultados

### 3.1 Subconjuntos de referencia frente a renders (medianas)

| Métrica | Pinar | Arbolado luz matinal | Bruma sin arbolado | v4-taa | v5-low-taau |
|---|---|---|---|---|---|
| L* P50 | 59,3 | 55,7 | 77,9 | 39,2 | 39,2 |
| L* P95 | 95,1 | 94,4 | 94,0 | 61,3 | 61,6 |
| L* P99 | 98,5 | 96,4 | 95,2 | 64,1 | 64,2 |
| Rango dinámico P1–P99 (stops) | 5,78 | 5,54 | 4,20 | 3,49 | 3,43 |
| Contraste local (mediana bloques) | 10,85 | 5,89 | 0,57 | 2,66 | 2,57 |
| C* global | 12,8 | 12,4 | 4,5 | 5,3 | 5,4 |
| b* sombras | 10,82 | 6,70 | 7,73 | 4,21 | 4,28 |
| b* medios | 14,94 | 6,87 | 6,92 | 2,44 | 2,54 |
| Fracción textura muy baja (bruma/cielo) | 0,046 | 0,277 | 0,545 | 0,335 | 0,379 |
| Pendiente espectral 4–256 | −2,27 | −2,25 | −2,38 | −2,73 | −2,88 |
| Energía 32–256 (suma de bandas) | 0,2411 | 0,0394 | 0,0013 | 0,0716 | 0,0679 |
| Energía 256–512 | 0,03385 | 0,00557 | 0,00034 | 0,00279 | 0,00146 |

Lectura: el valle se parece a la bruma sin árboles en la fracción de cielo y en el croma. En tono y detalle, se separa de las dos referencias forestales. Pinar tiene mucho más contraste local y más calidez. Las referencias de bruma por sí solas no bastan como objetivo.

### 3.2 Histograma completo (L* de los renders frente a la mediana de referencias)

| Métrica | Ref. mediana | Ref. P25–P75 | v1 | v4-taa | v5-low-taau | atmosphere-fog |
|---|---|---|---|---|---|---|
| L* P1 | 18,6 | 12,6–24,3 | 21,0 | 19,7 | 20,3 | 27,0 |
| L* P5 | 26,1 | 19,1–32,3 | 24,3 | 22,9 | 23,1 | 32,9 |
| L* P50 | 62,3 | 55,3–71,4 | 42,6 | 39,2 | 39,2 | 61,7 |
| L* P95 | 94,4 | 92,9–95,3 | 63,4 | 61,3 | 61,6 | 92,6 |
| L* P99 | 96,4 | 95,6–98,1 | 65,4 | 64,1 | 64,2 | 94,3 |
| Fracción L* > 70 | 0,417 | 0,370–0,563 | 0,000 | 0,000 | 0,000 | 0,310 |

En el valle, ningún píxel supera L* ≈ 65. Ninguna luz llega a "blanco". `m2-atmosphere-fog` sí llega (31 % de píxeles > 70).

### 3.3 Saturación, tinte y detalle (renders frente a referencia mediana)

| Métrica | Ref. mediana | v4-taa | v5-low-taau | atmosphere-fog |
|---|---|---|---|---|
| C* L*∈[40,60) | 11,4 | 3,9 | 4,0 | 12,1 |
| C* L*∈[60,80) | 11,7 | 9,2 | 9,2 | 11,6 |
| b* medios (a*, b*) | (3,2; 8,7) | (1,1; 2,4) | (1,2; 2,5) | (3,3; 9,9) |
| |Laplaciano| medio de L* | 5,68 | 1,60 | 1,06 | 1,19 |
| Energía 8–16 ciclos/imagen | 0,0332 | 0,0792 | 0,0817 | 0,2670 |
| Energía 256–512 | 0,00557 | 0,00279 | 0,00146 | 0,00406 |
| Pendiente espectral | −2,30 | −2,73 | −2,88 | −2,95 |

- El paso de v1 a v4 (TAA) reduce la energía 256–512 de 0,0158 a 0,0028 (×0,18). El paso de v4 a v5 (TAAU low) la reduce ×0,52 más. v1 tiene más detalle que la referencia, así que es probable que tenga shimmer o aliasing.
- `m2-atmosphere-fog` es el render de tono más parecido a las referencias (L* P50 61,7 frente a 62,3; C* 10,7 frente a 10,9). Aun así, es demasiado suave en alta frecuencia y tiene la pendiente espectral más empinada (−2,95).

### 3.4 Exposición relativa (EV = log2 de la mediana de Y de referencia / mediana del render)

| Render | EV (mediana) | EV (P95) | EV (P99) |
|---|---|---|---|
| v1 | +1,25 | +1,43 | +1,40 |
| v4-taa | +1,52 | +1,54 | +1,47 |
| v5-low-taau | +1,52 | +1,53 | +1,46 |
| atmosphere-fog | +0,03 | +0,07 | +0,08 |

Referencia: la mediana del conjunto completo. Un EV de +1,2 (no +1,5) es el valor que se obtiene al comparar con el subconjunto pinar+arbolado (`grade_fit.md`). Es el valor que usamos. Las referencias son JPG tonemapeados por el autor, así que el EV es orientativo.

### 3.5 Ajuste de curva de gradación (`grade_fit.md`)

Percentiles de luminancia lineal Y (1/5/50/95/99), mediana de las 10 vistas de pinar y arbolado frente a v5:

| | P1 | P5 | P50 | P95 | P99 |
|---|---|---|---|---|---|
| v5 (render) | 0,0306 | 0,0382 | 0,1075 | 0,2990 | 0,3301 |
| Objetivo (bosque) | 0,0177 | 0,0350 | 0,2520 | 0,8613 | 0,9213 |

Ajuste out = lift + gain · Y^γ: gain 2,99, γ 0,965, lift −0,090 (RMSE 0,011). Vale como diagnóstico. El motor no aplica esa curva; sirve como objetivo para la curva de tonos.

### 3.6 Validación de propuestas (`whatif.md`)

Operadores aplicados sobre v5 en luz lineal. Esto es una predicción de primer orden y no sustituye a un render del motor.

| Métrica | v5 actual | A: +1,2 EV | B: curva | E: B + cálido + sat ×1,35 | F: E + enfoque 0,5 | G: E + enfoque 1,0 | Objetivo (bosque) |
|---|---|---|---|---|---|---|---|
| L* P50 | 39,2 | 56,8 | 57,8 | 57,8 | 57,9 | 57,8 | 56–59 |
| L* P95 | 61,6 | 86,5 | 93,3 | 93,2 | 93,3 | 93,4 | ~94 |
| Rango P1–P99 (stops) | 3,43 | 3,43 | 6,16 | 6,14 | 6,49 | 7,00 | 5,5–5,8 |
| Contraste local | 2,57 | 3,38 | 5,17 | 5,17 | 5,42 | 5,73 | 5,9–10,9 |
| C* global | 5,4 | 7,1 | 6,5 | 10,0 | 10,1 | 10,1 | 12,4–12,8 |
| b* medios | 2,54 | 5,30 | 5,34 | 9,14 | 9,17 | 9,22 | 6,9–14,9 |
| b* sombras | 4,28 | 6,51 | 3,79 | 6,28 | 6,00 | 5,73 | 6,7–10,8 |
| Fracción textura muy baja | 0,379 | 0,303 | 0,216 | 0,216 | 0,197 | 0,179 | 0,05–0,28 |
| Energía 256–512 | 0,00146 | 0,00147 | 0,00149 | 0,00149 | 0,00290 | 0,00529 | 0,0056 |
| Pendiente espectral | −2,88 | −2,88 | −2,88 | −2,88 | −2,72 | −2,58 | ≈ −2,3 |

El enfoque no llega a la pendiente de referencia (−2,3) y deja más detalle de lo debido en 2–4 px. El 1,0 tiene riesgo de halos; el 0,5 es la opción prudente para empezar.

## 4. Recomendaciones (cambios concretos)

| # | Parámetro | Valor actual | Valor propuesto | Evidencia |
|---|---|---|---|---|
| 1 | Exposición del valle | (ref.) | **+1,2 EV** (× 2,3 antes del tonemapping) | EV de la mediana de referencias; atmosphere-fog ya está en rango |
| 2 | Curva de tonos / gradación | gain 1, lift 0 | Puntos de control en Y lineal post-tonemap: 0,031→0,018; 0,108→0,252; 0,30→0,86; 0,33→0,92 (≈ gain 3, γ 0,97, lift −0,09) | `grade_fit.md`; rango 3,4→6,2 stops |
| 3 | Balance de color | neutro | Ganancia RGB (1,03; 1,00; 0,97) en lineal | b* medios 2,5→9,1 (`whatif` E) |
| 4 | Saturación | 1,0 | **×1,35** en lineal respecto a la luma (ajustar sobre albedos de corteza, hierba y hojarasca) | C* global 5,4→10,0 (`whatif` E) |
| 5 | Detalle fino | TAA tal cual | Enfoque 3x3 de **0,5** tras TAA (prueba 1,0 con halos) y detalle de suelo a 2–5 px | Energía 256–512: 0,0015→0,0029 (0,5) o 0,0053 (1,0) |
| 6 | Densidad de bruma visible | fracción 0,38 | Reducir inscatter en la mitad lejana; objetivo 0,15–0,25 tras el cambio | Pinar 0,05, arbolado 0,28 (no validado en motor) |
| 7 | Relleno de sombras | P1 = 0,031 | P1 ≈ 0,018 lineal (−0,8 EV en sombra profunda) | `grade_fit`; sombras más claras que la referencia tras el EV |

Notas de implementación:
- Los puntos 2 y 4 se pueden hacer como LUT de 1D (luma) y de 3D en espacio de visualización al final del pipeline. Son pasos de corrección que compensan la curva del tonemapper, no sustituyen una revisión del hombro de tonos. Antes de fijarlos, conviene confirmar en el motor que la luminancia HDR de cielo y niebla no está infra-expuesta. El render solo llega a 0,33 lineal en el percentil 99.
- Si se pasa a un pipeline en ACEScg, la curva se traduce invirtiendo el tonemapper, lo que requiere medir el tonemapper real (no incluido aquí).
- El punto 6 requiere volver a renderizar. Sus números no se deben tomar como comprobados.

## 5. Limitaciones

- **Muestra pequeña**: 14 vistas de 7 panoramas. Solo 4 vistas son de pinar y la mitad de las referencias son de bruma o luz matinal sin arbolado. Las medianas por subconjunto son orientativas.
- **Referencias tonemapeadas**: son JPG de Poly Haven con su propia curva de tonos. Los valores absolutos de luminancia y el EV son relativos. Las métricas de forma (contraste, espectro, croma relativo) son más robustas.
- **Escena no equivalente**: el valle es un bosque denso con sotobosque y cielo oscuro. Las fotos son campos o bosques abiertos. Algunas referencias tienen sol directo, lo que no encaja con la bruma del valle.
- **Heurística de bruma**: la fracción de textura muy baja depende del contraste. No separa bien niebla de cielo ni de superficies lisas. Úsala como indicador, no como medida de densidad de niebla.
- **Operadores de la prueba**: los what-if son post-proceso sobre v5 en luz lineal. No recalculan sombras, GI ni niebla, así que no sustituyen a un render del motor.
- **Cámara**: se asume pitch 0 y 60° verticales. El motor puede usar otro encuadre en otras escenas.
- **Sin cambios al motor**: no se ha modificado código ni shaders. No se ha hecho commit ni push.

## 6. Archivos

- Informe: `/home/user/Tefra/docs/research/calibration/REPORT.md`
- Tablas completas (A–I): `/home/user/Tefra/docs/research/calibration/tables.md`
- Ajuste de gradación: `/home/user/Tefra/docs/research/calibration/grade_fit.md`, `grade_fit.json`
- Validación de propuestas: `/home/user/Tefra/docs/research/calibration/whatif.md`, `whatif.json`
- Datos por imagen: `/home/user/Tefra/docs/research/calibration/results.json`
- Procedencia y licencias: `/home/user/Tefra/docs/research/calibration/PROVENANCE.md`
- Referencias (JPG originales, ~166 MB, decidir antes de versionar): `/home/user/Tefra/docs/research/calibration/refs/`
- Vistas extraídas y hojas de contacto: `/home/user/Tefra/docs/research/calibration/views/`
- Scripts: `/home/user/Tefra/docs/research/calibration/{calib_lib,extract_views,contact_sheet,measure,tables,grade_fit,whatif}.py`
