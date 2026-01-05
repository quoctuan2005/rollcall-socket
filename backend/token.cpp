#include "token.h"

#include <chrono>
#include <cstdlib>

static std::int64_t now_ms()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

// NOTE: Lightweight keyed-hash for a student project/demo.
// For production-grade security, replace with HMAC-SHA256.
static std::uint64_t fnv1a64(const std::string &s)
{
    std::uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s)
    {
        h ^= static_cast<std::uint64_t>(c);
        h *= 1099511628211ull;
    }
    return h;
}

static std::string u64_to_bits(std::uint64_t x)
{
    std::string bits;
    bits.reserve(64);
    for (int i = 63; i >= 0; i--)
        bits.push_back(((x >> i) & 1ull) ? '1' : '0');
    return bits;
}

static std::string make_token_bits(const std::string &secret, const std::string &session_id, std::int64_t counter, int nbits)
{
    std::string out;
    out.reserve(static_cast<size_t>(nbits));
    int block = 0;
    while (static_cast<int>(out.size()) < nbits)
    {
        const std::string msg = secret + "|" + session_id + "|" + std::to_string(counter) + "|" + std::to_string(block);
        out += u64_to_bits(fnv1a64(msg));
        block++;
        if (block > 16)
            break;
    }
    out.resize(static_cast<size_t>(nbits));
    return out;
}

std::string read_secret_from_env()
{
    const char *env = std::getenv("ROLLCALL_SECRET");
    if (env && *env)
        return std::string(env);
    return std::string("dev-secret-change-me");
}

TokenService::TokenService(std::string secret, int ttl_ms)
    : secret_(std::move(secret))
{
    state_.ttl_ms = ttl_ms;
}

int TokenService::ttl_ms() const
{
    return state_.ttl_ms;
}

const std::string &TokenService::session_id()
{
    ensure_session();
    return state_.session_id;
}

void TokenService::ensure_session()
{
    if (!state_.session_id.empty())
        return;
    state_.session_id = "S" + std::to_string(now_ms());
}

void TokenService::start_session()
{
    state_.session_id = "S" + std::to_string(now_ms());
    used_counter_by_student_.clear();
}

std::int64_t TokenService::current_counter(std::int64_t now) const
{
    return now / state_.ttl_ms;
}

std::int64_t TokenService::counter_expires_at_ms(std::int64_t counter) const
{
    return (counter + 1) * state_.ttl_ms;
}

std::string TokenService::make_bits(const std::string &session_id, std::int64_t counter, int nbits) const
{
    return make_token_bits(secret_, session_id, counter, nbits);
}

TokenResponse TokenService::get_token(int nbits)
{
    ensure_session();
    const std::int64_t now = now_ms();
    const std::int64_t counter = current_counter(now);

    TokenResponse resp;
    resp.session_id = state_.session_id;
    resp.bits = make_bits(state_.session_id, counter, nbits);
    resp.expires_at_ms = counter_expires_at_ms(counter);
    resp.now_ms = now;
    resp.ttl_ms = state_.ttl_ms;
    resp.counter = counter;
    return resp;
}

SubmitResult TokenService::submit_attendance(const std::string &student_id, const std::string &bits)
{
    ensure_session();
    const std::int64_t now = now_ms();
    const std::int64_t counter = current_counter(now);

    const std::string expected = make_bits(state_.session_id, counter, static_cast<int>(bits.size()));
    if (bits != expected)
        return SubmitResult{false, "invalid_token"};

    if (auto it = used_counter_by_student_.find(student_id); it != used_counter_by_student_.end())
    {
        if (it->second == counter)
            return SubmitResult{false, "already_used"};
    }

    used_counter_by_student_[student_id] = counter;
    return SubmitResult{true, ""};
}
