#pragma once
#include <string>
// Runs the verified boot sequence: constructors -> JNI_OnLoad -> audio init ->
// NativeOnCreate -> NativeOnSurfaceCreated -> visibility -> DrawFrame xN.
std::string lifecycle_probe(const std::string& library, const std::string& data_root, unsigned frames);
