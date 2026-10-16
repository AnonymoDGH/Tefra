# Texturas con profundidad simulada (2026-10-16)

Cómo una imagen plana puede parecer tener relieve, qué técnicas existen y qué usa Tefra.

## Técnicas, de la más barata a la más cara

| Técnica | Qué hace | Limitación |
|---|---|---|
| **Normal map** | Cambia la normal por píxel: la luz "ve" relieve. | La silueta y el paralaje siguen siendo planos: de lado se nota que es una pegatina. |
| **Bump / height bump** | Igual, pero calcula la normal a partir de una altura (Tefra lo usa en los materiales procedurales de Part). | Igual que el normal map. |
| **Parallax mapping** (Kaneko 2001) | Desplaza la UV según la altura y el ángulo de visión: lo alto se acerca y lo bajo se aleja. | Con ángulos rasantes se deforma. |
| **Steep parallax / Parallax Occlusion Mapping (POM)** (Tatarchuk 2006) | Avanza el rayo de visión por capas dentro del mapa de alturas hasta chocar con la superficie e interpola entre las dos últimas capas. Los ladrillos tapan el mortero de detrás. | Coste por píxel (8–24 muestras). La silueta sigue plana. |
| **Relief mapping** | POM con búsqueda binaria y autosombra hacia la luz. | Más caro. |
| **Interior mapping** (van Dongen 2008) | Traza un rayo dentro de una caja virtual detrás de una ventana y dibuja paredes, suelo y techo desde una textura. Una sola cara simula una habitación entera. | Las habitaciones son cajas vacías con dibujos. |
| **Teselación + desplazamiento** | Geometría real a partir del mapa de alturas: cambia la silueta. | Necesita geometry/tessellation shaders y muchos triángulos. En la HD 520 no compensa. |

## Qué usa Tefra

- **POM** (`shaders/gbuffer.frag`, `parallax_uv`):
  - flag de material bit 5;
  - altura en el alfa del normal map (el cargador glTF la empaqueta desde `"tefra_height"`);
  - profundidad en `pad0`;
  - 8–20 pasos según el ángulo, solo hasta 22 m con fundido desde 12 m;
  - `textureGrad` con las derivadas originales para que los mips no salten;
  - oscurece las grietas (AO de la altura).
  - Se usa en el ladrillo (1,8 cm) y el enlucido (6 mm) de los edificios de la ciudad. Captura: `docs/captures/parallax-brick.png`.
- **Interior mapping** (`interior_uv`):
  - flag bit 6;
  - atlas de 4 × 3 habitaciones;
  - pared del fondo nítida, y laterales, suelo y techo con el color difuminado de la celda (mip alto) y más oscuros.
  - Lo usan todas las ventanas y escaparates de la ciudad.

## Cómo activarlo en un modelo propio

En Blender, con propiedades personalizadas del material (se exportan como `extras` con "Include > Custom Properties"):

- `tefra_parallax` = profundidad en unidades UV (metros de relieve ÷ metros por unidad UV);
- `tefra_height` = ruta del mapa de alturas relativa al `.gltf` (gris, blanco = alto, mismo tamaño que el normal map);
- o `tefra_interior` = fondo de la habitación en anchos de ventana, con UVs dentro de una celda de un atlas 4 × 3.

## Pendiente

- Autosombra del POM hacia el sol y las luces.
- POM en los materiales procedurales de Part (Brick, Cobblestone) con una función de altura barata.
- Silueta (POM con recorte en los bordes) o teselación en el preset High.
