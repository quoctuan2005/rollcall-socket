#pragma once

#include <optional>
#include <string>

std::string json_escape(const std::string &s);
std::optional<std::string> json_get_string(const std::string &body, const std::string &key);
bool is_bits01(const std::string &s);
