#pragma once
#include "native_draw_state.h"
#include "native_pipeline_description.h"
#include "shader.h"
#include "matrix4.h"
#include <cstring>

inline NativeMaterialCoordinates Describe_Native_Planar_Projection(const Matrix4x4& worldRow,
	float scaleX, float scaleY, float offsetX, float offsetY)
{
	Matrix4x4 projection(true);
	projection[0][0]=scaleX; projection[1][1]=scaleY;
	projection[3][0]=offsetX; projection[3][1]=offsetY;
	const Matrix4x4 matrix=worldRow*projection;
	NativeMaterialCoordinates coordinates;
	coordinates.position=coordinates.transform=true;
	std::memcpy(coordinates.matrix.data(),&matrix,sizeof(matrix));
	return coordinates;
}

// Value state for one procedural pass. Camera/light inputs come from its caller;
// no legacy installation/uninstallation is needed to interpret this description.
struct NativeMaterialPassDescription {
	NativeMaterialDescription material;
	NativePipelineDescription pipeline;
	NativeLightingState lighting;
	ShaderClass::FogFuncType fog = ShaderClass::FOG_DISABLE;
};
