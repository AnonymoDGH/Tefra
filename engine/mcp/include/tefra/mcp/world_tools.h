#pragma once

// MCP tools over the DataModel (spec M3, ADR-012/015): get_tree, get_instance, list_classes, create_instance,
// set_properties, delete_instance, reparent, clone, find, get_script, set_script, save_place, load_place.
// Usage and argument reference: docs/research/m3-mcp/REPORT.md.

#include "tefra/core/types.h"

namespace tefra::world {
class DataModel;
}

namespace tefra::mcp {

class Server;

// Adds the DataModel tools to the server and returns how many were added. The DataModel must outlive the server, and
// its tools must run only on the thread that calls Server::poll() (they change the tree directly).
int register_world_tools(Server& server, world::DataModel& dm);

} // namespace tefra::mcp
