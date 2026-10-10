# Tefra engine

Tefra (motor 3D) es un motor open source, clean-room, orientado a **mundos abiertos fotorrealistas** con rendimiento escalable por niveles de calidad. Usa C++20 y Vulkan 1.2 y funciona en Windows (MinGW) y Linux.

> Estado: **M0, fundaciones**. Hay build, RHI Vulkan, captura headless, `tefra_bench` con salida JSON y CI. Ver `docs/specs/` y `CLAUDE.md`.

## Compilar

Requisitos: CMake ≥ 3.25, Ninja y GCC ≥ 13 o Clang ≥ 17. Para compilar shaders hace falta glslang: en Windows viene con el Vulkan SDK y en Linux es el paquete `glslang-tools`.

```sh
# Linux
cmake --preset linux-release && cmake --build --preset linux-release
ctest --preset linux-release

# Windows (MinGW-w64 + Ninja en el PATH, Vulkan SDK instalado)
cmake --preset windows-mingw-release
cmake --build --preset windows-mingw-release
```

Cada push compila un build de Windows: está en *Actions → CI → artifact `tefra-windows`*.

## Ejecutables

| Ejecutable | Qué hace |
|---|---|
| `tefra_info` | Lista las GPU e indica si cumplen el baseline (Vulkan 1.2 + bindless + sync2) |
| `tefra_viewer` | Ventana interactiva. Botón derecho = mirar, WASD/QE = moverse, Shift = rápido, F12 = captura, Esc = salir |
| `tefra_bench` | N frames y un informe JSON `tefra-bench/1`: frame time p50/p95/p99, ms de GPU por pase, memoria y validación |

Opciones comunes: `--gpu N`, `--width/--height`, `--frames`, `--warmup`, `--capture file.png`, `--validation` y `--strict`. Ver `--help`.

## Licencia

Apache-2.0. Las dependencias están en `THIRD_PARTY.md`.
