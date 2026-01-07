#include "app.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cctype>
#include <sstream>
#include <vector>

#include "json.h"

static std::string trim(const std::string &s)
{
    size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a])))
        a++;
    size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
        b--;
    return s.substr(a, b - a);
}

static std::vector<std::string> split_lines(const std::string &s)
{
    std::vector<std::string> out;
    std::string cur;
    cur.reserve(64);
    for (char ch : s)
    {
        if (ch == '\r')
            continue;
        if (ch == '\n')
        {
            out.push_back(cur);
            cur.clear();
            continue;
        }
        cur.push_back(ch);
    }
    if (!cur.empty())
        out.push_back(cur);
    return out;
}

static std::int64_t wall_now_ms()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

static std::string http_json(int status, const std::string &json_body)
{
    std::ostringstream out;
    out << "HTTP/1.1 " << status << "\r\n";
    out << "Content-Type: application/json; charset=utf-8\r\n";
    out << "Content-Length: " << json_body.size() << "\r\n";
    out << "Connection: close\r\n";
    out << "\r\n";
    out << json_body;
    return out.str();
}

std::string handle_request(const HttpRequest &req, TokenService &tokens, RollcallDb &db)
{
    if (req.method == "GET" && req.path == "/api/health")
    {
        return http_json(200, "{\"ok\":true}");
    }

    if (req.method == "GET" && req.path == "/api/session")
    {
        const std::string body =
            "{\"session_id\":\"" + json_escape(tokens.session_id()) +
            "\",\"ttl_ms\":" + std::to_string(tokens.ttl_ms()) +
            ",\"counter\":" + std::to_string(tokens.current_counter_now()) + "}";
        return http_json(200, body);
    }

    if (req.method == "POST" && req.path == "/api/session/start")
    {
        tokens.start_session();
        if (db.ok())
        {
            db.ensure_session(tokens.session_id(), wall_now_ms());
        }
        const std::string body =
            "{\"session_id\":\"" + json_escape(tokens.session_id()) + "\",\"ttl_ms\":" + std::to_string(tokens.ttl_ms()) + "}";
        return http_json(200, body);
    }

    if (req.method == "POST" && req.path == "/api/roster/import")
    {
        // Body format (text/plain or any): one student per line:
        //   MSSV,Full Name
        // Blank lines and lines starting with # are ignored.
        // This avoids needing a full JSON parser in the C++ demo backend.
        if (!db.ok())
            return http_json(500, "{\"ok\":false,\"error\":\"db_unavailable\"}");

        int inserted = 0;
        int skipped = 0;
        for (const auto &raw_line : split_lines(req.body))
        {
            const std::string line = trim(raw_line);
            if (line.empty() || (!line.empty() && line[0] == '#'))
            {
                skipped++;
                continue;
            }

            const size_t comma = line.find(',');
            if (comma == std::string::npos)
            {
                skipped++;
                continue;
            }
            const std::string student_id = trim(line.substr(0, comma));
            const std::string full_name = trim(line.substr(comma + 1));
            if (student_id.empty())
            {
                skipped++;
                continue;
            }

            inserted += db.upsert_student(student_id, full_name);
        }

        const std::string body =
            "{\"ok\":true,\"inserted\":" + std::to_string(inserted) + ",\"skipped\":" + std::to_string(skipped) + "}";
        return http_json(200, body);
    }

    if (req.method == "GET" && req.path == "/api/roster/list")
    {
        if (!db.ok())
            return http_json(500, "{\"ok\":false,\"error\":\"db_unavailable\"}");

        const auto session_id = tokens.session_id();
        const auto rows = db.roster_with_status(session_id);
        std::string students = "[";
        for (size_t i = 0; i < rows.size(); i++)
        {
            const auto &r = rows[i];
            students += "{\"student_id\":\"" + json_escape(r.student_id) +
                        "\",\"full_name\":\"" + json_escape(r.full_name) +
                        "\",\"present\":" + std::string(r.present ? "true" : "false") +
                        ",\"at_ms\":" + std::to_string(r.at_ms) + "}";
            if (i + 1 < rows.size())
                students += ",";
        }
        students += "]";

        const std::string body =
            "{\"ok\":true,\"session_id\":\"" + json_escape(session_id) +
            "\",\"count\":" + std::to_string(rows.size()) +
            ",\"students\":" + students + "}";
        return http_json(200, body);
    }

    if (req.method == "GET" && req.path == "/api/token")
    {
        auto q = parse_query(req.target);
        int nbits = 8;
        if (auto it = q.find("bits"); it != q.end())
        {
            nbits = std::max(1, std::min(256, std::atoi(it->second.c_str())));
        }

        const TokenResponse t = tokens.get_token(nbits);
        const std::string body =
            "{\"session_id\":\"" + json_escape(t.session_id) +
            "\",\"bits\":\"" + json_escape(t.bits) +
            "\",\"expires_at_ms\":" + std::to_string(t.expires_at_ms) +
            ",\"now_ms\":" + std::to_string(t.now_ms) +
            ",\"ttl_ms\":" + std::to_string(t.ttl_ms) +
            ",\"counter\":" + std::to_string(t.counter) + "}";
        return http_json(200, body);
    }

    if (req.method == "POST" && req.path == "/api/attendance/submit")
    {
        const auto student_id_opt = json_get_string(req.body, "student_id");
        const auto bits_opt = json_get_string(req.body, "bits");
        if (!student_id_opt || !bits_opt)
            return http_json(400, "{\"ok\":false,\"error\":\"bad_request\"}");

        const std::string student_id = *student_id_opt;
        const std::string bits = *bits_opt;
        if (student_id.empty() || !is_bits01(bits) || bits.size() > 256)
            return http_json(400, "{\"ok\":false,\"error\":\"invalid_input\"}");

        // Layer 2 fingerprint payload (flat keys, no DB; stored in-memory per session)
        Fingerprint fp;
        fp.platform = json_get_string(req.body, "fp_platform").value_or("");
        fp.timezone = json_get_string(req.body, "fp_tz").value_or("");
        fp.language = json_get_string(req.body, "fp_lang").value_or("");
        fp.screen_w = static_cast<int>(json_get_int(req.body, "fp_sw").value_or(0));
        fp.screen_h = static_cast<int>(json_get_int(req.body, "fp_sh").value_or(0));
        fp.dpr = json_get_double(req.body, "fp_dpr").value_or(0.0);
        fp.hardware_concurrency = static_cast<int>(json_get_int(req.body, "fp_hc").value_or(0));
        fp.device_memory = static_cast<int>(json_get_int(req.body, "fp_dm").value_or(0));
        fp.touch = json_get_bool(req.body, "fp_touch").value_or(false);

        // Require minimal fingerprint for Layer 2
        if (fp.platform.empty() || fp.timezone.empty() || fp.language.empty() || fp.screen_w <= 0 || fp.screen_h <= 0 || fp.dpr <= 0.0)
            return http_json(400, "{\"ok\":false,\"error\":\"missing_fingerprint\"}");

        const SubmitResult r = tokens.submit_attendance(student_id, bits, fp);
        if (!r.ok)
        {
            const int status = (r.error == "already_used" || r.error == "already_checked_in" || r.error == "fingerprint_mismatch" || r.error == "fingerprint_conflict") ? 409 : 401;
            std::string body = "{\"ok\":false,\"error\":\"" + json_escape(r.error) + "\"";
            if (!r.fingerprint_status.empty())
                body += ",\"fingerprint_status\":\"" + json_escape(r.fingerprint_status) + "\"";
            if (r.fingerprint_score >= 0)
                body += ",\"fingerprint_score\":" + std::to_string(r.fingerprint_score);
            body += "}";
            return http_json(status, body);
        }

        if (db.ok())
        {
            db.ensure_session(tokens.session_id(), r.at_ms);
            db.record_attendance(tokens.session_id(), student_id, r.at_ms, r.counter, r.fingerprint_status, r.fingerprint_score);
        }

        const std::string body =
            "{\"ok\":true,\"status\":\"accepted\",\"student_id\":\"" + json_escape(student_id) +
            "\",\"session_id\":\"" + json_escape(tokens.session_id()) +
            "\",\"fingerprint_status\":\"" + json_escape(r.fingerprint_status) +
            "\",\"fingerprint_score\":" + std::to_string(r.fingerprint_score) + "}";
        return http_json(200, body);
    }

    if (req.method == "GET" && req.path == "/api/attendance/list")
    {
        const auto list = tokens.attendance_list();
        std::string attendees = "[";
        for (size_t i = 0; i < list.size(); i++)
        {
            const auto &e = list[i];
            attendees += "{\"student_id\":\"" + json_escape(e.student_id) +
                         "\",\"at_ms\":" + std::to_string(e.at_ms) +
                         ",\"counter\":" + std::to_string(e.counter) +
                         ",\"fingerprint_status\":\"" + json_escape(e.fingerprint_status) +
                         "\",\"fingerprint_score\":" + std::to_string(e.fingerprint_score) + "}";
            if (i + 1 < list.size())
                attendees += ",";
        }
        attendees += "]";

        const std::string body =
            "{\"ok\":true,\"session_id\":\"" + json_escape(tokens.session_id()) +
            "\",\"count\":" + std::to_string(list.size()) +
            ",\"attendees\":" + attendees + "}";
        return http_json(200, body);
    }

    return http_json(404, "{\"ok\":false,\"error\":\"not_found\"}");
}
