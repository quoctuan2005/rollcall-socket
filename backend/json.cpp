#include "json.h"

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
    const std::string needle = "\"" + key + "\"";
    size_t pos = body.find(needle);
    if (pos == std::string::npos)
        return std::nullopt;
    pos = body.find(':', pos + needle.size());
    if (pos == std::string::npos)
        return std::nullopt;
    pos = body.find('"', pos);
    if (pos == std::string::npos)
        return std::nullopt;
    size_t end = body.find('"', pos + 1);
    if (end == std::string::npos)
        return std::nullopt;
    return body.substr(pos + 1, end - (pos + 1));
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
