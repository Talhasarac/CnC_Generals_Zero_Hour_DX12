#include "native_d3d12_shaders.h"
#include "native_d3d12_lighting.h"

namespace {
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
}

namespace NativeD3D12Shaders {
const std::string& Basic()
{
	static const std::string source = std::string(NativeLightingHlsl) + R"(
cbuffer Transform : register(b0) { row_major float4x4 worldViewProjection; uint4 vertexFlags; row_major float4x4 textureMatrices[4]; uint4 textureFlags; float4 treeSway[11]; row_major float4x4 worldView; float4 fogParameters; float4 fogColor; LightingState lighting; };
cbuffer AlphaTest : register(b1) { uint alphaTestEnable; uint alphaTestFunction; uint alphaTestReference; uint alphaTestPadding; uint grayscaleEnabled; uint textureColorTexture; uint textureColorVertex; uint textureAlphaTexture; uint textureAlphaVertex; uint grayscaleTint; float grayscaleAmount; };
struct VSInput { float3 position : POSITION; float4 color : COLOR0; float3 normal : NORMAL; float4 secondary : COLOR1; };
struct VSOutput { float4 position : SV_POSITION; float4 color : COLOR0; float4 fog : COLOR1; float4 specular : COLOR2; };
VSOutput mainVS(VSInput input)
{
    VSOutput output;
    output.position = mul(float4(input.position, 1.0f), worldViewProjection);
    output.color = vertexFlags.x != 0 ? input.color.bgra : float4(1,1,1,1);
    float3 eye = mul(float4(input.position,1),worldView).xyz;
    LightVertex(lighting,eye,input.normal,output.color,
        lighting.parameters.y != 0 ? input.secondary.bgra : float4(0,0,0,0),output.color,output.specular);
    float distance = vertexFlags.w != 0 ? length(eye) : abs(eye.z);
    float d = distance * fogParameters.z;
    float factor = vertexFlags.z == 1 ? exp(-d) : (vertexFlags.z == 2 ? exp(-d*d) :
        (vertexFlags.z == 3 ? (fogParameters.y-distance)/max(0.00001,fogParameters.y-fogParameters.x) : 1));
    output.fog = float4(fogColor.rgb,saturate(factor));
    return output;
}

float4 mainPS(VSOutput input) : SV_TARGET
{
    if (alphaTestEnable != 0)
    {
        float alpha = input.color.a;
        float reference = (float)alphaTestReference / 255.0f;
		bool alphaPass = false;
		if (alphaTestFunction == 1) alphaPass = false;
		else if (alphaTestFunction == 2) alphaPass = alpha < reference;
		else if (alphaTestFunction == 3) alphaPass = alpha == reference;
		else if (alphaTestFunction == 4) alphaPass = alpha <= reference;
		else if (alphaTestFunction == 5) alphaPass = alpha > reference;
		else if (alphaTestFunction == 6) alphaPass = alpha != reference;
		else if (alphaTestFunction == 7) alphaPass = alpha >= reference;
		else alphaPass = true;
		if (!alphaPass) discard;
    }
    float4 color = input.color;
    color.rgb = lerp(input.fog.rgb,saturate(color.rgb+input.specular.rgb),input.fog.a);
    if (grayscaleEnabled != 0)
    {
        float3 tint = float3((grayscaleTint >> 16) & 255, (grayscaleTint >> 8) & 255, grayscaleTint & 255) / 255.0f;
        color.rgb = lerp(color.rgb, dot(color.rgb, float3(0.299f, 0.587f, 0.114f)).xxx * tint, saturate(grayscaleAmount));
    }
    return color;
}
)";
	return source;
}

