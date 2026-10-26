#pragma once
#include <string>
#include "guest_gl.hpp"
#include "guest_audio.hpp"
// Runs the verified boot sequence: constructors -> JNI_OnLoad -> audio init ->
// NativeOnCreate -> NativeOnSurfaceCreated -> visibility -> DrawFrame xN.
// frames=0 runs until graphics.keep_running returns false (callback required).
std::string lifecycle_probe(const std::string& library, const std::string& data_root, unsigned frames, ds::GraphicsBackend graphics={},ds::AudioBackend audio={},std::string save_root={});
