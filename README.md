## Chạy nhanh server (C++)

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

