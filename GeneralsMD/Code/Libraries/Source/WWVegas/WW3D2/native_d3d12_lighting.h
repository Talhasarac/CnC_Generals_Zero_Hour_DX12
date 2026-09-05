#pragma once

#include <array>
#include <windows.h>

// CPU/HLSL layout shared by both native mesh pipelines. Lights are in view
// space; prelit geometry and screen draws never opt into normal processing.
struct NativeLightingLight {
	std::array<float,4> ambient = {}, diffuse = {}, specular = {};
	std::array<float,4> position = {}, direction = {}, attenuation = {}, cone = {};
};
struct NativeLightingState {
	std::array<float,16> normalTransform = {};
	std::array<float,4> ambient = {1,1,1,1}, diffuse = {1,1,1,1};
	std::array<float,4> specular = {}, emissive = {}, globalAmbient = {}, parameters = {};
	std::array<UINT,4> flags = {}, sources = {};
	std::array<NativeLightingLight,4> lights = {};
};
static_assert(sizeof(NativeLightingState) == 640, "Native lighting constant layout");
