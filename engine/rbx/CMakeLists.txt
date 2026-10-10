# Roblox place / model importer (ADR-017; user-authorized, clean-room from public format docs).
tefra_add_library(tefra_rbx
    SOURCES src/document.cpp src/binary_reader.cpp src/decompress.cpp src/xml_reader.cpp src/importer.cpp
    PUBLIC_DEPS tefra_world
    PRIVATE_DEPS tefra_zstd)
