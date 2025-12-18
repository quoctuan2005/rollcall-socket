#pragma once
#include <map>
#include <set>
#include <vector>
#include <mutex>
#include <algorithm>
#include <iostream>
#include "ws_utils.hpp"

class GameManager
{
private:
    std::mutex data_lock;
    std::map<std::string, std::string> trusted_devices;
    std::map<std::string, std::string> active_ips;

    std::vector<int> client_sockets; // Socket sinh viên
    std::vector<int> admin_sockets;  // Socket Admin (để gửi báo cáo)

public:
    void add_socket(int sock)
    {
        std::lock_guard<std::mutex> lock(data_lock);
        client_sockets.push_back(sock);
    }

    // Đăng ký socket này là Admin
    void register_admin(int sock)
    {
        std::lock_guard<std::mutex> lock(data_lock);
        admin_sockets.push_back(sock);
        std::cout << ">>> ADMIN CONNECTED <<<" << std::endl;
    }

    void remove_socket(int sock)
    {
        std::lock_guard<std::mutex> lock(data_lock);
        client_sockets.erase(std::remove(client_sockets.begin(), client_sockets.end(), sock), client_sockets.end());
        admin_sockets.erase(std::remove(admin_sockets.begin(), admin_sockets.end(), sock), admin_sockets.end());
    }

    // Gửi sự kiện cho Admin
    void notify_admin(const std::string &json_msg)
    {
        // Lưu ý: Đã lock ở hàm gọi, hoặc lock tại đây nếu cần thiết.
        // Ở đây ta giả định hàm gọi đã xử lý logic, ta chỉ gửi.
        for (int sock : admin_sockets)
        {
            send_text_frame(sock, json_msg);
        }
    }

    // Ghi nhận nộp bài -> Báo Admin
    void log_submission(std::string mssv, std::string ip, std::string answer)
    {
        std::lock_guard<std::mutex> lock(data_lock);
        std::string msg = "{\"type\":\"NEW_SUBMISSION\",\"mssv\":\"" + mssv + "\",\"ip\":\"" + ip + "\",\"answer\":\"" + answer + "\"}";
        notify_admin(msg);
    }

    // Ghi nhận bị chặn -> Báo Admin
    void log_block(std::string ip, std::string mssv, std::string reason)
    {
        std::lock_guard<std::mutex> lock(data_lock);
        std::string msg = "{\"type\":\"NEW_BLOCK\",\"ip\":\"" + ip + "\",\"mssv\":\"" + mssv + "\",\"reason\":\"" + reason + "\"}";
        notify_admin(msg);
    }

    std::string handle_login(const std::string &ip, const std::string &mssv, const std::string &fp)
    {
        std::lock_guard<std::mutex> lock(data_lock);

        // Logic check IP trùng
        if (active_ips.count(ip) && active_ips[ip] != mssv)
        {
            // BÁO ADMIN GIAN LẬN
            std::string reason = "Chung IP với " + active_ips[ip];
            std::string msg = "{\"type\":\"NEW_BLOCK\",\"ip\":\"" + ip + "\",\"mssv\":\"" + mssv + "\",\"reason\":\"" + reason + "\"}";
            notify_admin(msg);

            return "{\"status\":\"BLOCKED\",\"msg\":\"Gian lận IP: Máy này đang dùng cho SV khác!\"}";
        }

        if (trusted_devices.find(mssv) == trusted_devices.end())
        {
            trusted_devices[mssv] = fp;
            active_ips[ip] = mssv;
            return "{\"status\":\"OK\",\"msg\":\"Đăng ký thiết bị thành công.\"}";
        }
        else
        {
            if (trusted_devices[mssv] == fp)
            {
                active_ips[ip] = mssv;
                return "{\"status\":\"OK\",\"msg\":\"Xác thực thành công.\"}";
            }
            else
            {
                // BÁO ADMIN GIAN LẬN
                std::string reason = "Sai Fingerprint (Thiết bị lạ)";
                std::string msg = "{\"type\":\"NEW_BLOCK\",\"ip\":\"" + ip + "\",\"mssv\":\"" + mssv + "\",\"reason\":\"" + reason + "\"}";
                notify_admin(msg);

                return "{\"status\":\"BLOCKED\",\"msg\":\"CẢNH BÁO: Phát hiện đăng nhập từ thiết bị lạ!\"}";
            }
        }
    }
};