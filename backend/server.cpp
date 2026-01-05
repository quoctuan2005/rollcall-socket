#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <unordered_map>

static void die(const char *msg)
{
    std::cerr << msg << ": " << std::strerror(errno) << "\n";
    std::exit(1);
}

static std::int64_t now_ms()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

static std::string random_bits(int nbits)
{
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<int> dist(0, 1);
    std::string s;
    s.reserve(static_cast<size_t>(nbits));
    for (int i = 0; i < nbits; i++)
        s.push_back(dist(rng) ? '1' : '0');
    return s;
}

// NOTE: This is a lightweight keyed-hash for a student project/demo.
// For production-grade security, replace with HMAC-SHA256.
static std::uint64_t fnv1a64(const std::string &s)
{
    std::uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s)
    {
        h ^= static_cast<std::uint64_t>(c);
        h *= 1099511628211ull;
    }
    return h;
}

static std::string u64_to_bits(std::uint64_t x)
{
    std::string bits;
    bits.reserve(64);
    for (int i = 63; i >= 0; i--)
    {
        bits.push_back(((x >> i) & 1ull) ? '1' : '0');
    }
    return bits;
}

static std::string make_token_bits(const std::string &secret, const std::string &session_id, std::int64_t counter, int nbits)
{
    std::string out;
    out.reserve(static_cast<size_t>(nbits));
    int block = 0;
    while (static_cast<int>(out.size()) < nbits)
    {
        const std::string msg = secret + "|" + session_id + "|" + std::to_string(counter) + "|" + std::to_string(block);
        const std::uint64_t h = fnv1a64(msg);
        out += u64_to_bits(h);
        block++;
        if (block > 16)
            break;
    }
    out.resize(static_cast<size_t>(nbits));
    return out;
}

struct HttpRequest
{
    std::string method;
    std::string target;
    std::string path;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
};

static std::string trim(const std::string &s)
{
    size_t start = 0;
    while (start < s.size() && (s[start] == ' ' || s[start] == '\t'))
        start++;
    size_t end = s.size();
    while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r' || s[end - 1] == '\n'))
        end--;
    return s.substr(start, end - start);
}

static std::optional<HttpRequest> read_http_request(int fd)
{
    std::string data;
    data.reserve(4096);

    char buf[2048];
    while (true)
    {
        ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n <= 0)
            return std::nullopt;
        data.append(buf, buf + n);
        if (data.find("\r\n\r\n") != std::string::npos)
            break;
        if (data.size() > 1024 * 1024)
            return std::nullopt;
    }

    const size_t header_end = data.find("\r\n\r\n");
    std::string header_block = data.substr(0, header_end);
    std::string rest = data.substr(header_end + 4);

    std::istringstream hs(header_block);
    std::string request_line;
    if (!std::getline(hs, request_line))
        return std::nullopt;
    if (!request_line.empty() && request_line.back() == '\r')
        request_line.pop_back();

    std::istringstream rl(request_line);
    HttpRequest req;
    std::string http_version;
    rl >> req.method >> req.target >> http_version;
    if (req.method.empty() || req.target.empty())
        return std::nullopt;

    // headers
    std::string line;
    while (std::getline(hs, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        auto pos = line.find(':');
        if (pos == std::string::npos)
            continue;
        std::string key = trim(line.substr(0, pos));
        std::string val = trim(line.substr(pos + 1));
        for (auto &c : key)
            c = static_cast<char>(::tolower(c));
        req.headers[key] = val;
    }

    // path (strip query)
    auto qpos = req.target.find('?');
    req.path = (qpos == std::string::npos) ? req.target : req.target.substr(0, qpos);

    // body
    size_t content_length = 0;
    if (auto it = req.headers.find("content-length"); it != req.headers.end())
    {
        content_length = static_cast<size_t>(std::strtoul(it->second.c_str(), nullptr, 10));
    }
    while (rest.size() < content_length)
    {
        ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n <= 0)
            break;
        rest.append(buf, buf + n);
    }
    if (content_length)
        req.body = rest.substr(0, content_length);

    return req;
}

