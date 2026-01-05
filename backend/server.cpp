#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <iostream>

#include "app.h"
#include "http.h"
#include "token.h"

static void die(const char *msg)
{
    std::cerr << msg << ": " << std::strerror(errno) << "\n";
    std::exit(1);
}

int main(int argc, char **argv)
{
    int port = 9000;
    if (argc >= 2)
        port = std::stoi(argv[1]);

    TokenService tokens(read_secret_from_env(), 8000);

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
    std::cout << "[backend] TTL(ms): " << tokens.ttl_ms() << " (set secret via ROLLCALL_SECRET env)\n";

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

        const std::string resp = handle_request(*req, tokens);
        send_all(client_fd, resp);
        ::close(client_fd);
    }

    ::close(server_fd);
    return 0;
}
