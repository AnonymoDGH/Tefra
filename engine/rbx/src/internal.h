#pragma once

// Helpers shared by the binary and XML readers (not part of the public API).

#include "tefra/rbx/document.h"

#include <string>
#include <string_view>
#include <vector>

namespace tefra::rbx::detail {

// ASCII case-insensitive comparison.
bool ieq(std::string_view a, std::string_view b);

// Builds InstanceData::children and Document::roots from the parent links. `order` lists instance indices in the
// order children are appended to their parent (sibling order); indices it misses are appended afterwards. Invalid
// parents become roots, parent cycles are cut (with a warning) and instances nested deeper than kMaxDepth are
// marked dropped (with a warning).
void finalize_tree(Document& doc, const std::vector<u32>& order);

void warn(Document& doc, std::string message);

} // namespace tefra::rbx::detail
