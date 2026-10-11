#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest.h>

#include "tefra/core/json_value.h"
#include "tefra/mcp/http.h"
#include "tefra/mcp/http_client.h"
#include "tefra/mcp/jsonrpc.h"
#include "tefra/mcp/server.h"
#include "tefra/mcp/tools.h"
#include "tefra/mcp/world_tools.h"
#include "tefra/world/datamodel.h"

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

using namespace tefra;
using namespace tefra::mcp;
namespace http = tefra::mcp::http;

namespace {

const char kInitialize[] =
    R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},)"
    R"("clientInfo":{"name":"test","version":"0"}}})";

JsonValue parse_or_fail(std::string_view text) {
    JsonValue v;
    const bool ok = JsonValue::parse(text, v);
    CHECK(ok);
    return v;
}

http::Parse parse(std::string_view raw, http::Request& req, usize& used, int& status, const http::Limits& limits = {}) {
    std::string error;
    return http::parse_request(raw, limits, req, used, error, status);
}

ToolHandler echo_handler() {
    return [](const JsonValue& args) { return ToolResult::text(args["message"].as_string()); };
}

// Sends one request from a helper thread and runs poll() on this thread until the reply arrives.
struct Rpc {
    int status = 0;
    std::string body;
    std::string session;
    std::string transport_error;
};

Rpc rpc_polled(Server& server, const std::string& session, const std::string& body) {
    Rpc out;
    std::atomic<bool> done{false};
    std::thread client([&] {
        std::vector<http::Header> headers = {{"Content-Type", "application/json"}};
        if (!session.empty()) headers.push_back(http::Header{"Mcp-Session-Id", session});
        http::Response resp;
        std::string error;
        if (http::request("127.0.0.1", server.port(), "POST", "/mcp", headers, body, 10000, resp, error)) {
            out.status = resp.status;
            out.body = resp.body;
            out.session = resp.header("Mcp-Session-Id");
        } else {
            out.transport_error = error;
        }
        done = true;
    });
    while (!done.load()) {
        server.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    client.join();
    return out;
}

} // namespace

// ---- HTTP parser -------------------------------------------------------------------------------------------

TEST_CASE("http: complete request, case-insensitive headers, exact consumption") {
    const std::string raw = "POST /mcp HTTP/1.1\r\nHost: 127.0.0.1\r\ncontent-length: 7\r\n"
                            "Content-Type: application/json\r\n\r\n{\"a\":1}extra";
    http::Request req;
    usize used = 0;
    int status = 0;
    REQUIRE(parse(raw, req, used, status) == http::Parse::Complete);
    CHECK(req.method == "POST");
    CHECK(req.target == "/mcp");
    CHECK(req.header("CONTENT-TYPE") == "application/json");
    CHECK(req.body == "{\"a\":1}");
    CHECK(used == raw.size() - 5); // "extra" stays for the next request
    CHECK(req.keep_alive);
}

TEST_CASE("http: incomplete heads and bodies ask for more bytes") {
    http::Request req;
    usize used = 0;
    int status = 0;
    CHECK(parse("POST / HTTP/1.1\r\nHost: x\r\n", req, used, status) == http::Parse::Incomplete);
    CHECK(parse("POST / HTTP/1.1\r\nContent-Length: 10\r\n\r\nabc", req, used, status) == http::Parse::Incomplete);
}

TEST_CASE("http: pipelined requests are parsed one after another") {
    const std::string first = "GET /a HTTP/1.1\r\n\r\n";
    const std::string second = "GET /b HTTP/1.1\r\n\r\n";
    const std::string both = first + second;
    http::Request req;
    usize used = 0;
    int status = 0;
    REQUIRE(parse(both, req, used, status) == http::Parse::Complete);
    CHECK(req.target == "/a");
    CHECK(used == first.size());
    REQUIRE(parse(std::string_view(both).substr(used), req, used, status) == http::Parse::Complete);
    CHECK(req.target == "/b");
}

