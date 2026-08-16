// Self-check for Matrix3D::Get_Inverse, which was reimplemented when the D3DX8
// dependency was dropped.  Run the 'wwmath_selfcheck' target; "OK" means pass.

#include "matrix3d.h"
#include <assert.h>
#include <stdio.h>

static void check_identity(const Matrix3D & m, const char * what)
{
	for (int r = 0; r < 3; r++) {
		for (int c = 0; c < 4; c++) {
			float expected = (r == c) ? 1.0f : 0.0f;
			float diff = m[r][c] - expected;
			if (diff < 0.0f) diff = -diff;
			if (diff > 0.0001f) {
				printf("FAIL %s: [%d][%d] = %f, expected %f\n", what, r, c, m[r][c], expected);
				assert(0);
			}
		}
	}
}

static void check_roundtrip(const Matrix3D & m, const char * what)
{
	Matrix3D inv, product;
	m.Get_Inverse(inv);

	Matrix3D::Multiply(m, inv, &product);
	check_identity(product, what);

	Matrix3D::Multiply(inv, m, &product);
	check_identity(product, what);
}

int main(void)
{
	// Rotation about an arbitrary axis plus a translation - the general affine case.
	Matrix3D rot(Vector3(0.3f, -0.7f, 0.65f), 1.1f);
	rot.Set_Translation(Vector3(12.0f, -4.5f, 3.25f));
	check_roundtrip(rot, "rotate+translate");

	// Non-uniform scale: the orthogonal-inverse shortcut gets this one wrong.
	Matrix3D scale(Vector3(2.0f, 0.0f, 0.0f),
	               Vector3(0.0f, 0.5f, 0.0f),
	               Vector3(0.0f, 0.0f, 4.0f),
	               Vector3(1.0f, 2.0f, 3.0f));
	check_roundtrip(scale, "scale+translate");

	printf("Matrix3D::Get_Inverse OK\n");
	return 0;
}
