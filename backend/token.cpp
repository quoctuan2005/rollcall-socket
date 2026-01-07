#include "token.h"

#include <chrono>
#include <cstdlib>
#include <cmath>
#include <string>

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
    checked_in_by_student_.clear();
    attendance_log_.clear();
}

static int fingerprint_score_percent(const Fingerprint &base, const Fingerprint &cur)
{
    // Weighted fuzzy match, total weight = 10
    int score = 0;
    const int total = 10;

    // platform (2)
    if (!base.platform.empty() && base.platform == cur.platform)
        score += 2;
    // timezone (2)
    if (!base.timezone.empty() && base.timezone == cur.timezone)
        score += 2;
    // language (1)
    if (!base.language.empty() && base.language == cur.language)
        score += 1;
    // screen (2) - allow small diffs due to browser UI/orientation quirks
    if (base.screen_w > 0 && base.screen_h > 0 && cur.screen_w > 0 && cur.screen_h > 0)
    {
        const int dw = std::abs(base.screen_w - cur.screen_w);
        const int dh = std::abs(base.screen_h - cur.screen_h);
        if (dw <= 120 && dh <= 120)
            score += 2;
    }
    // devicePixelRatio (1)
    if (base.dpr > 0.0 && cur.dpr > 0.0)
    {
        if (std::abs(base.dpr - cur.dpr) <= 0.25)
            score += 1;
    }
    // hardwareConcurrency (1)
    if (base.hardware_concurrency > 0 && cur.hardware_concurrency > 0)
    {
        if (std::abs(base.hardware_concurrency - cur.hardware_concurrency) <= 2)
            score += 1;
    }
    // deviceMemory (1) - may be missing on iOS
    if (base.device_memory > 0 && cur.device_memory > 0)
    {
        if (base.device_memory == cur.device_memory)
            score += 1;
    }
    // touch (1)
    if (base.touch == cur.touch)
        score += 1;

    const int percent = static_cast<int>(std::lround((100.0 * score) / total));
    return std::max(0, std::min(100, percent));
}

static bool fingerprint_unique_enroll_enabled()
{
    // Default ON: prevent one device/browser fingerprint from enrolling multiple student IDs.
    // Can be disabled for demos with shared devices: ROLLCALL_FP_UNIQUE=0
    static int cached = -1;
    if (cached != -1)
        return cached == 1;
    const char *env = std::getenv("ROLLCALL_FP_UNIQUE");
    if (!env || !*env)
    {
        cached = 1;
        return true;
    }
    std::string v(env);
    for (auto &ch : v)
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    if (v == "0" || v == "false" || v == "no" || v == "off")
        cached = 0;
    else
        cached = 1;
    return cached == 1;
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
    // Layer 1 only (kept for compatibility)
    ensure_session();
    const std::int64_t now = now_ms();
    const std::int64_t counter = current_counter(now);

    // One attendance per student per session (practical rollcall constraint)
    if (checked_in_by_student_.find(student_id) != checked_in_by_student_.end())
        return SubmitResult{false, "already_checked_in", "", -1, 0, 0};

    // Grace window: accept current counter or previous counter to reduce expiry edge failures.
    std::optional<std::int64_t> matched_counter;
    {
        const std::string expected_now = make_bits(state_.session_id, counter, static_cast<int>(bits.size()));
        if (bits == expected_now)
        {
            matched_counter = counter;
        }
        else if (counter > 0)
        {
            const std::string expected_prev = make_bits(state_.session_id, counter - 1, static_cast<int>(bits.size()));
            if (bits == expected_prev)
                matched_counter = counter - 1;
        }
    }

    if (!matched_counter)
        return SubmitResult{false, "invalid_token", "", -1, 0, 0};

    const std::int64_t used_counter = *matched_counter;

    if (auto it = used_counter_by_student_.find(student_id); it != used_counter_by_student_.end())
    {
        if (it->second == used_counter)
            return SubmitResult{false, "already_used", "", -1, 0, 0};
    }

    used_counter_by_student_[student_id] = used_counter;

    AttendanceEntry entry;
    entry.student_id = student_id;
    entry.at_ms = now;
    entry.counter = used_counter;
    checked_in_by_student_[student_id] = entry;
    attendance_log_.push_back(entry);
    return SubmitResult{true, "", "", -1, entry.at_ms, entry.counter};
}

