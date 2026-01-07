# Rollcall — Điểm danh/Quiz chống gian lận (Âm thanh cao tần + Passkey/WebAuthn + Fingerprint + DB roster)




## I. Giới thiệu chung




Hệ thống được thiết kế nhằm nâng cao tính minh bạch trong công tác điểm danh (và có thể mở rộng sang làm quiz/kiểm tra). Nguyên lý cốt lõi dựa trên kinh tế học hành vi: **gia tăng chi phí gian lận** (thời gian, công sức, rủi ro bị phát hiện) vượt quá lợi ích mang lại, từ đó giảm động lực thực hiện hành vi sai trái.




Về kỹ thuật, hệ thống kết hợp:




- **Âm thanh tần số cao** (mã hóa token thành chuỗi bit 0/1).
- **Passkey/WebAuthn** (xác thực sinh trắc học/khóa màn hình trên thiết bị).
- **Device fingerprint** (tín hiệu “mềm” để phát hiện bất thường).
- **SQLite roster DB** (danh sách lớp + tiến độ theo thời gian thực trên trang giảng viên).




## II. Các kịch bản gian lận mục tiêu




Hệ thống hướng tới giảm/khó hóa các hành vi:




1) Sinh viên không có mặt tại lớp nhưng vẫn điểm danh/quiz từ xa.
2) Sinh viên nhờ người khác điểm danh hộ tại lớp.
3) Sinh viên ở khu vực lân cận (ngoài phạm vi lớp học) cố gắng bắt tín hiệu để điểm danh.




## III. Mô hình hệ thống và giả định cơ sở




### 1) Hạ tầng lớp học




- Lớp học có loa đủ khả năng phát dải tần cao.
- Môi trường có thể có nhiễu âm; hệ thống chấp nhận sai số ở mức hợp lý.




### 2) Thiết bị đầu cuối




- Giảng viên: thiết bị có trình duyệt, phát được âm thanh.
- Sinh viên: smartphone/laptop có trình duyệt và micro.




### 3) Yêu cầu bảo mật web




- **HTTPS là bắt buộc** cho WebAuthn và (nhiều nền tảng) để truy cập micro.




## IV. Quy trình vận hành (Pipeline)




### A. Khởi tạo (lần đầu)




1) Sinh viên nhập MSSV.
2) Đăng ký Passkey/WebAuthn cho MSSV đó (1 lần cho mỗi host/rpId).
3) Hệ thống ghi nhận fingerprint baseline (thiết bị/môi trường) để dùng về sau.




### B. Vận hành (các lần sau)




1) Giảng viên bắt đầu session mới.
2) Backend tạo token TTL, mã hóa thành chuỗi bit và phát qua âm thanh.
3) Sinh viên thu âm → giải mã bit → xác thực Passkey → gửi điểm danh.
4) Trang giảng viên hiển thị tiến độ theo roster (tự tick ✅).




## V. Cấu trúc bảo mật đa lớp




### Base — Kiểm soát phạm vi mạng (CIDR allowlist)




- Gateway chỉ cho phép request từ các dải IP được whitelist bằng biến môi trường `CAMPUS_CIDRS`.
- Mục tiêu: hạn chế điểm danh từ Internet (bắt buộc truy cập từ mạng cho phép).




> Ghi chú: nếu tổ chức có 802.1X/VPN nội bộ, có thể coi đó là lớp hạ tầng bổ trợ. Trong repo hiện tại, cơ chế Base được triển khai bằng CIDR allowlist ở gateway.




### Lớp 1 — WebAuthn (Xác thực sinh trắc học + Token TTL)




- Sinh viên xác thực WebAuthn (FaceID/TouchID/Passcode...) với MSSV của mình.
- Gateway cấp `auth_token` ngắn hạn sau khi xác thực thành công.
- Token chỉ hợp lệ trong thời gian ngắn (TTL).
- Khi `POST /api/attendance/submit`, gateway chỉ forward nếu `auth_token` hợp lệ và **override `student_id` theo token** để tránh giả mạo MSSV từ client.
- Chấp nhận counter hiện tại hoặc counter trước đó (grace) để giảm lỗi sát hạn.
- Chặn gửi lặp trong cùng time-slice (single-use) và “1 lần/1 MSSV/1 session”.




### Lớp 2 — Fingerprint (Nhân dạng thiết bị/môi trường)




- Lưu fingerprint baseline cho MSSV (dựa trên đặc tính của sinh viên trong giao diện).
- Tính “độ giống” 0–100% theo trọng số; mismatch nếu dưới ngưỡng (mặc định 70%).
- Tuỳ cấu hình: chặn một thiết bị enroll cho nhiều MSSV (trả `fingerprint_conflict`).




> Lớp 2 là heuristic (tín hiệu phụ), không chứng minh tuyệt đối danh tính.




### Lớp 3 — Ultrasonic (Âm thanh cao tần + Single-use encoding)




