// tefra_info: lists Vulkan adapters and whether each meets the Tefra baseline (spec ADR-003), as JSON.
#include "tefra/core/cli.h"
#include "tefra/core/fs.h"
#include "tefra/core/json.h"
#include "tefra/core/sysinfo.h"
#include "tefra/rhi/device.h"

#include <cstdio>

using namespace tefra;

int main(int argc, char** argv) {
    CommandLine cl(argc, argv);
    if (cl.has("help")) {
        std::printf("tefra_info [--out FILE.json]\n  Lists GPUs and the baseline check.\n");
        return 0;
    }
    const auto adapters = rhi::enumerate_adapters();
    const SystemInfo sys = system_info();
    JsonWriter j;
    j.begin_object();
    j.kv("schema", "tefra-info/1");
    j.begin_object("system").kv("os", sys.os).kv("cpu", sys.cpu).kv("threads", sys.threads).kv("ram_mb", sys.ram_mb);
    j.end_object();
    j.begin_array("adapters");
    for (const auto& a : adapters) {
        j.begin_object()
            .kv("index", a.index)
            .kv("name", a.name)
            .kv("type", a.type)
            .kv("vendor_id", a.vendor_id)
            .kv("device_id", a.device_id)
            .kv("api", a.api_version)
            .kv("driver", a.driver_name + " " + a.driver_info)
            .kv("device_local_mb", a.device_local_mb)
            .kv("software", a.software)
            .kv("meets_baseline", a.meets_baseline);
        j.begin_array("missing");
        for (const auto& m : a.missing) j.value(m.c_str());
        j.end_array();
        j.end_object();
    }
    j.end_array();
    j.end_object();
    const char* out = cl.get("out");
    if (out) {
        if (!file_write_all(out, j.str().data(), j.str().size())) {
            std::fprintf(stderr, "cannot write %s\n", out);
            return 1;
        }
    } else {
        std::fputs(j.str().c_str(), stdout);
    }
    bool any = false;
    for (const auto& a : adapters) any = any || a.meets_baseline;
    return adapters.empty() ? 2 : (any ? 0 : 3);
}
