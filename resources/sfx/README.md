# Efectos de sonido integrados (`builtin:<nombre>`)

Procedencia: **obra propia**, generada por síntesis pura en `scripts/gen_sfx.py` (Python + módulo `wave`, sin
muestras, grabaciones ni código de terceros). Licencia: **CC0**. Se regeneran con
`python3 -I scripts/gen_sfx.py` y se verifican con `python3 -I scripts/gen_sfx.py --check` (la salida es
determinista: mismos bytes en cada ejecución).

| Archivo | Uso | Formato |
|---|---|---|
| `jump.wav` | salto del personaje (barrido ascendente) | 16 bit mono, 22050 Hz |
| `land.wav` | aterrizaje (golpe grave con ráfaga) | 16 bit mono, 22050 Hz |
| `footstep_grass.wav` | pisada sobre hierba, arena o suelo | 16 bit mono, 22050 Hz |
| `footstep_stone.wav` | pisada sobre piedra u otros materiales | 16 bit mono, 22050 Hz |
| `checkpoint.wav` | campanilla de checkpoint (arpegio C6-E6-G6) | 16 bit mono, 22050 Hz |
| `win.wav` | fanfarria corta de victoria | 16 bit mono, 22050 Hz |
| `death.wav` | "bloop" descendente y suave al morir | 16 bit mono, 22050 Hz |
| `click.wav` | clic de interfaz | 16 bit mono, 22050 Hz |
| `coin.wav` | moneda (dos notas ascendentes) | 16 bit mono, 22050 Hz |
| `hum_loop.wav` | zumbido de tubo fluorescente en bucle (2 s, 120 Hz y armónicos) | 16 bit mono, 8000 Hz |
| `wind_loop.wav` | viento en bucle sin costura (3,6 s, periódico por construcción) | 16 bit mono, 8000 Hz |

Los efectos cortos ocupan menos de 60 KB cada uno; el conjunto, unos 240 KB.
El build copia esta carpeta a `<build>/bin/resources/sfx` (objetivo `tefra_sfx_resources`).