static void send_all(int fd, const std::string &s)
{
    size_t off = 0;
    while (off < s.size())
    {
        ssize_t n = ::send(fd, s.data() + off, s.size() - off, 0);
        if (n <= 0)
            return;
        off += static_cast<size_t>(n);
    }
}

static std::string json_escape(const std::string &s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s)
    {
        switch (c)
        {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out += c;
            break;
        }
    }
    return out;
}

static std::unordered_map<std::string, std::string> parse_query(const std::string &target)
{
    std::unordered_map<std::string, std::string> out;
    auto qpos = target.find('?');
    if (qpos == std::string::npos)
        return out;
    std::string q = target.substr(qpos + 1);
    std::istringstream ss(q);
    std::string part;
    while (std::getline(ss, part, '&'))
    {
        auto eq = part.find('=');
        if (eq == std::string::npos)
            continue;
        out[part.substr(0, eq)] = part.substr(eq + 1);
    }
    return out;
}

struct TokenState
{
    std::string session_id;
    int ttl_ms = 8000;
};

static TokenState g_token;
static std::unordered_map<std::string, std::int64_t> g_used_counter_by_student;

static const std::string g_secret = []
{
    const char *env = std::getenv("ROLLCALL_SECRET");
    if (env && *env)
        return std::string(env);
    return std::string("dev-secret-change-me");
}();

static void ensure_session()
{
    if (!g_token.session_id.empty())
        return;
    g_token.session_id = "S" + std::to_string(now_ms());
}

static std::int64_t current_counter(std::int64_t now)
{
    return now / g_token.ttl_ms;
}

static std::int64_t counter_expires_at_ms(std::int64_t counter)
{
    return (counter + 1) * g_token.ttl_ms;
}

static std::optional<std::string> json_get_string(const std::string &body, const std::string &key)
{
    const std::string needle = "\"" + key + "\"";
    size_t pos = body.find(needle);
    if (pos == std::string::npos)
        return std::nullopt;
    pos = body.find(':', pos + needle.size());
    if (pos == std::string::npos)
        return std::nullopt;
    pos = body.find('"', pos);
    if (pos == std::string::npos)
        return std::nullopt;
    size_t end = body.find('"', pos + 1);
    if (end == std::string::npos)
        return std::nullopt;
    return body.substr(pos + 1, end - (pos + 1));
}

static bool is_bits01(const std::string &s)
{
    if (s.empty())
        return false;
    for (char c : s)
    {
        if (c != '0' && c != '1')
            return false;
    }
    return true;
}

static std::string http_json(int status, const std::string &json_body)
{
    std::ostringstream out;
    out << "HTTP/1.1 " << status << "\r\n";
    out << "Content-Type: application/json; charset=utf-8\r\n";
    out << "Content-Length: " << json_body.size() << "\r\n";
    out << "Connection: close\r\n";
    out << "\r\n";
    out << json_body;
    return out.str();
}

