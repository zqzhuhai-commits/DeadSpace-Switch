#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace ds {
struct TextureMip {uint32_t width,height,offset,size;};
class HdTexture {
    std::vector<uint8_t> bytes;
public:
    uint32_t format=0,width=0,height=0;
    std::vector<TextureMip> levels;
    static HdTexture parse(std::vector<uint8_t> bytes);
    std::vector<uint8_t> rgba(unsigned level) const;
};
std::vector<uint8_t> decode_texture_rgba(uint32_t format,uint32_t width,uint32_t height,const std::vector<uint8_t>& data);
std::string texture_key(uint32_t width,uint32_t height,uint32_t format,const std::vector<uint8_t>& level_zero);
// Empty means missing, not an invalid-file success. Runtime parser validates data.
std::vector<uint8_t> read_hd_texture(const std::string& directory,const std::string& key);
}
