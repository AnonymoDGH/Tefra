# Terrain micro detail

## Técnicas
- **Displacement en alfa del albedo** (mapas Poly Haven CC0 `*_disp`, empaquetados en upload): sin texturas extra ni samplers nuevos.
- **Height blending** entre capas (Mittring/Unity "height-based blend"): las piedras asoman sobre la tierra en lugar de un fundido lineal.
- **POM** (parallax occlusion mapping, Tatarchuk 2006) solo a < 16 m, con pasos adaptados al ángulo de visión; más lejos, mapeo normal.
- **Stochastic tiling** (Heitz & Neyret 2018, versión simplificada de 3 tiles hexagonales) para romper la repetición.
- **Macro variation**: ruido de baja frecuencia sobre albedo y rugosidad a escala de decenas de metros.

## Controles
`RenderSettings::terrain_detail` (`--no-terrain-detail`). `--pitch` en el bench para encuadres hacia el suelo.

## Coste estimado (HD 520, tier Low)
POM: ~8–16 taps extra por píxel cercano. Stochastic: ×3 fetches por capa. Estimado 0,4–0,8 ms a 540p. **Sin medir**: el número real tiene que salir del PC del usuario.

## Limitaciones y siguientes pasos
- POM sin sombras propias (self-shadowing).
- Sin tessellation (no es baseline en la iGPU objetivo).
- Siguiente paso: virtual texturing cacheado para cubrir los 2×2 km sin tanto fetch.

Capturas: `before.png` y `after.png`.
