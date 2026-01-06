# Rollcall-Socket: Hệ Thống Điểm Danh Thông Minh

## 1. Tổng Quan Hệ Thống

Rollcall-Socket là một hệ thống điểm danh dựa trên sóng siêu âm, thiết kế để phòng chống gian lận và đảm bảo chỉ những sinh viên có mặt trong lớp học mới có thể ghi danh.

### Mục Tiêu Chính

Chi phí gian lận (tiền mất, điểm 0, bị kỷ luật) PHẢI lớn hơn lợi ích mang lại. Hệ thống sẽ thất bại nếu:
- Chi phí gian lận < 0 (không có hậu quả)
- Lợi ích gian lận cao (mở khóa = bằng cấp)

Chiến lược phòng chống: Tạo nhiều lớp bảo vệ độc lập sao cho gian lận đòi hỏi kiến thức, công cụ, và thời gian vượt quá giá trị nhận được.

---

## 2. Kiến Trúc Hệ Thống (3 Lớp)

### Layer 1: Presentation Layer (Client-Side)
```
┌─────────────────────────────────────┐
│  Student.html / Lecturer.html       │
│  (HTML + JavaScript)                │
├─────────────────────────────────────┤
│ - User interface                    │
│ - WebAuthn Passkey API              │
│ - Web Audio API (phát/thu)          │
│ - HTTPS + mTLS validation           │
└─────────────────────────────────────┘
```

**Chống lại:**
- Gian lận từ xa (phải có thiết bị vật lý trong lớp)
- Injection attacks (Content Security Policy, input validation)
- Man-in-the-middle (HTTPS + certificate pinning thông qua domain)
- Credential theft (WebAuthn không gửi password qua mạng)

**Cơ chế bảo vệ:**
- Yêu cầu WebAuthn passkey (sinh viên phải có điện thoại/khóa bảo mật)
- Microphone phải được bật (không thể gian lận nếu không nghe tiếng)
- Âm thanh phải được ghi lại từ lớp học (proof of presence)

---

### Layer 2: API Gateway (Python - server.py)
```
┌─────────────────────────────────────┐
│  Python HTTPS Server (Port 8000)    │
├─────────────────────────────────────┤
│ - SSL/TLS endpoint                  │
│ - Certificate validation            │
│ - Campus network filter (CIDRS)     │
│ - Request routing & proxying        │
│ - WebAuthn server-side operations   │
└─────────────────────────────────────┘
```

**Chống lại:**
- Gian lận ngoài campus (IP filtering)
- Phishing attacks (HTTPS + mTLS)
- Brute force attacks (rate limiting thông qua mạng)
- Unauthorized access (campus network requirement)

**Cơ chế bảo vệ:**
- Chỉ chấp nhận requests từ 127.0.0.0/8, 10.0.0.0/8, 172.16.0.0/12, 192.168.0.0/16
- Yêu cầu origin/rpId khớp với domain WebAuthn
- Proxy tất cả API requests đến backend
- Certificate pinning cho domain sslip.io/nip.io

---

### Layer 3: Backend Service (C++ - backend/)
```
┌─────────────────────────────────────┐
│  C++ Backend Server (Port 9000)     │
├─────────────────────────────────────┤
│ - Attendance database                │
│ - Token generation & validation      │
│ - Session management                 │
│ - Data persistence                   │
└─────────────────────────────────────┘
```

**Chống lại:**
- Gian lận dữ liệu (token validation, signature verification)
- Replay attacks (token TTL: time-to-live 30 giây)
- Unauthorized database modification
- Session hijacking (unique tokens per session)

**Cơ chế bảo vệ:**
- Secret-based token generation (HMAC)
- Token expiration (TTL = 30 giây - đủ để ghi danh, không đủ để sử dụng lại)
- Session ID tied to timestamp
- HTTP-only, no CORS (chỉ Python gateway gọi)

---

## 3. Luồng Điểm Danh Chi Tiết

### Bước 1: Sinh Viên Đến Lớp
```
[Sinh viên] --HTTPS--> [Python Gateway] --HTTP--> [C++ Backend]
     |
     ├─ Kiểm tra Passkey (WebAuthn)
     ├─ Xác thực identity
     └─ Ghi nhận presence
```

1. Sinh viên mở Student.html trên điện thoại
2. Browser yêu cầu WebAuthn challenge từ Python server
3. Sinh viên xác thực bằng Passkey (Face ID / Fingerprint / PIN)
4. Python server gửi challenge đến C++ backend

