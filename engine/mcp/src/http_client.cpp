#include "tefra/mcp/http_client.h"

#include "net.h"

#include <cstring>

namespace tefra::mcp::http {

namespace {

constexpr usize kNpos = std::string_view::npos;

// Bodies from the server are small JSON replies or screenshots; allow generous room.
constexpr Limits kClientLimits{64 * 1024, 256ull * 1024 * 1024};

// Reads more bytes into buf. False on close, error or timeout.
bool read_more(net::Socket s, std::string& buf) {
    char chunk[8192];
    const int n = net::recv_some(s, chunk, static_cast<int>(sizeof chunk));
    if (n <= 0) return false;
    buf.append(chunk, static_cast<usize>(n));
    return true;
}

} // namespace

bool request(const std::string& host, u16 port, std::string_view method, std::string_view target,
             const std::vector<Header>& headers, std::string_view body, int timeout_ms, Response& out,
             std::string& error) {
    if (host != "127.0.0.1" && host != "localhost") {
        error = "only loopback hosts are allowed (127.0.0.1 / localhost)";
        return false;
    }
    if (!net::init()) {
        error = "socket start-up failed";
        return false;
    }
    int err = 0;
    const net::Socket s = net::connect_loopback(port, timeout_ms, err);
    if (s == net::kInvalidSocket) {
        error = "cannot connect to 127.0.0.1:" + std::to_string(port) + " (error " + std::to_string(err) + ")";
        net::cleanup();
        return false;
    }

    std::string wire;
    wire.reserve(256 + body.size());
    wire.append(method).append(" ").append(target).append(" HTTP/1.1\r\n");
    wire += "Host: " + host + ":" + std::to_string(port) + "\r\n";
    for (const Header& h : headers) wire += h.name + ": " + h.value + "\r\n";
    wire += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    wire += "Connection: close\r\n\r\n";
    wire.append(body);

    auto fail = [&](std::string why) {
        error = std::move(why);
        net::close_socket(s);
        net::cleanup();
        return false;
    };
    if (!net::send_all(s, wire.data(), wire.size())) return fail("send failed");

    std::string buf;
    usize head_end = kNpos;
    while ((head_end = buf.find("\r\n\r\n")) == kNpos)
        if (!read_more(s, buf)) return fail("connection closed before the response headers");

    Response r;
    if (!parse_response_head(std::string_view(buf).substr(0, head_end), r)) return fail("malformed status line");
    const usize body_start = head_end + 4;
    const std::string te = r.header("Transfer-Encoding");
    const std::string cl = r.header("Content-Length");

    if (!te.empty()) {
        for (;;) {
            usize consumed = 0;
            std::string decoded;
            std::string decode_error;
            int status = 0;
            const Parse p = decode_chunked(std::string_view(buf).substr(body_start), kClientLimits, consumed, decoded,
                                           decode_error, status);
            if (p == Parse::Complete) {
                r.body = std::move(decoded);
                break;
            }
            if (p == Parse::Error) return fail("bad chunked response: " + decode_error);
            if (!read_more(s, buf)) return fail("connection closed inside a chunked response");
        }
    } else if (!cl.empty()) {
        usize want = 0;
        for (char c : cl) {
            if (c < '0' || c > '9') return fail("bad Content-Length in response");
            want = want * 10 + static_cast<usize>(c - '0');
        }
        if (want > kClientLimits.max_body_bytes) return fail("response too large");
        while (buf.size() < body_start + want)
            if (!read_more(s, buf)) return fail("connection closed inside the response body");
        r.body = buf.substr(body_start, want);
    } else {
        while (read_more(s, buf)) {}
        r.body = buf.size() > body_start ? buf.substr(body_start) : std::string();
    }

    net::close_socket(s);
    net::cleanup();
    out = std::move(r);
    error.clear();
    return true;
}

} // namespace tefra::mcp::http
