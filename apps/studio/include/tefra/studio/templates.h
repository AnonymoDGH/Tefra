#pragma once

// Place templates for "New" on the start page (spec M3b §1): each one fills an empty DataModel.

#include "tefra/world/datamodel.h"

#include <string_view>
#include <vector>

namespace tefra::studio {

struct TemplateInfo {
    const char* id;          // "baseplate", "obby", ... (also the thumbnail file name)
    const char* name;        // shown on the card
    const char* description; // one line
    const char* category;    // "Basic" or "Game"
};

const std::vector<TemplateInfo>& templates();
const TemplateInfo* find_template(std::string_view id);

// Fills `dm` (a fresh DataModel) with the template. False for an unknown id.
bool build_template(std::string_view id, world::DataModel& dm);

} // namespace tefra::studio
