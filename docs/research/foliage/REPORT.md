# Follaje de coníferas

## Técnica
- **Atlas de agujas horneado en CPU** (`make_needle_textures`, 1024×512 = 2×2 tiles de 512×256). Los tiles 0–2 son ramitas vivas (3 variantes) y el tile 3 es una ramita muerta. Cada ramita es geometría 3D: tallo, ramitas y unas 1000 agujas como cilindros, trazadas por rayo con supersampling 2×2.
  - Albedo azul-verde de abeto, con puntas más claras.
  - Alpha = cobertura (cutoff 0,4; mips con preservación de cobertura).
  - Normal en espacio tangente desde la geometría de las agujas.
  - AO por profundidad, guardado en el alpha del normal map.
- **Copa por verticilos irregulares.** Cada rama caída lleva 3 estaciones con 2 tarjetas de sub-rama a los flancos. Las normales se inclinan hacia fuera de la copa. Hay ramas muertas en el fuste bajo y una guía apical. Todo sigue el eje inclinado y curvado del tronco (`axis_point`).
- **Shader** (`gbuffer.frag`): el follaje (bit 0) mezcla un 35 % de la normal esférica de la copa con la del normal map. El AO horneado se aplica solo con el bit 3 (agujas).

## Ajuste tras la integración (sesión principal)
El resultado del agente era demasiado ralo para un bosque denso; se veían árboles "quemados". Cambios:
- verticilos cada 0,55–0,75 m (antes 0,8–1,1);
- 7–8 ramas por verticilo;
- 10 % de huecos (antes 20 %);
- tarjetas más largas y anchas;
- 3 estaciones por rama (antes 4).

LOD0 resultante: 3,0–4,3k triángulos por árbol, de los que ~1,5k son del tronco nuevo. El test exige < 4500.

## Coste (HD 520)
Sin medir. Los draws son los mismos que antes. Hay más fill alpha-tested en árboles cercanos. El atlas ocupa ~2,7 MB con mips y se hornea en ~0,25 s.

## Limitaciones y siguientes pasos
- La madera muerta usa el material de follaje (translucencia 0,5); debería tener un material propio.
- El LOD1 repite un tile con fract(u).
- No se ha comparado con fotos de referencia.
- Siguiente paso: impostores de copa horneados para la distancia media.

Capturas: `before_crowns.png`, `after_crowns.png` (versión del agente, antes del ajuste) y `needle_atlas.png`.
