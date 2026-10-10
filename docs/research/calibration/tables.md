## A. Histograma de luminancia (L* CIELAB; códigos sRGB 0-255 de luma)

| Métrica | Ref. mediana | Ref. P25–P75 | m2-valley-v1 | m2-valley-v2 | m2-valley-v3-water | m2-valley-v4-taa | m2-valley-v5-low-taau | m2-atmosphere-fog |
|---|---|---|---|---|---|---|---|---|
| L* P1 | 18.6 | 12.6–24.3 | 21.0 | 21.0 | 19.0 | 19.7 | 20.3 | 27.0 |
| L* P5 | 26.1 | 19.1–32.3 | 24.3 | 24.3 | 22.6 | 22.9 | 23.1 | 32.9 |
| L* P50 | 62.3 | 55.3–71.4 | 42.6 | 39.1 | 39.3 | 39.2 | 39.2 | 61.7 |
| L* P95 | 94.4 | 92.9–95.3 | 63.4 | 61.6 | 61.6 | 61.3 | 61.6 | 92.6 |
| L* P99 | 96.4 | 95.6–98.1 | 65.4 | 64.4 | 64.4 | 64.1 | 64.2 | 94.3 |
| L* media | 62.0 | 58.2–67.5 | 42.5 | 39.9 | 39.3 | 39.3 | 39.4 | 61.5 |
| código luma P1 | 45 | 33–57 | 50 | 50 | 46 | 48 | 49 | 64 |
| código luma P50 | 150 | 132–175 | 101 | 92 | 92 | 92 | 92 | 149 |
| código luma P99 | 245 | 242–249 | 159 | 156 | 156 | 155 | 155 | 238 |
| Rango dinámico P1–P99 (stops, Y lineal) | 5.10 | 4.51–5.97 | 3.40 | 3.35 | 3.59 | 3.49 | 3.43 | 4.08 |

## B. Contraste local (desv. típica de L* en bloques 32x32)

| Métrica | Ref. mediana | Ref. P25–P75 | m2-valley-v1 | m2-valley-v2 | m2-valley-v3-water | m2-valley-v4-taa | m2-valley-v5-low-taau | m2-atmosphere-fog |
|---|---|---|---|---|---|---|---|---|
| mediana bloques | 5.89 | 0.98–11.44 | 3.11 | 2.65 | 3.03 | 2.66 | 2.57 | 2.32 |
| media bloques | 5.65 | 2.22–11.36 | 3.77 | 3.33 | 3.64 | 3.29 | 3.22 | 4.23 |
| P90 bloques | 11.04 | 6.01–16.92 | 7.51 | 7.36 | 7.34 | 6.96 | 7.04 | 12.80 |

## C. Saturación (croma CIE C*) por rango de L* y fracción de píxeles

| Métrica | Ref. mediana | Ref. P25–P75 | m2-valley-v1 | m2-valley-v2 | m2-valley-v3-water | m2-valley-v4-taa | m2-valley-v5-low-taau | m2-atmosphere-fog |
|---|---|---|---|---|---|---|---|---|
| C* L*∈[0,20) | 7.1 | 5.5–8.1 | 4.7 | 4.7 | 4.7 | 4.9 | 5.2 | nan |
| C* L*∈[20,40) | 11.6 | 10.0–14.9 | 5.7 | 5.3 | 6.0 | 5.9 | 5.9 | 7.4 |
| C* L*∈[40,60) | 11.4 | 10.0–19.3 | 4.9 | 3.9 | 3.9 | 3.9 | 4.0 | 12.1 |
| C* L*∈[60,80) | 11.7 | 2.8–16.8 | 8.8 | 9.2 | 9.2 | 9.2 | 9.2 | 11.6 |
| C* L*∈[80,100) | 7.6 | 2.3–12.3 | nan | nan | nan | nan | nan | 9.6 |
| fracción L*∈[0,20) | 0.014 | 0.002–0.056 | 0.006 | 0.006 | 0.016 | 0.012 | 0.008 | 0.000 |
| fracción L*∈[20,40) | 0.225 | 0.140–0.261 | 0.454 | 0.524 | 0.509 | 0.515 | 0.518 | 0.149 |
| fracción L*∈[40,60) | 0.215 | 0.189–0.238 | 0.402 | 0.396 | 0.399 | 0.402 | 0.395 | 0.316 |
| fracción L*∈[60,80) | 0.205 | 0.167–0.281 | 0.138 | 0.075 | 0.076 | 0.071 | 0.079 | 0.337 |
| fracción L*∈[80,100) | 0.321 | 0.232–0.406 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.199 |
| C* global | 10.9 | 5.9–14.4 | 5.8 | 5.0 | 5.4 | 5.3 | 5.4 | 10.7 |

