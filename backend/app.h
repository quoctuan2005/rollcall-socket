#pragma once

#include <string>

#include "http.h"
#include "token.h"

std::string handle_request(const HttpRequest &req, TokenService &tokens);
