#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

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
    std::string error;              // empty when ok=true
    std::string fingerprint_status; // enrolled | matched | mismatch | missing
    int fingerprint_score = -1;     // 0..100, -1 if N/A
};

struct Fingerprint
{
    std::string platform;
    std::string timezone;
    std::string language;
    int screen_w = 0;
    int screen_h = 0;
    double dpr = 0.0;
    int hardware_concurrency = 0;
    int device_memory = 0;
    bool touch = false;
};

struct AttendanceEntry
{
    std::string student_id;
    std::int64_t at_ms = 0;
    std::int64_t counter = 0;
    std::string fingerprint_status;
    int fingerprint_score = -1;
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
    SubmitResult submit_attendance(const std::string &student_id, const std::string &bits, const Fingerprint &fp);

    std::vector<AttendanceEntry> attendance_list() const;
    std::int64_t current_counter_now() const;

private:
    void ensure_session();
    std::int64_t current_counter(std::int64_t now) const;
    std::int64_t counter_expires_at_ms(std::int64_t counter) const;
    std::string make_bits(const std::string &session_id, std::int64_t counter, int nbits) const;

    std::string secret_;
    TokenState state_;
    std::unordered_map<std::string, std::int64_t> used_counter_by_student_;
    std::unordered_map<std::string, AttendanceEntry> checked_in_by_student_;
    std::unordered_map<std::string, Fingerprint> fingerprint_baseline_by_student_;
    std::vector<AttendanceEntry> attendance_log_;
};

std::string read_secret_from_env();