TEST_CASE("http: chunked bodies, with extensions and trailers") {
    const std::string raw = "POST / HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n4;ext=1\r\nWiki\r\n5\r\npedia\r\n"
                            "0\r\nTrailer: x\r\n\r\n";
    http::Request req;
    usize used = 0;
    int status = 0;
    REQUIRE(parse(raw, req, used, status) == http::Parse::Complete);
    CHECK(req.body == "Wikipedia");
    CHECK(used == raw.size());
    CHECK(parse(raw.substr(0, raw.size() - 4), req, used, status) == http::Parse::Incomplete);
}

TEST_CASE("http: malformed and oversized requests are errors with the right status") {
    http::Request req;
    usize used = 0;
    int status = 0;
    CHECK(parse("POSTX\r\n\r\n", req, used, status) == http::Parse::Error);
    CHECK(status == 400);
    CHECK(parse("GET mcp HTTP/1.1\r\n\r\n", req, used, status) == http::Parse::Error);
    CHECK(parse("GET / HTTP/2.0\r\n\r\n", req, used, status) == http::Parse::Error);
    CHECK(status == 505);
    CHECK(parse("POST / HTTP/1.1\r\nContent-Length: 999999999999\r\n\r\n", req, used, status) == http::Parse::Error);
    CHECK(status == 413);
    CHECK(parse("POST / HTTP/1.1\r\nContent-Length: 1x\r\n\r\n", req, used, status) == http::Parse::Error);
    CHECK(status == 400);
    CHECK(parse("GET / HTTP/1.1\r\nX: a\r\n folded\r\n\r\n", req, used, status) == http::Parse::Error);
    CHECK(parse("POST / HTTP/1.1\r\nTransfer-Encoding: gzip\r\n\r\n", req, used, status) == http::Parse::Error);
    const std::string big_header = "GET / HTTP/1.1\r\nX: " + std::string(200, 'a') + "\r\n\r\n";
    const http::Limits tight{64, 1024};
    CHECK(parse(big_header, req, used, status, tight) == http::Parse::Error);
    CHECK(status == 431);
    CHECK(parse("GET / HTTP/1.1\r\nX: " + std::string(200, 'a'), req, used, status, tight) == http::Parse::Error);
}

TEST_CASE("http: keep-alive follows the version and the Connection header") {
    http::Request req;
    usize used = 0;
    int status = 0;
    REQUIRE(parse("GET / HTTP/1.0\r\n\r\n", req, used, status) == http::Parse::Complete);
    CHECK_FALSE(req.keep_alive);
    REQUIRE(parse("GET / HTTP/1.0\r\nConnection: keep-alive\r\n\r\n", req, used, status) == http::Parse::Complete);
    CHECK(req.keep_alive);
    REQUIRE(parse("GET / HTTP/1.1\r\nConnection: close\r\n\r\n", req, used, status) == http::Parse::Complete);
    CHECK_FALSE(req.keep_alive);
}

TEST_CASE("http: Expect 100-continue detection and response building") {
    CHECK(http::wants_continue("POST / HTTP/1.1\r\nExpect: 100-continue\r\nContent-Length: 5\r\n\r\n"));
    CHECK_FALSE(http::wants_continue("POST / HTTP/1.1\r\nContent-Length: 5\r\n\r\n"));

    const std::string r = http::build_response(200, {{"Content-Type", "application/json"}}, "{}", false);
    CHECK(r.rfind("HTTP/1.1 200 OK\r\n", 0) == 0);
    CHECK(r.find("Content-Length: 2\r\n") != std::string::npos);
    CHECK(r.find("Connection: close\r\n\r\n{}") != std::string::npos);

    http::Response resp;
    REQUIRE(http::parse_response_head("HTTP/1.1 202 Accepted\r\nMcp-Session-Id: abc\r\n", resp));
    CHECK(resp.status == 202);
    CHECK(resp.header("mcp-session-id") == "abc");
}

// ---- Tools and arguments ------------------------------------------------------------------------------------

