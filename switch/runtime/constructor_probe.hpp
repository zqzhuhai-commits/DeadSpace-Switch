#pragma once
#include <string>
// Executes initializers with single-thread host services; no JNI/game loop.
std::string constructor_probe(const std::string& library_path);
