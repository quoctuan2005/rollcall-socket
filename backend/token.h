#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

struct TokenState
{
    std::string session_id;
    int ttl_ms = 8000;
};

struct TokenResponse
{
    std::string session_id;
    std::string bits;
    std::int64_t expires_at_ms = 0;
    std::int64_t now_ms = 0;
    int ttl_ms = 0;
    std::int64_t counter = 0;
};

struct SubmitResult
{
    bool ok = false;
    std::string error; // empty when ok=true
};

class TokenService
{
public:
    TokenService(std::string secret, int ttl_ms);

    int ttl_ms() const;
    const std::string &session_id();

    void start_session();
    TokenResponse get_token(int nbits);
    SubmitResult submit_attendance(const std::string &student_id, const std::string &bits);

private:
    void ensure_session();
    std::int64_t current_counter(std::int64_t now) const;
    std::int64_t counter_expires_at_ms(std::int64_t counter) const;
    std::string make_bits(const std::string &session_id, std::int64_t counter, int nbits) const;

    std::string secret_;
    TokenState state_;
    std::unordered_map<std::string, std::int64_t> used_counter_by_student_;
};

std::string read_secret_from_env();