## D. Tono por región (a*, b*, ángulo de tinte en grados; 0 = rojo, 90 = amarillo, 180 = verde/cian azulado, 270 = azul)

| Métrica | Ref. mediana | Ref. P25–P75 | m2-valley-v1 | m2-valley-v2 | m2-valley-v3-water | m2-valley-v4-taa | m2-valley-v5-low-taau | m2-atmosphere-fog |
|---|---|---|---|---|---|---|---|---|
| sombras L*<30: a* | 2.01 | -1.26–3.71 | 0.65 | 0.65 | 0.52 | 0.46 | 0.42 | 2.99 |
| sombras L*<30: b* | 8.35 | 6.59–8.82 | 2.87 | 2.87 | 4.09 | 4.21 | 4.28 | 3.97 |
| sombras L*<30: tinte (grados) | 85 | 69–115 | 77 | 77 | 83 | 84 | 84 | 53 |
| sombras L*<30: fracción de píxeles | 0.097 | 0.034–0.162 | 0.209 | 0.209 | 0.268 | 0.272 | 0.274 | 0.026 |
| medios 30-70: a* | 3.20 | -3.65–4.19 | 0.69 | 1.20 | 1.11 | 1.14 | 1.17 | 3.31 |
| medios 30-70: b* | 8.74 | 5.91–16.36 | 1.52 | 1.96 | 2.53 | 2.44 | 2.54 | 9.91 |
| medios 30-70: tinte (grados) | 78 | 60–111 | 65 | 59 | 66 | 65 | 65 | 72 |
| medios 30-70: fracción de píxeles | 0.451 | 0.389–0.494 | 0.791 | 0.791 | 0.732 | 0.728 | 0.726 | 0.663 |
| luces L*>70: a* | 0.24 | -1.45–0.50 | -2.25 | 4.44 | 4.44 | nan | nan | 1.30 |
| luces L*>70: b* | -0.80 | -2.05–7.11 | 14.85 | 20.81 | 20.81 | nan | nan | 10.31 |
| luces L*>70: tinte (grados) | 223 | 95–282 | 99 | 78 | 78 | nan | nan | 83 |
| luces L*>70: fracción de píxeles | 0.417 | 0.370–0.563 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.310 |
| global a* | 1.45 | -2.27–1.91 | 0.68 | 1.08 | 0.95 | 0.96 | 0.96 | 2.68 |
| global b* | 4.01 | 1.12–12.61 | 1.80 | 2.15 | 2.94 | 2.92 | 3.02 | 9.88 |

## E. Cielo y bruma (heurística: textura 9x9 de L* < 1.0)

| Métrica | Ref. mediana | Ref. P25–P75 | m2-valley-v1 | m2-valley-v2 | m2-valley-v3-water | m2-valley-v4-taa | m2-valley-v5-low-taau | m2-atmosphere-fog |
|---|---|---|---|---|---|---|---|---|
| fracción textura muy baja (bruma+cielo) | 0.277 | 0.045–0.500 | 0.234 | 0.306 | 0.256 | 0.335 | 0.379 | 0.518 |
| fracción azulada (b*<-2) con textura baja | 0.095 | 0.009–0.191 | 0.106 | 0.086 | 0.050 | 0.053 | 0.054 | 0.015 |
| fracción brillante (L*>70) con textura baja | 0.245 | 0.022–0.400 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.183 |

## F. Nitidez y energía de alta frecuencia

