# Dependencias de terceros

Todas permisivas y compatibles con Apache-2.0. Cada una conserva su licencia en su carpeta.

| Dependencia | Versión | Licencia | Ruta | Uso |
|---|---|---|---|---|
| Vulkan-Headers | vulkan-sdk-1.4.341.0 | Apache-2.0 / MIT | `third_party/vulkan-headers` | API Vulkan (solo headers C) |
| volk | vulkan-sdk-1.4.341.0 | MIT | `third_party/volk` | Carga dinámica de Vulkan |
| Vulkan Memory Allocator | 3.4.0 | MIT | `third_party/vma` | Suballocation de memoria GPU |
| GLFW | 3.5.1 | zlib | `third_party/glfw` | Ventana, entrada, gamepad |
| doctest | 2.5.3 | MIT | `third_party/doctest` | Tests |
| stb_image / stb_image_write | commit 2c980bb (2026-08-01) | MIT o dominio público | `third_party/stb` | PNG (captura, golden images) |

Herramientas de build (no se redistribuyen): glslang (BSD-3 / Apache-2.0), CMake, Ninja.

## Assets

Todavía no hay. Política (spec §4): solo CC0, CC-BY o propios, con la procedencia registrada en `assets/PROVENANCE.md`.
