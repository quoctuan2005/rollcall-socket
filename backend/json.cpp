#include "json.h"

#include <cctype>
#include <cstdlib>

static size_t skip_ws(const std::string &s, size_t i)
{
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])))
        i++;
    return i;
}

static std::optional<size_t> find_value_pos(const std::string &body, const std::string &key)
{
    const std::string needle = "\"" + key + "\"";
    size_t pos = body.find(needle);
    if (pos == std::string::npos)
        return std::nullopt;
    pos = body.find(':', pos + needle.size());
    if (pos == std::string::npos)
        return std::nullopt;
    pos++;
    pos = skip_ws(body, pos);
    if (pos >= body.size())
        return std::nullopt;
    return pos;
}

std::string json_escape(const std::string &s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s)
    {
        switch (c)
        {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            out += c;
            break;
        }
    }
    return out;
}

std::optional<std::string> json_get_string(const std::string &body, const std::string &key)
{
    auto pos_opt = find_value_pos(body, key);
    if (!pos_opt)
        return std::nullopt;
    size_t pos = *pos_opt;
    if (body[pos] != '"')
        return std::nullopt;
    size_t end = body.find('"', pos + 1);
    if (end == std::string::npos)
        return std::nullopt;
    return body.substr(pos + 1, end - (pos + 1));
}

std::optional<long long> json_get_int(const std::string &body, const std::string &key)
{
    auto pos_opt = find_value_pos(body, key);
    if (!pos_opt)
        return std::nullopt;
    size_t pos = *pos_opt;

    char *endptr = nullptr;
    const long long v = std::strtoll(body.c_str() + pos, &endptr, 10);
    if (endptr == body.c_str() + pos)
        return std::nullopt;
    return v;
}

std::optional<double> json_get_double(const std::string &body, const std::string &key)
{
    auto pos_opt = find_value_pos(body, key);
    if (!pos_opt)
        return std::nullopt;
    size_t pos = *pos_opt;

    char *endptr = nullptr;
    const double v = std::strtod(body.c_str() + pos, &endptr);
    if (endptr == body.c_str() + pos)
        return std::nullopt;
    return v;
}

std::optional<bool> json_get_bool(const std::string &body, const std::string &key)
{
    auto pos_opt = find_value_pos(body, key);
    if (!pos_opt)
        return std::nullopt;
    size_t pos = *pos_opt;

    if (body.compare(pos, 4, "true") == 0)
        return true;
    if (body.compare(pos, 5, "false") == 0)
        return false;
    return std::nullopt;
}

bool is_bits01(const std::string &s)
{
    if (s.empty())
        return false;
    for (char c : s)
    {
        if (c != '0' && c != '1')
            return false;
    }
    return true;
}
