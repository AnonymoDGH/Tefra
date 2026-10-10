#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest.h>

#include "tefra/core/json.h"

#include <cmath>

using namespace tefra;

TEST_CASE("compact JSON structure and escaping") {
    JsonWriter j(false);
    j.begin_object()
        .kv("s", "a\"b\\c\n")
        .kv("i", i64(-3))
        .kv("u", u64(7))
        .kv("f", 1.5)
        .kv("b", true)
        .kv("nan", std::nan(""));
    j.begin_array("arr").value(i64(1)).value("x").end_array();
    j.begin_object("o").end_object();
    j.end_object();
    CHECK(j.str() == R"({"s":"a\"b\\c\n","i":-3,"u":7,"f":1.5,"b":true,"nan":null,"arr":[1,"x"],"o":{}})");
}
