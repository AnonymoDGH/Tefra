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
| `rain_loop.wav` | lluvia constante en bucle (4 s: siseo, gotas sobre superficies duras, rumor del agua) | 16 bit mono, 8000 Hz |
| `hum_loop.wav` | zumbido de tubo fluorescente en bucle (2 s, 120 Hz y armónicos) | 16 bit mono, 8000 Hz |
| `wind_loop.wav` | viento en bucle sin costura (3,6 s, periódico por construcción) | 16 bit mono, 8000 Hz |
| `thunder.wav` | trueno cercano: crack seco y retumbo grave que decae en ~5 s (6 s) | 16 bit mono, 22050 Hz |
| `thunder_far.wav` | trueno lejano: retumbo grave de inicio suave, sin crack (7 s) | 16 bit mono, 22050 Hz |
| `lever.wav` | palanca metálica pesada: trinquete y golpe metálico al final (0,6 s) | 16 bit mono, 22050 Hz |
| `lamp_on.wav` | lámpara eléctrica al encender: clic y zumbido ascendente que se asienta en un zumbido (1,5 s) | 16 bit mono, 22050 Hz |
| `foghorn.wav` | sirena de niebla de diafragma: 100 Hz con armónicos impares, sostenida (4 s) | 16 bit mono, 22050 Hz |
| `surf_loop.wav` | oleaje rompiendo en rocas en bucle sin costura (12 s: tres embates cada 4 s, espuma y mar de fondo) | 16 bit mono, 8000 Hz |
| `wind_storm_loop.wav` | viento de tormenta en bucle sin costura (10 s: ráfagas, frecuencia central errante y silbido) | 16 bit mono, 8000 Hz |

Los efectos de disparo original ocupan menos de 60 KB cada uno. Los añadidos son más largos (trueno y sirena hasta
unos 310 KB; bucles de oleaje y tormenta a 8 kHz, de 160 a 190 KB); el conjunto, unos 1,5 MB.
El build copia esta carpeta a `<build>/bin/resources/sfx` (objetivo `tefra_sfx_resources`).
