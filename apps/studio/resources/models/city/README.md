# Props de ciudad (noche lluviosa)

Modelos `.glb` para la plantilla de ciudad de Tefra. Son obra propia, generada por
`tools/blender/make_city_props.py` con la API de Python de Blender (bpy), y se publican con
licencia **CC0**: sin marcas, logotipos ni nombres reales (el único texto es la palabra genérica "OPEN",
hecha con segmentos de tubo).

## Convenciones

- Unidades en metros. Blender usa Z arriba; el exportador glTF pasa a Y arriba.
- El frente de cada prop es el **+Y de Blender**, que se convierte en **-Z** en glTF.
- El origen es el punto de contacto con el suelo, o el punto del muro indicado en la tabla.
- Materiales PBR (rugosidad y metalicidad). Las fuentes de luz usan material emisivo
  (Emission Color + Emission Strength 1.0): `LampRed`, `LampAmber`, `LampGreen`, `LensLED`,
  `Headlight`, `TailLight`, `ShopLight`, `NeonPink`, `NeonCyan`.
- Cada prop tiene menos de 8 000 triángulos (los coches, menos de 15 000).

## Props

Tamaño en metros (x ancho, y profundidad, z alto). Tris = triángulos.

| Archivo | Tamaño (x × y × z) | Origen | Tris |
|---|---|---|---|
| `street_lamp.glb` | 0.42 × 2.23 × 7.47 | pie del poste, en el suelo | 1136 |
| `traffic_light.glb` | 0.30 × 0.40 × 4.00 | pie del poste, en el suelo | 1632 |
| `car_sedan.glb` | 1.93 × 4.76 × 1.42 | centro de la huella, en el suelo (carrocería 4,6 m) | 2916 |
| `car_hatch.glb` | 1.89 × 4.16 × 1.40 | centro de la huella, en el suelo (carrocería 4,0 m) | 2916 |
| `fire_hydrant.glb` | 0.45 × 0.40 × 0.69 | centro de la brida, en el suelo | 1764 |
| `trash_bin.glb` | 0.60 × 0.62 × 0.95 | centro, en el suelo | 724 |
| `bench.glb` | 1.80 × 0.47 × 0.90 | centro, en el suelo | 1320 |
| `manhole.glb` | 0.64 × 0.64 × 0.03 | centro, en el suelo | 1948 |
| `bollard.glb` | 0.24 × 0.24 × 0.98 | centro, en el suelo | 1220 |
| `parking_meter.glb` | 0.24 × 0.24 × 1.36 | pie del poste, en el suelo | 856 |
| `neon_sign_bar.glb` | 1.60 × 0.19 × 0.60 | muro, centro trasero del letrero | 1512 |
| `neon_sign_open.glb` | 1.60 × 0.19 × 0.60 | muro, centro trasero del letrero | 3400 |
| `ac_unit.glb` | 0.80 × 0.31 × 0.55 | muro, centro inferior de la cara trasera | 1056 |
| `fire_escape.glb` | 3.00 × 1.27 × 3.00 | muro, centro de la base | 1028 |
| `awning.glb` | 3.00 × 1.21 × 0.78 | muro, centro de la línea de montaje superior | 704 |
| `storefront.glb` | 6.00 × 0.42 × 3.50 | centro inferior del plano de fachada (y = 0) | 372 |

### Puntos de referencia

- **Cabeza de la farola** (`street_lamp.glb`): centro de la lente LED en
  `(0.00, 1.80, 7.16)` m, con la lente hacia abajo. El brazo de 1,8 m sale hacia +Y (hacia la calzada).
- **Lámparas del semáforo** (`traffic_light.glb`): rojo z = 3,7 m, ámbar z = 3,4 m, verde z = 3,1 m (todas miran a +Y).
- **Plano de la fachada** (`storefront.glb`): los escaparates y la puerta van en y < 0.12 m; el panel
  `ShopLight` queda detrás del cristal (y = -0,22 m) para que la tienda brille de noche.
- **Letreros de neón**: el origen está en el muro, en el centro del letrero. El tubo queda a y = 0,178 m.
  Las letras de `neon_sign_open.glb` se leen de izquierda a derecha desde +Y.

## Regenerar

Hace falta el paquete `bpy` (Python 3.13):

```sh
python3 -m venv /tmp/bpyenv && /tmp/bpyenv/bin/pip install bpy
/tmp/bpyenv/bin/python tools/blender/make_city_props.py [carpeta_de_salida]
```

La carpeta por defecto es `apps/studio/resources/models/city`. Los avisos de Draco y meshopt al
arrancar son normales: el exportador no comprime con esas bibliotecas.