static std::string handle_request(const HttpRequest &req)
{
    if (req.method == "GET" && req.path == "/api/health")
    {
        const std::string body = "{\"ok\":true}";
        return http_json(200, body);
    }

    if (req.method == "POST" && req.path == "/api/session/start")
    {
        g_token.session_id = "S" + std::to_string(now_ms());
        g_used_counter_by_student.clear();
        const std::string body =
            "{\"session_id\":\"" + json_escape(g_token.session_id) + "\",\"ttl_ms\":" + std::to_string(g_token.ttl_ms) + "}";
        return http_json(200, body);
    }

    if (req.method == "GET" && req.path == "/api/token")
    {
        auto q = parse_query(req.target);
        int nbits = 8;
        if (auto it = q.find("bits"); it != q.end())
        {
            nbits = std::max(1, std::min(256, std::atoi(it->second.c_str())));
        }
        const std::int64_t now = now_ms();
        ensure_session();
        const std::int64_t counter = current_counter(now);
        const std::int64_t expires_at = counter_expires_at_ms(counter);
        const std::string bits = make_token_bits(g_secret, g_token.session_id, counter, nbits);
        const std::string body =
            "{\"session_id\":\"" + json_escape(g_token.session_id) +
            "\",\"bits\":\"" + json_escape(bits) +
            "\",\"expires_at_ms\":" + std::to_string(expires_at) +
            ",\"now_ms\":" + std::to_string(now) +
            ",\"ttl_ms\":" + std::to_string(g_token.ttl_ms) +
            ",\"counter\":" + std::to_string(counter) + "}";
        return http_json(200, body);
    }

    if (req.method == "POST" && req.path == "/api/attendance/submit")
    {
        const std::int64_t now = now_ms();
        ensure_session();
        const std::int64_t counter = current_counter(now);

        const auto student_id_opt = json_get_string(req.body, "student_id");
        const auto bits_opt = json_get_string(req.body, "bits");
        if (!student_id_opt || !bits_opt)
        {
            return http_json(400, "{\"ok\":false,\"error\":\"bad_request\"}");
        }

        const std::string student_id = *student_id_opt;
        const std::string bits = *bits_opt;
        if (student_id.empty() || !is_bits01(bits) || bits.size() > 256)
        {
            return http_json(400, "{\"ok\":false,\"error\":\"invalid_input\"}");
        }

        const std::string expected = make_token_bits(g_secret, g_token.session_id, counter, static_cast<int>(bits.size()));
        if (bits != expected)
        {
            return http_json(401, "{\"ok\":false,\"error\":\"invalid_token\"}");
        }

        if (auto it = g_used_counter_by_student.find(student_id); it != g_used_counter_by_student.end())
        {
            if (it->second == counter)
            {
                return http_json(409, "{\"ok\":false,\"error\":\"already_used\"}");
            }
        }

        g_used_counter_by_student[student_id] = counter;
        const std::string body =
            "{\"ok\":true,\"status\":\"accepted\",\"student_id\":\"" + json_escape(student_id) +
            "\",\"session_id\":\"" + json_escape(g_token.session_id) + "\"}";
        return http_json(200, body);
    }

    const std::string body = "{\"ok\":false,\"error\":\"not_found\"}";
    return http_json(404, body);
}

int main(int argc, char **argv)
{
    int port = 9000;
    if (argc >= 2)
        port = std::stoi(argv[1]);

    int server_fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
        die("socket");

    int yes = 1;
    if (::setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) < 0)
    {
        die("setsockopt(SO_REUSEADDR)");
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(static_cast<uint16_t>(port));

    if (::bind(server_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0)
    {
        die("bind");
    }
    if (::listen(server_fd, 64) < 0)
    {
        die("listen");
    }

    std::cout << "[backend] HTTP server listening on http://127.0.0.1:" << port << "\n";
    std::cout << "[backend] Endpoints: GET /api/health, POST /api/session/start, GET /api/token?bits=N, POST /api/attendance/submit\n";
    std::cout << "[backend] TTL(ms): " << g_token.ttl_ms << " (set secret via ROLLCALL_SECRET env)\n";

    while (true)
    {
        sockaddr_in client_addr{};
        socklen_t client_len = sizeof(client_addr);
        int client_fd = ::accept(server_fd, reinterpret_cast<sockaddr *>(&client_addr), &client_len);
        if (client_fd < 0)
            die("accept");

        auto req = read_http_request(client_fd);
        if (!req)
        {
            ::close(client_fd);
            continue;
        }

        const std::string resp = handle_request(*req);
        send_all(client_fd, resp);
        ::close(client_fd);
    }

    ::close(server_fd);
    return 0;
}
