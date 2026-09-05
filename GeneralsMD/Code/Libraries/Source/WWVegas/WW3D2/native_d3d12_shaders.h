#pragma once

#include <string>

// Embedded shader source: no runtime dependency on the checkout or working directory.
namespace NativeD3D12Shaders {
const std::string& Basic();
const std::string& Textured();
}
