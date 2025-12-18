#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <cstring>
#include <sstream>

// Headers cho Linux/macOS Socket
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

// ==========================================
// 1. MODULE MÃ HÓA (SHA1 & BASE64)
// Dùng để bắt tay (Handshake) với trình duyệt
// ==========================================
namespace crypto
{

    // --- Base64 Encoding ---
    static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    inline std::string base64_encode(const unsigned char *data, size_t len)
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

    // --- SHA1 Hashing (Minimal Implementation) ---
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
        unsigned H[5];
        unsigned length_low, length_high;
        unsigned char message_block[64];
        int message_block_index;
        bool computed, corrupted;

        static unsigned circular_shift(int bits, unsigned word)
        {
            return (word << bits) | (word >> (32 - bits));
        }
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

    // Hàm tiện ích: String -> SHA1 -> Base64
    inline std::string sha1_base64(const std::string &s)
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
// 2. MODULE WEBSOCKET UTILS
// Xử lý gửi, nhận frame và handshake
// ==========================================

struct WSFrame
{
    bool fin = true;
    int opcode = 1; // 1 = text, 8 = close
    bool masked = false;
    uint64_t payload_len = 0;
    unsigned char masking_key[4] = {0};
    std::string payload;
};

// --- Socket Helpers ---
inline bool recv_all(int sock, void *buf, size_t len)
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

inline bool send_all(int sock, const std::string &data)
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

// --- Frame Reading (Client -> Server) ---
inline bool read_ws_frame(int sock, WSFrame &frame)
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

    // Unmask logic
    if (frame.masked)
    {
        for (size_t i = 0; i < frame.payload.size(); ++i)
        {
            frame.payload[i] = static_cast<char>(frame.payload[i] ^ frame.masking_key[i % 4]);
        }
    }
    return true;
}

// --- Frame Sending (Server -> Client) ---
inline bool send_text_frame(int sock, const std::string &text)
{
    std::string out;
    // FIN=1 (0x80) | Text Opcode=1 (0x01) -> 0x81
    out.push_back(static_cast<char>(0x81));

    size_t len = text.size();
    if (len <= 125)
    {
        out.push_back(static_cast<char>(len));
    }
    else if (len <= 65535)
    {
        out.push_back(static_cast<char>(126));
        unsigned char ext[2] = {
            static_cast<unsigned char>((len >> 8) & 0xFF),
            static_cast<unsigned char>(len & 0xFF)};
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
    // Server does not mask frames usually
    out.append(text);
    return send_all(sock, out);
}

// ==========================================
// 3. HTTP HANDSHAKE HELPERS
// ==========================================

inline bool read_http_request(int sock, std::string &request)
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
            return false; // Max header size guard
    }
    return true;
}

inline std::string get_header_value(const std::string &req, const std::string &key)
{
    std::string needle = key + ":";
    size_t pos = req.find(needle);
    if (pos == std::string::npos)
        return "";
    pos += needle.size();
    while (pos < req.size() && req[pos] == ' ')
        pos++;
    size_t end = req.find("\r\n", pos);
    if (end == std::string::npos)
        end = req.size();
    return req.substr(pos, end - pos);
}

inline bool websocket_handshake(int client_sock, const std::string &request)
{
    // 1. Lấy Sec-WebSocket-Key từ header
    std::string sec_key = get_header_value(request, "Sec-WebSocket-Key");
    if (sec_key.empty())
        return false;

    // 2. Magic GUID của WebSocket Protocol
    const std::string guid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

    // 3. Tính Accept Key: Base64(SHA1(Key + GUID))
    std::string accept = crypto::sha1_base64(sec_key + guid);

    // 4. Gửi phản hồi HTTP 101
    std::string resp =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: " +
        accept + "\r\n\r\n";

    return send_all(client_sock, resp);
}

// ==========================================
// 4. JSON HELPER (MINIMAL)
// ==========================================
inline std::string get_json_str(const std::string &json, const std::string &key)
{
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos)
        return "";

    // Tìm dấu hai chấm
    pos = json.find(':', pos);
    if (pos == std::string::npos)
        return "";

    // Tìm dấu ngoặc kép mở đầu giá trị
    size_t start_quote = json.find('"', pos);
    if (start_quote == std::string::npos)
        return "";

    // Tìm dấu ngoặc kép đóng
    size_t end_quote = json.find('"', start_quote + 1);
    if (end_quote == std::string::npos)
        return "";

    return json.substr(start_quote + 1, end_quote - start_quote - 1);
}