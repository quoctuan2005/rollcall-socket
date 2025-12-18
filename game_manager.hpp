#pragma once
#include <map>
#include <set>
#include <vector>
#include <mutex>
#include <algorithm>
#include <iostream>
#include "ws_utils.hpp" // Sử dụng các hàm tiện ích từ file utils

class GameManager
{
private:
    std::mutex data_lock;

    // Dữ liệu chống gian lận
    std::map<std::string, std::string> trusted_devices; // MSSV -> VisitorID (Fingerprint)
    std::map<std::string, std::string> active_ips;      // IP -> MSSV

    // Danh sách socket để Broadcast (Gửi tin nhắn cho cả lớp)
    std::vector<int> client_sockets;

public:
    // Thêm một sinh viên mới kết nối
    void add_socket(int sock)
    {
        std::lock_guard<std::mutex> lock(data_lock);
        client_sockets.push_back(sock);
    }

    // Xóa sinh viên khi ngắt kết nối
    void remove_socket(int sock)
    {
        std::lock_guard<std::mutex> lock(data_lock);
        client_sockets.erase(
            std::remove(client_sockets.begin(), client_sockets.end(), sock),
            client_sockets.end());
    }

    // Gửi tin nhắn cho TOÀN BỘ sinh viên (Ví dụ: Giáo viên gửi câu hỏi)
    void broadcast(const std::string &msg)
    {
        std::lock_guard<std::mutex> lock(data_lock);
        for (int sock : client_sockets)
        {
            // Gửi tin nhắn text frame cho từng socket
            send_text_frame(sock, msg);
        }
    }

    // Xử lý logic Đăng nhập & Check gian lận
    // Trả về: Chuỗi JSON phản hồi để gửi lại cho Client
    std::string handle_login(const std::string &ip, const std::string &mssv, const std::string &fp)
    {
        std::lock_guard<std::mutex> lock(data_lock);

        // 1. Check IP Cheating: Một IP mạng chỉ được dùng cho 1 MSSV
        if (active_ips.count(ip) && active_ips[ip] != mssv)
        {
            std::cout << "[CHEAT ALERT] IP " << ip << " dang dung cho " << active_ips[ip]
                      << " nhung lai dang nhap " << mssv << std::endl;
            return "{\"status\":\"BLOCKED\",\"msg\":\"Gian lận IP: Máy này đang dùng cho SV khác!\"}";
        }

        // 2. Trust on First Use (TOFU):
        // Nếu MSSV này chưa từng đăng nhập -> Tin tưởng thiết bị hiện tại
        if (trusted_devices.find(mssv) == trusted_devices.end())
        {
            trusted_devices[mssv] = fp;
            active_ips[ip] = mssv; // Đánh dấu IP này đã có chủ

            std::cout << "[NEW USER] MSSV " << mssv << " linked to DeviceID " << fp << std::endl;
            return "{\"status\":\"OK\",\"msg\":\"Đăng ký thiết bị thành công.\"}";
        }
        else
        {
            // Nếu đã từng đăng nhập -> Kiểm tra xem có đúng thiết bị cũ không
            if (trusted_devices[mssv] == fp)
            {
                // Update lại IP hiện tại (phòng trường hợp rớt mạng vào lại)
                active_ips[ip] = mssv;
                return "{\"status\":\"OK\",\"msg\":\"Xác thực thành công.\"}";
            }
            else
            {
                std::cout << "[CHEAT ALERT] MSSV " << mssv << " login tu thiet bi la!" << std::endl;
                return "{\"status\":\"BLOCKED\",\"msg\":\"CẢNH BÁO: Phát hiện đăng nhập từ thiết bị lạ!\"}";
            }
        }
    }
};