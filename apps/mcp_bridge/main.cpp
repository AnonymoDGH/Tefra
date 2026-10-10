// tefra_mcp: stdio <-> HTTP bridge for MCP clients that only speak stdio (for example Claude Desktop). It reads one
// JSON-RPC message per line from stdin, POSTs it to the MCP endpoint of Tefra Studio, and writes each reply as one
// line on stdout. It keeps the Mcp-Session-Id between messages. Diagnostics go to stderr only.
//
//   tefra_mcp [--url http://127.0.0.1:44755/mcp] [--timeout-ms 60000]

#include "tefra/core/json_value.h"
#include "tefra/mcp/http.h"
#include "tefra/mcp/http_client.h"
#include "tefra/mcp/jsonrpc.h"

#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace tefra;
namespace http = tefra::mcp::http;

namespace {

constexpr usize kNpos = std::string::npos;

struct Endpoint {
    std::string host;
    u16 port = 0;
    std::string target;
};

// Only http://127.0.0.1:PORT/... and http://localhost:PORT/... are accepted (ADR-015: localhost only).
bool parse_url(const std::string& url, Endpoint& ep) {
    const std::string prefix = "http://";
    if (url.compare(0, prefix.size(), prefix) != 0) return false;
    const std::string rest = url.substr(prefix.size());
    const usize slash = rest.find('/');
    const std::string authority = slash == kNpos ? rest : rest.substr(0, slash);
    ep.target = slash == kNpos ? "/" : rest.substr(slash);
    const usize colon = authority.rfind(':');
    if (colon == kNpos) return false;
    ep.host = authority.substr(0, colon);
    char* end = nullptr;
    const unsigned long port = std::strtoul(authority.c_str() + colon + 1, &end, 10);
    if (port == 0 || port > 65535 || (end && *end != '\0')) return false;
    ep.port = static_cast<u16>(port);
    return ep.host == "127.0.0.1" || ep.host == "localhost";
}

void write_line(const std::string& line) {
    std::fwrite(line.data(), 1, line.size(), stdout);
    std::fputc('\n', stdout);
    std::fflush(stdout);
}

void usage() { std::fprintf(stderr, "usage: tefra_mcp [--url http://127.0.0.1:44755/mcp] [--timeout-ms 60000]\n"); }

} // namespace

int main(int argc, char** argv) {
    std::string url = "http://127.0.0.1:44755/mcp";
    int timeout_ms = 60000;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--url" && i + 1 < argc) {
            url = argv[++i];
        } else if (arg == "--timeout-ms" && i + 1 < argc) {
            timeout_ms = std::atoi(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            usage();
            return 0;
        } else {
            usage();
            return 2;
        }
    }
    Endpoint ep;
    if (!parse_url(url, ep)) {
        std::fprintf(stderr, "tefra_mcp: --url must be http://127.0.0.1:PORT/path (loopback only)\n");
        return 2;
    }

    std::string session;
    std::string line;
    while (std::getline(std::cin, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
        if (line.empty()) continue;

        JsonValue msg;
        std::string parse_error;
        if (!JsonValue::parse(line, msg, &parse_error)) {
            write_line(mcp::make_error(JsonValue(), mcp::kParseError, "parse error: " + parse_error).dump());
            continue;
        }
        // Requests (with an id and a method) get a reply line. Notifications and client replies do not.
        const bool wants_reply = msg.is_object() && msg.contains("id") && msg.contains("method");

        std::vector<http::Header> headers = {
            {"Content-Type", "application/json"},
            {"Accept", "application/json, text/event-stream"},
            {"MCP-Protocol-Version", mcp::kProtocolVersion},
        };
        if (!session.empty()) headers.push_back(http::Header{"Mcp-Session-Id", session});

        http::Response resp;
        std::string error;
        if (!http::request(ep.host, ep.port, "POST", ep.target, headers, line, timeout_ms, resp, error)) {
            std::fprintf(stderr, "tefra_mcp: %s\n", error.c_str());
            if (wants_reply)
                write_line(mcp::make_error(msg["id"], mcp::kServerError,
                                           "Tefra Studio is not reachable at " + url + " (" + error +
                                               "). Start Tefra Studio, or tefra_mcp_demo, first.")
                               .dump());
            continue;
        }
        const std::string sid = resp.header("Mcp-Session-Id");
        if (!sid.empty()) session = sid;
        if (!wants_reply) continue;

        JsonValue body;
        if (!resp.body.empty() && JsonValue::parse(resp.body, body) && body.is_object()) {
            write_line(body.dump());
        } else {
            write_line(
                mcp::make_error(msg["id"], mcp::kServerError, "HTTP " + std::to_string(resp.status) + ": " + resp.body)
                    .dump());
        }
    }
    return 0;
}
