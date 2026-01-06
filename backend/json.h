#pragma once

#include <optional>
#include <string>

std::optional<long long> json_get_int(const std::string &body, const std::string &key);
std::optional<double> json_get_double(const std::string &body, const std::string &key);
std::optional<bool> json_get_bool(const std::string &body, const std::string &key);

std::string json_escape(const std::string &s);
std::optional<std::string> json_get_string(const std::string &body, const std::string &key);
bool is_bits01(const std::string &s);
