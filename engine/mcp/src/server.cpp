#include "tefra/mcp/server.h"

#include "net.h"
#include "tefra/core/log.h"
#include "tefra/mcp/http.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace tefra::mcp {

namespace {

using Clock = std::chrono::steady_clock;
using http::Header;

constexpr usize kNpos = std::string_view::npos;
constexpr usize kMaxConnections = 32;
constexpr usize kMaxSessions = 64;
constexpr int kIdleTimeoutMs = 15000; // keep-alive connections close after this much silence
constexpr int kAcceptSliceMs = 100;   // how often the listener checks for stop()
constexpr int kWaitSliceMs = 50;      // how often a waiting request checks for stop() and its deadline
constexpr char kContinueLine[] = "HTTP/1.1 100 Continue\r\n\r\n";

// One tools/call waiting for the engine thread.
struct Job {
    enum class State : u8 { Queued, Running, Done, Cancelled };
    ToolHandler handler;
    JsonValue args;
    std::mutex m;
    std::condition_variable cv;
    State state = State::Queued;
    ToolResult result;
};

// One accepted socket and the thread serving it. Owned by the acceptor thread (stop() touches them only after
// joining it).
struct Conn {
    net::Socket sock = net::kInvalidSocket;
    std::thread thread;
    std::atomic<bool> finished{false};
};

struct Reply {
    int status = 200;
    std::vector<Header> headers;
    std::string body;
};

// DNS-rebinding defence (spec: Streamable HTTP, "Security Warning"): a browser page may only come from loopback.
// Requests without an Origin header (CLI clients, the stdio bridge) are always allowed.
bool origin_allowed(std::string_view origin) {
    const usize sep = origin.find("://");
    if (sep == kNpos) return false;
    const std::string_view scheme = origin.substr(0, sep);
    if (!http::iequals(scheme, "http") && !http::iequals(scheme, "https")) return false;
    const std::string_view authority = origin.substr(sep + 3);
    std::string_view host = authority;
    if (!authority.empty() && authority.front() == '[') {
        const usize close = authority.find(']');
        if (close == kNpos) return false;
        host = authority.substr(0, close + 1);
    } else {
        const usize colon = authority.find(':');
        if (colon != kNpos) host = authority.substr(0, colon);
    }
    return host == "localhost" || host == "127.0.0.1" || host == "[::1]";
}

Reply plain_reply(int status, std::string text) {
    Reply r;
    r.status = status;
    r.headers.push_back(Header{"Content-Type", "text/plain; charset=utf-8"});
    r.body = std::move(text);
    return r;
}

Reply json_reply(int status, const JsonValue& body, std::vector<Header> extra = {}) {
    Reply r;
    r.status = status;
    r.headers.push_back(Header{"Content-Type", "application/json"});
    for (Header& h : extra) r.headers.push_back(std::move(h));
    r.body = body.dump();
    return r;
}

} // namespace

struct Server::Impl {
    ToolRegistry tools;
    ServerInfo info;
    std::atomic<u32> timeout_ms{15000};

    // Guarded by mu: the job queue, the session list and the lifecycle flags.
    std::mutex mu;
    std::deque<std::shared_ptr<Job>> queue;
    std::vector<std::string> sessions;
    std::mt19937_64 rng{std::random_device{}()};
    bool running = false;
    std::atomic<bool> stopping{false};

    // Lifecycle, touched by the owning (engine) thread only.
    net::Socket listener = net::kInvalidSocket;
    u16 bound_port = 0;
    std::string last_error;
    std::thread acceptor;
    std::vector<std::unique_ptr<Conn>> conns;

    // ---- Engine thread side -------------------------------------------------------------------------------

    usize run_queued(usize max_calls) {
        usize ran = 0;
        while (ran < max_calls) {
            std::shared_ptr<Job> job;
            {
                std::lock_guard<std::mutex> lock(mu);
                if (queue.empty()) break;
                job = std::move(queue.front());
                queue.pop_front();
            }
            {
                std::lock_guard<std::mutex> lock(job->m);
                if (job->state != Job::State::Queued) continue; // its request gave up: do not run it late
                job->state = Job::State::Running;
            }
            ToolResult result = job->handler(job->args);
            {
                std::lock_guard<std::mutex> lock(job->m);
                job->result = std::move(result);
                job->state = Job::State::Done;
            }
            job->cv.notify_all();
            ++ran;
        }
        return ran;
    }

    // ---- HTTP thread side ---------------------------------------------------------------------------------

