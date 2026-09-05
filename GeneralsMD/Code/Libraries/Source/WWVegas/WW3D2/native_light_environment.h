#pragma once
#include "native_d3d12_renderer.h"
#include "lightenvironment.h"
#include "matrix4.h"
#include <cmath>

// The engine authors world-space lights; native shaders consume view-space
// vectors. This does not install or read the legacy render-state cache.
inline void Describe_Native_Light_Environment(const LightEnvironmentClass& environment,
	const Matrix4x4& view, NativeLightingState& state)
{
	const auto color = [](const Vector3& c) { return std::array<float,4>{c.X,c.Y,c.Z,0}; };
	const auto transform = [&view](const Vector3& v, float w) {
		return std::array<float,4>{
			view[0][0]*v.X+view[0][1]*v.Y+view[0][2]*v.Z+view[0][3]*w,
			view[1][0]*v.X+view[1][1]*v.Y+view[1][2]*v.Z+view[1][3]*w,
			view[2][0]*v.X+view[2][1]*v.Y+view[2][2]*v.Z+view[2][3]*w,0};
	};
	state.globalAmbient = color(environment.Get_Equivalent_Ambient());
	state.lights = {};
	for (int i=0; i<environment.Get_Light_Count() && i<4; ++i) {
		auto& light = state.lights[i];
		light.diffuse = color(environment.Get_Light_Diffuse(i));
		light.direction = transform(-environment.Get_Light_Direction(i),0);
		light.position[3] = 3; // directional
		if (i==0) light.specular = {1,1,1,0};
		if (environment.isPointLight(i)) {
			const float inner = environment.getPointIrad(i);
			const float outer = environment.getPointOrad(i);
			light.diffuse = color(environment.getPointDiffuse(i));
			light.ambient = color(environment.getPointAmbient(i));
			light.position = transform(environment.getPointCenter(i),1);
			light.position[3] = 1; // point
			light.direction[3] = outer;
			// Preserve the engine's attenuation, guarding degenerate authored radii.
			light.attenuation = {1,
				std::fabs(inner-outer)<1e-5f || std::fabs(inner)<1e-5f ? 0 : 0.1f/inner,
				outer>1e-5f ? 8.0f/(outer*outer) : 0,0};
		}
	}
}
