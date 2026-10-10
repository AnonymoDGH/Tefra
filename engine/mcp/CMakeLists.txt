# MCP server for Tefra Studio (spec M3, ADR-015). Winsock2 on Windows; POSIX sockets elsewhere.
set(TEFRA_MCP_SYSTEM_LIBS "")
if(WIN32)
    set(TEFRA_MCP_SYSTEM_LIBS ws2_32)
endif()

tefra_add_library(tefra_mcp
    SOURCES
        src/http.cpp src/http_client.cpp src/jsonrpc.cpp src/net.cpp
        src/server.cpp src/tools.cpp src/world_tools.cpp
    PUBLIC_DEPS tefra_world ${TEFRA_MCP_SYSTEM_LIBS})
