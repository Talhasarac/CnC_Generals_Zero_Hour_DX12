// Round-trip self-check for CompressionManager over every codec it exposes.
// Run the 'compression_selfcheck' target; "OK" per codec means pass.

#include "Compression.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void)
{
	// Compressible-but-not-trivial payload: repeated text with a counter mixed in.
	enum { SRC_LEN = 64 * 1024 };
	static char src[SRC_LEN];
	for (int i = 0; i < SRC_LEN; i++) {
		src[i] = (char)("Command & Conquer Generals Zero Hour "[i % 37] + (i / 1024) % 7);
	}

	// Explicit list: COMPRESSION_MAX==COMPRESSION_REFPACK in the enum, so a
	// MIN..MAX sweep would only cover NONE+REFPACK.  Names are local because
	// getCompressionNameByType's table also stops at COMPRESSION_MAX.
	static const struct { CompressionType type; const char * name; } codecs[] = {
		{ COMPRESSION_REFPACK, "RefPack" }, { COMPRESSION_NOXLZH, "NoxLZH" },
		{ COMPRESSION_ZLIB1,   "ZLib1"   }, { COMPRESSION_ZLIB9,  "ZLib9"  },
		{ COMPRESSION_BTREE,   "BTree"   }, { COMPRESSION_HUFF,   "Huff"   },
	};

	int failures = 0;
	for (int t = 0; t < (int)(sizeof(codecs) / sizeof(codecs[0])); t++) {
		CompressionType type = codecs[t].type;
		const char * name = codecs[t].name;

		int max_out = CompressionManager::getMaxCompressedSize(SRC_LEN, type);
		char * packed = (char *)malloc(max_out);
		int packed_len = CompressionManager::compressData(type, src, SRC_LEN, packed, max_out);
		if (packed_len <= 0) {
			printf("FAIL %-12s compressData returned %d\n", name, packed_len);
			failures++; free(packed); continue;
		}

		static char unpacked[SRC_LEN];
		memset(unpacked, 0, SRC_LEN);
		int unpacked_len = CompressionManager::decompressData(packed, packed_len, unpacked, SRC_LEN);
		if (unpacked_len != SRC_LEN || memcmp(src, unpacked, SRC_LEN) != 0) {
			printf("FAIL %-12s roundtrip: %d bytes back, expected %d\n", name, unpacked_len, SRC_LEN);
			failures++; free(packed); continue;
		}

		printf("OK   %-12s %d -> %d bytes\n", name, SRC_LEN, packed_len);
		free(packed);
	}

	if (failures) { printf("%d codec(s) FAILED\n", failures); return 1; }
	printf("all codecs OK\n");
	return 0;
}
