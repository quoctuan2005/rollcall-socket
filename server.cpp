#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <thread>
#include <mutex>
#include <algorithm>
#include <cstring>
#include <cerrno>
#include <csignal>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

// ... (Giữ nguyên phần namespace mini và các class SHA1 như cũ) ...
// ĐỂ TIẾT KIỆM KHÔNG GIAN, TÔI CHỈ VIẾT LẠI PHẦN MAIN VÀ CÁC THAY ĐỔI CẦN THIẾT
// CÁC PHẦN LOGIC MD5/SHA1/BASE64 GIỮ NGUYÊN NHƯ FILE TRƯỚC

// --- (Copy lại phần namespace mini từ code cũ của bạn vào đây) ---
namespace mini
{
    static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string base64_encode(const unsigned char *data, size_t len)
    {
        std::string out;
        out.reserve(((len + 2) / 3) * 4);
        for (size_t i = 0; i < len; i += 3)
        {
            unsigned int v = data[i] << 16;
            if (i + 1 < len)
                v |= data[i + 1] << 8;
            if (i + 2 < len)
                v |= data[i + 2];
            out.push_back(b64_table[(v >> 18) & 0x3F]);
            out.push_back(b64_table[(v >> 12) & 0x3F]);
            out.push_back((i + 1 < len) ? b64_table[(v >> 6) & 0x3F] : '=');
            out.push_back((i + 2 < len) ? b64_table[v & 0x3F] : '=');
        }
        return out;
    }
    class SHA1
    {
    public:
        SHA1() { reset(); }
        void reset()
        {
            length_low = 0;
            length_high = 0;
            message_block_index = 0;
            computed = false;
            corrupted = false;
            H[0] = 0x67452301;
            H[1] = 0xEFCDAB89;
            H[2] = 0x98BADCFE;
            H[3] = 0x10325476;
            H[4] = 0xC3D2E1F0;
        }
        void input(const unsigned char *message_array, unsigned length)
        {
            if (!length)
                return;
            if (computed || corrupted)
            {
                corrupted = true;
                return;
            }
            for (unsigned i = 0; i < length && !corrupted; ++i)
            {
                message_block[message_block_index++] = message_array[i];
                length_low += 8;
                if (length_low == 0)
                {
                    length_high++;
                    if (length_high == 0)
                        corrupted = true;
                }
                if (message_block_index == 64)
                    process_message_block();
            }
        }
        void result(unsigned *digest_array)
        {
            if (corrupted)
                return;
            if (!computed)
            {
                pad_message();
                computed = true;
            }
            for (int i = 0; i < 5; ++i)
                digest_array[i] = H[i];
        }

