# Tài liệu hệ thống — Base + Lớp 1 + Lớp 2 + Lớp 3

Tài liệu này mô tả **các lớp chống gian lận** trong dự án Rollcall.

- **Base**: lọc truy cập theo mạng (CIDR whitelist).
- **Lớp 1**: token theo time-slice (TTL) để chống replay và giới hạn thời điểm điểm danh.
- **Lớp 2**: fingerprint (thiết bị/môi trường) để giảm “điểm danh hộ” bằng thiết bị lạ.
- **Lớp 3**: Passkey/WebAuthn để ràng buộc thao tác điểm danh với xác thực sinh trắc học trên thiết bị.

> Mục tiêu chung: tăng chi phí gian lận. Không có lớp nào đảm bảo 100% nếu người dùng thông đồng trực tiếp.

---

## Base — Lọc truy cập theo mạng (Campus CIDR)

### Mục tiêu

- Chỉ cho phép truy cập UI/API từ các dải IP “được phép” (ví dụ mạng trường/campus hoặc LAN).
- Giảm rủi ro “điểm danh từ xa” qua Internet.

### Hệ thống dùng gì?

- **IP của client** (địa chỉ nguồn kết nối HTTP).
- Danh sách CIDR allowlist cấu hình bằng biến môi trường `CAMPUS_CIDRS`.
  - Nếu không set thì mặc định cho phép các dải nội bộ: `127.0.0.0/8`, `10.0.0.0/8`, `172.16.0.0/12`, `192.168.0.0/16`.

### Giới hạn

- Nếu sinh viên dùng 4G (không thuộc dải allowlist) sẽ bị chặn.
- Nếu mạng NAT/proxy/VPN làm IP thay đổi, có thể chặn nhầm.
- Base chỉ là “cổng” lọc; không xác minh danh tính.

---

## Lớp 1 — Token theo time-slice (TTL)

### Mục tiêu

- Token chỉ hợp lệ trong một khoảng thời gian ngắn (TTL).
- Chống:
  - **replay** (gửi lại token cũ),
  - gửi token từ buổi trước,
  - một người gửi lại nhiều lần trong cùng 1 lát cắt thời gian.

### Hệ thống dùng gì?

- **Token bits** (chuỗi 0/1) được tạo từ:
  - `secret` (bí mật server),
  - `session_id` (ID buổi điểm danh),
  - `counter` (đếm theo thời gian: `now_ms / ttl_ms`).

### Quy tắc chính (diễn giải)

- Giảng viên lấy token qua endpoint `GET /api/token?bits=N` (phía backend C++).
- Sinh viên gửi lại token bits trong `POST /api/attendance/submit`.
- Backend chấp nhận nếu:
  - bits khớp **counter hiện tại** hoặc **counter trước đó** (grace window để tránh sát giờ hết hạn),
  - và chưa dùng lại token trong cùng time-slice,
  - và chưa “checked in” lần nào trong cùng buổi.

### Tham số quan trọng

- `ttl_ms`: thời gian sống của mỗi time-slice.
  - TTL quá ngắn dễ gây `invalid_token` (đang phát/nhận âm thanh thì counter nhảy).
  - TTL có thể cấu hình qua `ROLLCALL_TTL_MS` (backend C++).

### Giới hạn

- Nếu hai người cùng phòng nghe được âm thanh và gửi nhanh, Lớp 1 một mình không chặn “điểm danh hộ”.
- Lớp 1 chủ yếu đảm bảo “đúng thời điểm/đúng buổi”, không đảm bảo “đúng người”.

---

## Lớp 2 — Fingerprint (thiết bị/môi trường)

Lớp 2 dùng một số thông tin “mềm” của trình duyệt/thiết bị để giảm khả năng một người dùng thiết bị lạ để điểm danh thay.

### Mục tiêu

