# Optimización de carga (Beach / coast) — 2026-10-12

Objetivo: reducir el tiempo de apertura de la plantilla Beach sin cambiar la imagen. Medido en la nube (Xeon 2,8 GHz,
4 vCPU, GCC 13, SW Vulkan llvmpipe). **No son números del PC objetivo (i5-6200U)**; en el PC del usuario la generación
del terreno y la decodificación de los escaneos serán más lentas en valor absoluto, pero el orden de mejora debería
mantenerse.

## Qué cambia

- **Terreno en paralelo**: la generación de muestras, la máscara y la distancia a la orilla (EDT) usan el job system
  (`global_jobs()`) en vez de hilos ad hoc. Muestras independientes: el resultado no depende del número de hilos.
- **Caché de campos de terreno en disco** (`config_dir()/cache/terrain/*.tfld`): clave = tipo, semilla, resolución,
  texel, nivel del mar (solo costa) y versión. Se conservan los 6 más recientes. `TEFRA_NO_FIELD_CACHE=1` la desactiva.
- **Caché de glTF simplificados** (`config_dir()/cache/gltf/*.tglc`): clave = ruta, tamaño, mtime, `max_triangles`,
  `lod1_triangles`, versión y `sizeof(Vertex)`. Solo mallas estáticas; las esqueletales no se cachean.
  `TEFRA_NO_GLTF_CACHE=1` la desactiva.
- **glTF en dos fases**: `gltf_prepare()` (parseo, decodificación y simplificación, sin objetos GPU, en jobs) y
  `gltf_commit()` (subida de texturas, materiales y mallas, en el hilo principal).
- **Costa**: palmeras, atlas de frondes y rocas se preparan en paralelo antes de subir nada a la GPU.
- **Océano (shaders)**: los términos de olas con `keep == 0` se saltan (son exactamente cero). Sin cambio visible;
  no se ha medido el coste en GPU.

## Medidas (un ciclo por configuración; `tefra_bench --scene coast --frames 3 --no-validation`)

Tiempo de la línea "Terrain ... generated" y de la sección costera (desde esa línea hasta "Coast: 239 palms, 1079 rocks"):

| Build | Ejecución | Terreno | Costa (palmeras + rocas) | Proceso completo |
|---|---|---|---|---|
| release, base f9f8d7c | frío | 1974 ms | 8,3 s | 17,0 s |
| release, base f9f8d7c | caliente (sin caché que usar) | 2348 ms | 8,6 s | 18,0 s |
| release, nuevo | frío | 674 ms | 3,9 s | 11,9 s |
| release, nuevo | caliente | 41 ms | 0,9 s | 7,2 s |
| debug, base f9f8d7c | frío | 6744 ms | ~17,6 s | 41,8 s |
| debug, nuevo | frío | 3022 ms | 13,6 s (una ejecución; otra 6,3 s) | 34,5 s |
| debug, nuevo | caliente | 51 ms | 0,7 s | 12,2 s |

Los tiempos en debug son ruidosos (la misma configuración varió entre 6,3 y 13,6 s en la sección costera).

Imágenes: `--capture` del base y del nuevo son **idénticas píxel a píxel** (0 píxeles distintos) en debug y en release,
en frío y en caliente.

Tests: `ctest --preset linux-debug`, 11/11 pasan. Nuevo test: caché de terreno (la muestra leída del disco es idéntica
a la generada, y la caché se limita a 6 ficheros).

## Frame time

No medido de forma fiable: llvmpipe no representa el coste de la GPU del PC objetivo. Los cambios de océano son
cálculos exactamente nulos omitidos, por lo que no requieren flag de `RenderSettings`.

## Riesgos

- La caché ocupa disco: unos 24 MB por campo de 2048² (hasta 6 campos, ~144 MB) más los `.tglc` (hasta 64 ficheros).
- Cambiar el generador de terreno o la simplificación exige subir `kFieldCacheVersion` / `kGltfCacheVersion`; si no,
  se sirven datos antiguos.
- La clave del glTF usa tamaño y mtime, no un hash del contenido.
- El atlas de frondes de palmera no se cachea (~0,3 s en release).
- No se ha medido en el i5-6200U ni en la HD 520 / R5 M330.