| Métrica | Ref. mediana | Ref. P25–P75 | m2-valley-v1 | m2-valley-v2 | m2-valley-v3-water | m2-valley-v4-taa | m2-valley-v5-low-taau | m2-atmosphere-fog |
|---|---|---|---|---|---|---|---|---|
| |Laplaciano| medio de L* | 5.675 | 2.117–10.469 | 4.224 | 3.202 | 3.694 | 1.602 | 1.064 | 1.186 |
| |Laplaciano| P99 de L* | 31.82 | 22.43–50.80 | 35.03 | 26.27 | 28.79 | 10.91 | 6.71 | 19.95 |
| desv. típica del Laplaciano | 9.232 | 4.971–15.546 | 8.109 | 6.328 | 6.942 | 2.698 | 1.710 | 4.081 |

## G. Densidad de detalle: fracción del espectro de L* por banda de octava (ciclos por 1280 px)

Periodo en px = 1280 / banda (p. ej. 256-512 ~ 2.5-5 px; 32-64 ~ 20-40 px).

| Métrica | Ref. mediana | Ref. P25–P75 | m2-valley-v1 | m2-valley-v2 | m2-valley-v3-water | m2-valley-v4-taa | m2-valley-v5-low-taau | m2-atmosphere-fog |
|---|---|---|---|---|---|---|---|---|
| banda 2-4 | 0.1421 | 0.1153–0.2093 | 0.2380 | 0.2828 | 0.2991 | 0.3028 | 0.3104 | 0.0685 |
| banda 4-8 | 0.0669 | 0.0126–0.1125 | 0.1817 | 0.1998 | 0.1741 | 0.1770 | 0.1841 | 0.0979 |
| banda 8-16 | 0.0332 | 0.0065–0.1030 | 0.0836 | 0.0906 | 0.0787 | 0.0792 | 0.0817 | 0.2670 |
| banda 16-32 | 0.0185 | 0.0038–0.1026 | 0.0505 | 0.0568 | 0.0492 | 0.0502 | 0.0483 | 0.1066 |
| banda 32-64 | 0.0158 | 0.0028–0.0900 | 0.0491 | 0.0457 | 0.0397 | 0.0399 | 0.0389 | 0.0389 |
| banda 64-128 | 0.0136 | 0.0026–0.0612 | 0.0243 | 0.0231 | 0.0207 | 0.0200 | 0.0195 | 0.0223 |
| banda 128-256 | 0.0115 | 0.0018–0.0592 | 0.0194 | 0.0165 | 0.0153 | 0.0116 | 0.0096 | 0.0082 |
| banda 256-512 | 0.0056 | 0.0013–0.0450 | 0.0158 | 0.0121 | 0.0113 | 0.0028 | 0.0015 | 0.0041 |
| detalle medio 32-256 (suma) | 0.0394 | 0.0075–0.2104 | 0.0928 | 0.0853 | 0.0756 | 0.0716 | 0.0679 | 0.0694 |
| alta frecuencia 256-512 | 0.00557 | 0.00127–0.04501 | 0.01582 | 0.01210 | 0.01131 | 0.00279 | 0.00146 | 0.00406 |
| baja frecuencia 2-16 | 0.3070 | 0.1760–0.3946 | 0.5033 | 0.5732 | 0.5518 | 0.5590 | 0.5762 | 0.4334 |

## H. Exposición relativa a la mediana de referencias

EV = log2(mediana(Y ref) / mediana(Y render)). Positivo = el render está más oscuro que las referencias. Las referencias son JPG tonemapeados por Poly Haven, así que el EV es orientativo.

| Render | EV (mediana) | EV (P95) | EV (P99) | P95 de L* | P99 de L* |
|---|---|---|---|---|---|
| m2-valley-v1 | +1.25 | +1.43 | +1.40 | 63.4 | 65.4 |
| m2-valley-v2 | +1.52 | +1.53 | +1.45 | 61.6 | 64.4 |
| m2-valley-v3-water | +1.51 | +1.53 | +1.45 | 61.6 | 64.4 |
| m2-valley-v4-taa | +1.52 | +1.54 | +1.47 | 61.3 | 64.1 |
| m2-valley-v5-low-taau | +1.52 | +1.53 | +1.46 | 61.6 | 64.2 |
| m2-atmosphere-fog | +0.03 | +0.07 | +0.08 | 92.6 | 94.3 |
| *referencia (mediana)* | 0 | 0 | 0 | 94.4 | 96.4 |
