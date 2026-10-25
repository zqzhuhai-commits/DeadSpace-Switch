#pragma once
#include <cstdint>
namespace ds {
struct GuestInput {
    enum class Kind {key,pointer,acceleration,action,look,quit};
    Kind kind=Kind::key;
    uint32_t code=0,module=600,id=0;
    float x=0,y=0,z=0;
    bool down=false;
};
enum class GameAction:uint32_t {pause=0x352fb91,rig,previous_weapon,next_weapon,locator,melee,reload,aim,fire,turn_stasis,kinesis,accept,cancel,unused,jump,alt_fire};
}