- **Không** dùng GPS/camera/ảnh/định danh hệ điều hành sâu.
- Chỉ dùng thuộc tính trình duyệt/thiết bị để:
  - tạo **baseline** lần đầu (enroll),
  - so sánh lần sau xem có “giống thiết bị quen” hay không (matched/mismatch),
  - (tuỳ cấu hình) chặn một thiết bị cố enroll cho nhiều MSSV.

> Lưu ý: Đây là fingerprint dạng **heuristic** (ước lượng), không phải định danh tuyệt đối.

## Các trường fingerprint đang dùng

### 1) Nền tảng thiết bị (platform)
- Nguồn: trình duyệt cung cấp (tương ứng với `navigator.platform`).
- Ý nghĩa: gợi ý loại nền tảng, ví dụ Windows/Mac/Linux/iPhone/Android.
- Độ ổn định: khá ổn định, nhưng có thể bị “spoof” hoặc thay đổi theo browser.
- Rủi ro/riêng tư: mức thấp–vừa.

### 2) Múi giờ (timezone)
- Nguồn: cài đặt hệ thống/Trình duyệt (tương ứng `Intl.DateTimeFormat().resolvedOptions().timeZone`).
- Ý nghĩa: gợi ý **khu vực địa lý** ở mức thô (ví dụ `Asia/Ho_Chi_Minh`).
- Không phải GPS: **không** cho biết vị trí chính xác; chỉ phản ánh cấu hình múi giờ.
- Độ ổn định: thường ổn định; có thể đổi nếu người dùng đổi múi giờ.
- Rủi ro/riêng tư: mức vừa (vì có thể suy ra vùng).

### 3) Ngôn ngữ trình duyệt (language)
- Nguồn: cấu hình browser (tương ứng `navigator.language`).
- Ý nghĩa: gợi ý ngôn ngữ ưu tiên, ví dụ `vi-VN`, `en-US`.
- Độ ổn định: ổn định; có thể đổi trong settings.
- Rủi ro/riêng tư: thấp.

### 4) Kích thước màn hình (screen width/height)
- Nguồn: độ phân giải hiển thị (tương ứng `screen.width`, `screen.height`).
- Ý nghĩa: gợi ý loại thiết bị (điện thoại vs laptop) và chế độ (xoay dọc/ngang).
- Độ ổn định: vừa; có thể thay đổi do xoay màn hình, thanh điều hướng, browser UI.
- Rủi ro/riêng tư: thấp–vừa.

### 5) Mật độ điểm ảnh (devicePixelRatio / DPR)
- Nguồn: trình duyệt (tương ứng `window.devicePixelRatio`).
- Ý nghĩa: gợi ý loại màn hình/độ sắc nét; khác nhau giữa nhiều thiết bị.
- Độ ổn định: khá ổn định; đôi khi thay đổi khi zoom hoặc chế độ hiển thị.
- Rủi ro/riêng tư: thấp–vừa.

### 6) Số luồng CPU logic (hardwareConcurrency)
- Nguồn: trình duyệt (tương ứng `navigator.hardwareConcurrency`).
- Ý nghĩa: gợi ý “mức phần cứng” (số core/luồng).
- Độ ổn định: tương đối; có thể bị làm tròn hoặc bị ẩn tuỳ browser.
- Rủi ro/riêng tư: vừa.

### 7) Bộ nhớ thiết bị (deviceMemory)
- Nguồn: trình duyệt (tương ứng `navigator.deviceMemory`).
- Ý nghĩa: gợi ý mức RAM (thường là số làm tròn: 2/4/8/… GB).
- Độ ổn định: tương đối; trên iOS có thể không có hoặc luôn 0.
- Rủi ro/riêng tư: vừa.

### 8) Có cảm ứng hay không (touch)
- Nguồn: trình duyệt (tương ứng `navigator.maxTouchPoints > 0`).
- Ý nghĩa: phân biệt thiết bị cảm ứng (điện thoại/tablet) với máy không cảm ứng.
- Độ ổn định: cao.
- Rủi ro/riêng tư: thấp.

