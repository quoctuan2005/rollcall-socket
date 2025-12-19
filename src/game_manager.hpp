#pragma once
#include <map>
#include <set>
#include <vector>
#include <mutex>
#include <algorithm>
#include <iostream>
#include <ctime>
#include "ws_utils.hpp"

class GameManager
{
private:
    std::mutex data_lock;
    std::map<std::string, std::string> mssv_to_fingerprint; // MSSV -> Fingerprint gốc
    std::map<std::string, std::string> fingerprint_to_mssv; // Fingerprint -> MSSV gốc
    std::map<std::string, std::string> mssv_to_ip;          // MSSV -> IP gốc (lần đầu)
    std::map<std::string, std::string> active_ips;          // IP -> MSSV hiện tại

    std::vector<int> client_sockets; // Socket sinh viên
    std::vector<int> admin_sockets;  // Socket Admin

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

    // Ghi nhận gian lận (nhưng vẫn cho vào quiz) -> Báo Admin
    void log_fraud(std::string ip, std::string mssv, std::string reason)
    {
        std::lock_guard<std::mutex> lock(data_lock);
        std::string msg = "{\"type\":\"NEW_FRAUD_ALERT\",\"ip\":\"" + ip + "\",\"mssv\":\"" + mssv + "\",\"reason\":\"" + reason + "\",\"timestamp\":\"" + std::to_string(std::time(nullptr)) + "\"}";
        notify_admin(msg);
    }

    std::string handle_login(const std::string &ip, const std::string &mssv, const std::string &fp)
    {
        std::lock_guard<std::mutex> lock(data_lock);

        // Kiểm tra: Fingerprint này đã dùng cho MSSV khác chưa? (1 máy không được 2 acc)
        if (fingerprint_to_mssv.count(fp) && fingerprint_to_mssv[fp] != mssv)
        {
            std::string reason = "Thiết bị này đã đăng ký cho " + fingerprint_to_mssv[fp] + ". 1 máy chỉ cho 1 mã sinh viên!";
            std::string fraudMsg = "{\"type\":\"NEW_FRAUD_ALERT\",\"ip\":\"" + ip + "\",\"mssv\":\"" + mssv + "\",\"reason\":\"" + reason + "\",\"severity\":\"CRITICAL\"}";
            for (int sock : admin_sockets)
            {
                send_text_frame(sock, fraudMsg);
            }
            active_ips[ip] = mssv;
            return "{\"status\":\"OK\",\"msg\":\"Đăng nhập thành công.\",\"fraud_flag\":true,\"fraud_type\":\"same_device_multiple_accounts\"}";
        }

        // Kiểm tra: MSSV này đã dùng FP khác chưa? (1 acc chỉ 1 thiết bị)
        if (mssv_to_fingerprint.count(mssv) && mssv_to_fingerprint[mssv] != fp)
        {
            std::string reason = "Tài khoản này đã dùng thiết bị khác: " + mssv_to_fingerprint[mssv];
            std::string fraudMsg = "{\"type\":\"NEW_FRAUD_ALERT\",\"ip\":\"" + ip + "\",\"mssv\":\"" + mssv + "\",\"reason\":\"" + reason + "\",\"severity\":\"HIGH\"}";
            for (int sock : admin_sockets)
            {
                send_text_frame(sock, fraudMsg);
            }
            active_ips[ip] = mssv;
            return "{\"status\":\"OK\",\"msg\":\"Đăng nhập thành công.\",\"fraud_flag\":true,\"fraud_type\":\"same_account_different_device\"}";
        }

        // Kiểm tra: MSSV này đã đăng ký từ IP khác chưa? (Lần đầu = lưu IP)
        if (mssv_to_ip.count(mssv) && mssv_to_ip[mssv] != ip)
        {
            std::string reason = "Tài khoản này đã đăng ký từ IP: " + mssv_to_ip[mssv] + ", bây giờ từ: " + ip;
            std::string fraudMsg = "{\"type\":\"NEW_FRAUD_ALERT\",\"ip\":\"" + ip + "\",\"mssv\":\"" + mssv + "\",\"reason\":\"" + reason + "\",\"severity\":\"CRITICAL\"}";
            for (int sock : admin_sockets)
            {
                send_text_frame(sock, fraudMsg);
            }
            active_ips[ip] = mssv;
            return "{\"status\":\"OK\",\"msg\":\"Đăng nhập thành công.\",\"fraud_flag\":true,\"fraud_type\":\"different_ip\"}";
        }

        // Nếu cả hai chưa từng gặp: lần đầu đăng ký
        if (mssv_to_fingerprint.find(mssv) == mssv_to_fingerprint.end())
        {
            mssv_to_fingerprint[mssv] = fp;
            fingerprint_to_mssv[fp] = mssv;
            mssv_to_ip[mssv] = ip; // LƯU IP LẦN ĐẦU
            active_ips[ip] = mssv;
            return "{\"status\":\"OK\",\"msg\":\"Đăng ký thiết bị thành công.\"}";
        }

        // Nếu FP và MSSV đều khớp + IP khớp: xác thực thành công
        active_ips[ip] = mssv;
        return "{\"status\":\"OK\",\"msg\":\"Xác thực thành công.\"}";
    }
};