- Giảng viên bắt đầu session mới.
- Backend tạo token TTL, mã hóa thành chuỗi bit (0/1) và phát qua âm thanh tần số cao (ultrasonic).
- Sinh viên thu âm → giải mã bit → xác thực qua các lớp 1 và 2 → gửi điểm danh.
- Tín hiệu âm thanh chỉ phát trong lớp (phạm vi sóng âm hạn chế) để giảm khả năng bắt từ xa.
- Token được mã hóa thành chuỗi bit duy nhất, chặn replay và sử dụng lại.




## VI. Theo dõi tiến độ trực tiếp (SQLite roster DB)




Backend dùng SQLite DB để lưu:




- `students`: danh sách lớp (MSSV, họ tên)
- `sessions`: session_id
- `attendance`: trạng thái đã điểm danh theo session




Trang giảng viên sẽ hiển thị danh sách lớp và tự tick ✅ khi sinh viên submit thành công.




## VII. Chạy hệ thống




### 1) Build & Run backend (C++)




Build từ thư mục project root:




```bash
g++ -std=c++17 -O2 -Wall -Wextra \
  backend/server.cpp backend/http.cpp backend/json.cpp backend/token.cpp backend/app.cpp backend/db.cpp \
  -lsqlite3 -o backend/server
```




Hoặc build trong thư mục `backend/`:




```bash
g++ -std=c++17 -O2 -Wall -Wextra \
  server.cpp http.cpp json.cpp token.cpp app.cpp db.cpp \
  -lsqlite3 -o server
```




Run (DB mặc định là `rollcall.db` trong thư mục chạy):




```bash
cd backend
ROLLCALL_DB_PATH=rollcall.db ./server 9000
```

#### Chạy CSDL (SQLite) cố định tạm thời

`ROLLCALL_DB_PATH` phải trỏ tới **file SQLite DB** (ví dụ `rollcall.db`), không phải file roster `.txt`.

Khuyến nghị dùng đường dẫn cố định (absolute path) để DB không bị “đổi chỗ” theo thư mục đang chạy:

```bash
mkdir -p data
cd backend
ROLLCALL_DB_PATH="$PWD/../data/rollcall.db" ./server 9000
```

Muốn reset CSDL (xoá toàn bộ roster/attendance) thì xóa các file sau rồi chạy lại backend:

```bash
rm -f data/rollcall.db data/rollcall.db-wal data/rollcall.db-shm
```




Tuỳ chọn:




- TTL token: `ROLLCALL_TTL_MS=30000 ./server 9000`
- Tắt chặn “1 thiết bị -> nhiều MSSV”: `ROLLCALL_FP_UNIQUE=0 ./server 9000`




### 2) Run gateway HTTPS + UI (Python)

#### Tạo `cert.pem` và `key.pem` (không commit lên GitHub)

Do WebAuthn và quyền micro thường yêu cầu HTTPS, repo này chạy gateway bằng HTTPS và cần 2 file:

- `cert.pem`
- `key.pem`


Script có sẵn:

```bash
./scripts/mkcert_dev.sh
```

Yêu cầu: cài `mkcert` và cài root CA của mkcert trên máy:

```bash
brew install mkcert
mkcert -install
```

Script sẽ tạo `cert.pem`/`key.pem` ngay tại project root (đúng với cách `server.py` load cert).




Từ thư mục project root:




```bash
python3 server.py
```




Mở UI:




- `https://localhost:8000/lecturer.html`
- `https://localhost:8000/student.html`




## VIII. Thêm danh sách lớp vào DB (qua API)




### Import roster




Endpoint: `POST /api/roster/import`




Body là text, mỗi dòng một sinh viên theo format:




```
MSSV,Full Name
```




Ví dụ import qua gateway HTTPS:




```bash
curl -k -X POST https://localhost:8000/api/roster/import \
  --data-binary $'22123456,Nguyễn Văn A\n22123457,Trần Thị B\n'
```

#### Import “danh sách sinh viên vào web” (để hiện trên trang giảng viên)

1) Mở và sửa file mẫu (tuỳ ý thêm/bớt sinh viên):

- `data/roster_sample.txt`

2) Import file đó vào hệ thống (gateway sẽ forward vào backend SQLite):

```bash
curl -k -X POST https://localhost:8000/api/roster/import \
  --data-binary @data/roster_sample.txt
```

3) Vào web để xem:

- Mở `https://localhost:8000/lecturer.html`
- Nhấn **Làm Mới Tiến Độ** (hoặc chờ auto refresh)

Ghi chú: import là “upsert” — trùng MSSV thì cập nhật lại tên.




### Xem tiến độ theo roster




```bash
curl -k https://localhost:8000/api/roster/list
```




## IX. API chính




- `GET /api/health`
- `GET /api/session`
- `POST /api/session/start`
- `GET /api/token?bits=N`
- `POST /api/attendance/submit`
- `GET /api/attendance/list`
- `POST /api/roster/import`
- `GET /api/roster/list`




## X. Hạn chế và hướng tăng cường




- Không có hệ thống nào chống gian lận 100% nếu người dùng thông đồng trực tiếp.
- Hiệu quả âm thanh phụ thuộc loa/micro và môi trường nhiễu.
- Có thể tăng cường phần cứng bằng loa định hướng (parametric) để giảm rò âm ra ngoài lớp.




