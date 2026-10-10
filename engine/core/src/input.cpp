#include "tefra/core/input.h"

namespace tefra {

namespace {
constexpr const char* kNames[] = {
    "Unknown", "A",         "B",      "C",      "D",         "E",          "F",           "G",
    "H",       "I",         "J",      "K",      "L",         "M",          "N",           "O",
    "P",       "Q",         "R",      "S",      "T",         "U",          "V",           "W",
    "X",       "Y",         "Z",      "Zero",   "One",       "Two",        "Three",       "Four",
    "Five",    "Six",       "Seven",  "Eight",  "Nine",      "Space",      "Return",      "Escape",
    "Tab",     "Backspace", "Delete", "Insert", "Home",      "End",        "PageUp",      "PageDown",
    "Left",    "Right",     "Up",     "Down",   "LeftShift", "RightShift", "LeftControl", "RightControl",
    "LeftAlt", "RightAlt",  "F1",     "F2",     "F3",        "F4",         "F5",          "F6",
    "F7",      "F8",        "F9",     "F10",    "F11",       "F12"};
static_assert(sizeof(kNames) / sizeof(kNames[0]) == static_cast<usize>(Key::Count), "key names out of sync");
} // namespace

const char* key_name(Key k) {
    const usize i = static_cast<usize>(k);
    return i < static_cast<usize>(Key::Count) ? kNames[i] : "Unknown";
}

Key key_from_name(std::string_view name) {
    for (usize i = 1; i < static_cast<usize>(Key::Count); ++i)
        if (name == kNames[i]) return static_cast<Key>(i);
    return Key::Unknown;
}

} // namespace tefra
