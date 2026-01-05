#pragma once

#include <optional>
#include <string>
#include <unordered_map>

struct HttpRequest
{
    std::string method;
    std::string target;
    std::string path;
    std::unordered_map<std::string, std::string> headers;
    std::string body;
};

std::optional<HttpRequest> read_http_request(int fd);
void send_all(int fd, const std::string &s);
std::unordered_map<std::string, std::string> parse_query(const std::string &target);
