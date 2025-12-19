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
    // Paths để tìm file
    std::vector<std::string> paths = {
        "web/" + filename,           // Chạy từ root
        "../web/" + filename,        // Chạy từ build/
        filename                     // Fallback
    };
    
    for (const auto &path : paths) {
        std::ifstream f(path);
        if (f.is_open()) {
            std::stringstream buffer;
            buffer << f.rdbuf();
            return buffer.str();
        }
    }
    return "";  // File not found
}

// Logic Firewall - Chỉ cho phép mạng trường (10.11.x.x)
bool is_ip_allowed(const std::string &ip)
{
    // localhost - cho phép (admin local)
    if (ip.find("127.0.0.1") == 0)
        return true;
    
    // Mạng trường: 10.11.16.0/21 (10.11.16.0 - 10.11.23.255)
    if (ip.find("10.11.") == 0)
        return true;
    
    // Từ chối tất cả IP khác (WiFi nhà, mạng công cộng, VPN, v.v.)
    return false;
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
            std::string mssv = get_json_str(frame.payload, "mssv");
            std::string fp = get_json_str(frame.payload, "fingerprint");
            
            std::cout << "[LOGIN] MSSV=" << mssv << " | FP=" << fp.substr(0, 10) << "..." << std::endl;
            
            // Check IP khi Login - ONLY campus network allowed
            if (!is_ip_allowed(real_ip))
            {
                // TỪ CHỐI - IP không phải mạng trường
                std::cout << "  -> ❌ REJECTED: IP không từ mạng trường (" << real_ip << ")" << std::endl;
                game.log_fraud(real_ip, mssv, "Cố gian lận từ mạng lạ: " + real_ip);
                std::string msg = "{\"status\":\"ERROR\",\"msg\":\"❌ BẠN PHẢI SỬ DỤNG MẠNG CỦA TRƯỜNG!\\n\\nIP hiện tại: " + real_ip + "\\nMạng cho phép: 10.11.x.x\"}";
                send_text_frame(sock, msg);
            }
            else
            {
                std::string response = game.handle_login(real_ip, mssv, fp);
                std::cout << "  -> Response: " << response << std::endl;
                send_text_frame(sock, response);
            }
        }
        else if (type == "SUBMIT")
        {
            std::string mssv = get_json_str(frame.payload, "mssv");
            std::string ans = get_json_str(frame.payload, "answer");
            std::string fraud_flag = get_json_str(frame.payload, "fraud_flag");
            std::string fraud_type = get_json_str(frame.payload, "fraud_type");
            
            std::cout << "[SUBMIT] " << mssv << " : " << ans << " | fraud_flag=" << fraud_flag << " | fraud_type=" << fraud_type << std::endl;

            // Nếu có gian lận, gửi vào bảng gian lận
            if (fraud_flag == "true")
            {
                std::cout << "  -> Ghi nhận GIAN LẬN!" << std::endl;
                std::string reason = "Gian lận: " + fraud_type;
                game.log_block(real_ip, mssv, reason);
            }
            else
            {
                std::cout << "  -> Ghi nhận BÌNH THƯỜNG" << std::endl;
                // Gửi dữ liệu sang Admin Dashboard (danh sách bình thường)
                game.log_submission(mssv, real_ip, ans);
            }
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