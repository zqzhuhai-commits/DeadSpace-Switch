#pragma once
#include <string>
#include "guest_gl.hpp"
// Runs the verified boot sequence: constructors -> JNI_OnLoad -> audio init ->
// NativeOnCreate -> NativeOnSurfaceCreated -> visibility -> DrawFrame xN.
std::string lifecycle_probe(const std::string& library, const std::string& data_root, unsigned frames, ds::GraphicsBackend graphics={});
