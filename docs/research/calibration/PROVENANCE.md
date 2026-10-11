# Procedencia de las referencias de calibración

Descargadas el 2026-10-10 desde la API pública de Poly Haven (`https://api.polyhaven.com`, endpoint `/files/<id>`, campo `tonemapped`).
Cada archivo se verificó con el MD5 publicado en la API (7/7 coinciden).

Licencia: Poly Haven publica sus assets bajo **CC0** (dominio público; https://polyhaven.com/license). La API no expone un campo de licencia por asset, así que la atribución se basa en la política general del sitio. Autores según la API.

| Archivo (`refs/`) | Asset | Autor(es) según API | Resolución | MD5 | Uso en el análisis |
|---|---|---|---|---|---|
| misty_pines.jpg | https://polyhaven.com/a/misty_pines | Greg Zaal | 8192x4096 equirect | d2073546d2a8d0c7bf5e82d071fa2b29 | Pinar en niebla (principal) |
| phalzer_forest_01.jpg | https://polyhaven.com/a/phalzer_forest_01 | Andreas Mischok | 8192x4096 | 48b75b0f597981a1b2b17629b553b849 | Pinar seco, luz difusa |
| je_gray_02.jpg | https://polyhaven.com/a/je_gray_02 | Greg Zaal | 8192x4096 | 82884fa69ddaedcd0af5b159523da16a | Luz matinal con sombras largas (eucaliptos, no conífera) |
| spruit_dawn.jpg | https://polyhaven.com/a/spruit_dawn | Greg Zaal | 8192x4096 | cf1136d0cb6ff8efe6d98221b08ba0f5 | Amanecer con bruma baja y arbolado |
| misty_dawn.jpg | https://polyhaven.com/a/misty_dawn | Greg Zaal | 8192x4096 | 4372868cc7b3db5d6d89c971dbc31232 | Amanecer brumoso, campo con árboles |
| kloofendal_misty_morning.jpg | https://polyhaven.com/a/kloofendal_misty_morning | Greg Zaal | 8192x4096 | 8926ab2bdff2e61ef2e25a0249b5cbb5 | Bruma densa sin arbolado |
| kloofendal_28d_misty.jpg | https://polyhaven.com/a/kloofendal_28d_misty | Greg Zaal (Processing), Jenelle van Heerden (Photography) | 8192x4096 | 73197068cb736046dd9abb8261237397 | Bruma densa sin arbolado |

Notas:
- Los JPG son los "tonemapped" de Poly Haven: panoramas 360 (equirectangulares) ya tonemapeados por el autor, no los HDR lineales. Su curva de tonos y su exposición son las del autor, así que los valores absolutos de luminancia son orientativos. Las comparaciones relativas (contraste, croma, espectro, forma del histograma) son más robustas.
- Del panorama se extraen vistas rectilíneas de 1280x720 con HFOV 91.49 grados (la cámara del valle: fovy 60 grados, 16:9) y pitch 0, con supermuestreo 2x. Los yaws elegidos están en `extract_views.py`.
- Tamaño total de `refs/`: ~166 MB. No conviene subirlos al repo sin decidir antes (LFS o ignorar).