TEST_CASE("tools: registry rejects bad names, bad schemas and duplicates") {
    ToolRegistry reg;
    CHECK(reg.add("get_tree", "d", R"({"type":"object"})", echo_handler()));
    CHECK_FALSE(reg.add("get_tree", "d", R"({"type":"object"})", echo_handler()));
    CHECK_FALSE(reg.add("bad name", "d", R"({"type":"object"})", echo_handler()));
    CHECK_FALSE(reg.add("not_object", "d", R"({"type":"string"})", echo_handler()));
    CHECK_FALSE(reg.add("broken", "d", "{not json", echo_handler()));
    CHECK(reg.size() == 1);
    CHECK(reg.find("get_tree") != nullptr);
}

TEST_CASE("tools: argument validation reports the first problem") {
    const JsonValue schema = parse_or_fail(
        R"({"type":"object","properties":{"n":{"type":"integer"},"s":{"type":"string","enum":["a","b"]}},)"
        R"("required":["n"]})");
    CHECK(validate_arguments(schema, parse_or_fail(R"({"n":1})")).empty());
    CHECK(validate_arguments(schema, parse_or_fail(R"({"n":1,"s":null})")).empty()); // null = absent
    CHECK(validate_arguments(schema, parse_or_fail("{}")).find("'n'") != std::string::npos);
    CHECK(validate_arguments(schema, parse_or_fail(R"({"n":1.5})")).find("integer") != std::string::npos);
    CHECK(validate_arguments(schema, parse_or_fail(R"({"n":1,"s":"c"})")).find("allowed") != std::string::npos);
    CHECK(!validate_arguments(schema, parse_or_fail("[1]")).empty());
}

TEST_CASE("tools: results serialise to MCP content items") {
    const JsonValue text = ToolResult::text("hi").to_json();
    CHECK(text["content"].at(0)["type"].as_string() == "text");
    CHECK(text["content"].at(0)["text"].as_string() == "hi");
    CHECK_FALSE(text["isError"].as_bool());
    const JsonValue err = ToolResult::error("nope").to_json();
    CHECK(err["isError"].as_bool());
    const JsonValue img = ToolResult::image("QUJD").to_json();
    CHECK(img["content"].at(0)["type"].as_string() == "image");
    CHECK(img["content"].at(0)["mimeType"].as_string() == "image/png");
}

// ---- JSON-RPC dispatch (no sockets) -------------------------------------------------------------------------

TEST_CASE("jsonrpc: initialize, ping, tools/list and notifications") {
    ToolRegistry reg;
    REQUIRE(reg.add("echo", "Echoes.",
                    R"({"type":"object","properties":{"message":{"type":"string"}},"required":["message"]})",
                    echo_handler()));
    ServerInfo info;
    info.instructions = "hello";
    const Executor direct = [](const ToolDef& t, const JsonValue& a) { return t.handler(a); };

    const auto init = handle_message(parse_or_fail(kInitialize), reg, info, direct);
    REQUIRE(init.has_value());
    CHECK((*init)["id"].as_int() == 1);
    CHECK((*init)["result"]["protocolVersion"].as_string() == "2025-06-18");
    CHECK((*init)["result"]["serverInfo"]["name"].as_string() == "tefra-studio");
    CHECK((*init)["result"]["capabilities"].contains("tools"));
    CHECK((*init)["result"]["instructions"].as_string() == "hello");

    CHECK_FALSE(
        handle_message(parse_or_fail(R"({"jsonrpc":"2.0","method":"notifications/initialized"})"), reg, info, direct)
            .has_value());

    const auto ping = handle_message(parse_or_fail(R"({"jsonrpc":"2.0","id":"p","method":"ping"})"), reg, info, direct);
    REQUIRE(ping.has_value());
    CHECK((*ping)["id"].as_string() == "p");
    CHECK((*ping)["result"].is_object());
    CHECK((*ping)["result"].size() == 0);

    const auto list =
        handle_message(parse_or_fail(R"({"jsonrpc":"2.0","id":3,"method":"tools/list"})"), reg, info, direct);
    REQUIRE(list.has_value());
    CHECK((*list)["result"]["tools"].size() == 1);
    CHECK((*list)["result"]["tools"].at(0)["name"].as_string() == "echo");
    CHECK((*list)["result"]["tools"].at(0)["inputSchema"]["type"].as_string() == "object");
}