    private:
        unsigned H[5], length_low, length_high;
        unsigned char message_block[64];
        int message_block_index;
        bool computed, corrupted;
        static unsigned circular_shift(int bits, unsigned word) { return (word << bits) | (word >> (32 - bits)); }
        void process_message_block()
        {
            const unsigned K[] = {0x5A827999, 0x6ED9EBA1, 0x8F1BBCDC, 0xCA62C1D6};
            unsigned W[80];
            for (int t = 0; t < 16; ++t)
            {
                W[t] = ((unsigned)message_block[t * 4]) << 24;
                W[t] |= ((unsigned)message_block[t * 4 + 1]) << 16;
                W[t] |= ((unsigned)message_block[t * 4 + 2]) << 8;
                W[t] |= ((unsigned)message_block[t * 4 + 3]);
            }
            for (int t = 16; t < 80; ++t)
                W[t] = circular_shift(1, W[t - 3] ^ W[t - 8] ^ W[t - 14] ^ W[t - 16]);
            unsigned A = H[0], B = H[1], C = H[2], D = H[3], E = H[4];
            for (int t = 0; t < 80; ++t)
            {
                unsigned temp = circular_shift(5, A) + E + W[t] + K[t / 20];
                if (t < 20)
                    temp += ((B & C) | ((~B) & D));
                else if (t < 40)
                    temp += (B ^ C ^ D);
                else if (t < 60)
                    temp += ((B & C) | (B & D) | (C & D));
                else
                    temp += (B ^ C ^ D);
                E = D;
                D = C;
                C = circular_shift(30, B);
                B = A;
                A = temp;
            }
            H[0] += A;
            H[1] += B;
            H[2] += C;
            H[3] += D;
            H[4] += E;
            message_block_index = 0;
        }
        void pad_message()
        {
            message_block[message_block_index++] = 0x80;
            if (message_block_index > 56)
            {
                while (message_block_index < 64)
                    message_block[message_block_index++] = 0;
                process_message_block();
            }
            while (message_block_index < 56)
                message_block[message_block_index++] = 0;
            message_block[56] = (unsigned char)(length_high >> 24);
            message_block[57] = (unsigned char)(length_high >> 16);
            message_block[58] = (unsigned char)(length_high >> 8);
            message_block[59] = (unsigned char)(length_high);
            message_block[60] = (unsigned char)(length_low >> 24);
            message_block[61] = (unsigned char)(length_low >> 16);
            message_block[62] = (unsigned char)(length_low >> 8);
            message_block[63] = (unsigned char)(length_low);
            process_message_block();
        }
    };
    std::string sha1_base64(const std::string &s)
    {
        SHA1 sha;
        sha.input(reinterpret_cast<const unsigned char *>(s.data()), static_cast<unsigned>(s.size()));
        unsigned digest[5] = {0};
        sha.result(digest);
        unsigned char bytes[20];
        for (int i = 0; i < 5; ++i)
        {
            bytes[i * 4 + 0] = (digest[i] >> 24) & 0xFF;
            bytes[i * 4 + 1] = (digest[i] >> 16) & 0xFF;
            bytes[i * 4 + 2] = (digest[i] >> 8) & 0xFF;
            bytes[i * 4 + 3] = (digest[i]) & 0xFF;
        }
        return base64_encode(bytes, 20);
    }
}

// ==========================================
// LOGIC CHỐNG GIAN LẬN
// ==========================================
std::mutex db_mutex;
std::map<std::string, std::string> trusted_devices;
std::map<std::string, std::string> active_ips;

bool starts_with(const std::string &s, const std::string &prefix)
{
    return s.size() >= prefix.size() && std::equal(prefix.begin(), prefix.end(), s.begin());
}

