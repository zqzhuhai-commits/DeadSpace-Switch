#pragma once
#include <string>
// Executes a standalone platform test, never calls original game code.
std::string platform_probe();
std::string fixed_function_probe();
std::string game_graphics_probe(const std::string& data_root, bool interactive=false);