TEST_CASE("jsonrpc: tools/call runs the executor; argument problems are tool errors, bad calls are RPC errors") {
    ToolRegistry reg;
    REQUIRE(reg.add("echo", "Echoes.",
                    R"({"type":"object","properties":{"message":{"type":"string"}},"required":["message"]})",
                    echo_handler()));
    ServerInfo info;
    int runs = 0;
    const Executor counting = [&](const ToolDef& t, const JsonValue& a) {
        ++runs;
        return t.handler(a);
    };

    const auto ok = handle_message(
        parse_or_fail(
            R"({"jsonrpc":"2.0","id":"abc","method":"tools/call","params":{"name":"echo","arguments":{"message":"hi"}}})"),
        reg, info, counting);
    REQUIRE(ok.has_value());
    CHECK((*ok)["id"].as_string() == "abc");
    CHECK((*ok)["result"]["content"].at(0)["text"].as_string() == "hi");
    CHECK(runs == 1);

    const auto bad = handle_message(
        parse_or_fail(R"({"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"echo","arguments":{}}})"), reg,
        info, counting);
    REQUIRE(bad.has_value());
    CHECK((*bad)["result"]["isError"].as_bool());
    CHECK(runs == 1); // rejected before the handler ran

    const auto unknown =
        handle_message(parse_or_fail(R"({"jsonrpc":"2.0","id":6,"method":"tools/call","params":{"name":"nope"}})"), reg,
                       info, counting);
    CHECK((*unknown)["error"]["code"].as_int() == kInvalidParams);
    const auto no_name =
        handle_message(parse_or_fail(R"({"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"arguments":{}}})"),
                       reg, info, counting);
    CHECK((*no_name)["error"]["code"].as_int() == kInvalidParams);
    const auto array_args = handle_message(
        parse_or_fail(R"({"jsonrpc":"2.0","id":8,"method":"tools/call","params":{"name":"echo","arguments":[1]}})"),
        reg, info, counting);
    CHECK((*array_args)["error"]["code"].as_int() == kInvalidParams);
}

TEST_CASE("jsonrpc: malformed messages get the right error codes") {
    ToolRegistry reg;
    ServerInfo info;
    const Executor direct = [](const ToolDef& t, const JsonValue& a) { return t.handler(a); };

    const auto batch = handle_message(parse_or_fail("[]"), reg, info, direct);
    REQUIRE(batch.has_value());
    CHECK((*batch)["error"]["code"].as_int() == kInvalidRequest);
    CHECK((*batch)["id"].is_null());

    const auto object_id =
        handle_message(parse_or_fail(R"({"jsonrpc":"2.0","id":{"x":1},"method":"ping"})"), reg, info, direct);
    CHECK((*object_id)["error"]["code"].as_int() == kInvalidRequest);

    const auto no_version = handle_message(parse_or_fail(R"({"id":1,"method":"ping"})"), reg, info, direct);
    CHECK((*no_version)["error"]["code"].as_int() == kInvalidRequest);

    const auto unknown =
        handle_message(parse_or_fail(R"({"jsonrpc":"2.0","id":2,"method":"resources/list"})"), reg, info, direct);
    CHECK((*unknown)["error"]["code"].as_int() == kMethodNotFound);

    const auto response = handle_message(parse_or_fail(R"({"jsonrpc":"2.0","id":3,"result":{}})"), reg, info, direct);
    CHECK_FALSE(response.has_value());
}

// ---- Server over loopback ----------------------------------------------------------------------------------