// ... (Giữ nguyên các hàm extract_json_string, WSFrame, recv_all, read_http_request...) ...
bool extract_json_string(const std::string &json, const std::string &key, std::string &out)
{
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos)
        return false;
    pos = json.find(':', pos);
    if (pos == std::string::npos)
        return false;
    while (pos < json.size() && (json[pos] == ':' || json[pos] == ' '))
        pos++;
    if (pos >= json.size() || json[pos] != '"')
        return false;
    pos++;
    size_t end = pos;
    while (end < json.size() && json[end] != '"')
        end++;
    if (end >= json.size())
        return false;
    out = json.substr(pos, end - pos);
    return true;
}
struct WSFrame
{
    bool fin = true;
    unsigned char opcode = 0x1;
    bool masked = false;
    uint64_t payload_len = 0;
    unsigned char masking_key[4] = {0, 0, 0, 0};
    std::string payload;
};
bool recv_all(int sock, void *buf, size_t len)
{
    char *p = static_cast<char *>(buf);
    size_t received = 0;
    while (received < len)
    {
        ssize_t r = ::recv(sock, p + received, len - received, 0);
        if (r <= 0)
            return false;
        received += static_cast<size_t>(r);
    }
    return true;
}
bool send_all(int sock, const std::string &data)
{
    size_t sent = 0;
    while (sent < data.size())
    {
        ssize_t s = ::send(sock, data.data() + sent, data.size() - sent, 0);
        if (s <= 0)
            return false;
        sent += static_cast<size_t>(s);
    }
    return true;
}
bool send_text_frame(int sock, const std::string &text)
{
    std::string out;
    unsigned char hdr1 = 0x80 | 0x1;
    out.push_back(static_cast<char>(hdr1));
    size_t len = text.size();
    if (len <= 125)
    {
        out.push_back(static_cast<char>(len));
    }
    else if (len <= 65535)
    {
        out.push_back(static_cast<char>(126));
        unsigned char ext[2] = {static_cast<unsigned char>((len >> 8) & 0xFF), static_cast<unsigned char>(len & 0xFF)};
        out.append(reinterpret_cast<char *>(ext), 2);
    }
    else
    {
        out.push_back(static_cast<char>(127));
        unsigned char ext[8];
        uint64_t l64 = static_cast<uint64_t>(len);
        for (int i = 7; i >= 0; --i)
        {
            ext[i] = static_cast<unsigned char>(l64 & 0xFF);
            l64 >>= 8;
        }
        out.append(reinterpret_cast<char *>(ext), 8);
    }
    return send_all(sock, out);
}
bool read_http_request(int sock, std::string &request)
{
    request.clear();
    char buf[1024];
    while (true)
    {
        ssize_t r = ::recv(sock, buf, sizeof(buf), 0);
        if (r <= 0)
            return false;
        request.append(buf, buf + r);
        if (request.find("\r\n\r\n") != std::string::npos)
            break;
        if (request.size() > 32 * 1024)
            return false;
    }
    return true;
}
bool parse_header_value(const std::string &req, const std::string &header, std::string &value)
{
    std::string key = header + ":";
    size_t pos = req.find(key);
    if (pos == std::string::npos)
        return false;
    pos += key.size();
    while (pos < req.size() && (req[pos] == ' '))
        pos++;
    size_t end = req.find("\r\n", pos);
    if (end == std::string::npos)
        end = req.size();
    value = req.substr(pos, end - pos);
    return true;
}
bool websocket_handshake(int client_sock, const std::string &request)
{
    size_t line_end = request.find("\r\n");
    if (line_end == std::string::npos)
        return false;
    std::string sec_key;
    if (!parse_header_value(request, "Sec-WebSocket-Key", sec_key))
        return false;
    const std::string guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::string accept_b64 = mini::sha1_base64(sec_key + guid);
    std::string resp = "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: " + accept_b64 + "\r\n\r\n";
    return send_all(client_sock, resp);
}
bool read_ws_frame(int sock, WSFrame &frame)
{
    unsigned char hdr[2];
    if (!recv_all(sock, hdr, 2))
        return false;
    frame.fin = (hdr[0] & 0x80) != 0;
    frame.opcode = hdr[0] & 0x0F;
    frame.masked = (hdr[1] & 0x80) != 0;
    uint64_t len = hdr[1] & 0x7F;
    if (len == 126)
    {
        unsigned char ext[2];
        if (!recv_all(sock, ext, 2))
            return false;
        len = (static_cast<uint64_t>(ext[0]) << 8) | static_cast<uint64_t>(ext[1]);
    }
    else if (len == 127)
    {
        unsigned char ext[8];
        if (!recv_all(sock, ext, 8))
            return false;
        len = 0;
        for (int i = 0; i < 8; ++i)
            len = (len << 8) | ext[i];
    }
    frame.payload_len = len;
    if (frame.masked)
    {
        if (!recv_all(sock, frame.masking_key, 4))
            return false;
    }
    frame.payload.clear();
    frame.payload.resize(static_cast<size_t>(frame.payload_len));
    if (!recv_all(sock, frame.payload.data(), static_cast<size_t>(frame.payload_len)))
        return false;
    if (frame.masked)
    {
        for (size_t i = 0; i < frame.payload.size(); ++i)
            frame.payload[i] = static_cast<char>(frame.payload[i] ^ frame.masking_key[i % 4]);
    }
    return true;
}

// ... (Giữ nguyên logic is_ip_allowed và handle_client) ...
bool is_ip_allowed(const std::string &ip)
{
    if (starts_with(ip, "127.0.0.1"))
        return true;
    if (starts_with(ip, "192.168."))
        return true;
    return false;
}

