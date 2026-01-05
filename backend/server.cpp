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
    std::string bits;
    std::int64_t expires_at_ms = 0;
    int ttl_ms = 10000;
};

static TokenState g_token;

static void ensure_session()
{
    if (!g_token.session_id.empty())
        return;
    g_token.session_id = "S" + std::to_string(now_ms());
}

static void refresh_token_if_needed(int nbits)
{
    ensure_session();
    const std::int64_t now = now_ms();
    if (g_token.bits.empty() || now >= g_token.expires_at_ms)
    {
        g_token.bits = random_bits(nbits);
        g_token.expires_at_ms = now + g_token.ttl_ms;
    }
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
        g_token.bits.clear();
        g_token.expires_at_ms = 0;
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
        refresh_token_if_needed(nbits);
        const std::int64_t now = now_ms();
        const std::string body =
            "{\"session_id\":\"" + json_escape(g_token.session_id) +
            "\",\"bits\":\"" + json_escape(g_token.bits) +
            "\",\"expires_at_ms\":" + std::to_string(g_token.expires_at_ms) +
            ",\"now_ms\":" + std::to_string(now) +
            ",\"ttl_ms\":" + std::to_string(g_token.ttl_ms) + "}";
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
    std::cout << "[backend] Endpoints: GET /api/health, POST /api/session/start, GET /api/token?bits=N\n";

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
