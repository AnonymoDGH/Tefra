#include "tefra/mcp/http.h"

#include <cctype>

namespace tefra::mcp::http {

namespace {

constexpr usize kNpos = std::string_view::npos;

bool is_token_char(char c) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (std::isalnum(u)) return true;
    switch (c) {
    case '!':
    case '#':
    case '$':
    case '%':
    case '&':
    case '\'':
    case '*':
    case '+':
    case '-':
    case '.':
    case '^':
    case '_':
    case '`':
    case '|':
    case '~': return true;
    default: return false;
    }
}

bool is_token(std::string_view s) {
    if (s.empty()) return false;
    for (char c : s)
        if (!is_token_char(c)) return false;
    return true;
}

// True when a comma-separated header value contains the token (case-insensitive), e.g. "keep-alive, Upgrade".
bool has_token(std::string_view value, std::string_view token) {
    usize pos = 0;
    while (pos <= value.size()) {
        usize comma = value.find(',', pos);
        if (comma == kNpos) comma = value.size();
        if (iequals(trim(value.substr(pos, comma - pos)), token)) return true;
        pos = comma + 1;
    }
    return false;
}

bool parse_header_lines(std::string_view block, std::vector<Header>& out, std::string& error) {
    usize pos = 0;
    while (pos < block.size()) {
        usize eol = block.find("\r\n", pos);
        if (eol == kNpos) eol = block.size();
        const std::string_view line = block.substr(pos, eol - pos);
        pos = eol + 2;
        if (line.empty()) continue;
        if (line.front() == ' ' || line.front() == '\t') {
            error = "obsolete line folding is not supported";
            return false;
        }
        const usize colon = line.find(':');
        if (colon == kNpos || colon == 0) {
            error = "malformed header line";
            return false;
        }
        const std::string_view name = line.substr(0, colon);
        if (!is_token(name)) {
            error = "invalid header name";
            return false;
        }
        out.push_back(Header{std::string(name), std::string(trim(line.substr(colon + 1)))});
    }
    return true;
}

bool parse_decimal(std::string_view s, u64& out) {
    if (s.empty() || s.size() > 18) return false;
    out = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        out = out * 10 + static_cast<u64>(c - '0');
    }
    return true;
}

bool parse_hex(std::string_view s, u64& out) {
    if (s.empty() || s.size() > 12) return false;
    out = 0;
    for (char c : s) {
        u64 v = 0;
        if (c >= '0' && c <= '9')
            v = static_cast<u64>(c - '0');
        else if (c >= 'a' && c <= 'f')
            v = static_cast<u64>(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F')
            v = static_cast<u64>(c - 'A' + 10);
        else
            return false;
        out = out * 16 + v;
    }
    return true;
}

} // namespace

bool iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (usize i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.remove_suffix(1);
    return s;
}

const std::string* Request::find_header(std::string_view name) const {
    for (const Header& h : headers)
        if (iequals(h.name, name)) return &h.value;
    return nullptr;
}

std::string Request::header(std::string_view name) const {
    const std::string* v = find_header(name);
    return v ? *v : std::string();
}

std::string Response::header(std::string_view name) const {
    for (const Header& h : headers)
        if (iequals(h.name, name)) return h.value;
    return std::string();
}

Parse decode_chunked(std::string_view buf, const Limits& limits, usize& consumed, std::string& out, std::string& error,
                     int& status) {
    usize pos = 0;
    for (;;) {
        const usize eol = buf.find("\r\n", pos);
        if (eol == kNpos) {
            if (buf.size() - pos > 1024) {
                error = "chunk size line too long";
                status = 400;
                return Parse::Error;
            }
            return Parse::Incomplete;
        }
        std::string_view size_text = buf.substr(pos, eol - pos);
        const usize ext = size_text.find(';');
        if (ext != kNpos) size_text = size_text.substr(0, ext);
        u64 size = 0;
        if (!parse_hex(trim(size_text), size)) {
            error = "bad chunk size";
            status = 400;
            return Parse::Error;
        }
        pos = eol + 2;
        if (size == 0) {
            // Trailer section: zero or more header lines, ended by an empty line.
            for (;;) {
                const usize tend = buf.find("\r\n", pos);
                if (tend == kNpos) return Parse::Incomplete;
                if (tend == pos) {
                    consumed = pos + 2;
                    return Parse::Complete;
                }
                pos = tend + 2;
            }
        }
        if (size > limits.max_body_bytes || out.size() + size > limits.max_body_bytes) {
            error = "request body too large";
            status = 413;
            return Parse::Error;
        }
        const usize n = static_cast<usize>(size);
        if (buf.size() < pos + n + 2) return Parse::Incomplete;
        out.append(buf.substr(pos, n));
        pos += n;
        if (buf.substr(pos, 2) != "\r\n") {
            error = "missing CRLF after chunk data";
            status = 400;
            return Parse::Error;
        }
        pos += 2;
    }
}