SubmitResult TokenService::submit_attendance(const std::string &student_id, const std::string &bits, const Fingerprint &fp)
{
    // Layer 1 checks (token validity + single use) + Layer 2 fingerprint gating.
    // Important: do NOT call the Layer1-only submit_attendance() here, because it mutates
    // used_counter_by_student_ and would cause the token to be considered already-used.
    ensure_session();
    const std::int64_t now = now_ms();
    const std::int64_t counter = current_counter(now);

    if (checked_in_by_student_.find(student_id) != checked_in_by_student_.end())
        return SubmitResult{false, "already_checked_in", "", -1, 0, 0};

    std::optional<std::int64_t> matched_counter;
    {
        const std::string expected_now = make_bits(state_.session_id, counter, static_cast<int>(bits.size()));
        if (bits == expected_now)
            matched_counter = counter;
        else if (counter > 0)
        {
            const std::string expected_prev = make_bits(state_.session_id, counter - 1, static_cast<int>(bits.size()));
            if (bits == expected_prev)
                matched_counter = counter - 1;
        }
    }
    if (!matched_counter)
        return SubmitResult{false, "invalid_token", "", -1, 0, 0};
    const std::int64_t used_counter = *matched_counter;

    if (auto it = used_counter_by_student_.find(student_id); it != used_counter_by_student_.end())
    {
        if (it->second == used_counter)
            return SubmitResult{false, "already_used", "", -1, 0, 0};
    }

    // Layer 2: fingerprint fuzzy match (70% threshold)
    const int threshold = 70;
    auto it_base = fingerprint_baseline_by_student_.find(student_id);
    if (it_base == fingerprint_baseline_by_student_.end())
    {
        if (fingerprint_unique_enroll_enabled())
        {
            // If this device fingerprint is already enrolled for another student,
            // block enrolling a new baseline to reduce "điểm danh hộ".
            int best = 0;
            for (const auto &kv : fingerprint_baseline_by_student_)
            {
                const int s = fingerprint_score_percent(kv.second, fp);
                if (s > best)
                    best = s;
            }
            const int conflict_threshold = 85;
            if (best >= conflict_threshold)
            {
                return SubmitResult{false, "fingerprint_conflict", "conflict", best, 0, 0};
            }
        }

        // Enroll baseline
        fingerprint_baseline_by_student_[student_id] = fp;
        AttendanceEntry entry;
        entry.student_id = student_id;
        entry.at_ms = now;
        entry.counter = used_counter;
        entry.fingerprint_status = "enrolled";
        entry.fingerprint_score = 100;

        used_counter_by_student_[student_id] = used_counter;
        checked_in_by_student_[student_id] = entry;
        attendance_log_.push_back(entry);
        return SubmitResult{true, "", entry.fingerprint_status, entry.fingerprint_score, entry.at_ms, entry.counter};
    }

    const int score = fingerprint_score_percent(it_base->second, fp);
    if (score < threshold)
    {
        // Do not record attendance
        return SubmitResult{false, "fingerprint_mismatch", "mismatch", score, 0, 0};
    }

    AttendanceEntry entry;
    entry.student_id = student_id;
    entry.at_ms = now;
    entry.counter = used_counter;
    entry.fingerprint_status = "matched";
    entry.fingerprint_score = score;

    used_counter_by_student_[student_id] = used_counter;
    checked_in_by_student_[student_id] = entry;
    attendance_log_.push_back(entry);
    return SubmitResult{true, "", entry.fingerprint_status, entry.fingerprint_score, entry.at_ms, entry.counter};
}

std::vector<AttendanceEntry> TokenService::attendance_list() const
{
    return attendance_log_;
}

std::int64_t TokenService::current_counter_now() const
{
    const std::int64_t now = now_ms();
    return current_counter(now);
}
