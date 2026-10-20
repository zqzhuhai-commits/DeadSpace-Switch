#include "preflight.hpp"
#include <cstdio>
int main(int argc,char** argv) {
    if (argc!=2) { std::fprintf(stderr,"Usage: ds-preflight EXTRACTED_GAME_ROOT\n"); return 64; }
    const auto r=ds::preflight(argv[1]);
    for (const auto& m:r.messages) std::puts(m.c_str());
    if (r.ready()) std::puts("Donor preflight passed. This does not establish playability.");
    return r.ready()?0:2;
}
