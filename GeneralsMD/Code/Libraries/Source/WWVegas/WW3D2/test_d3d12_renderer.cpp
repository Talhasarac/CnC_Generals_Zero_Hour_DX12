/* Native D3D12 regression tests: assertions verify GPU pixels, not just Present. */
#include "native_d3d12_renderer.h"
#include "surface_pixel_write.h"
#include "native_dds_layout.h"
#include <d3d12sdklayers.h>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <cstring>

namespace {
LRESULT CALLBACK TestWindowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{ return DefWindowProcW(hwnd, message, wParam, lParam); }

bool Pixel(const std::vector<unsigned char>& pixels, UINT width, UINT x, UINT y,
	int r, int g, int b, const char* name)
{
	const size_t i = (static_cast<size_t>(y) * width + x) * 4;
	if (i + 3 >= pixels.size() || abs(int(pixels[i])-r)>3 ||
		abs(int(pixels[i+1])-g)>3 || abs(int(pixels[i+2])-b)>3) {
		std::fprintf(stderr, "FAIL %s: got %u,%u,%u expected %d,%d,%d\n", name,
			i+3<pixels.size()?pixels[i]:0, i+3<pixels.size()?pixels[i+1]:0,
			i+3<pixels.size()?pixels[i+2]:0, r,g,b);
		return false;
	}
	std::printf("PASS %s\n", name);
	return true;
}

void State(NativeD3D12Renderer& r, bool blend = false)
{
	r.SetFixedFunctionState(D3D12_CULL_MODE_NONE, false, false,
		D3D12_COMPARISON_FUNC_ALWAYS, blend, D3D12_BLEND_SRC_ALPHA,
		D3D12_BLEND_INV_SRC_ALPHA, D3D12_BLEND_OP_ADD, D3D12_COLOR_WRITE_ENABLE_ALL);
	r.SetAlphaTestState(false, D3D12_COMPARISON_FUNC_ALWAYS, 0);
	r.SetTextureCombine(true, true, true, true);
	r.SetGrayscale(false);
}

#define REQUIRE(expr) do { if (!(expr)) { std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #expr); return false; } } while (0)

bool Run(NativeD3D12Renderer& r, HWND window)
{
	// Radar chooses BGR24 first. Its bulk pixel writer previously omitted this
	// format entirely, leaving the terrain black despite successful GPU uploads.
	for (unsigned int size = 1; size <= 4; ++size) {
		unsigned char storage[12];
		std::memset(storage, 0xcd, sizeof(storage));
		WritePackedSurfacePixel(storage + 3, 0x78563412, size);
		for (unsigned int i = 0; i < sizeof(storage); ++i)
			REQUIRE(storage[i] == ((i >= 3 && i < 3 + size) ?
				static_cast<unsigned char>(0x78563412u >> ((i - 3) * 8)) : 0xcd));
		WritePackedSurfacePixel(storage + 3, 0, 0);
		WritePackedSurfacePixel(storage + 3, 0, 5);
		REQUIRE(storage[3] == 0x12 && storage[3 + size] == 0xcd);
	}
	std::printf("PASS packed surface pixels: 8/16/24/32-bit and guard bytes\n");
	// CPU decompression shares the exact path used for DDS-only editable assets
	// and GPU surface readback. Cover partial blocks, alpha and pitched storage.
	std::vector<unsigned char> decoded;
	unsigned char blocks[32] = {};
	blocks[0] = 0; blocks[1] = 0xf8; // BC1 endpoint zero is opaque red.
	NativeD3D12TextureLevel compressed = {blocks,16,8};
	REQUIRE(r.DecodeTextureBgra(DXGI_FORMAT_BC1_UNORM,3,1,compressed,decoded));
	REQUIRE(decoded.size()==12 && decoded[2]==255 && decoded[11]==255);
	compressed.slicePitch = 7;
	REQUIRE(!r.DecodeTextureBgra(DXGI_FORMAT_BC1_UNORM,3,1,compressed,decoded));
	compressed.slicePitch = 8;
	blocks[0]=blocks[1]=0; blocks[2]=blocks[3]=255; blocks[4]=255;
	REQUIRE(r.DecodeTextureBgra(DXGI_FORMAT_BC1_UNORM,1,1,compressed,decoded));
	REQUIRE(decoded[0]==0 && decoded[3]==0);
	std::memset(blocks,0,sizeof(blocks));
	blocks[0]=0x55; blocks[9]=0xf8;
	compressed = {blocks,16,16};
	REQUIRE(r.DecodeTextureBgra(DXGI_FORMAT_BC2_UNORM,1,1,compressed,decoded));
	REQUIRE(decoded[2]==255 && decoded[3]==85);
	blocks[0]=123; blocks[1]=255; blocks[2]=0;
	REQUIRE(r.DecodeTextureBgra(DXGI_FORMAT_BC3_UNORM,1,1,compressed,decoded));
	REQUIRE(decoded[2]==255 && decoded[3]==123);
	blocks[2]=6;
	REQUIRE(r.DecodeTextureBgra(DXGI_FORMAT_BC3_UNORM,1,1,compressed,decoded));
	REQUIRE(decoded[3]==0);
	blocks[2]=7;
	REQUIRE(r.DecodeTextureBgra(DXGI_FORMAT_BC3_UNORM,1,1,compressed,decoded));
	REQUIRE(decoded[3]==255);
	std::printf("PASS BC1/BC2/BC3 CPU decoding and partial-block bounds\n");
	// Stock wave256.dds is 256x128 BC3 with nine mips; its 4x2, 2x1
	// and 1x1 levels each occupy a complete 16-byte block.
	std::array<unsigned char,128> ddsHeader = {};
	const auto putDDS = [&](unsigned offset, unsigned value) { std::memcpy(ddsHeader.data()+offset,&value,4); };
	std::memcpy(ddsHeader.data(),"DDS ",4);
	putDDS(4,124); putDDS(12,128); putDDS(16,256); putDDS(28,9);
	putDDS(76,32); putDDS(80,4); putDDS(84,0x35545844);
	NativeDDSLayout ddsLayout;
	REQUIRE(ddsLayout.Parse(ddsHeader.data(),128,43856));
	REQUIRE(ddsLayout.count==9 && ddsLayout.dataBytes==43728);
	REQUIRE(ddsLayout.levels[6].width==4 && ddsLayout.levels[6].height==2);
	REQUIRE(ddsLayout.levels[6].size==16 && ddsLayout.levels[8].size==16);
	REQUIRE(!ddsLayout.Parse(ddsHeader.data(),128,43855));
	REQUIRE(ddsLayout.count==0);
	putDDS(12,32); putDDS(16,128); putDDS(28,8); putDDS(84,0x31545844);
	REQUIRE(ddsLayout.Parse(ddsHeader.data(),128,2888));
	REQUIRE(ddsLayout.dataBytes==2760 && ddsLayout.levels[7].size==8);
	putDDS(28,16);
	REQUIRE(!ddsLayout.Parse(ddsHeader.data(),128,2888));
	putDDS(28,8); putDDS(112,0x200);
	REQUIRE(!ddsLayout.Parse(ddsHeader.data(),128,2888));
	std::printf("PASS native rectangular DDS mip sizes, complete chains and malformed bounds\n");
	REQUIRE(r.Initialize(window, 64, 64, true));
	Microsoft::WRL::ComPtr<ID3D12InfoQueue> messages;
	r.Device()->QueryInterface(IID_PPV_ARGS(&messages));
	std::printf("D3D12 debug validation: %s\n", messages ? "enabled" : "unavailable");
	State(r);
	const FLOAT black[] = {0,0,0,1};
	std::vector<unsigned char> pixels;
	REQUIRE(r.BeginFrame(black));
	REQUIRE(!r.BeginFrame(black)); // Never discard an unsubmitted frame.
	REQUIRE(r.DrawScreenQuad(0,0,64,64,0xffff0000));
	REQUIRE(r.ReadbackFrame(pixels));
	REQUIRE(Pixel(pixels,64,32,32,255,0,0,"packed solid color"));
	putDDS(12,128); putDDS(16,256); putDDS(28,9); putDDS(84,0x35545844); putDDS(112,0);
	REQUIRE(ddsLayout.Parse(ddsHeader.data(),128,43856));
	std::vector<unsigned char> rectangularDDS(ddsLayout.dataBytes,0);
	std::array<NativeD3D12TextureLevel,9> rectangularLevels;
	for (UINT mip=0;mip<9;++mip) {
		const auto& source=ddsLayout.levels[mip];
		for (UINT offset=source.offset;offset<source.offset+source.size;offset+=16) {
			rectangularDDS[offset]=255; // BC3 opaque alpha endpoint.
			rectangularDDS[offset+8]=31; // Blue BC1 color endpoint.
		}
		rectangularLevels[mip]={rectangularDDS.data()+source.offset,source.rowPitch,source.size};
	}
	std::unique_ptr<NativeD3D12Texture> rectangularTexture(r.CreateTexture2D(256,128,9,DXGI_FORMAT_BC3_UNORM));
	REQUIRE(rectangularTexture && r.UploadTexture2D(*rectangularTexture,rectangularLevels.data(),9));
	for (UINT mip=6;mip<9;++mip) {
		REQUIRE(r.ReadbackTexture(*rectangularTexture,mip,decoded));
		REQUIRE(decoded.size()==ddsLayout.levels[mip].width*ddsLayout.levels[mip].height*4);
		REQUIRE(decoded[0]==255 && decoded[1]==0 && decoded[2]==0 && decoded[3]==255);
	}
	std::printf("PASS native rectangular BC3 upload/readback through 1x1 mip\n");

	REQUIRE(r.TextureMipCount(0,1) == 0);
	REQUIRE(r.TextureMipCount(32,32) == 6);
	REQUIRE(r.TextureMipCount(3,1) == 2);
	REQUIRE(r.TextureMipCount(32,32,1) == 1);
	std::unique_ptr<NativeD3D12Texture> mipTexture(r.CreateTexture2D(32,32,6,DXGI_FORMAT_B8G8R8A8_UNORM));
	REQUIRE(mipTexture != nullptr);
	std::vector<unsigned char> checker(32*32*4);
	for (UINT y=0;y<32;++y) for (UINT x=0;x<32;++x) {
		const size_t i = (y*32+x)*4;
		checker[i] = checker[i+1] = checker[i+2] = (x+y)%2 ? 255 : 0;
		checker[i+3] = 255;
	}
	NativeD3D12TextureLevel checkerLevel = {checker.data(),32*4,32*32*4};
	REQUIRE(r.UploadBgraTexture(*mipTexture,checkerLevel));
	REQUIRE(r.BeginFrame(black));
	r.SetSamplerState(NativeD3D12FilterMode::Linear,NativeD3D12FilterMode::Linear,
		NativeD3D12FilterMode::Linear,false,false);
	REQUIRE(r.DrawTexturedScreenQuad(0,0,64,64,0,0,64,64,0xffffffff,mipTexture.get()));
	REQUIRE(r.ReadbackFrame(pixels));
	REQUIRE(Pixel(pixels,64,32,32,128,128,128,"minified checker uses generated mip chain"));
	std::unique_ptr<NativeD3D12Texture> oddMip(r.CreateTexture2D(3,1,2,DXGI_FORMAT_B8G8R8A8_UNORM));
	const unsigned char oddPixels[16] = {0,0,0,0, 0,0,0,0, 0,0,255,0, 0xcd,0xcd,0xcd,0xcd};
	NativeD3D12TextureLevel oddLevel = {oddPixels,16,12};
	REQUIRE(r.UploadBgraTexture(*oddMip,oddLevel,true));
	REQUIRE(r.BeginFrame(black));
	State(r,true);
	REQUIRE(r.DrawTexturedScreenQuad(0,0,64,64,0,0,64,64,0xffffffff,oddMip.get()));
	REQUIRE(r.ReadbackFrame(pixels));
	REQUIRE(Pixel(pixels,64,32,32,85,0,0,"odd-width mip includes last pixel and forces opaque alpha"));
	oddLevel.slicePitch = 11;
	REQUIRE(!r.UploadBgraTexture(*oddMip,oddLevel));
	State(r);
	r.SetSamplerState(NativeD3D12FilterMode::Point,NativeD3D12FilterMode::Point,
		NativeD3D12FilterMode::Point,false,false);
	REQUIRE(r.ReadbackTexture(*oddMip,1,decoded));
	REQUIRE(decoded.size()==4 && decoded[2]==85 && decoded[3]==255);
	REQUIRE(!r.ReadbackTexture(*oddMip,2,decoded));
	// A readback in the middle of a frame must neither clear nor present it.
	std::unique_ptr<NativeD3D12Texture> readTarget(r.CreateTexture2D(64,64,1,DXGI_FORMAT_B8G8R8A8_UNORM,true));
	REQUIRE(readTarget);
	REQUIRE(r.BeginFrame(black));
	REQUIRE(r.SetRenderTarget(readTarget.get(),false));
	REQUIRE(r.DrawScreenQuad(0,0,64,64,0xffff0000));
	REQUIRE(r.ReadbackTexture(*readTarget,0,decoded));
	REQUIRE(decoded[(32*64+32)*4+2]==255);
	REQUIRE(r.DrawScreenQuad(32,0,32,64,0xff0000ff));
	REQUIRE(r.ReadbackTexture(*readTarget,0,decoded));
	REQUIRE(decoded[(32*64+16)*4+2]==255 && decoded[(32*64+48)*4]==255);
	REQUIRE(r.SetRenderTarget(nullptr));
	REQUIRE(r.DrawTexturedScreenQuad(0,0,64,64,0,0,1,1,0xffffffff,readTarget.get()));
	REQUIRE(r.ReadbackFrame(pixels));
	REQUIRE(Pixel(pixels,64,16,32,255,0,0,"readback preserves earlier draws"));
	REQUIRE(Pixel(pixels,64,48,32,0,0,255,"readback resumes offscreen target without clearing"));
	// Read native compressed mip data, not a blank CPU placeholder.
	std::unique_ptr<NativeD3D12Texture> bcTexture(r.CreateTexture2D(4,4,3,DXGI_FORMAT_BC1_UNORM));
	REQUIRE(bcTexture);
	unsigned char redBlock[8] = {0,0xf8,0,0,0,0,0,0};
	NativeD3D12TextureLevel bcLevels[3] = {{redBlock,8,8},{redBlock,8,8},{redBlock,8,8}};
	REQUIRE(r.UploadTexture2D(*bcTexture,bcLevels,3));
	REQUIRE(r.ReadbackTexture(*bcTexture,2,decoded));
	REQUIRE(decoded.size()==4 && decoded[2]==255 && decoded[3]==255);
	std::unique_ptr<NativeD3D12Texture> sharedBc(bcTexture->ShareResource());
	REQUIRE(sharedBc->Resource() == bcTexture->Resource());
	bcTexture.reset();
	REQUIRE(r.ReadbackTexture(*sharedBc,2,decoded));
	REQUIRE(decoded[2]==255 && decoded[3]==255);
	std::printf("PASS native texture readback, compressed final mip and frame continuation\n");

	std::unique_ptr<NativeD3D12Texture> tiny(r.CreateTexture2D(1,1,1,DXGI_FORMAT_R8G8B8A8_UNORM));
	REQUIRE(tiny != nullptr);
	const unsigned char green[] = {0,255,0,255};
	const unsigned char blue[] = {0,0,255,255};
	NativeD3D12TextureLevel level = {green,4,4};
	REQUIRE(r.UploadTexture2D(*tiny,&level,1));
	REQUIRE(r.BeginFrame(black));
	REQUIRE(r.DrawTexturedScreenQuad(0,0,32,64,0,0,1,1,0xffffffff,tiny.get()));
	level = {blue,4,4};
	REQUIRE(r.UploadTexture2D(*tiny,&level,1)); // Ordered after the green draw, before the blue draw.
	REQUIRE(r.DrawTexturedScreenQuad(32,0,32,64,0,0,1,1,0xffffffff,tiny.get()));
	tiny.reset(); // Both the resource AND its descriptor must survive submission.
	REQUIRE(r.ReadbackFrame(pixels));
	REQUIRE(Pixel(pixels,64,16,32,0,255,0,"1x1 upload and in-frame ordering"));
	REQUIRE(Pixel(pixels,64,48,32,0,0,255,"updated texture survives owner deletion"));

	std::unique_ptr<NativeD3D12Texture> white(r.CreateTexture2D(1,1,1,DXGI_FORMAT_R8G8B8A8_UNORM));
	const unsigned char whiteData[] = {255,255,255,255};
	level = {whiteData,4,4};
	REQUIRE(white && r.UploadTexture2D(*white,&level,1));
	REQUIRE(r.BeginFrame(black));
	r.SetAlphaTestState(true,D3D12_COMPARISON_FUNC_GREATER_EQUAL,128);
	REQUIRE(r.DrawTexturedScreenQuad(0,0,32,64,0,0,1,1,0x40ffffff,white.get()));
	REQUIRE(r.DrawTexturedScreenQuad(32,0,32,64,0,0,1,1,0xffffffff,white.get()));
	REQUIRE(r.ReadbackFrame(pixels));
	REQUIRE(Pixel(pixels,64,16,32,0,0,0,"cutout alpha rejects transparent pixels"));
	REQUIRE(Pixel(pixels,64,48,32,255,255,255,"cutout alpha retains opaque pixels"));
	State(r);
	REQUIRE(r.BeginFrame(black));
	State(r, true);
	REQUIRE(r.DrawScreenQuad(0,0,32,64,0x80ff0000));
	REQUIRE(r.DrawTexturedScreenQuad(32,0,32,64,0,0,1,1,0x8000ff00,white.get()));
	REQUIRE(r.ReadbackFrame(pixels));
	REQUIRE(Pixel(pixels,64,16,32,128,0,0,"basic PSO blend state"));
	REQUIRE(Pixel(pixels,64,48,32,0,128,0,"textured PSO blend state"));
	State(r);

	struct MaterialVertex { float position[3]; UINT color; float uv[2][2]; };
	const MaterialVertex quad[] = {
		{{-1,-1,0},0xffffffff,{{0,1},{0,1}}}, {{-1,1,0},0xffffffff,{{0,0},{0,0}}},
		{{1,-1,0},0xffffffff,{{1,1},{1,1}}}, {{1,1,0},0xffffffff,{{1,0},{1,0}}}};
	const unsigned short quadIndices[] = {0,1,2,2,1,3};
	std::unique_ptr<NativeD3D12Texture> layers[4];
	const unsigned char layerPixels[4][4] = {{64,0,0,255},{0,64,0,255},{0,0,64,255},{64,64,64,255}};
	for (UINT stage=0;stage<4;++stage) {
		layers[stage].reset(r.CreateTexture2D(1,1,1,DXGI_FORMAT_R8G8B8A8_UNORM));
		level = {layerPixels[stage],4,4};
		REQUIRE(layers[stage] && r.UploadTexture2D(*layers[stage],&level,1));
		NativeMaterialStage material;
		material.colorOp = stage ? NativeMaterialOp::Add : NativeMaterialOp::Select1;
		NativeMaterialCoordinates coords;
		coords.offset = 16+(stage%2)*8;
		r.SetMaterialStage(stage,material,coords,layers[stage].get());
	}
	r.SetMaterialEnabled(true);
	REQUIRE(r.BeginFrame(black));
	REQUIRE(r.DrawIndexedTextured(quad,sizeof(quad),sizeof(MaterialVertex),4,16,
		quadIndices,6,D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST,layers[0].get(),12));
	REQUIRE(r.ReadbackFrame(pixels));
	REQUIRE(Pixel(pixels,64,32,32,128,128,128,"four native material texture stages"));
	NativeMaterialStage disabled;
	NativeMaterialCoordinates coords;
	const int stageColors[4][3] = {{255,255,255},{64,0,0},{64,64,0},{64,64,64}};
	for (int last=3;last>=0;--last) {
		r.SetMaterialStage(last,disabled,coords,nullptr);
		REQUIRE(r.BeginFrame(black));
		REQUIRE(r.DrawIndexedTextured(quad,sizeof(quad),sizeof(MaterialVertex),4,16,
			quadIndices,6,D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST,layers[0].get(),12));
		REQUIRE(r.ReadbackFrame(pixels));
		REQUIRE(Pixel(pixels,64,32,32,stageColors[last][0],stageColors[last][1],stageColors[last][2],
			"specialized material stage-count PSO transition"));
	}
	// Terrain blends the two atlas samples with diffuse alpha before lighting.
	MaterialVertex blendQuad[4];
	std::memcpy(blendQuad,quad,sizeof(quad));
	for (auto& v : blendQuad) v.color = 0x80808080;
	NativeMaterialCoordinates atlasUV;
	atlasUV.offset = 16;
	NativeMaterialStage atlasBase, atlasBlend, atlasLighting;
	atlasBase.colorOp = NativeMaterialOp::Select1;
	atlasBlend.colorOp = NativeMaterialOp::BlendDiffuseAlpha;
	atlasLighting.colorOp = NativeMaterialOp::Modulate;
	atlasLighting.colorArg1 = UINT(NativeMaterialSource::Current);
	atlasLighting.colorArg2 = UINT(NativeMaterialSource::Diffuse);
	r.SetMaterialStage(0,atlasBase,atlasUV,layers[0].get());
	r.SetMaterialStage(1,atlasBlend,atlasUV,layers[1].get());
	r.SetMaterialStage(2,atlasLighting,coords,nullptr);
	r.SetMaterialStage(3,disabled,coords,nullptr);
	REQUIRE(r.BeginFrame(black));
	REQUIRE(r.DrawIndexedTextured(blendQuad,sizeof(blendQuad),sizeof(MaterialVertex),4,16,
		quadIndices,6,D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST,layers[0].get(),12));
	REQUIRE(r.ReadbackFrame(pixels));
	REQUIRE(Pixel(pixels,64,32,32,16,16,0,"terrain atlas blend then diffuse lighting"));
	NativeMaterialStage sparkle, noise, shroud;
	sparkle.colorOp = NativeMaterialOp::Select1;
	sparkle.resultFlags[0] = 1;
	noise.colorOp = NativeMaterialOp::MultiplyAdd;
	noise.colorArg1 = UINT(NativeMaterialSource::Temporary);
	noise.colorArg2 = UINT(NativeMaterialSource::Texture);
	noise.colorArg0 = UINT(NativeMaterialSource::Current);
	shroud.colorOp = NativeMaterialOp::Modulate;
	r.SetMaterialStage(0,atlasBase,atlasUV,layers[0].get());
	r.SetMaterialStage(1,sparkle,atlasUV,layers[1].get());
	r.SetMaterialStage(2,noise,atlasUV,layers[3].get());
	r.SetMaterialStage(3,shroud,atlasUV,layers[3].get());
	REQUIRE(r.BeginFrame(black));
	REQUIRE(r.DrawIndexedTextured(quad,sizeof(quad),sizeof(MaterialVertex),4,16,
		quadIndices,6,D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST,layers[0].get(),12));
	REQUIRE(r.ReadbackFrame(pixels));
	REQUIRE(Pixel(pixels,64,32,32,16,4,0,"water base plus sparkle-noise product then shroud"));
	for (UINT stage=0;stage<4;++stage) r.SetMaterialStage(stage,disabled,coords,nullptr);
	r.SetMaterialEnabled(false);
	const float translated[] = {0.25f,0,0,0, 0,0.25f,0,0, 0,0,1,0, 0.5f,0,0,1};
	struct TreeVertex { float position[3], tree[3]; UINT color; float uv[2]; };
	const TreeVertex treeQuad[] = {
		{{-.75f,-.75f,0},{1,.5f,0},0xffffffff,{0,1}},
		{{-.75f,.75f,0},{1,.5f,-1},0xffffffff,{0,0}},
		{{-.25f,-.75f,0},{1,.5f,0},0xffffffff,{1,1}},
		{{-.25f,.75f,0},{1,.5f,-1},0xffffffff,{1,0}}};
	const float wind[2][4] = {{0,0,0,0},{1,0,0,0}};
	r.SetTreeSway(wind,2);
	REQUIRE(r.BeginFrame(black));
	REQUIRE(r.DrawIndexedTextured(treeQuad,sizeof(treeQuad),sizeof(TreeVertex),4,28,
		quadIndices,6,D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST,white.get(),24));
	REQUIRE(r.ReadbackFrame(pixels));
	REQUIRE(Pixel(pixels,64,21,48,128,128,128,"tree wind anchors lower vertices and applies darkening"));
	REQUIRE(Pixel(pixels,64,43,16,128,128,128,"tree wind displaces canopy on GPU"));
	r.SetTreeSway(nullptr,0);
	REQUIRE(r.BeginFrame(black));
	REQUIRE(r.DrawIndexedTextured(treeQuad,sizeof(treeQuad),sizeof(TreeVertex),4,28,
		quadIndices,6,D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST,white.get(),24));
	REQUIRE(r.ReadbackFrame(pixels));
	REQUIRE(Pixel(pixels,64,43,16,0,0,0,"tree wind state does not leak to following geometry"));
	r.SetWorldViewProjection(translated);
	REQUIRE(r.BeginFrame(black));
	REQUIRE(r.DrawIndexedTextured(quad,sizeof(quad),sizeof(MaterialVertex),4,16,
		quadIndices,6,D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST,white.get(),12));
	REQUIRE(r.ReadbackFrame(pixels));
	REQUIRE(Pixel(pixels,64,48,32,255,255,255,"row-major world transform translation"));
	REQUIRE(Pixel(pixels,64,16,32,0,0,0,"translated geometry does not remain at origin"));
	const float identity[] = {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
	r.SetWorldViewProjection(identity);
	const float fogView[] = {0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,5,1};
	r.SetWorldView(fogView);
	const UINT fogModes[] = {3,1,2};
	const int fogBrightness[] = {128,94,5};
	for (UINT i=0;i<3;++i) {
		r.SetVertexFog(fogModes[i],0,10,i==2 ? .4f : .2f,0xff000000,false);
		REQUIRE(r.BeginFrame(black));
		REQUIRE(r.DrawIndexedTextured(quad,sizeof(quad),sizeof(MaterialVertex),4,16,
			quadIndices,6,D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST,white.get(),12));
		REQUIRE(r.ReadbackFrame(pixels));
		REQUIRE(Pixel(pixels,64,32,32,fogBrightness[i],fogBrightness[i],fogBrightness[i],
			"native vertex fog linear/exp/exp2"));
	}
	r.SetVertexFog(3,0,10,0,0xff0000ff,true);
	REQUIRE(r.BeginFrame(black));
	REQUIRE(r.DrawIndexed(quad,sizeof(quad),sizeof(MaterialVertex),4,quadIndices,6,0,0,
		D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST,12));
	REQUIRE(r.DrawScreenQuad(0,0,16,16,0xffffffff));
	REQUIRE(r.DrawTexturedScreenQuad(48,0,16,16,0,0,1,1,0xffffffff,white.get()));
	REQUIRE(r.ReadbackFrame(pixels));
	REQUIRE(Pixel(pixels,64,32,32,128,128,255,"basic geometry range fog and fog color"));
	REQUIRE(Pixel(pixels,64,8,8,255,255,255,"solid UI bypasses world fog"));
	REQUIRE(Pixel(pixels,64,56,8,255,255,255,"textured UI bypasses world fog"));
	r.SetVertexFog(0,0,0,0,0,false);
	r.SetWorldView(identity);

	std::unique_ptr<NativeD3D12Texture> stripe(r.CreateTexture2D(2,1,1,DXGI_FORMAT_R8G8B8A8_UNORM));
	const unsigned char stripeData[] = {255,0,0,255,0,255,0,255};
	level = {stripeData,8,8};
	REQUIRE(stripe && r.UploadTexture2D(*stripe,&level,1));
	NativeMaterialStage stripeStage;
	stripeStage.colorOp = NativeMaterialOp::Select1;
	NativeMaterialCoordinates stripeUV;
	stripeUV.offset = 16;
	stripeUV.transform = true;
	stripeUV.matrix[0] = -1;
	stripeUV.matrix[8] = 1; // Ordinary two-component UVs carry homogeneous 1 in z.
	r.SetMaterialStage(0,stripeStage,stripeUV,stripe.get());
	NativeMaterialStage stopStage;
	stopStage.colorOp = NativeMaterialOp::Disable;
	r.SetMaterialStage(1,stopStage,coords,nullptr);
	r.SetMaterialEnabled(true);
	REQUIRE(r.BeginFrame(black));
	REQUIRE(r.DrawIndexedTextured(quad,sizeof(quad),sizeof(MaterialVertex),4,16,
		quadIndices,6,D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST,stripe.get(),12));
	REQUIRE(r.ReadbackFrame(pixels));
	REQUIRE(Pixel(pixels,64,16,32,0,255,0,"GPU texture transform translation and reflection"));
	REQUIRE(Pixel(pixels,64,48,32,255,0,0,"GPU transformed UV opposite edge"));
	stripeUV.position = stripeUV.projected = true;
	stripeUV.matrix = {1,0,0,0, 0,0,0,0, 0,0,0,0, 1,0,2,1};
	r.SetMaterialStage(0,stripeStage,stripeUV,stripe.get());
	REQUIRE(r.BeginFrame(black));
	REQUIRE(r.DrawIndexedTextured(quad,sizeof(quad),sizeof(MaterialVertex),4,16,
		quadIndices,6,D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST,stripe.get(),12));
	REQUIRE(r.ReadbackFrame(pixels));
	REQUIRE(Pixel(pixels,64,16,32,255,0,0,"GPU position-generated projected texture coordinates"));
	REQUIRE(Pixel(pixels,64,48,32,0,255,0,"GPU projected texture coordinate divisor"));
	r.SetMaterialEnabled(false);

	// Model submeshes referencing sparse vertices in a shared, mostly unused VB.
	// Index 65535 is a valid vertex (strip-cut is disabled), not a restart marker.
	std::vector<MaterialVertex> sparse(65537);
	const unsigned short sparseStrip[] = {65000,42,65535,17};
	for (UINT i=0;i<4;++i) {
		sparse[sparseStrip[i]+1] = quad[i];
		sparse[sparseStrip[i]+1].color = 0xff4080c0;
	}
	const UINT sparseBytes = static_cast<UINT>(sparse.size()*sizeof(MaterialVertex));
	REQUIRE(r.BeginFrame(black));
	REQUIRE(r.DrawIndexed(sparse.data(),sparseBytes,sizeof(MaterialVertex),65537,
		sparseStrip,4,0,1,D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP,12));
	REQUIRE(r.ReadbackFrame(pixels));
	REQUIRE(Pixel(pixels,64,16,16,64,128,192,"sparse basic indices with nonzero base vertex"));
	REQUIRE(Pixel(pixels,64,48,48,64,128,192,"triangle strip winding and maximum 16-bit index"));
	const unsigned short sparseList[] = {65000,42,65535,65535,42,17};
	REQUIRE(r.BeginFrame(black));
	// A rejected draw must not affect the following valid submission.
	const unsigned short invalid[] = {42,65000,65535};
	REQUIRE(!r.DrawIndexedTextured(sparse.data()+1,sparseBytes-sizeof(MaterialVertex),
		sizeof(MaterialVertex),65001,16,invalid,3,D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST,white.get(),12));
	REQUIRE(r.DrawIndexedTextured(sparse.data()+1,sparseBytes-sizeof(MaterialVertex),
		sizeof(MaterialVertex),65536,16,sparseList,6,D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST,white.get(),12));
	REQUIRE(r.ReadbackFrame(pixels));
	REQUIRE(Pixel(pixels,64,32,32,64,128,192,"sparse textured indices and rejected-draw recovery"));

	// The stencil shadow composite must use its packed vertex color, not white.
	State(r,true);
	REQUIRE(r.BeginFrame(black));
	REQUIRE(r.DrawScreenQuad(0,0,64,64,0xffffffff));
	for (UINT i=0;i<4;++i) sparse[sparseStrip[i]+1].color = 0x80000000;
	REQUIRE(r.DrawIndexed(sparse.data(),sparseBytes,sizeof(MaterialVertex),65537,
		sparseStrip,4,0,1,D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP,12));
	REQUIRE(r.ReadbackFrame(pixels));
	REQUIRE(Pixel(pixels,64,32,32,127,127,127,"packed shadow-composite color"));
	State(r);

	auto residentVB = std::make_unique<NativeD3D12UploadBuffer>(sizeof(quad));
	auto residentIB = std::make_unique<NativeD3D12UploadBuffer>(sizeof(quadIndices));
	void* writable = nullptr;
	REQUIRE(SUCCEEDED(residentVB->Lock(0,sizeof(quad),&writable)));
	std::memcpy(writable,quad,sizeof(quad));
	REQUIRE(SUCCEEDED(residentVB->Unlock()));
	REQUIRE(SUCCEEDED(residentIB->Lock(0,sizeof(quadIndices),&writable)));
	std::memcpy(writable,quadIndices,sizeof(quadIndices));
	REQUIRE(SUCCEEDED(residentIB->Unlock()));
	auto drawResident = [&]() {
		return r.DrawIndexedTextured(residentVB->Data(),sizeof(quad),sizeof(MaterialVertex),4,16,
			static_cast<const unsigned short*>(residentIB->Data()),6,D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
			white.get(),12,residentVB.get(),residentIB.get());
	};
	REQUIRE(r.BeginFrame(black));
	REQUIRE(drawResident());
	REQUIRE(r.ResidentBufferCopiesThisFrame() == 2);
	REQUIRE(drawResident());
	REQUIRE(r.ResidentBufferCopiesThisFrame() == 2); // Same revision, no second copy.
	REQUIRE(r.ReadbackFrame(pixels));
	REQUIRE(Pixel(pixels,64,32,32,255,255,255,"GPU-resident geometry initial copy and reuse"));
	REQUIRE(r.BeginFrame(black));
	REQUIRE(drawResident());
	REQUIRE(r.ResidentBufferCopiesThisFrame() == 0); // Reuse across submitted frames too.
	REQUIRE(r.ReadbackFrame(pixels));
	REQUIRE(Pixel(pixels,64,32,32,255,255,255,"unchanged geometry survives across frames without upload"));
	REQUIRE(r.BeginFrame(black));
	REQUIRE(!r.DrawIndexedTextured(residentVB->Data(),sizeof(quad),sizeof(MaterialVertex),3,16,
		static_cast<const unsigned short*>(residentIB->Data()),6,D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
		white.get(),12,residentVB.get(),residentIB.get())); // Cached max index is still bounds-checked.
	REQUIRE(SUCCEEDED(residentIB->Lock(0,sizeof(quadIndices),&writable)));
	static_cast<unsigned short*>(writable)[0] = 99;
	REQUIRE(SUCCEEDED(residentIB->Unlock()));
	REQUIRE(!drawResident()); // A write must invalidate the previously valid cached range.
	REQUIRE(SUCCEEDED(residentIB->Lock(0,sizeof(quadIndices),&writable)));
	std::memcpy(writable,quadIndices,sizeof(quadIndices));
	REQUIRE(SUCCEEDED(residentIB->Unlock()));
	REQUIRE(drawResident());
	REQUIRE(r.ReadbackFrame(pixels));
	REQUIRE(Pixel(pixels,64,32,32,255,255,255,"cached index range bounds and mutation invalidation"));
	const float leftHalf[] = {0.5f,0,0,0,0,1,0,0,0,0,1,0,-0.5f,0,0,1};
	const float rightHalf[] = {0.5f,0,0,0,0,1,0,0,0,0,1,0,0.5f,0,0,1};
	REQUIRE(r.BeginFrame(black));
	r.SetWorldViewProjection(leftHalf);
	REQUIRE(drawResident());
	REQUIRE(SUCCEEDED(residentVB->Lock(0,sizeof(quad),&writable)));
	for (UINT i=0;i<4;++i) static_cast<MaterialVertex*>(writable)[i].color = 0xff0000ff;
	REQUIRE(SUCCEEDED(residentVB->Unlock()));
	r.SetWorldViewProjection(rightHalf);
	REQUIRE(drawResident());
	REQUIRE(r.ResidentBufferCopiesThisFrame() == 1);
	REQUIRE(r.ReadbackFrame(pixels));
	REQUIRE(Pixel(pixels,64,16,32,255,255,255,"in-frame mutation preserves the earlier GPU buffer version"));
	REQUIRE(Pixel(pixels,64,48,32,0,0,255,"in-frame mutation uploads the new GPU buffer version"));
	// A dynamic arena can be large, but only this tiny draw should be streamed.
	NativeD3D12UploadBuffer streamingVB(1024*1024,true);
	REQUIRE(SUCCEEDED(streamingVB.Lock(0,sizeof(quad),&writable)));
	std::memcpy(writable,quad,sizeof(quad));
	REQUIRE(SUCCEEDED(streamingVB.Unlock()));
	REQUIRE(r.BeginFrame(black));
	r.SetWorldViewProjection(identity);
	REQUIRE(r.DrawIndexedTextured(streamingVB.Data(),sizeof(quad),sizeof(MaterialVertex),4,16,
		static_cast<const unsigned short*>(residentIB->Data()),6,D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST,
		white.get(),12,&streamingVB,residentIB.get()));
	REQUIRE(r.ResidentBufferCopiesThisFrame() == 0);
	REQUIRE(r.ReadbackFrame(pixels));
	REQUIRE(Pixel(pixels,64,32,32,255,255,255,"streaming arena does not copy its full capacity to a resident buffer"));
	// Writes through a still-locked pointer must be captured separately per draw.
	REQUIRE(SUCCEEDED(residentVB->Lock(0,sizeof(quad),&writable)));
	REQUIRE(r.BeginFrame(black));
	r.SetWorldViewProjection(leftHalf);
	REQUIRE(drawResident());
	for (UINT i=0;i<4;++i) static_cast<MaterialVertex*>(writable)[i].color = 0xff00ff00;
	r.SetWorldViewProjection(rightHalf);
	REQUIRE(drawResident());
	REQUIRE(SUCCEEDED(residentVB->Unlock()));
	REQUIRE(r.ReadbackFrame(pixels));
	REQUIRE(Pixel(pixels,64,16,32,0,0,255,"draw inside write lock snapshots the first contents"));
	REQUIRE(Pixel(pixels,64,48,32,0,255,0,"draw inside write lock snapshots later pointer edits"));
	r.SetWorldViewProjection(identity);
	for (UINT frame=0;frame<12;++frame) {
		REQUIRE(SUCCEEDED(residentVB->Lock(0,sizeof(quad),&writable)));
		for (UINT i=0;i<4;++i) static_cast<MaterialVertex*>(writable)[i].color = frame&1 ? 0xff00ff00 : 0xffff0000;
		REQUIRE(SUCCEEDED(residentVB->Unlock()));
		REQUIRE(r.BeginFrame(black));
		REQUIRE(drawResident());
		REQUIRE(r.EndFrame(0));
	}
	REQUIRE(r.BeginFrame(black));
	REQUIRE(drawResident());
	residentVB.reset(); residentIB.reset();
	REQUIRE(r.ReadbackFrame(pixels));
	REQUIRE(Pixel(pixels,64,32,32,0,255,0,"buffer version retirement and owner destruction"));

	// Model lighting operates on original mesh normals, including zero-color
	// skinned vertices. The same GPU constants drive textured and solid meshes.
	struct LitVertex { float position[3], normal[3]; UINT color, secondary; float uv[2]; };
	LitVertex litQuad[] = {
		{{-1,-1,0},{0,0,-1},0,0xff00ff00,{0,1}}, {{-1,1,0},{0,0,-1},0,0xff00ff00,{0,0}},
		{{1,-1,0},{0,0,-1},0,0xff00ff00,{1,1}}, {{1,1,0},{0,0,-1},0,0xff00ff00,{1,0}}};
	r.SetWorldView(identity);
	r.SetWorldViewProjection(identity);
	r.SetMaterialEnabled(false);
	State(r);
	NativeLightingState lighting;
	lighting.flags = {1,0,1,0};
	lighting.globalAmbient = {.25f,0,0,0};
	lighting.emissive = {0,.25f,0,0};
	lighting.diffuse = {0,0,.25f,1};
	lighting.lights[0].position[3] = 3;
	lighting.lights[0].direction = {0,0,1,0};
	lighting.lights[0].diffuse = {1,1,1,1};
	const auto litDraw = [&](bool textured) {
		return textured ? r.DrawIndexedTextured(litQuad,sizeof(litQuad),sizeof(LitVertex),4,32,
			quadIndices,6,D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST,white.get(),24,nullptr,nullptr,12,28) :
			r.DrawIndexed(litQuad,sizeof(litQuad),sizeof(LitVertex),4,quadIndices,6,0,0,
				D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST,24,nullptr,nullptr,12,28);
	};
	for (UINT textured=0;textured<2;++textured) {
		r.SetLighting(lighting);
		REQUIRE(r.BeginFrame(black));
		REQUIRE(litDraw(textured!=0));
		REQUIRE(r.ReadbackFrame(pixels));
		REQUIRE(Pixel(pixels,64,32,32,64,64,64,"ambient + emissive + directional diffuse, zero vertex color"));
	}
	lighting.flags[1] = 1;
	lighting.specular = {.25f,.25f,0,0};
	lighting.parameters[0] = 16;
	lighting.lights[0].specular = {1,1,1,1};
	r.SetLighting(lighting);
	REQUIRE(r.BeginFrame(black));
	REQUIRE(litDraw(true));
	REQUIRE(r.ReadbackFrame(pixels));
	REQUIRE(Pixel(pixels,64,32,32,128,128,64,"specular highlight added after texturing"));
	// UI draws have no mesh-normal layout and must not inherit model lighting.
	REQUIRE(r.BeginFrame(black));
	REQUIRE(r.DrawTexturedScreenQuad(0,0,64,64,0,0,1,1,0xffff0000,white.get()));
	REQUIRE(r.ReadbackFrame(pixels));
	REQUIRE(Pixel(pixels,64,32,32,255,0,0,"screen draw isolated from model lighting"));
	lighting = {};
	lighting.flags = {1,0,1,0};
	lighting.sources[3] = 2;
	r.SetLighting(lighting);
	REQUIRE(r.BeginFrame(black));
	REQUIRE(litDraw(true));
	REQUIRE(r.ReadbackFrame(pixels));
	REQUIRE(Pixel(pixels,64,32,32,0,255,0,"secondary vertex color supplies emissive material"));
	lighting.sources[3] = 0;
	lighting.diffuse = {0,0,1,1};
	lighting.lights[0].position[3] = 3;
	lighting.lights[0].direction = {0,0,1,0};
	lighting.lights[0].diffuse = {1,1,1,1};
	for (auto& vertex : litQuad) { vertex.normal[0]=.70710678f; vertex.normal[2]=-.70710678f; }
	const float nonuniformView[] = {2,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
	r.SetWorldView(nonuniformView);
	r.SetLighting(lighting);
	REQUIRE(r.BeginFrame(black));
	REQUIRE(litDraw(true));
	REQUIRE(r.ReadbackFrame(pixels));
	REQUIRE(Pixel(pixels,64,32,32,0,0,228,"inverse-transpose normal with nonuniform scale"));
	r.SetWorldView(identity);
	lighting = {};
	lighting.flags = {1,0,1,0};
	lighting.lights[0].ambient = {1,0,0,0};
	lighting.lights[0].position = {0,0,-1,1};
	lighting.lights[0].direction[3] = 3;
	lighting.lights[0].attenuation[0] = 2;
	r.SetLighting(lighting);
	REQUIRE(r.BeginFrame(black));
	REQUIRE(litDraw(false));
	REQUIRE(r.ReadbackFrame(pixels));
	REQUIRE(Pixel(pixels,64,32,32,128,0,0,"point light attenuation"));
	lighting.lights[0].direction[3] = 1;
	r.SetLighting(lighting);
	REQUIRE(r.BeginFrame(black));
	REQUIRE(litDraw(false));
	REQUIRE(r.ReadbackFrame(pixels));
	REQUIRE(Pixel(pixels,64,32,32,0,0,0,"point light range cutoff"));
	lighting.lights[0].position = {0,0,-10,2};
	lighting.lights[0].direction = {0,0,1,20};
	lighting.lights[0].cone = {.97f,.9f,0,0};
	lighting.lights[0].attenuation[3] = 1;
	for (UINT outside=0;outside<2;++outside) {
		lighting.lights[0].direction[2] = outside ? -1.f : 1.f;
		r.SetLighting(lighting);
		REQUIRE(r.BeginFrame(black));
		REQUIRE(litDraw(true));
		REQUIRE(r.ReadbackFrame(pixels));
		REQUIRE(Pixel(pixels,64,32,32,outside?0:128,0,0,"spotlight cone inclusion/exclusion"));
	}
	r.SetLighting(NativeLightingState{});
	State(r);

	std::unique_ptr<NativeD3D12Texture> target(r.CreateTexture2D(64,64,1,DXGI_FORMAT_B8G8R8A8_UNORM,true));
	REQUIRE(target && r.BeginFrame(black));
	REQUIRE(r.SetRenderTarget(target.get(),false));
	REQUIRE(r.DrawScreenQuad(0,0,64,64,0xff0000ff));
	REQUIRE(r.SetRenderTarget(nullptr));
	REQUIRE(r.DrawTexturedScreenQuad(0,0,64,64,0,0,1,1,0xffffffff,target.get()));
	target.reset();
	REQUIRE(r.ReadbackFrame(pixels));
	REQUIRE(Pixel(pixels,64,32,32,0,0,255,"BGRA offscreen PSO and render-to-sample barrier"));

	for (UINT i=0;i<4200;++i) {
		std::unique_ptr<NativeD3D12Texture> transient(r.CreateTexture2D(1,1,1,DXGI_FORMAT_R8G8B8A8_UNORM));
		REQUIRE(transient != nullptr);
	}
	std::printf("PASS descriptor recycling beyond heap capacity\n");
	for (UINT frame=0;frame<64;++frame) {
		REQUIRE(r.BeginFrame(black));
		REQUIRE(r.DrawScreenQuad(0,0,64,64,frame&1?0xff00ff00:0xffff0000));
		REQUIRE(r.EndFrame(0));
	}
	REQUIRE(r.BeginFrame(black));
	REQUIRE(r.Resize(80,48)); // Includes retirement of an open frame.
	REQUIRE(r.BeginFrame(black));
	REQUIRE(r.DrawScreenQuad(0,0,80,48,0xffffff00));
	REQUIRE(r.ReadbackFrame(pixels));
	REQUIRE(Pixel(pixels,80,40,24,255,255,0,"frame reuse and resize"));

	if (messages) {
		for (UINT64 i=0;i<messages->GetNumStoredMessages();++i) {
			SIZE_T size=0;
			messages->GetMessage(i,nullptr,&size);
			std::vector<unsigned char> data(size);
			auto* message=reinterpret_cast<D3D12_MESSAGE*>(data.data());
			REQUIRE(SUCCEEDED(messages->GetMessage(i,message,&size)));
			if (message->Severity<=D3D12_MESSAGE_SEVERITY_ERROR) {
				std::fprintf(stderr,"D3D12 ERROR: %s\n",message->pDescription);
				return false;
			}
		}
	}
	messages.Reset();
	r.Shutdown();
	REQUIRE(r.Initialize(window,64,64,true));
	REQUIRE(r.BeginFrame(black));
	REQUIRE(!r.DrawTexturedScreenQuad(0,0,64,64,0,0,1,1,0xffffffff,white.get()));
	State(r);
	REQUIRE(r.DrawScreenQuad(0,0,64,64,0xffff00ff));
	REQUIRE(r.ReadbackFrame(pixels));
	REQUIRE(Pixel(pixels,64,32,32,255,0,255,"reinitialize and reject stale-device textures"));
	return true;
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
	Microsoft::WRL::ComPtr<ID3D12Debug> debug;
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) debug->EnableDebugLayer();
	const wchar_t name[] = L"GeneralsNativeD3D12Smoke";
	WNDCLASSW cls = {};
	cls.lpfnWndProc = TestWindowProc;
	cls.hInstance = instance;
	cls.lpszClassName = name;
	if (!RegisterClassW(&cls)) return 1;
	HWND window = CreateWindowExW(0,name,name,WS_OVERLAPPEDWINDOW,0,0,64,64,nullptr,nullptr,instance,nullptr);
	if (!window) return 2;
	NativeD3D12Renderer renderer;
	const bool success=Run(renderer,window);
	renderer.Shutdown();
	DestroyWindow(window);
	UnregisterClassW(name,instance);
	return success?0:3;
}
