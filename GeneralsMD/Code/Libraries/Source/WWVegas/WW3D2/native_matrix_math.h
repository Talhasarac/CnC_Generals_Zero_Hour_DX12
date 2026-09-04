/*
** Native matrix helpers used by renderer-side code that historically called
** the D3DX8 math library.  These are DirectXMath value operations only; they
** do not create a device, translate graphics calls, or depend on D3DX.
*/
#pragma once

#include <DirectXMath.h>
#include <d3dx8math.h>
#include <cstring>

namespace NativeMatrixMath
{
	inline DirectX::XMMATRIX Load(const D3DXMATRIX* source)
	{
		DirectX::XMFLOAT4X4 value;
		std::memcpy(&value, source, sizeof(value));
		return DirectX::XMLoadFloat4x4(&value);
	}

	inline void Store(D3DXMATRIX* destination, DirectX::FXMMATRIX value)
	{
		DirectX::XMFLOAT4X4 result;
		DirectX::XMStoreFloat4x4(&result, value);
		std::memcpy(destination, &result, sizeof(result));
	}

	inline void Multiply(D3DXMATRIX* destination, const D3DXMATRIX* left,
		const D3DXMATRIX* right)
	{
		Store(destination, DirectX::XMMatrixMultiply(Load(left), Load(right)));
	}

	inline D3DXMATRIX MultiplyValue(const D3DXMATRIX& left,
		const D3DXMATRIX& right)
	{
		D3DXMATRIX result;
		Multiply(&result, &left, &right);
		return result;
	}

	inline void Transpose(D3DXMATRIX* destination, const D3DXMATRIX* source)
	{
		Store(destination, DirectX::XMMatrixTranspose(Load(source)));
	}

	inline void Inverse(D3DXMATRIX* destination, float* determinant,
		const D3DXMATRIX* source)
	{
		DirectX::XMVECTOR det;
		Store(destination, DirectX::XMMatrixInverse(&det, Load(source)));
		if (determinant != nullptr)
			*determinant = DirectX::XMVectorGetX(det);
	}

	inline void Identity(D3DXMATRIX* destination)
	{
		Store(destination, DirectX::XMMatrixIdentity());
	}

	inline void Scaling(D3DXMATRIX* destination, float x, float y, float z)
	{
		Store(destination, DirectX::XMMatrixScaling(x, y, z));
	}

	inline void Translation(D3DXMATRIX* destination, float x, float y, float z)
	{
		Store(destination, DirectX::XMMatrixTranslation(x, y, z));
	}

	inline void Transform(D3DXVECTOR4* destination, const D3DXVECTOR4* source,
		const D3DXMATRIX* matrix)
	{
		DirectX::XMFLOAT4 input(source->x, source->y, source->z, source->w);
		DirectX::XMFLOAT4 output;
		DirectX::XMStoreFloat4(&output,
			DirectX::XMVector4Transform(DirectX::XMLoadFloat4(&input), Load(matrix)));
		destination->x = output.x;
		destination->y = output.y;
		destination->z = output.z;
		destination->w = output.w;
	}
}
