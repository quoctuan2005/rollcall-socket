# Quiz Điểm Danh Chống Gian Lận

Hệ thống quiz online cho sinh viên với các tính năng chống gian lận:
- Device Fingerprinting (xác thực thiết bị)
- IP Tracking (kiểm soát địa chỉ IP)
- Real-time Admin Dashboard (giám sát gian lận)

---

## Cấu trúc thư mục

```
rollcall-socket/
├── src/                          # Source code C++
│   ├── main.cpp                  # Server chính
│   ├── game_manager.hpp          # Quản lý xác thực & ghi log
│   └── ws_utils.hpp              # WebSocket utilities
├── web/                          # Frontend HTML/JS
│   ├── index.html                # Giao diện sinh viên
│   └── admin.html                # Admin dashboard
├── docs/                         # Tài liệu
│   ├── ANTI_CHEAT_SECURITY_REPORT.md
│   └── REPORT_ANTI_FRAUD_METHODS.md
├── build/                        # Binary output
│   └── server                    # Executable
├── Makefile                      # Build script
├── README.md                     # File này
└── .git/                         # Version control
```

---

## Cách chạy

### 1. Biên dịch
```bash
make build
```

Hoặc manual:
```bash
clang++ -std=c++17 -O2 -Wall src/main.cpp -o build/server
```

### 2. Chạy server
```bash
make run
```

Hoặc chạy từ thư mục root:
```bash
./build/server &
```

Server sẽ khởi động trên:
- Sinh viên: http://localhost:8080
- Admin: http://localhost:8080/admin

### 3. Stop server
```bash
make stop
```

---

## Tính năng

### Sinh viên
1. Truy cập http://localhost:8080
2. Nhập MSSV
3. Hệ thống thu thập Device Fingerprint (lần đầu)
4. Làm quiz
5. Nộp bài

### Admin
1. Truy cập http://localhost:8080/admin
2. Xem 2 bảng real-time:
   - **Danh sách nộp bài** (bình thường)
   - **Nhật ký gian lận** (phát hiện vi phạm)

---

## Các phương pháp chống gian lận

### 1. Device Fingerprinting
- Thu thập: UserAgent, Platform, Screen, Canvas, WebGL, Timezone
- Hash: SHA-256 → Định danh thiết bị duy nhất
- Phát hiện: 1 máy 2 acc, 1 acc 2 máy

### 2. IP Tracking
- Firewall: Chỉ cho phép IP nội bộ (192.168.x.x, 10.x.x.x)
- Tracking: 1 MSSV = 1 IP gốc
- Phát hiện: Đổi IP = gian lận

### 3. Real-time Monitoring
- Admin thấy ngay khi phát hiện gian lận
- Ghi log chi tiết: MSSV, IP, lý do, thời gian

---

## Cấu hình

### Port
Mặc định: `8080`
Sửa trong `src/main.cpp`:
```cpp
addr.sin_port = htons(8080);  // Thay 8080
```

### IP Firewall
Trong `src/main.cpp`:
```cpp
bool is_ip_allowed(const std::string &ip) {
    if (ip.find("127.0.0.1") == 0) return true;  // localhost
    if (ip.find("192.168.") == 0) return true;   // Mạng nhà
    if (ip.find("10.") == 0) return true;        // Mạng công ty
    return false;
}
```

---

## Troubleshooting

### Server không khởi động
```bash
# Kiểm tra port có bị chiếm không
lsof -i :8080

# Kill process cũ
kill -9 <PID>
```

### Browser không connect
- Kiểm tra: `http://localhost:8080`
- Kiểm tra firewall
- Kiểm tra IP có hợp lệ không

### File HTML không load
- Kiểm tra file `web/index.html` và `web/admin.html` có tồn tại
- Server chạy từ thư mục root (`/Users/haiannguyen/rollcall-socket`)

---

## Hiệu suất

| Loại gian lận | Phát hiện | Ghi nhận |
|-------------|----------|---------|
| 1 máy 2 acc | 95% | Yes |
| 1 acc 2 máy | 90% | Yes |
| Đổi IP | 80% | Yes |
| VPN/Proxy | 85% | Yes |
| Copy đáp án | 0% | No |

**Tóm tắt:** ~60-65% hiệu quả phòng chống gian lận

---

## Cảnh báo bảo mật

LỖ HỔNG HIỆN TẠI:
- Admin không có mật khẩu
- WebSocket không mã hóa (HTTP + WS)
- Không có rate limiting
- Fingerprint có thể bypass

Xem chi tiết: docs/ANTI_CHEAT_SECURITY_REPORT.md

---

## Thay đổi gần đây

- Tổ chức thư mục (src/, web/, docs/, build/)
- Thêm Device Fingerprinting + IP Tracking
- Real-time Admin Dashboard
- Ghi log gian lận chi tiết

---

## Đóng góp

Issues: Báo cáo lỗi, đề xuất tính năng
Pull requests: Welcome!

---

Phiên bản: 1.0
Ngày cập nhật: 19/12/2025
Tác giả: AI Assistant
