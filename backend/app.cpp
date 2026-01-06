#include "app.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>

#include "json.h"

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

std::string handle_request(const HttpRequest &req, TokenService &tokens)
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
        const std::string body =
            "{\"session_id\":\"" + json_escape(tokens.session_id()) + "\",\"ttl_ms\":" + std::to_string(tokens.ttl_ms()) + "}";
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
