# Survey: fotorrealismo de bosque de coníferas brumoso (resumen del agente investigador)

Leyenda: [V] verificado en fuente, [M] de memoria, [E] estimación para HD 520 (medir en el PC objetivo).

## Técnicas ordenadas por impacto/coste
1. Atmósfera con sky-view LUT + aerial perspective (Hillaire 2020; Bruneton & Neyret 2008). Coste: 0,2–0,4 ms. **Hecho.**
2. GTAO + bent normals para ocluir el cielo/IBL (Jimenez et al. 2016; XeGTAO). Coste: 1–1,5 ms a media resolución. **GTAO hecho; faltan bent normals.**
3. Translucencia de follaje two-sided (Lumen docs; Separable SSS de Jimenez 2015). Coste: 0,1–0,3 ms. **Hecho (básica).**
4. Niebla volumétrica en froxels + niebla de altura (Hillaire 2015; Wronski 2014). Coste: 1–2 ms. **En curso (agente).**
5. Mips con cobertura alfa preservada (Castaño) / hashed alpha (Wyman & McGuire 2017). Coste: 0 ms. **Hecho.**
6. Impostores octaédricos para copas lejanas (Brucks; Simplygon). Coste: 0,2–0,5 ms; el límite es la memoria.
7. Viento multicapa con ráfagas (Ghost of Tsushima). Amplitud ∝ altura²; ramas 0,3–1,5 Hz, hojas 2–6 Hz.
8. Hierba procedural culleada en GPU (Ghost of Tsushima GDC). Coste: 1–2 ms; limitar a 20–30 m. **En curso (agente).**
9. Terreno por capas con mezcla por altura y pendiente, ruido de 10–30 m (Horizon FW). **En curso (agente).**
10. Cascadas + PCSS + alfa del follaje en la sombra. **En curso (agente).**
11. Luz de cielo SH modulada por oclusión direccional: sin ella, las sombras parecen de "juego barato". **En curso (agente: oclusión del dosel).**
12. AgX + control de saturación de verdes. **AgX hecho.**
13. LOD con transición por dither + TAA.
14. Shafts de luz a través de la copa (salen del punto 4).
15. TAA/TAAU para follaje alpha-tested. **Hecho.**

## Errores típicos de "bosque barato"
- Niebla de color fijo.
- Sotobosque iluminado como si estuviera a cielo abierto, o negro.
- Verdes saturados: las coníferas tienen albedo bajo y tono grisáceo.
- Follaje sin translucencia.
- Árboles lejanos que se vuelven transparentes.
- Árboles idénticos.
- Texturas repetidas en el suelo.
- Viento uniforme.
- Popping de LOD.
- Troncos sin oclusión de contacto.
- Sombras sin penumbra.

## Calibración física
- **Luminancia del cielo** [V]:
  - nublado: 1 000–2 000 cd/m²
  - despejado: 3 000–10 000 cd/m²
  - disco solar en el horizonte: 6×10⁵ cd/m²
- **Iluminancia** [V]:
  - sol directo: 32k–100k lux
  - día sin sol directo: 10k–25k lux
  - nublado: ~1 000 lux
  - amanecer: ~400 lux
- **Interior de bosque:** T = exp(−0,52·LAI) (Pierce & Running 1988). Con LAI 3–6, T ≈ 0,04–0,21 para el haz directo. El suelo recibe ~50–200 lux en bosque denso.
- **Albedos:**
  - bosque de coníferas: 0,08–0,15 [V]
  - corteza: 0,05–0,15 [E]
  - agujas: 0,03–0,10 [E]
  - hojarasca: 0,10–0,20 [E]
  - musgo: 0,05–0,15 [E]
  - suelo húmedo: 0,05–0,10 [E]
- **Niebla** (Koschmieder: V ≈ 3/β):
  - bruma matinal: β ≈ 0,006–0,015 m⁻¹
  - valle: β₀ ≈ 0,02–0,04 m⁻¹ con H = 20–60 m

## Fuentes
- Hillaire 2020: https://diglib.eg.org/items/8a3e5350-18b3-46bd-9274-3add5af88c75
- Bruneton & Neyret 2008: https://diglib.eg.org/items/0af15b7c-795a-4cca-97d3-4cc9b99126e4
- Frostbite 2015: https://www.frostbite.com/2015/08/physically-based-unified-volumetric-rendering-in-frostbite/
- GTAO 2016: https://www.iryoku.com/downloads/Practical-Realtime-Strategies-for-Accurate-Indirect-Occlusion.pdf
- Castaño, Computing Alpha Mipmaps: https://ludicon.com/castano/blog/articles/computing-alpha-mipmaps/
- Hashed Alpha Testing: https://research.nvidia.com/sites/default/files/pubs/2017-02_Hashed-Alpha-Testing/Wyman2017Hashed.pdf
- Ghost of Tsushima, Procedural Grass: https://gdcvault.com/play/1027214/
- Horizon FW, Deferred Texturing: https://gdcvault.com/play/1027553/
- Simplygon, impostors: https://simplygon.com/posts/4bf1787d-6d76-48a7-9111-787d6985005c
