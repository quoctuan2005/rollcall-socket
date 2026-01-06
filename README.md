# Backend (C/C++)

Thư mục này dành cho phần **backend C/C++** theo yêu cầu bài lập trình mạng.

## Chạy nhanh server mẫu (C++)

- Build:

Từ thư mục project root:

`g++ -std=c++17 -O2 -Wall -Wextra backend/server.cpp backend/http.cpp backend/json.cpp backend/token.cpp backend/app.cpp -o backend/server`

Hoặc từ trong thư mục `backend/`:

`g++ -std=c++17 -O2 -Wall -Wextra server.cpp http.cpp json.cpp token.cpp app.cpp -o server`

- Run:

`./server`

Mặc định backend chạy HTTP ở `http://127.0.0.1:9000`.

Có thể tăng TTL (giảm lỗi `invalid_token` do hết hạn trong lúc phát/nhận âm thanh) bằng env:

`ROLLCALL_TTL_MS=30000 ./server 9000`

Layer 2 (fingerprint) mặc định bật chặn "1 thiết bị -> nhiều MSSV":

- Nếu một fingerprint đã enroll cho một MSSV, fingerprint tương tự sẽ **không được enroll** cho MSSV khác (trả lỗi `fingerprint_conflict`).
- Nếu bạn muốn tắt (ví dụ demo dùng thiết bị dùng chung): `ROLLCALL_FP_UNIQUE=0 ./server 9000`

Frontend sẽ gọi qua HTTPS server Python bằng đường dẫn `/api/...` (Python sẽ reverse-proxy sang C++), để tránh CORS và để iPhone luôn ở HTTPS.

## API (tạm thời)

- `GET /api/health` -> `{ "ok": true }`
- `POST /api/session/start` -> tạo session mới, trả `{ session_id, ttl_ms }`
- `GET /api/token?bits=N` -> trả token bits hiện tại (có TTL), ví dụ `{ session_id, bits, expires_at_ms, now_ms, ttl_ms }`
- `POST /api/attendance/submit` -> submit điểm danh (single-use theo time-slice), body JSON: `{ "student_id": "...", "bits": "0101..." }`

Mục tiêu: Sau khi chốt format gói tin điểm danh (session/token/deviceId/...), mình sẽ mở rộng các endpoint này để có `submit attendance`, `single-use`, fingerprint, v.v.

## FRONTEND
- Mở 1 terminal khác
- `python3 server.py`

Lưu ý: Lecturer sẽ **không** phát “random bits” nếu backend không chạy (để tránh sinh viên luôn bị `invalid_token`).

## Android / Passkeys (WebAuthn) và HTTPS certificate

Nếu trên Android bạn gặp lỗi:

`NotAllowedError: WebAuthn is not supported on sites with TLS certificate errors`

thì nghĩa là Chrome đang thấy **cảnh báo certificate** (self-signed, untrusted, hoặc sai host). WebAuthn/Passkeys sẽ bị chặn hoàn toàn trong trường hợp này.

Cách làm đúng để demo trên LAN:

- Tạo certificate dev “được tin cậy” bằng `mkcert`:
	- Cài `mkcert`: `brew install mkcert`
	- Cài root CA vào macOS: `mkcert -install`
	- Chạy script trong project root: `./scripts/mkcert_dev.sh`
		- Script sẽ tạo `cert.pem` và `key.pem` đúng tên mà `server.py` đang dùng.
- Cài root CA lên Android:
	- Trên Mac: lấy đường dẫn CA root bằng `mkcert -CAROOT`, rồi copy file `rootCA.pem` sang điện thoại.
	- Trên Android: Settings → Security → Encryption & credentials → Install a certificate → CA certificate.
- Mở đúng host khớp với certificate:
	- Trên Android, WebAuthn/Passkeys thường **không cho dùng IP trực tiếp** làm domain (sẽ báo `SecurityError: this is an invalid domain`).
	- Hãy dùng domain trỏ về IP LAN, ví dụ:
		- `https://192.168.1.12.sslip.io:8000/student.html`
		- hoặc `https://192-168-1-12.nip.io:8000/student.html`
	- Script `./scripts/mkcert_dev.sh` đã tạo cert bao gồm các hostname kiểu `*.sslip.io` và `*.nip.io` tương ứng với IP LAN hiện tại.
	- Không trộn `localhost` và LAN domain/IP giữa lúc đăng ký/xác thực.

Ghi chú quan trọng (Android): một số máy/phiên bản Chrome/Google Play Services có thể **không chấp nhận CA do người dùng tự cài** cho WebAuthn, dù Chrome vẫn hiển thị HTTPS “hợp lệ”. Nếu bạn vẫn gặp `NotAllowedError: WebAuthn is not supported on sites with TLS certificate errors` sau khi đã cài `rootCA.pem`:

- Cách chắc chắn nhất để demo Passkey trên Android là dùng **domain + certificate được tin cậy công khai** (ví dụ dùng tunnel như `ngrok`/`cloudflared` để có HTTPS thật).