TEST_CASE("server: tool calls run on the thread that calls poll()") {
    Server server;
    REQUIRE(server.start(0));
    CHECK(server.port() != 0);
    const std::thread::id main_thread = std::this_thread::get_id();
    CHECK(server.add_tool("where", "Reports the thread it runs on.", R"({"type":"object"})",
                          [main_thread](const JsonValue&) {
                              return ToolResult::text(std::this_thread::get_id() == main_thread ? "main" : "other");
                          }));

    const Rpc init = rpc_polled(server, "", kInitialize);
    CHECK(init.status == 200);
    CHECK_FALSE(init.session.empty());
    const Rpc call =
        rpc_polled(server, init.session,
                   R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"where","arguments":{}}})");
    CHECK(call.status == 200);
    CHECK(parse_or_fail(call.body)["result"]["content"].at(0)["text"].as_string() == "main");

    const Rpc unknown_session = rpc_polled(server, "0000", R"({"jsonrpc":"2.0","id":3,"method":"ping"})");
    CHECK(unknown_session.status == 404);

    server.stop();
    CHECK_FALSE(server.running());
    CHECK(server.port() == 0);
}

TEST_CASE("server: a call nobody polls fails with a message after the timeout") {
    Server server;
    REQUIRE(server.start(0));
    server.set_request_timeout_ms(150);
    CHECK(server.add_tool("never", "Never polled.", R"({"type":"object"})",
                          [](const JsonValue&) { return ToolResult::text("ran"); }));
    std::atomic<bool> done{false};
    std::string body;
    std::thread client([&] {
        http::Response resp;
        std::string error;
        const std::vector<http::Header> headers = {{"Content-Type", "application/json"}};
        std::string init_error;
        http::Response init;
        if (http::request("127.0.0.1", server.port(), "POST", "/mcp", headers, kInitialize, 5000, init, init_error)) {
            const std::vector<http::Header> with_session = {{"Content-Type", "application/json"},
                                                            {"Mcp-Session-Id", init.header("Mcp-Session-Id")}};
            http::request("127.0.0.1", server.port(), "POST", "/mcp", with_session,
                          R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"never","arguments":{}}})",
                          5000, resp, error);
        }
        body = resp.body;
        done = true;
    });
    // Deliberately no poll(): the request must give up on its own.
    while (!done.load()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
    client.join();
    CHECK(body.find("did not run the tool") != std::string::npos);
    CHECK(body.find("\"isError\":true") != std::string::npos);
    server.stop();
}

TEST_CASE("world tools: create, all-or-nothing set and get over JSON-RPC") {
    world::DataModel dm; // must outlive the server
    Server server;
    REQUIRE(server.start(0));
    CHECK(register_world_tools(server, dm) == 13);
    CHECK(server.tool_count() == 13);

    const Rpc init = rpc_polled(server, "", kInitialize);
    REQUIRE(init.status == 200);
    const std::string session = init.session;

    const Rpc created =
        rpc_polled(server, session,
                   R"({"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"create_instance","arguments":)"
                   R"({"class":"Part","parent_path":"Workspace","name":"P","properties":{"Size":[1,2,3]}}}})");
    const JsonValue created_reply = parse_or_fail(created.body);
    const JsonValue created_payload = parse_or_fail(created_reply["result"]["content"].at(0)["text"].as_string());
    CHECK(created_payload["path"].as_string() == "Workspace.P");
    world::Instance* part = dm.find_path("Workspace.P");
    CHECK(part != nullptr);
    if (part == nullptr) {
        server.stop();
        return;
    }
    CHECK(part->get_vec3("Size").x == doctest::Approx(1.0f));
    CHECK(part->get_vec3("Size").z == doctest::Approx(3.0f));

    // One bad value: nothing changes, not even the valid Anchored.
    const Rpc rejected =
        rpc_polled(server, session,
                   R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"set_properties","arguments":)"
                   R"({"path":"Workspace.P","properties":{"Size":"big","Anchored":true}}}})");
    CHECK(parse_or_fail(rejected.body)["result"]["isError"].as_bool());
    CHECK_FALSE(part->get_bool("Anchored"));

    server.stop();
}
