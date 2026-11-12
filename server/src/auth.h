#pragma once
#include <string>

constexpr long SESSION_SECONDS = 7 * 24 * 60 * 60;

void auth_load();
bool auth_add_user(const std::string& user, const std::string& password);
bool auth_check_password(const std::string& user, const std::string& password);
std::string auth_new_session(const std::string& user);
bool auth_session_valid(const std::string& token);