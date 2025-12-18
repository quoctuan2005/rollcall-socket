#include <iostream>
#include <thread>
#include <csignal>
#include <string>

// Include 2 file header chúng ta vừa tạo
#include "ws_utils.hpp"
#include "game_manager.hpp"

// Khởi tạo Game Manager toàn cục
GameManager game;

// Cấu hình mạng (Filter IP)
bool is_ip_allowed(const std::string &ip)
{
    // 1. Cho phép Localhost (để test)
    if (ip.find("127.0.0.1") == 0)
        return true;

    // 2. Cho phép mạng nội bộ (WiFi trường, mạng LAN)
    // Bạn cần sửa "192.168." thành dải IP thực tế của trường (VD: "10.")
    if (ip.find("192.168.") == 0)
        return true;

    return false; // Chặn tất cả các IP khác (3G/4G, v.v.)
}

// Luồng xử lý riêng cho từng sinh viên
void client_thread(int sock, std::string ip)
{
    // 1. IP Filtering Layer
    if (!is_ip_allowed(ip))
    {
        std::cout << "[BLOCKED] Connection from outside network: " << ip << std::endl;
        close(sock);
        return;
    }

    // 2. HTTP Handshake (Nâng cấp lên WebSocket)
    std::string req;
    if (!read_http_request(sock, req))
    {
        close(sock);
        return;
    }

    if (!websocket_handshake(sock, req))
    {
        std::cout << "Handshake failed for " << ip << std::endl;
        close(sock);
        return;
    }

    // Đăng ký socket này vào danh sách online
    game.add_socket(sock);

    // 3. Vòng lặp nhận tin nhắn (Frame Loop)
    while (true)
    {
        WSFrame frame;
        if (!read_ws_frame(sock, frame))
            break; // Lỗi mạng hoặc đóng socket

        if (frame.opcode == 8)
            break; // Client chủ động đóng (Close Frame)
        if (frame.opcode != 1)
            continue; // Chỉ xử lý Text Frame

        // Parse nội dung JSON gửi lên
        std::string type = get_json_str(frame.payload, "type");

        if (type == "LOGIN")
        {
            std::string mssv = get_json_str(frame.payload, "mssv");
            std::string fp = get_json_str(frame.payload, "fingerprint"); // Khớp với script.js

            // Nếu script.js gửi 'visitorId' thay vì 'fingerprint', hãy sửa dòng trên
            // Trong script.js cũ của bạn có dòng: payload = { ..., fingerprint: visitorId } -> OK

            // Gọi Logic kiểm tra trong GameManager
            std::string response = game.handle_login(ip, mssv, fp);

            // Gửi phản hồi về cho Client
            send_text_frame(sock, response);
        }
        else
        {
            // Xử lý các loại tin nhắn khác (VD: nộp bài)
            // Hiện tại chỉ log ra để xem
            std::cout << "[" << ip << "] sent unknown type: " << type << std::endl;
        }
    }

    // Dọn dẹp khi ngắt kết nối
    game.remove_socket(sock);
    close(sock);
    std::cout << "Client disconnected: " << ip << std::endl;
}

int main()
{
    // --- QUAN TRỌNG: Fix lỗi crash trên macOS/Linux ---
    // Bỏ qua tín hiệu SIGPIPE (xảy ra khi gửi data vào socket đã đóng)
    signal(SIGPIPE, SIG_IGN);

    // 1. Tạo Socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        std::cerr << "Failed to create socket" << std::endl;
        return 1;
    }

    // Cho phép tái sử dụng port (tránh lỗi "Address already in use" khi restart nhanh)
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // 2. Bind vào port 8080
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY; // Lắng nghe mọi IP
    addr.sin_port = htons(8080);

    if (bind(server_fd, (sockaddr *)&addr, sizeof(addr)) < 0)
    {
        std::cerr << "Bind failed! Port 8080 might be busy." << std::endl;
        return 1;
    }

    // 3. Listen
    if (listen(server_fd, 20) < 0)
    {
        std::cerr << "Listen failed" << std::endl;
        return 1;
    }

    std::cout << "=== ANTI-CHEAT SERVER IS RUNNING ON PORT 8080 ===" << std::endl;
    std::cout << "Waiting for students to connect..." << std::endl;

    // 4. Accept Loop
    while (true)
    {
        sockaddr_in client_addr;
        socklen_t len = sizeof(client_addr);
        int client_sock = accept(server_fd, (sockaddr *)&client_addr, &len);

        if (client_sock >= 0)
        {
            // Lấy IP client
            char ipStr[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client_addr.sin_addr, ipStr, INET_ADDRSTRLEN);
            std::string client_ip(ipStr);

            std::cout << "New connection from: " << client_ip << std::endl;

            // Tạo luồng mới để xử lý kết nối này (Detach để chạy ngầm)
            std::thread(client_thread, client_sock, client_ip).detach();
        }
    }

    close(server_fd);
    return 0;
}