Parse parse_request(std::string_view buf, const Limits& limits, Request& out, usize& consumed, std::string& error,
                    int& status) {
    const usize head_end = buf.find("\r\n\r\n");
    if (head_end == kNpos) {
        if (buf.size() > limits.max_header_bytes) {
            error = "request headers too large";
            status = 431;
            return Parse::Error;
        }
        return Parse::Incomplete;
    }
    if (head_end > limits.max_header_bytes) {
        error = "request headers too large";
        status = 431;
        return Parse::Error;
    }
    const std::string_view head = buf.substr(0, head_end);
    const usize line_end = head.find("\r\n");
    const std::string_view line = head.substr(0, line_end == kNpos ? head.size() : line_end);

    // Request line: METHOD SP TARGET SP VERSION.
    const usize sp1 = line.find(' ');
    const usize sp2 = line.rfind(' ');
    if (sp1 == kNpos || sp2 == sp1 || sp1 == 0) {
        error = "malformed request line";
        status = 400;
        return Parse::Error;
    }
    const std::string_view method = line.substr(0, sp1);
    const std::string_view target = line.substr(sp1 + 1, sp2 - sp1 - 1);
    const std::string_view version = line.substr(sp2 + 1);
    if (!is_token(method)) {
        error = "invalid method";
        status = 400;
        return Parse::Error;
    }
    if (target.empty() || target.front() != '/') {
        error = "request target must start with '/'";
        status = 400;
        return Parse::Error;
    }
    if (version != "HTTP/1.1" && version != "HTTP/1.0") {
        error = "unsupported HTTP version";
        status = 505;
        return Parse::Error;
    }

    out = Request{};
    out.method = std::string(method);
    out.target = std::string(target);
    out.version = std::string(version);
    if (line_end != kNpos && !parse_header_lines(head.substr(line_end + 2), out.headers, error)) {
        status = 400;
        return Parse::Error;
    }

    // Body framing: chunked wins over Content-Length (RFC 9112 section 6.3).
    const usize body_start = head_end + 4;
    const std::string_view rest = buf.substr(body_start);
    const std::string te = out.header("Transfer-Encoding");
    if (!te.empty()) {
        if (!has_token(te, "chunked")) {
            error = "unsupported transfer coding";
            status = 400;
            return Parse::Error;
        }
        usize used = 0;
        const Parse p = decode_chunked(rest, limits, used, out.body, error, status);
        if (p != Parse::Complete) return p;
        consumed = body_start + used;
    } else {
        const std::string cl = out.header("Content-Length");
        if (cl.empty()) {
            consumed = body_start;
        } else {
            u64 length = 0;
            if (!parse_decimal(cl, length)) {
                error = "invalid Content-Length";
                status = 400;
                return Parse::Error;
            }
            if (length > limits.max_body_bytes) {
                error = "request body too large";
                status = 413;
                return Parse::Error;
            }
            const usize n = static_cast<usize>(length);
            if (rest.size() < n) return Parse::Incomplete;
            out.body.assign(rest.substr(0, n));
            consumed = body_start + n;
        }
    }

    const std::string connection = out.header("Connection");
    if (out.version == "HTTP/1.0")
        out.keep_alive = has_token(connection, "keep-alive");
    else
        out.keep_alive = !has_token(connection, "close");
    return Parse::Complete;
}

bool wants_continue(std::string_view buf) {
    const usize head_end = buf.find("\r\n\r\n");
    const std::string_view head = buf.substr(0, head_end == kNpos ? buf.size() : head_end);
    usize pos = head.find("\r\n");
    while (pos != kNpos && pos < head.size()) {
        pos += 2;
        usize eol = head.find("\r\n", pos);
        if (eol == kNpos) eol = head.size();
        const std::string_view line = head.substr(pos, eol - pos);
        const usize colon = line.find(':');
        if (colon != kNpos && iequals(trim(line.substr(0, colon)), "Expect") &&
            iequals(trim(line.substr(colon + 1)), "100-continue"))
            return true;
        pos = eol;
    }
    return false;
}

bool parse_response_head(std::string_view head, Response& out) {
    const usize line_end = head.find("\r\n");
    const std::string_view line = head.substr(0, line_end == kNpos ? head.size() : line_end);
    if (line.size() < 12 || line.substr(0, 5) != "HTTP/") return false;
    const usize sp = line.find(' ');
    if (sp == kNpos || line.size() < sp + 4) return false;
    int status = 0;
    for (usize i = sp + 1; i < sp + 4; ++i) {
        const char c = line[i];
        if (c < '0' || c > '9') return false;
        status = status * 10 + (c - '0');
    }
    out = Response{};
    out.status = status;
    if (line_end == kNpos) return true;
    std::string error;
    return parse_header_lines(head.substr(line_end + 2), out.headers, error);
}

std::string build_response(int status, const std::vector<Header>& headers, std::string_view body, bool keep_alive) {
    std::string out = "HTTP/1.1 " + std::to_string(status) + " " + reason_phrase(status) + "\r\n";
    for (const Header& h : headers) out += h.name + ": " + h.value + "\r\n";
    out += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    out += keep_alive ? "Connection: keep-alive\r\n" : "Connection: close\r\n";
    out += "\r\n";
    out.append(body);
    return out;
}

const char* reason_phrase(int status) {
    switch (status) {
    case 100: return "Continue";
    case 200: return "OK";
    case 202: return "Accepted";
    case 400: return "Bad Request";
    case 403: return "Forbidden";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 411: return "Length Required";
    case 413: return "Payload Too Large";
    case 415: return "Unsupported Media Type";
    case 431: return "Request Header Fields Too Large";
    case 500: return "Internal Server Error";
    case 503: return "Service Unavailable";
    case 504: return "Gateway Timeout";
    case 505: return "HTTP Version Not Supported";
    default: return "Status";
    }
}

} // namespace tefra::mcp::http
