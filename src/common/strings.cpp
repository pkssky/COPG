// COPG open-source engine — string helpers.
#include "strings.hpp"

#include <cctype>

namespace copg {

std::string trim(const std::string &s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::string to_lower(const std::string &s) {
    std::string r = s;
    for (char &c : r) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}

std::vector<std::string> split(const std::string &s, char delim) {
    std::vector<std::string> out;
    size_t start = 0;
    while (true) {
        size_t pos = s.find(delim, start);
        if (pos == std::string::npos) {
            std::string last = s.substr(start);
            if (!last.empty()) out.push_back(last);
            break;
        }
        if (pos > start) out.push_back(s.substr(start, pos - start));
        start = pos + 1;
    }
    return out;
}

bool starts_with(const std::string &s, const std::string &prefix) {
    return s.size() >= prefix.size() && s.compare(0, prefix.size(), prefix) == 0;
}

bool ends_with(const std::string &s, const std::string &suffix) {
    return s.size() >= suffix.size() &&
           s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string replace_all(std::string s, const std::string &from, const std::string &to) {
    if (from.empty()) return s;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

}  // namespace copg
