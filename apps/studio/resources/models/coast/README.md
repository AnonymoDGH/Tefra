# Modelos de costa (faro en la tormenta)

Modelos de la plantilla **Lighthouse in the Storm** (`lighthouse_storm`), generados con la API de Python de Blender:

| Script | Qué genera | Origen |
|---|---|---|
| `tools/blender/make_lighthouse.py` | `lighthouse.gltf`, `lens_off.glb`, `lens_on.glb`, `keeper_cottage.gltf`, `rowboat.glb` y `textures/` compartidas | Geometría propia (CC0); texturas CC0 de Poly Haven |
| `tools/blender/make_coast_ph_props.py` | acantilados, rocas, muelle modular, barco, barriles, caja, farol, cubo, salvavidas, boya, chaleco, botas, sombrero, escalera, cama, mesa, taburete, reloj, radio, brújula, latas, aplique, lámpara colgante, prismáticos, estantería, cofre | Poly Haven, CC0 (colecciones "Smuggler's Cove" y "Project Lighthouse") |

## El faro (`lighthouse.gltf`)

- Origen en el suelo, en el centro del zócalo. La puerta mira a +Y de Blender (-Z en glTF).
- Zócalo octogonal de piedra de 1,2 m.
- Fuste troncocónico de mampostería: radio de 3,6 m abajo a 2,7 m arriba, muro de 0,55 m. Está encalado con dos franjas rojas y enlucido por dentro. La puerta y seis ventanas atraviesan el muro.
- Galería con ménsulas y barandilla a 20,5 m.
- Sala de guardia con puerta a la galería.
- Linterna de 10 lados: antepecho de hierro, cristales con astrágalos y cornisa.
- Cúpula, bola de ventilación y pararrayos.
- La lente (`lens_*.glb`) va aparte, con el centro a 25,3 m. Al encender la lámpara se cambia por la versión emisiva.
- La escalera de caracol, los suelos y las colisiones son Parts que coloca la plantilla. Sus medidas están en `build_lighthouse_storm` y tienen que coincidir con el script.
- Relieve (POM):
  - piedra: `seaworn_stone_tiles`, 2 cm;
  - enlucido: `white_plaster_rough_01`, 6 mm;
  - piedra de la casa: `rustic_stone_wall`, 3 cm;
  - pizarra: `roof_slates_02`, 1,2 cm.

## Regenerar

```sh
python3.13 -m venv /tmp/bpyenv && /tmp/bpyenv/bin/pip install bpy
/tmp/bpyenv/bin/python tools/blender/make_lighthouse.py [cache] [salida]
/tmp/bpyenv/bin/python tools/blender/make_coast_ph_props.py [cache] [salida]
```