void handle_client(int client_sock, std::string client_ip)
{
    if (!is_ip_allowed(client_ip))
    {
        std::cout << "[BLOCKED] IP outside network: " << client_ip << std::endl;
        ::close(client_sock);
        return;
    }
    std::string req;
    if (!read_http_request(client_sock, req))
    {
        ::close(client_sock);
        return;
    }
    if (!websocket_handshake(client_sock, req))
    {
        ::close(client_sock);
        return;
    }

    while (true)
    {
        WSFrame f;
        if (!read_ws_frame(client_sock, f))
            break;
        if (!f.fin || f.opcode == 0x8)
            break;
        if (f.opcode != 0x1)
            continue;

        std::string visitorId, mssv;
        bool ok_vis = extract_json_string(f.payload, "visitorId", visitorId);
        bool ok_id = extract_json_string(f.payload, "mssv", mssv);
        std::string resp_json;

        if (ok_vis && ok_id)
        {
            std::lock_guard<std::mutex> lock(db_mutex);
            bool ip_cheat = false;
            if (active_ips.count(client_ip))
            {
                if (active_ips[client_ip] != mssv)
                    ip_cheat = true;
            }

            if (ip_cheat)
            {
                resp_json = "{\"status\":\"BLOCKED\",\"msg\":\"Gian lận IP!\"}";
            }
            else
            {
                if (trusted_devices.find(mssv) == trusted_devices.end())
                {
                    trusted_devices[mssv] = visitorId;
                    active_ips[client_ip] = mssv;
                    std::cout << "[NEW USER] MSSV " << mssv << " registered with " << visitorId << std::endl;
                    resp_json = "{\"status\":\"OK\",\"msg\":\"Đăng ký thành công.\"}";
                }
                else
                {
                    if (trusted_devices[mssv] == visitorId)
                    {
                        resp_json = "{\"status\":\"OK\",\"msg\":\"Xác thực thành công.\"}";
                    }
                    else
                    {
                        std::cout << "[ALERT] Strange device for " << mssv << std::endl;
                        resp_json = "{\"status\":\"BLOCKED\",\"msg\":\"Thiết bị lạ!\"}";
                    }
                }
            }
        }
        else
        {
            resp_json = "{\"status\":\"BLOCKED\",\"msg\":\"Invalid JSON\"}";
        }

        if (!send_text_frame(client_sock, resp_json))
            break;
    }
    ::close(client_sock);
}

int main()
{
    // --- KHẮC PHỤC LỖI CRASH TRÊN MACOS/LINUX ---
    // Khi gửi dữ liệu vào socket đã đóng, macOS sẽ gửi signal SIGPIPE làm crash app.
    // Lệnh này sẽ bỏ qua signal đó.
    std::signal(SIGPIPE, SIG_IGN);

    int server_sock = ::socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0)
    {
        std::cerr << "socket error\n";
        return 1;
    }
    int opt = 1;
    ::setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(8080);

    if (::bind(server_sock, (sockaddr *)&addr, sizeof(addr)) < 0)
    {
        std::cerr << "bind error\n";
        return 1;
    }
    if (::listen(server_sock, 16) < 0)
    {
        std::cerr << "listen error\n";
        return 1;
    }

    std::cout << "=== SERVER STARTED PORT 8080 (MAC/LINUX SAFE) ===" << std::endl;

    while (true)
    {
        sockaddr_in client_addr{};
        socklen_t clen = sizeof(client_addr);
        int client_sock = ::accept(server_sock, (sockaddr *)&client_addr, &clen);
        if (client_sock < 0)
            continue;

        char ipbuf[INET_ADDRSTRLEN];
        const char *ipstr = ::inet_ntop(AF_INET, &client_addr.sin_addr, ipbuf, sizeof(ipbuf));
        std::string client_ip = ipstr ? std::string(ipstr) : "unknown";

        std::thread th(handle_client, client_sock, client_ip);
        th.detach();
    }
    ::close(server_sock);
    return 0;
}