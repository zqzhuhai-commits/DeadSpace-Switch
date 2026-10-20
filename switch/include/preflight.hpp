#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ds {
constexpr const char* donor_sha1 = "0ed42b611415015807f759ec9b5457857143ce39";
struct Report {
    bool library_present = false;
    bool elf_valid = false;
    bool donor_matches = false;
    bool assets_present = false;
    std::string sha1;
    std::vector<std::string> messages;
    bool ready() const { return elf_valid && donor_matches && assets_present; }
};
std::string sha1(const std::vector<uint8_t>& bytes);
bool validate_elf32(const std::vector<uint8_t>& bytes, std::string& error);
Report preflight(const std::string& root);
}
