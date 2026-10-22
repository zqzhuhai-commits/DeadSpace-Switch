#pragma once
#include <cstddef>
#include <deque>
#include <string>
#include <utility>
namespace ds {
// Keep recent failure context without retaining every GL/JNI call for hours.
class GuestTrace {
    std::deque<std::string> entries;
    size_t discarded=0;
public:
    static constexpr size_t capacity=512, message_limit=2048;
    void push_back(std::string message) {
        if(entries.size()==capacity){entries.pop_front();++discarded;}
        entries.emplace_back(message.data(),message.size()>message_limit?message_limit:message.size());
    }
    auto begin() const {return entries.begin();}
    auto end() const {return entries.end();}
    size_t dropped() const {return discarded;}
    size_t total() const {return discarded+entries.size();}
};
}
