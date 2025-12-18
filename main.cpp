#include <iostream>
#include <thread>
#include <csignal>
#include <string>

// Include 2 file header (đảm bảo bạn đã có file ws_utils.hpp và game_manager.hpp cùng thư mục)
#include "ws_utils.hpp"
#include "game_manager.hpp"

// Khởi tạo Game Manager toàn cục
GameManager game;

// Cấu hình mạng (Filter IP)
bool is_ip_allowed(const std::string &ip)
{
    if (ip.find("127.0.0.1") == 0)
        return true;
    if (ip.find("192.168.") == 0)
        return true;
    // Thêm các dải IP khác nếu cần
    return false;
}

// Luồng xử lý riêng cho từng sinh viên
void client_thread(int sock, std::string ip)
{
    // 1. IP Filter
    if (!is_ip_allowed(ip))
    {
        close(sock);
        return;
    }

    // 2. Handshake
    std::string req;
    if (!read_http_request(sock, req))
    {
        close(sock);
        return;
    }
    if (!websocket_handshake(sock, req))
    {
        close(sock);
        return;
    }

    game.add_socket(sock);

    // 3. Vòng lặp nhận tin nhắn
    while (true)
    {
        WSFrame frame;
        if (!read_ws_frame(sock, frame))
            break;
        if (frame.opcode == 8)
            break; // Close frame
        if (frame.opcode != 1)
            continue; // Chỉ nhận Text

        // Phân tích JSON (type)
        std::string type = get_json_str(frame.payload, "type");

        if (type == "LOGIN")
        {
            std::string mssv = get_json_str(frame.payload, "mssv");
            std::string fp = get_json_str(frame.payload, "fingerprint");

            // Xử lý đăng nhập
            std::string response = game.handle_login(ip, mssv, fp);
            send_text_frame(sock, response);
        }
        else if (type == "SUBMIT")
        {
            // --- XỬ LÝ NỘP BÀI ---
            std::string mssv = get_json_str(frame.payload, "mssv");
            std::string ans = get_json_str(frame.payload, "answer");

            // Log kết quả ra màn hình Server
            std::cout << "[SUBMISSION] Student: " << mssv << " | Answer: " << ans << std::endl;

            // (Thực tế bạn có thể lưu vào file .txt hoặc database tại đây)
        }
    }

    game.remove_socket(sock);
    close(sock);
    // std::cout << "Client disconnected: " << ip << std::endl;
}

int main()
{
    // Fix lỗi crash trên macOS/Linux khi client ngắt kết nối đột ngột
    signal(SIGPIPE, SIG_IGN);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
        return 1;

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(8080);

    if (bind(server_fd, (sockaddr *)&addr, sizeof(addr)) < 0)
    {
        std::cerr << "Port 8080 is busy!" << std::endl;
        return 1;
    }

    if (listen(server_fd, 20) < 0)
        return 1;

    std::cout << "=== SERVER READY ON PORT 8080 ===" << std::endl;

    while (true)
    {
        sockaddr_in client_addr;
        socklen_t len = sizeof(client_addr);
        int client_sock = accept(server_fd, (sockaddr *)&client_addr, &len);

        if (client_sock >= 0)
        {
            char ipStr[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client_addr.sin_addr, ipStr, INET_ADDRSTRLEN);
            std::string client_ip(ipStr);

            // Detach thread để xử lý song song nhiều sinh viên
            std::thread(client_thread, client_sock, client_ip).detach();
        }
    }

    close(server_fd);
    return 0;
}