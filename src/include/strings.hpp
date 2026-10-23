// COPG open-source engine — small string helpers.
// C++17 only; no external dependencies.
#pragma once

#include <string>
#include <vector>

namespace copg {

// Trim ASCII whitespace from both ends (in place semantics).
std::string trim(const std::string &s);

// Lower-case a copy (ASCII only; package names and keys are ASCII).
std::string to_lower(const std::string &s);

// Split by a single-char delimiter, dropping empty fields.
std::vector<std::string> split(const std::string &s, char delim);

// True when `s` starts with `prefix`.
bool starts_with(const std::string &s, const std::string &prefix);

// True when `s` ends with `suffix`.
bool ends_with(const std::string &s, const std::string &suffix);

// Replace every occurrence of `from` with `to`.
std::string replace_all(std::string s, const std::string &from, const std::string &to);

}  // namespace copg