const std::string& Textured()
{
	static const std::string source = std::string(NativeLightingHlsl) + R"(
cbuffer Transform : register(b0) { row_major float4x4 worldViewProjection; uint4 vertexFlags; row_major float4x4 textureMatrices[4]; uint4 textureFlags; float4 treeSway[11]; row_major float4x4 worldView; float4 fogParameters; float4 fogColor; LightingState lighting; };
cbuffer AlphaTest : register(b1) { uint alphaTestEnable; uint alphaTestFunction; uint alphaTestReference; uint alphaTestPadding; uint grayscaleEnabled; uint textureColorTexture; uint textureColorVertex; uint textureAlphaTexture; uint textureAlphaVertex; uint grayscaleTint; float grayscaleAmount; };
struct MaterialStage { uint colorOp; uint colorArg1; uint colorArg2; uint colorArg0; uint alphaOp; uint alphaArg1; uint alphaArg2; uint alphaArg0; uint4 resultFlags; float4 bumpMatrix; float4 bumpParameters; };
cbuffer Material : register(b2) { MaterialStage stages[4]; float4 materialFactor; };
Texture2D texture0 : register(t0);
Texture2D texture1 : register(t1);
Texture2D texture2 : register(t2);
Texture2D texture3 : register(t3);
SamplerState sampler1 : register(s1);
SamplerState sampler2 : register(s2);
SamplerState sampler3 : register(s3);
SamplerState sampler0 : register(s0);
struct VSInput { float3 position : POSITION; float4 color : COLOR0; float2 texcoord : TEXCOORD0; float2 uv1 : TEXCOORD1; float2 uv2 : TEXCOORD2; float2 uv3 : TEXCOORD3; float3 tree : TEXCOORD4; float3 normal : NORMAL; float4 secondary : COLOR1; };
struct VSOutput { float4 position : SV_POSITION; float4 color : COLOR0; float2 texcoord : TEXCOORD0; float2 uv1 : TEXCOORD1; float2 uv2 : TEXCOORD2; float2 uv3 : TEXCOORD3; float4 fog : COLOR1; float4 specular : COLOR2; };
float2 VertexUV(uint stage, float2 uv, float3 position, float3 normal) {
    uint flags = textureFlags[stage];
    float4 coordinate = (flags & 1) != 0 ? float4(position,1) : float4((flags & 8) != 0 ? uv : float2(0,0),1,0);
    if ((flags & 48) != 0) {
        float3 n = (flags & 64) != 0 ? mul(float4(normal,0),lighting.normalTransform).xyz : float3(0,0,1);
        n *= rsqrt(max(dot(n,n),1e-20));
        float3 eye = mul(float4(position,1),worldView).xyz;
        eye *= rsqrt(max(dot(eye,eye),1e-20));
        coordinate = float4((flags & 32) != 0 ? reflect(eye,n) : n,1);
    }
    if ((flags & 2) != 0) {
        coordinate = mul(coordinate,textureMatrices[stage]);
        float divisor = (flags & 4) != 0 && coordinate.z != 0 ? coordinate.z : 1;
        return coordinate.xy/divisor;
    }
    return coordinate.xy;
}
VSOutput mainVS(VSInput input)
{
    VSOutput output;
    output.position = mul(float4(input.position, 1.0f), worldViewProjection);
    output.color = vertexFlags.x != 0 ? input.color.bgra : float4(1,1,1,1);
    if (vertexFlags.y != 0) {
        uint swayIndex = input.tree.x >= 0 && input.tree.x < 11 ? (uint)input.tree.x : 0;
        input.position += treeSway[swayIndex].xyz * max(0,input.position.z-input.tree.z);
        output.position = mul(float4(input.position,1),worldViewProjection);
        output.color.rgb *= saturate(input.tree.y);
    }
    output.texcoord = VertexUV(0,input.texcoord,input.position,input.normal);
    output.uv1 = VertexUV(1,input.uv1,input.position,input.normal);
    output.uv2 = VertexUV(2,input.uv2,input.position,input.normal);
    output.uv3 = VertexUV(3,input.uv3,input.position,input.normal);
    float3 eye = mul(float4(input.position,1),worldView).xyz;
    LightVertex(lighting,eye,input.normal,output.color,
        lighting.parameters.y != 0 ? input.secondary.bgra : float4(0,0,0,0),output.color,output.specular);
    float distance = vertexFlags.w != 0 ? length(eye) : abs(eye.z);
    float d = distance * fogParameters.z;
    float factor = vertexFlags.z == 1 ? exp(-d) : (vertexFlags.z == 2 ? exp(-d*d) :
        (vertexFlags.z == 3 ? (fogParameters.y-distance)/max(0.00001,fogParameters.y-fogParameters.x) : 1));
    output.fog = float4(fogColor.rgb,saturate(factor));
    return output;
}

float4 Argument(uint selection, float4 diffuse, float4 current, float4 sampled, float4 temporary, float4 specular) {
    uint source = selection & 15;
    float4 value = source == 0 ? diffuse : (source == 1 ? current : (source == 2 ? sampled : (source == 3 ? materialFactor : (source == 5 ? temporary : (source == 4 ? specular : float4(0,0,0,0))))));
    if ((selection & 32) != 0) value = value.aaaa;
    if ((selection & 16) != 0) value = 1-value;
    return value;
}
float4 Combine(uint op, float4 a, float4 b, float4 c, float4 diffuse, float4 current, float4 sampled) {

    if (op == 1) return saturate(a);
    if (op == 2) return saturate(b);
    if (op == 3) return saturate(a*b);
    if (op == 4) return saturate(2*a*b);
    if (op == 5) return saturate(4*a*b);
    if (op == 6) return saturate(a+b);
    if (op == 7) return saturate(a+b-0.5);
    if (op == 8) return saturate(2*(a+b-0.5));
    if (op == 9) return saturate(a-b);
    if (op == 10) return saturate(a+b*(1-a));
    if (op == 11) return saturate(lerp(b,a,diffuse.a));
    if (op == 12) return saturate(lerp(b,a,sampled.a));
    if (op == 13) return saturate(lerp(b,a,materialFactor.a));
    if (op == 14) return saturate(lerp(b,a,current.a));
    if (op == 15) return saturate(a+b*(1-sampled.a));
    if (op == 16) return saturate(a+b*a.a);
    if (op == 17) return saturate(a*b+a.a);
    if (op == 18) return saturate(a+b*(1-a.a));
    if (op == 19) return saturate((1-a)*b+a.a);
    if (op == 20) return saturate(dot(a.rgb*2-1,b.rgb*2-1).xxxx);
    if (op == 21) return saturate(a*b+c);
    if (op == 22) return saturate(lerp(b,a,c));
    return current;
}
float4 mainPS(VSOutput input) : SV_TARGET
{
	// Variant zero is a direct sprite. Material variants contain only the
	// active texture stages, so a one-texture UI draw never samples four maps.
#if MATERIAL_VARIANT == 0
	float4 sampled = texture0.Sample(sampler0, input.texcoord);
	float4 color = float4(1.0f, 1.0f, 1.0f, 1.0f);
	if (textureColorTexture != 0) color.rgb *= sampled.rgb;
	if (textureColorVertex != 0) color.rgb *= input.color.rgb;
	if (textureAlphaTexture != 0) color.a *= sampled.a;
	if (textureAlphaVertex != 0) color.a *= input.color.a;
#else
    float4 color = input.color;
#if MATERIAL_VARIANT > 1
        float4 samples[MATERIAL_VARIANT-1];
        float2 bumpOffset = 0;
        float bumpLuminance = 1;
        float4 temporary = 0;
        [unroll] for (uint stage = 0; stage < MATERIAL_VARIANT-1; ++stage) {
            MaterialStage settings = stages[stage];
            // A bump stage offsets only the following stage's texture fetch.
            // Sampling all textures before evaluation loses that dependency.
            float4 texel;
            if (stage == 0) texel = texture0.Sample(sampler0,input.texcoord+bumpOffset);
            else if (stage == 1) texel = texture1.Sample(sampler1,input.uv1+bumpOffset);
            else if (stage == 2) texel = texture2.Sample(sampler2,input.uv2+bumpOffset);
            else texel = texture3.Sample(sampler3,input.uv3+bumpOffset);
            samples[stage] = float4(texel.rgb*bumpLuminance,texel.a);
            bumpOffset = 0;
            bumpLuminance = 1;
            if (settings.colorOp == 23 || settings.colorOp == 24) {
                float2 delta = clamp(texel.rg*settings.bumpParameters.z+settings.bumpParameters.w,-1,1);
                bumpOffset = float2(dot(delta,settings.bumpMatrix.xy),dot(delta,settings.bumpMatrix.zw));
                if (settings.colorOp == 24)
                    bumpLuminance = saturate(texel.b*settings.bumpParameters.x+settings.bumpParameters.y);
                continue; // Bump samples are vectors, not a replacement surface color/alpha.
            }
            float4 a = Argument(settings.colorArg1,input.color,color,samples[stage],temporary,input.specular);
            float4 b = Argument(settings.colorArg2,input.color,color,samples[stage],temporary,input.specular);
            float4 c = Argument(settings.colorArg0,input.color,color,samples[stage],temporary,input.specular);
            float3 rgb = Combine(settings.colorOp,a,b,c,input.color,color,samples[stage]).rgb;
            a = Argument(settings.alphaArg1,input.color,color,samples[stage],temporary,input.specular);
            b = Argument(settings.alphaArg2,input.color,color,samples[stage],temporary,input.specular);
            c = Argument(settings.alphaArg0,input.color,color,samples[stage],temporary,input.specular);
            float alpha = Combine(settings.alphaOp,a,b,c,input.color,color,samples[stage]).a;
            if (settings.resultFlags.x != 0) temporary = float4(rgb,alpha);
            else color = float4(rgb,alpha);
        }
#endif
#endif
    color.rgb = lerp(input.fog.rgb,saturate(color.rgb+input.specular.rgb),input.fog.a);
    if (grayscaleEnabled != 0)
    {
        float3 tint = float3((grayscaleTint >> 16) & 255, (grayscaleTint >> 8) & 255, grayscaleTint & 255) / 255.0f;
        color.rgb = lerp(color.rgb, dot(color.rgb, float3(0.299f, 0.587f, 0.114f)).xxx * tint, saturate(grayscaleAmount));
    }
    if (alphaTestEnable != 0)
    {
        float reference = (float)alphaTestReference / 255.0f;
		bool alphaPass = false;
		if (alphaTestFunction == 1) alphaPass = false;
		else if (alphaTestFunction == 2) alphaPass = color.a < reference;
		else if (alphaTestFunction == 3) alphaPass = color.a == reference;
		else if (alphaTestFunction == 4) alphaPass = color.a <= reference;
		else if (alphaTestFunction == 5) alphaPass = color.a > reference;
		else if (alphaTestFunction == 6) alphaPass = color.a != reference;
		else if (alphaTestFunction == 7) alphaPass = color.a >= reference;
		else alphaPass = true;
		if (!alphaPass) discard;
    }
    return color;
}
)";
	return source;
}
}