    // Queues a tool call for the engine thread and waits for its result (or the timeout / stop).
    ToolResult run_on_engine(const ToolDef& tool, const JsonValue& args) {
        auto job = std::make_shared<Job>();
        job->handler = tool.handler;
        job->args = args;
        {
            std::lock_guard<std::mutex> lock(mu);
            if (!running || stopping) return ToolResult::error("The Tefra MCP server is stopping.");
            queue.push_back(job);
        }
        const u32 limit_ms = timeout_ms.load();
        const auto deadline = Clock::now() + std::chrono::milliseconds(limit_ms);
        std::unique_lock<std::mutex> lk(job->m);
        for (;;) {
            if (job->state == Job::State::Done) return std::move(job->result);
            if (job->state == Job::State::Cancelled) return ToolResult::error("The tool call was cancelled.");
            if (job->state == Job::State::Queued) {
                const bool late = Clock::now() >= deadline;
                if (late || stopping.load()) {
                    job->state = Job::State::Cancelled;
                    if (late)
                        return ToolResult::error("Tefra Studio did not run the tool within " +
                                                 std::to_string(limit_ms) +
                                                 " ms. The editor's main loop must call Server::poll().");
                    return ToolResult::error("The Tefra MCP server stopped before the tool ran.");
                }
            }
            job->cv.wait_for(lk, std::chrono::milliseconds(kWaitSliceMs));
        }
    }

    std::string new_session() {
        char id[40];
        std::lock_guard<std::mutex> lock(mu);
        const u64 a = rng();
        const u64 b = rng();
        std::snprintf(id, sizeof id, "%016llx%016llx", static_cast<unsigned long long>(a),
                      static_cast<unsigned long long>(b));
        if (sessions.size() >= kMaxSessions) sessions.erase(sessions.begin());
        sessions.emplace_back(id);
        return id;
    }

    bool has_session(const std::string& id) {
        std::lock_guard<std::mutex> lock(mu);
        for (const std::string& s : sessions)
            if (s == id) return true;
        return false;
    }

    bool drop_session(const std::string& id) {
        std::lock_guard<std::mutex> lock(mu);
        for (auto it = sessions.begin(); it != sessions.end(); ++it)
            if (*it == id) {
                sessions.erase(it);
                return true;
            }
        return false;
    }

    Reply post(const http::Request& req) {
        const std::string content_type = req.header("Content-Type");
        std::string_view media = content_type;
        media = media.substr(0, media.find(';'));
        if (!http::iequals(http::trim(media), "application/json"))
            return plain_reply(415, "Content-Type must be application/json");
        const std::string version = req.header("MCP-Protocol-Version");
        if (!version.empty() && version != kProtocolVersion)
            return plain_reply(400, "Unsupported MCP-Protocol-Version " + version + "; this server speaks " +
                                        kProtocolVersion + ".");

        JsonValue msg;
        std::string parse_error;
        if (!JsonValue::parse(req.body, msg, &parse_error))
            return json_reply(400, make_error(JsonValue(), kParseError, "parse error: " + parse_error));

        const JsonValue& method = msg["method"];
        const bool is_initialize = method.is_string() && method.as_string() == "initialize";
        if (!is_initialize) {
            const std::string session = req.header("Mcp-Session-Id");
            if (session.empty())
                return json_reply(400, make_error(JsonValue(), kInvalidRequest,
                                                  "missing Mcp-Session-Id header: send initialize first"));
            if (!has_session(session))
                return json_reply(
                    404, make_error(JsonValue(), kInvalidRequest, "unknown Mcp-Session-Id: send initialize again"));
        }

        const Executor exec = [this](const ToolDef& tool, const JsonValue& args) { return run_on_engine(tool, args); };
        const std::optional<JsonValue> reply_body = handle_message(msg, tools, info, exec);
        if (!reply_body) {
            Reply accepted;
            accepted.status = 202; // notification or client response: no body
            return accepted;
        }
        std::vector<Header> extra;
        if (is_initialize && reply_body->find("result")) extra.push_back(Header{"Mcp-Session-Id", new_session()});
        return json_reply(200, *reply_body, std::move(extra));
    }

    Reply route(const http::Request& req) {
        const std::string_view path = std::string_view(req.target).substr(0, req.target.find('?'));
        if (path != "/mcp") return plain_reply(404, "Not found. The MCP endpoint is POST /mcp.");
        const std::string origin = req.header("Origin");
        if (!origin.empty() && !origin_allowed(origin)) return plain_reply(403, "Origin not allowed.");
        if (req.method == "POST") return post(req);
        if (req.method == "DELETE") {
            const std::string session = req.header("Mcp-Session-Id");
            if (session.empty()) return plain_reply(400, "Mcp-Session-Id header required.");
            if (!drop_session(session)) return plain_reply(404, "Unknown session.");
            return plain_reply(200, "");
        }
        // No server-initiated stream (SSE) is offered, so GET is refused as the spec allows.
        Reply r = plain_reply(405, "Use POST /mcp. This server does not open an SSE stream.");
        r.headers.push_back(Header{"Allow", "POST, DELETE"});
        return r;
    }

