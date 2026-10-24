#pragma once
#include <cstdint>
namespace ds {
struct GuestInput {
    enum class Kind {key,pointer,acceleration,quit};
    Kind kind=Kind::key;
    uint32_t code=0,module=600,id=0;
    float x=0,y=0,z=0;
    bool down=false;
};
}
