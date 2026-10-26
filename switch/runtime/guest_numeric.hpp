#pragma once
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

namespace ds {
struct GuestDouble {
    uint32_t low,high,consumed;
    int error;
};
// ARM32 softfp returns a double in r0/r1; endptr must stay in guest memory.
inline GuestDouble parse_guest_double(const std::string& text) {
    const int saved_errno=errno;
    errno=0;char* end=nullptr;
    const double value=std::strtod(text.c_str(),&end);
    const int error=errno;errno=saved_errno;
    uint64_t bits=0;std::memcpy(&bits,&value,sizeof(bits));
    return {uint32_t(bits),uint32_t(bits>>32),uint32_t(end-text.c_str()),error};
}
}