    void serve(Conn& conn) {
        const net::Socket sock = conn.sock;
        net::set_timeouts(sock, kIdleTimeoutMs);
        const http::Limits limits;
        std::string buf;
        bool continue_sent = false;
        char chunk[8192];
        for (;;) {
            http::Request req;
            usize consumed = 0;
            std::string error;
            int status = 0;
            const http::Parse parsed = http::parse_request(buf, limits, req, consumed, error, status);
            if (parsed == http::Parse::Error) {
                const std::string bytes = http::build_response(status, {}, error, false);
                net::send_all(sock, bytes.data(), bytes.size());
                return;
            }
            if (parsed == http::Parse::Incomplete) {
                if (!continue_sent && http::wants_continue(buf)) {
                    net::send_all(sock, kContinueLine, sizeof kContinueLine - 1);
                    continue_sent = true;
                }
                const int n = net::recv_some(sock, chunk, static_cast<int>(sizeof chunk));
                if (n <= 0 || stopping.load()) return;
                buf.append(chunk, static_cast<usize>(n));
                continue;
            }
            buf.erase(0, consumed);
            continue_sent = false;
            const bool keep = req.keep_alive && !stopping.load();
            const Reply reply = route(req);
            const std::string bytes = http::build_response(reply.status, reply.headers, reply.body, keep);
            if (!net::send_all(sock, bytes.data(), bytes.size()) || !keep) return;
        }
    }

    // Joins the threads of finished connections (or of all, when all is set) and closes their sockets.
    void reap(bool all) {
        for (auto it = conns.begin(); it != conns.end();) {
            Conn& c = **it;
            if (!all && !c.finished.load()) {
                ++it;
                continue;
            }
            if (c.thread.joinable()) c.thread.join();
            net::close_socket(c.sock);
            it = conns.erase(it);
        }
    }

    void accept_loop() {
        while (!stopping.load()) {
            reap(false);
            if (net::wait_readable(listener, kAcceptSliceMs) <= 0) continue;
            const net::Socket client = net::accept_client(listener);
            if (client == net::kInvalidSocket) continue;
            if (conns.size() >= kMaxConnections) {
                const std::string busy = http::build_response(503, {}, "too many connections", false);
                net::send_all(client, busy.data(), busy.size());
                net::close_socket(client);
                continue;
            }
            auto conn = std::make_unique<Conn>();
            conn->sock = client;
            Conn* raw = conn.get();
            raw->thread = std::thread([this, raw] {
                serve(*raw);
                raw->finished.store(true);
            });
            conns.push_back(std::move(conn));
        }
    }
};

Server::Server() : impl_(std::make_unique<Impl>()) {}

Server::~Server() { stop(); }

void Server::set_server_info(std::string name, std::string version, std::string instructions) {
    impl_->info = ServerInfo{std::move(name), std::move(version), std::move(instructions)};
}

void Server::set_request_timeout_ms(u32 ms) { impl_->timeout_ms.store(ms); }

bool Server::add_tool(std::string name, std::string description, std::string_view input_schema_json,
                      ToolHandler handler) {
    return impl_->tools.add(std::move(name), std::move(description), input_schema_json, std::move(handler));
}

bool Server::start(u16 port) {
    Impl& s = *impl_;
    if (running()) return true;
    if (!net::init()) {
        s.last_error = "socket start-up failed";
        return false;
    }
    u16 bound = 0;
    bool in_use = false;
    int err = 0;
    const net::Socket listener = net::listen_loopback(port, bound, in_use, err);
    if (listener == net::kInvalidSocket) {
        s.last_error = in_use
                           ? "port " + std::to_string(port) + " is already in use"
                           : "cannot bind 127.0.0.1:" + std::to_string(port) + " (error " + std::to_string(err) + ")";
        TEFRA_LOG_ERROR("mcp: %s", s.last_error.c_str());
        net::cleanup();
        return false;
    }
    s.last_error.clear();
    s.listener = listener;
    s.bound_port = bound;
    s.stopping.store(false);
    {
        std::lock_guard<std::mutex> lock(s.mu);
        s.running = true;
    }
    s.acceptor = std::thread([&s] { s.accept_loop(); });
    TEFRA_LOG_INFO("MCP server listening on http://127.0.0.1:%u/mcp", static_cast<unsigned>(bound));
    return true;
}

void Server::stop() {
    Impl& s = *impl_;
    std::vector<std::shared_ptr<Job>> cancelled;
    {
        std::lock_guard<std::mutex> lock(s.mu);
        if (!s.running) return;
        s.running = false;
        s.stopping.store(true);
        for (auto& job : s.queue) {
            std::lock_guard<std::mutex> jl(job->m);
            if (job->state == Job::State::Queued) job->state = Job::State::Cancelled;
            cancelled.push_back(job);
        }
        s.queue.clear();
        s.sessions.clear();
    }
    for (auto& job : cancelled) job->cv.notify_all();
    if (s.acceptor.joinable()) s.acceptor.join();          // exits within kAcceptSliceMs
    for (auto& c : s.conns) net::shutdown_socket(c->sock); // wakes blocked recv calls
    s.reap(true);
    net::close_socket(s.listener);
    s.listener = net::kInvalidSocket;
    s.bound_port = 0;
    net::cleanup();
}

bool Server::running() const {
    std::lock_guard<std::mutex> lock(impl_->mu);
    return impl_->running;
}

u16 Server::port() const {
    std::lock_guard<std::mutex> lock(impl_->mu);
    return impl_->running ? impl_->bound_port : u16(0);
}

const std::string& Server::last_error() const { return impl_->last_error; }

usize Server::tool_count() const { return impl_->tools.size(); }

usize Server::poll(usize max_calls) { return impl_->run_queued(max_calls); }

} // namespace tefra::mcp