### Bước 2: Nhận Tín Hiệu Âm Thanh
```
[Lecturer phát] --Ultrasonic Audio--> [Sinh viên] --Decode--> Bits
```

1. Giảng viên nhấn "Tạo Dữ Liệu" → sinh dữ liệu bits ngẫu nhiên
2. Giảng viên nhấn "Bắt Đầu Phát" → máy phát âm thanh cao tần (18kHz & 20kHz)
3. Sinh viên nhấn "Bắt Đầu Nhận" → Web Audio API giải mã thành bits
4. So sánh bits sinh viên nhận được với dữ liệu giảng viên phát

### Bước 3: Gửi Proof of Attendance
```
[Sinh viên] --{passkey, bits, studentId}--> [Python] --> [C++ Backend]
```

1. Sinh viên nhập mã số (MSSV)
2. Gửi: (student_id, received_bits, webauthn_assertion)
3. Python verify WebAuthn signature
4. C++ backend kiểm tra:
   - MSSV có hiệu lực không?
   - Bits khớp với session hiện tại không?
   - Token chưa hết hạn không?
   - Lần đầu ghi danh trong session này không?

---

## 4. Các Lớp Bảo vệ Chống Gian Lận

### Level 1: Physical Presence
**Chống:** Gian lận từ xa (someone else's phone, VPN)

- Âm thanh siêu âm (18-20kHz) chỉ nghe được trong phạm vi 50m
- Web Audio API yêu cầu user permission (không thể tự động ghi)
- Microphone phải bật (visible indicator)

**Công nhân gian lận cần:** Có mặt trong lớp hoặc ghi âm lớp học trước

### Level 2: Credential Authentication
**Chống:** Gian lận bằng sinh viên khác (account takeover)

- WebAuthn Passkey (FIDO2 standard)
- Asymmetric cryptography (private key không bao giờ rời khỏi device)
- Per-user enrollment (mỗi sinh viên đăng ký riêng)
- Face/Fingerprint biometric

**Công nhân gian lận cần:** Điều khiển device của sinh viên hoặc đánh cắp sinh trắc học

### Level 3: Network Boundary
**Chống:** Gian lận từ ngoài campus (thay đổi IP, fake location)

- Campus CIDR filtering (only 127.0.0.0/8, 10.0.0.0/8, etc.)
- HTTPS with certificate validation
- Domain binding (origin = domain, not IP)

**Công nhân gian lận cần:** Truy cập mạng campus (VPN, IP spoofing - rất khó)

### Level 4: Data Validation
**Chống:** Gian lận dữ liệu (replay bits, invalid tokens)

- Token expiration (30 giây TTL)
- Session-specific tokens (mỗi lần phát mới = token mới)
- Bit validation (phải khớp với dữ liệu backend phát)
- Timestamp validation

**Công nhân gian lận cần:** Biết secret key, reverse-engineer token, hoặc record & replay (nhưng token hết hạn)

### Level 5: Enrollment Verification
**Chống:** Gian lận bằng sinh viên fake (enrollment bypass)

- Backend kiểm tra MSSV hợp lệ (student database)
- Một MSSV chỉ đăng ký Passkey một lần
- Passkey credential lưu trữ backend

**Công nhân gian lận cần:** Truy cập CSDL học sinh, hoặc fake MSSV (sẽ bị phát hiện)

---

## 5. Mô Hình Đe Dọa (Threat Model)

| Mối Đe Dọa | Kiểu Gian Lận | Chi Phí | Phòng Chống | Kết Luận |
|-----------|--------------|--------|----------|----------|
| Gian lận từ xa | VPN, IP spoofing | CAO | CIDR filtering | Khó |
| Chia sẻ tài khoản | Share phone | Cao | Passkey + biometric | Khó |
| Replay bits | Ghi âm & replay | CAO | Token TTL + session ID | Khó |
| SQL injection | Xâm nhập backend | Cao | Input validation | Khó |
| Phishing | Giả mạo domain | Cao | HTTPS + pinning | Khó |
| Brute force token | Đoán secret | CAO | HMAC + entropy | Không khả thi |
| Gian lận vắng mặt | Không có tiếng | Tầm thường | Audio capture | Không thể |

---

## 6. Tính Khả Thi Chi Phí / Lợi Ích

### Lợi Ích của Gian Lận
- Vắng 1 buổi: thường không trừ điểm (lớp 30 sinh viên = 1 buổi)
- Hoặc trừ 1-2% điểm = ~0.1 điểm (nếu GPA tính 4.0)

**Lợi ích tiền tệ: ~0 (gân như không)**

### Chi Phí của Gian Lận
- Kỷ luật học vụ (nếu bị phát hiện): 0 điểm môn học
- Ghi chép hành vi: ảnh hưởng học bổng, xin việc
- Thời gian: ~2 giờ học hack, reverse engineer token, setup VPN
- Công cụ: VPN ($5-10/tháng), có thể cần laptop

**Chi phí tiền tệ: ~$5-20 + thời gian**
**Chi phí xã hội: Cao (kỷ luật viết)**

### Kết Luận
```
Lợi ích gian lận: 0 ~ 0.1 điểm
Chi phí gian lận: Cao (kỷ luật) + phức tạp kỹ thuật
Tỷ số: Chi phí >> Lợi ích ✓ (Hệ thống THÀNH CÔNG)
```

Gian lận không có giá trị vì:
1. Lợi ích quá nhỏ (vắng 1 buổi = gần 0 điểm)
2. Chi phí kỷ luật quá lớn (0 điểm môn)
3. Độ khó kỹ thuật cao (cần kiến thức crypto, network)

---

## 7. Các Thành Phần Chi Tiết

### Frontend (Student/Lecturer HTML)
- WebAuthn Registration & Authentication
- Web Audio API (OscillatorNode, AnalyserNode)
- Frequency detection (FFT analysis)
- HTTPS communication

### Gateway (Python server.py)
- HTTPS server (SSL/TLS)
- WebAuthn server-side operations (challenge generation, assertion verification)
- Request proxying to backend
- CIDRS filtering

### Backend (C++ backend/)
- Attendance database (in-memory + JSON persistence)
- Token generation (HMAC-SHA256)
- Session management
- HTTP API endpoints

### Security Libraries
- fido2 (Python): WebAuthn FIDO2 library
- OpenSSL: HTTPS/TLS
- mkcert: Self-signed certificate generation

---

## 8. Cách Tấn Công & Phòng Chống

### Scenario 1: Gian Lận từ Ngoài Campus
```
Attacker (Home): https://192.168.1.42:8000/student.html
                                    |
                         ✗ FAIL: IP not in CIDRS
                         ✗ FAIL: Certificate domain mismatch
                         ✗ FAIL: WebAuthn origin mismatch
```
Phòng chống: Layer 2 + Layer 3

### Scenario 2: Chia Sẻ Tài Khoản
```
Student A: Passkey enrolled on iPhone
Attacker: Uses Student A's phone
          ✓ Bypass Layer 1, 2, 3
          BUT: Layer 5 catches mismatch
          
Problem: Backend record shows iPhone + Passkey X
         But Attacker 2 also claiming Student A at same time
         --> Suspicious patterns detected
```
Phòng chống: Session-based duplicate detection

### Scenario 3: Replay Attack
```
Attacker: Captures bits during class
          Replays bits after class (5 minutes later)
          
          ✗ FAIL: Token expired (TTL = 30s)
          ✗ FAIL: Session ID changed (new session = new token)
          ✓ Same bits but invalid token = rejected
```
Phòng chống: Layer 4

### Scenario 4: Token Forging
```
Attacker: Tries to forge HMAC token
          Secret key unknown
          Token format: HMAC-SHA256(student_id + bits + session_id, secret)
          
          ✗ FAIL: 2^256 entropy = 10^77 possibilities
          ✓ Brute force not feasible in 30 seconds
```
Phòng chống: Layer 4 (cryptography)

---

## 9. Khuyến Nghị Bảo Mật Bổ Sung

### Hiện Tại (MVP)
- In-memory attendance storage
- 30-second token TTL
- HMAC-SHA256 signing

### Tương Lai
- Attendance logging (write-ahead logs)
- Rate limiting per MSSV (max 1 attendance per session)
- IP logging (detect VPN usage)
- Biometric verification (Web Authentication API Level 2)
- Encrypt tokens (AES-GCM instead of HMAC)
- Implement certificate pinning

---

## 10. Kết Luận

Rollcall-Socket bảo vệ điểm danh bằng **5 lớp độc lập**:

1. **Physical** - Âm thanh siêu âm (phải ở trong lớp)
2. **Cryptographic** - WebAuthn (phải có device)
3. **Network** - CIDR filtering (phải ở campus)
4. **Data** - Token validation (token hết hạn nhanh)
5. **Enrollment** - MSSV verification (database validation)

**Kết quả:** Chi phí gian lận >> Lợi ích gian lận

Hệ thống **KHÔNG KHÔNG THỂ** gian lận tuyệt đối, nhưng tăng chi phí lên mức không có giá trị, khiến gian lận không hợp lý từ góc độ kinh tế và kỹ thuật.
