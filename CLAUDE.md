# Tefra (motor 3D) — memoria del proyecto

Las sesiones no recuerdan nada: este archivo es la memoria. Actualízalo al final de cada sesión.

## Estado actual

- **Fase 0: diseño.** No hay código del motor. Prohibido escribir código del motor, instalar dependencias del producto o crear la estructura del proyecto hasta que el usuario apruebe el spec por escrito.
- Brief original del usuario: proceso por fases (spec → plan → implementación), clean-room estricto, visión, hardware y restricciones. Lo esencial está resumido abajo.
- Repo `AnonymoDGH/Tefra`: **público** a 2026-10-10. El brief exige que sea privado → no hacer push hasta que el usuario lo ponga privado o autorice lo contrario.

## Decisiones (ADR cortos)

- ADR-000: nombre **Tefra** (decidido por el usuario, no proponer otros). Nada de "RAGE", "estilo GTA" ni marcas de terceros.
- ADR-001: clean-room. Ni código, ni assets, ni formatos, ni nombres de archivo de Rockstar/Take-Two u otros juegos comerciales (incluye filtraciones y lectores RPF/RSC).

## Pendiente de decidir (en este orden, de una en una)

1. Vertical slice objetivo. ← en curso
2. Lenguaje + API (hipótesis: C++20 + Vulkan 1.2, volk + VMA).
3. Plataformas (Windows primero, luego Linux).
4. Licencia (Apache-2.0 / MIT / MPL-2.0).
5. Alcance de herramientas/editor.
6. Arquitectura de render (forward+/clustered, deferred o visibility buffer) para el tier Low.

## Hardware objetivo (tier Low = PC del usuario)

- i5-6200U (2C/4T, AVX2, **sin AVX-512**), 7,9 GB de RAM, Windows 10.
- Intel HD 520 (Vulkan 1.2.203) y AMD R5 M330 de 2 GB (Vulkan 1.2.170). Sin VK 1.3, sin dynamic rendering, sin mesh shaders, sin RT y sin VRS.
- Límites: push constants ≤ 128 B, ≤ 8 descriptor sets, 4096 allocations (VMA obligatorio), sin fp16 en AMD, 32 KB de shared memory en AMD.
- Toolchain: MinGW-w64 g++ 15.2, CMake 4.0.2, Ninja 1.13, Vulkan SDK 1.4.341.1. **Sin MSVC ni clang.**
- Todo número de rendimiento sale de su PC (ejecutable `bench` → JSON).

## Entorno de la nube (medido 2026-10-10)

- Xeon @ 2,80 GHz (no 3,00), 4 vCPU sin SMT, AVX-512. 15 GiB de RAM, ~31 GB de disco libre. Ubuntu 24.04. Sin GPU.
- gcc 13.3, clang 18.1.3, CMake 3.28.3, Ninja 1.11.1, Rust 1.97, Python 3.13, git-lfs.
- No vienen preinstalados: glslc, glslang, slangc, dxc, mingw-w64 ni ccache. Todos están en apt; el Vulkan SDK de LunarG se puede descargar desde aquí.
- Lavapipe (`mesa-vulkan-drivers` 25.2.8) funciona: expone **Vulkan 1.4**, dynamic rendering y mesh shaders. Es un superconjunto del hardware Low, así que el CI tiene que forzar el perfil baseline (Vulkan Profiles layer con el JSON de las GPU del usuario) para probar la ruta real.
- Cuidado con `-march=native` en la nube: generaría AVX-512, que el i5-6200U no tiene.

## Comandos de build y test

Todavía no hay (fase de diseño).

## Próximos pasos

1. El usuario responde a la pregunta del vertical slice.
2. Seguir con las preguntas 2–6, presentar el diseño por secciones y escribir el spec en `docs/specs/`.
