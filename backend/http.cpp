#include "http.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <sstream>

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

std::optional<HttpRequest> read_http_request(int fd)
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
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
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

void send_all(int fd, const std::string &s)
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

std::unordered_map<std::string, std::string> parse_query(const std::string &target)
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