## Những thứ hệ thống *không* thu thập ở Lớp 2

- GPS/toạ độ vị trí thực.
- Wi‑Fi SSID/BSSID, Bluetooth.
- Camera/ảnh/khuôn mặt/vân tay sinh trắc học.
- Danh bạ, ID thiết bị hệ điều hành.

## Lớp 2 dùng fingerprint như thế nào (diễn giải)

- **Lần đầu** một MSSV gửi điểm danh hợp lệ:
  - hệ thống lưu fingerprint làm **baseline** cho MSSV đó (trạng thái: `enrolled`).
- **Các lần sau**:
  - hệ thống tính “độ giống” (0–100%) so với baseline.
  - nếu thấp hơn ngưỡng (mặc định 70%) → `fingerprint_mismatch`.
  - nếu đạt ngưỡng → `matched`.
- (Tuỳ chọn trong backend) **chặn một thiết bị enroll cho nhiều MSSV**:
  - nếu fingerprint mới “quá giống” fingerprint đã baseline của MSSV khác, hệ thống có thể trả `fingerprint_conflict`.

## Giới hạn và cách hiểu đúng

- Fingerprint **không chống được thông đồng** nếu hai người cố tình dùng chung thiết bị.
- Fingerprint **có thể sai** do:
  - đổi máy, đổi browser, xoay màn hình, đổi múi giờ/ngôn ngữ,
  - quyền riêng tư/anti‑fingerprinting của browser làm giảm độ chính xác.
- Vì vậy Lớp 2 nên được xem như “tín hiệu phụ” để giảm gian lận, không thay thế xác minh danh tính.

---

## Lớp 3 — Passkey/WebAuthn (xác thực sinh trắc học)

### Mục tiêu

- Buộc thao tác điểm danh phải đi kèm **xác thực sinh trắc học** (FaceID/TouchID/vân tay/khóa màn hình) trên thiết bị.
- Giảm “điểm danh hộ từ xa” (ai đó biết MSSV và token âm thanh nhưng không có thiết bị đã đăng ký).

### Hệ thống dùng gì?

- WebAuthn/Passkeys trong trình duyệt (`navigator.credentials.create/get`).
- Server (Python gateway) lưu credential theo `student_id` vào file JSON (không dùng DB).
- Khi sinh viên xác thực thành công, server phát `auth_token` **ngắn hạn** để “mở khóa” 1 lần submit điểm danh.

### Quy tắc chính (diễn giải)

1) **Đăng ký passkey** (enroll)
  - Sinh viên nhập `student_id`.
  - Trình duyệt tạo passkey (có xác thực sinh trắc học).
  - Server lưu credential gắn với `student_id`.

2) **Xác thực passkey** (auth)
  - Sinh viên bấm “Xác Thực Passkey”.
  - Trình duyệt ký challenge bằng khóa riêng (unlock bằng sinh trắc học).
  - Server verify và trả về `auth_token` (ngắn hạn).

3) **Gửi điểm danh**
  - Client gửi `auth_token` kèm payload điểm danh.
  - Python gateway kiểm tra `auth_token` hợp lệ rồi mới forward sang backend C++.
  - Gateway override `student_id` theo `auth_token` để tránh giả mạo MSSV phía client.

### Giới hạn (rất quan trọng)

- Passkey chứng minh “có quyền dùng thiết bị”, không chứng minh “đúng người ngoài đời”.
- Nếu ai đó có thể **đăng ký passkey cho MSSV của người khác** ngay từ đầu (vì `student_id` chỉ là chuỗi nhập ở client), Layer 3 hiện tại không tự ngăn được kịch bản này.
- Một số nền tảng (đặc biệt Android) có ràng buộc HTTPS nghiêm ngặt:
  - WebAuthn bị chặn nếu có TLS warning / certificate không được tin cậy.
  - Nhiều máy Android không chấp nhận IP trực tiếp làm domain và/hoặc không chấp nhận CA tự cài cho WebAuthn.

