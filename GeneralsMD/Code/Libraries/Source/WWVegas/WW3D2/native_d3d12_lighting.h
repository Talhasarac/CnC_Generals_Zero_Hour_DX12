#pragma once

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

static const char NativeLightingHlsl[] = R"(
struct LightingLight {
    float4 ambient, diffuse, specular, position, direction, attenuation, cone;
};
struct LightingState {
    row_major float4x4 normalTransform;
    float4 ambient, diffuse, specular, emissive, globalAmbient, parameters;
    uint4 flags, sources;
    LightingLight lights[4];
};
float3 SafeNormal(float3 value) { return value * rsqrt(max(dot(value,value),1e-20)); }
float4 MaterialColor(uint source, float4 material, float4 color1, float4 color2) {
    return source == 1 ? color1 : (source == 2 ? color2 : material);
}
void LightVertex(LightingState state, float3 eye, float3 normal, float4 color1,
    float4 color2, out float4 diffuse, out float4 specular) {
    diffuse = color1;
    specular = state.flags.y != 0 ? float4(color2.rgb,0) : 0;
    if (state.flags.x == 0) return;
    float3 n = mul(float4(normal,0),state.normalTransform).xyz;
    if (state.flags.z != 0) n = SafeNormal(n);
    float4 ma = MaterialColor(state.sources.x,state.ambient,color1,color2);
    float4 md = MaterialColor(state.sources.y,state.diffuse,color1,color2);
    float4 ms = MaterialColor(state.sources.z,state.specular,color1,color2);
    float4 me = MaterialColor(state.sources.w,state.emissive,color1,color2);
    float3 rgb = me.rgb + ma.rgb*state.globalAmbient.rgb;
    float3 shine = 0;
    float3 viewer = state.flags.w != 0 ? SafeNormal(-eye) : float3(0,0,-1);
    [unroll] for (uint i=0; i<4; ++i) {
        LightingLight light = state.lights[i];
        if (light.position.w == 0) continue;
        float3 delta = light.position.xyz-eye;
        float distance = length(delta);
        float3 l = light.position.w == 3 ? -SafeNormal(light.direction.xyz) : SafeNormal(delta);
        float attenuation = 1;
        if (light.position.w != 3) {
            attenuation = distance <= light.direction.w ? rcp(max(1e-20,
                light.attenuation.x+distance*light.attenuation.y+distance*distance*light.attenuation.z)) : 0;
            if (light.position.w == 2) {
                float rho = dot(-l,SafeNormal(light.direction.xyz));
                float cone = rho >= light.cone.x ? 1 : (rho <= light.cone.y ? 0 :
                    pow(saturate((rho-light.cone.y)/max(1e-20,light.cone.x-light.cone.y)),light.attenuation.w));
                attenuation *= cone;
            }
        }
        float ndotl = max(0,dot(n,l));
        rgb += attenuation*(ma.rgb*light.ambient.rgb + md.rgb*light.diffuse.rgb*ndotl);
        if (state.flags.y != 0 && ndotl > 0) {
            float power = pow(max(0,dot(n,SafeNormal(l+viewer))),max(0,state.parameters.x));
            shine += attenuation*ms.rgb*light.specular.rgb*power;
        }
    }
    diffuse = float4(saturate(rgb),md.a);
    specular = float4(saturate(shine),0);
}
)";
