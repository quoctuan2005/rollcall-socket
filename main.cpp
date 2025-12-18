#include <iostream>
#include <thread>
#include <csignal>
#include <string>
#include <fstream>
#include <sstream>

#include "ws_utils.hpp"
#include "game_manager.hpp"

GameManager game;

// Hàm đọc file index.html trả về chuỗi
std::string load_html_file()
{
    std::ifstream f("index.html");
    if (!f.is_open())
        return "<h1>Error: index.html not found on server</h1>";
    std::stringstream buffer;
    buffer << f.rdbuf();
    return buffer.str();
}

// Logic kiểm tra IP
bool is_ip_allowed(const std::string &ip)
{
    if (ip.find("127.0.0.1") == 0)
        return true;

    if (ip.find("192.168.") == 0)
        return true;
    return false;
}

void client_thread(int sock, std::string ip)
{
    if (!is_ip_allowed(ip))
    {
        // Vẫn cho họ xem trang Web để hiện thông báo lỗi, nhưng không cho Socket
        // Hoặc đóng luôn cũng được. Ở đây đóng luôn cho bảo mật.
        std::cout << "[BLOCKED] " << ip << std::endl;
        close(sock);
        return;
    }

    std::string req;
    if (!read_http_request(sock, req))
    {
        close(sock);
        return;
    }

    // --- PHÂN LOẠI KẾT NỐI ---
    // Kiểm tra xem đây là yêu cầu Web (GET /) hay Socket (Upgrade)

    // TRƯỜNG HỢP 1: YÊU CẦU TRANG WEB (HTTP GET)
    if (req.find("Upgrade: websocket") == std::string::npos)
    {
        std::string html = load_html_file();
        std::string response =
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html; charset=utf-8\r\n"
            "Content-Length: " +
            std::to_string(html.size()) + "\r\n"
                                          "Connection: close\r\n\r\n" +
            html;

        send_all(sock, response);
        close(sock); // Gửi xong HTML thì đóng kết nối ngay
        return;
    }

    // TRƯỜNG HỢP 2: KẾT NỐI SOCKET (GAME)
    if (!websocket_handshake(sock, req))
    {
        close(sock);
        return;
    }
    game.add_socket(sock);

    while (true)
    {
        WSFrame frame;
        if (!read_ws_frame(sock, frame))
            break;
        if (frame.opcode == 8)
            break;
        if (frame.opcode != 1)
            continue;

        std::string type = get_json_str(frame.payload, "type");
        if (type == "LOGIN")
        {
            std::string mssv = get_json_str(frame.payload, "mssv");
            std::string fp = get_json_str(frame.payload, "fingerprint");
            std::string response = game.handle_login(ip, mssv, fp);
            send_text_frame(sock, response);
        }
        else if (type == "SUBMIT")
        {
            std::string mssv = get_json_str(frame.payload, "mssv");
            std::string ans = get_json_str(frame.payload, "answer");
            std::cout << "[SUBMIT] " << mssv << " : " << ans << std::endl;
        }
    }

    game.remove_socket(sock);
    close(sock);
}

int main()
{
    signal(SIGPIPE, SIG_IGN);
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(8080);
    bind(server_fd, (sockaddr *)&addr, sizeof(addr));
    listen(server_fd, 20);

    std::cout << "=== SERVER RUNNING (WEB + SOCKET) ON PORT 8080 ===" << std::endl;

    while (true)
    {
        sockaddr_in client_addr;
        socklen_t len = sizeof(client_addr);
        int client_sock = accept(server_fd, (sockaddr *)&client_addr, &len);
        if (client_sock >= 0)
        {
            char ipStr[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client_addr.sin_addr, ipStr, INET_ADDRSTRLEN);
            std::thread(client_thread, client_sock, std::string(ipStr)).detach();
        }
    }
    return 0;
}