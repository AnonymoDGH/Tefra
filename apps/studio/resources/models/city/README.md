# Modelos de ciudad (noche lluviosa)

Modelos de la plantilla **Rainy City** (`city_rain`). Todos se generan con la API de Python de Blender (`bpy`)
a partir de scripts de `tools/blender/`:

| Script | Qué genera | Origen |
|---|---|---|
| `make_city_buildings.py` | `buildings/*.gltf` (8 edificios de 18 × 18 m) y `buildings/textures/` compartidas | Kits de fachada modular CC0 de Poly Haven y atlas de habitaciones propio |
| `make_city_ph_props.py` | hidrante, papeleras, banco, AC, alcantarilla, farola clásica, farol de pared, foco, caja eléctrica, barrera, coche tapado, cajas, palé, jardinera, neumático, cartel de suelo mojado, persianas | Poly Haven, CC0 (reducidos y con texturas embebidas) |
| `make_city_cars.py` | `car_sedan*`, `car_hatch*`, `car_suv*`, `car_taxi` | Propio, CC0 |
| `make_city_props.py` | `traffic_light*`, `neon_sign_*`, `awning`, `bollard`, `parking_meter` | Propio, CC0 (letras de neón: contornos de la fuente integrada de Blender, Inter, OFL) |

No hay marcas, logotipos ni nombres reales: los neones dicen palabras genéricas (OPEN, BAR, HOTEL, RAMEN, 24H,
PIZZA, CAFE, LIQUOR).

## Convenciones

- Metros. Blender usa Z arriba; el exportador glTF pasa a Y arriba.
- El frente de cada prop es el **+Y de Blender**, que en glTF es **-Z** (la plantilla orienta con `yaw_to`).
- Origen: punto de contacto con el suelo en el centro de la huella. Los neones tienen el origen en el muro.
- Los edificios tienen el origen en el centro de la huella, a nivel de calle. Plantas de 3 m y cornisa de 0,75 m.

## Edificios

Se montan con los módulos de 3 × 3 m de `modular_urban_apartments_facade` (enlucido) y `modular_factory_facade`
(ladrillo). Cada edificio es un solo `.gltf` con todas sus piezas fundidas por material, y todos comparten la carpeta
`textures/`: el motor carga cada textura una vez (`Scene::find_texture`).

- Planta baja: escaparates de lado a lado y una entrada por fachada.
- Plantas altas: dos tipos de ventana por edificio. Las plantas 5 y siguientes usan versiones simplificadas.
- Escalera de incendios en una fachada lateral, bajante en las esquinas, AC en algunas ventanas, losa de cubierta y
  máquinas en el tejado.
- Ventanas: el cristal se quita y en su lugar va una tarjeta con **interior mapping** (`"tefra_interior"` en los
  extras del material). El shader traza una habitación falsa detrás del cristal a partir de una celda del atlas
  `city_rooms.png` de 4 × 3: fila de habitaciones a oscuras, fila de pisos iluminados y fila de tiendas.
- Paredes: **parallax occlusion mapping** (`"tefra_parallax"` = profundidad en unidades UV, `"tefra_height"` = mapa
  de alturas). El ladrillo usa el `brick_disp` de Poly Haven (1,8 cm) y el enlucido una altura derivada de su AO
  (6 mm).

| Archivo | Tamaño | Plantas | Triángulos |
|---|---|---|---|
| `apt_a.gltf` | 18 × 18 × 18 m | 6 | 66 k |
| `apt_b.gltf` | 18 × 18 × 24 m | 8 | 85 k |
| `apt_c.gltf` | 18 × 18 × 15 m | 5 | 54 k |
| `apt_tower.gltf` | 18 × 18 × 42 m | 14 | 125 k |
| `loft_a.gltf` | 18 × 18 × 15 m | 5 | 92 k |
| `loft_b.gltf` | 18 × 18 × 21 m | 7 | 122 k |
| `loft_tower.gltf` | 18 × 18 × 30 m | 10 | 186 k |
| `warehouse.gltf` | 18 × 18 × 9 m | 3 | 54 k |

## Regenerar

Hace falta el paquete `bpy` (Python 3.13):

```sh
python3.13 -m venv /tmp/bpyenv && /tmp/bpyenv/bin/pip install bpy
/tmp/bpyenv/bin/python tools/blender/make_city_buildings.py [kits] [salida]
/tmp/bpyenv/bin/python tools/blender/make_city_ph_props.py [kits] [salida]
/tmp/bpyenv/bin/python tools/blender/make_city_cars.py [salida]
/tmp/bpyenv/bin/python tools/blender/make_city_props.py [salida]
```

`kits` es la carpeta de descargas de Poly Haven (`build/polyhaven` por defecto). Los scripts bajan lo que falte por
la API pública de Poly Haven. Los avisos de Draco y meshopt son normales.
