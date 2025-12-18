#include <iostream>
#include <thread>
#include <csignal>
#include <string>
#include <fstream>
#include <sstream>

#include "ws_utils.hpp"
#include "game_manager.hpp"

GameManager game;

// Hàm đọc file tổng quát
std::string load_file(const std::string &filename)
{
    std::ifstream f(filename);
    if (!f.is_open())
        return "";
    std::stringstream buffer;
    buffer << f.rdbuf();
    return buffer.str();
}

// Logic Firewall
bool is_ip_allowed(const std::string &ip)
{
    if (ip.find("127.0.0.1") == 0)
        return true;
    if (ip.find("192.168.") == 0)
        return true;
    if (ip.find("10.") == 0)
        return true;
    return false; // Chặn IP lạ
}

void client_thread(int sock, std::string socket_ip)
{
    std::string req;
    if (!read_http_request(sock, req))
    {
        close(sock);
        return;
    }

    std::string real_ip = socket_ip;
    std::string forwarded = get_header_value(req, "X-Forwarded-For");
    if (!forwarded.empty())
        real_ip = forwarded.substr(0, forwarded.find(','));

    // --- XỬ LÝ HTTP (TRANG WEB) ---
    if (req.find("Upgrade: websocket") == std::string::npos)
    {
        std::string content;

        // ROUTING: Phân biệt trang chủ và trang Admin
        // Nếu URL chứa "/admin" -> trả về admin.html
        if (req.find("GET /admin ") != std::string::npos)
        {
            std::cout << "[ADMIN ACCESS] from " << real_ip << std::endl;
            content = load_file("admin.html");
            if (content.empty())
                content = "<h1>Loi: Khong tim thay admin.html</h1>";
        }
        // Mặc định trả về index.html
        else
        {
            content = load_file("index.html");
            if (content.empty())
                content = "<h1>Loi: Khong tim thay index.html</h1>";
        }

        std::string response = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: " + std::to_string(content.size()) + "\r\nConnection: close\r\n\r\n" + content;
        send_all(sock, response);
        close(sock);
        return;
    }

    // --- XỬ LÝ SOCKET ---
    if (!websocket_handshake(sock, req))
    {
        close(sock);
        return;
    }

    // Mặc định add vào list client, nếu là admin thì sẽ move sau
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

        // --- ADMIN LOGIN ---
        if (type == "ADMIN_LOGIN")
        {
            // Chuyển socket này sang danh sách Admin để nhận thông báo
            game.register_admin(sock);
        }
        else if (type == "LOGIN")
        {
            // Check IP khi Login
            if (!is_ip_allowed(real_ip))
            {
                // Báo Admin biết có người bị chặn
                game.log_block(real_ip, "Unknown", "IP không hợp lệ (Mạng lạ)");

                std::string msg = "{\"status\":\"BLOCKED\",\"msg\":\"IP của bạn không được phép!\"}";
                send_text_frame(sock, msg);
            }
            else
            {
                std::string mssv = get_json_str(frame.payload, "mssv");
                std::string fp = get_json_str(frame.payload, "fingerprint");
                std::string response = game.handle_login(real_ip, mssv, fp);
                send_text_frame(sock, response);
            }
        }
        else if (type == "SUBMIT")
        {
            std::string mssv = get_json_str(frame.payload, "mssv");
            std::string ans = get_json_str(frame.payload, "answer");
            std::cout << "[SUBMIT] " << mssv << " : " << ans << std::endl;

            // Gửi dữ liệu sang Admin Dashboard
            game.log_submission(mssv, real_ip, ans);
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
    if (bind(server_fd, (sockaddr *)&addr, sizeof(addr)) < 0)
        return 1;
    listen(server_fd, 20);

    std::cout << "=== SERVER ONLINE ===" << std::endl;
    std::cout << "User Link: http://localhost:8080" << std::endl;
    std::cout << "Admin Link: http://localhost:8080/admin" << std::endl;

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