/*
 * CompressionManager coverage.
 *
 * The existing compression_selfcheck already proves one 64K payload survives
 * every codec.  This goes after the parts around that: header sniffing, the
 * size estimators, the buffer-too-small paths, degenerate payload sizes, and
 * the documented trap that the name tables stop at COMPRESSION_MAX while the
 * enum keeps going.
 */
#include "test_harness.h"
#include "Compression.h"

#include <stdlib.h>

//////////////////////////////////////////////////////////////////////////////
// Helpers
//////////////////////////////////////////////////////////////////////////////

/* Every codec CompressionManager can actually produce.  A MIN..MAX sweep is
   useless here - COMPRESSION_MAX == COMPRESSION_REFPACK even though the enum
   runs on for another twelve entries. */
static const struct { CompressionType type; const char *name; } CODECS[] = {
	{ COMPRESSION_REFPACK, "RefPack" },
	{ COMPRESSION_NOXLZH,  "NoxLZH"  },
	{ COMPRESSION_ZLIB1,   "ZLib1"   },
	{ COMPRESSION_ZLIB2,   "ZLib2"   },
	{ COMPRESSION_ZLIB3,   "ZLib3"   },
	{ COMPRESSION_ZLIB4,   "ZLib4"   },
	{ COMPRESSION_ZLIB5,   "ZLib5"   },
	{ COMPRESSION_ZLIB6,   "ZLib6"   },
	{ COMPRESSION_ZLIB7,   "ZLib7"   },
	{ COMPRESSION_ZLIB8,   "ZLib8"   },
	{ COMPRESSION_ZLIB9,   "ZLib9"   },
	{ COMPRESSION_BTREE,   "BTree"   },
	{ COMPRESSION_HUFF,    "Huff"    },
};
static const int CODEC_COUNT = (int)(sizeof(CODECS) / sizeof(CODECS[0]));

/* Compressible but not trivial - a pure run would let a broken match finder
   pass by accident. */
static void fill_text(char *buf, int len, int variant)
{
	static const char *seed = "Command & Conquer Generals Zero Hour ";
	for (int i = 0; i < len; ++i)
		buf[i] = (char)(seed[i % 37] + ((i / 1024 + variant) % 7));
}

//////////////////////////////////////////////////////////////////////////////
// Header sniffing
//////////////////////////////////////////////////////////////////////////////

TEST(compression_type_from_magic)
{
	struct { const char *magic; CompressionType type; } cases[] = {
		{ "NOX\0", COMPRESSION_NOXLZH  },
		{ "ZL1\0", COMPRESSION_ZLIB1   },
		{ "ZL5\0", COMPRESSION_ZLIB5   },
		{ "ZL9\0", COMPRESSION_ZLIB9   },
		{ "EAB\0", COMPRESSION_BTREE   },
		{ "EAH\0", COMPRESSION_HUFF    },
		{ "EAR\0", COMPRESSION_REFPACK },
	};

	for (int i = 0; i < 7; ++i)
	{
		char header[16];
		memset(header, 0, sizeof(header));
		memcpy(header, cases[i].magic, 4);

		CHECK_EQ((int)CompressionManager::getCompressionType(header, sizeof(header)),
		         (int)cases[i].type);
		CHECK(CompressionManager::isDataCompressed(header, sizeof(header)) != 0);
	}
}

TEST(compression_type_rejects_junk)
{
	char header[16];
	memset(header, 0, sizeof(header));

	memcpy(header, "ZLA\0", 4);
	CHECK_EQ((int)CompressionManager::getCompressionType(header, sizeof(header)),
	         (int)COMPRESSION_NONE);

	memcpy(header, "plain text, not a header", 16);
	CHECK_EQ((int)CompressionManager::getCompressionType(header, sizeof(header)),
	         (int)COMPRESSION_NONE);
	CHECK(!CompressionManager::isDataCompressed(header, sizeof(header)));

	/* The magic is only checked when there is a full 8-byte header to check;
	   anything shorter is uncompressed by definition. */
	memcpy(header, "EAR\0", 4);
	for (int len = 0; len < 8; ++len)
	{
		CHECK_EQ((int)CompressionManager::getCompressionType(header, len),
		         (int)COMPRESSION_NONE);
		CHECK(!CompressionManager::isDataCompressed(header, len));
	}
	CHECK_EQ((int)CompressionManager::getCompressionType(header, 8),
	         (int)COMPRESSION_REFPACK);
}

TEST(compression_preferred_is_refpack)
{
	/* Everything that writes a .sav leans on this; if it ever changes, the
	   corresponding decompress path has to have been verified first. */
	CHECK_EQ((int)CompressionManager::getPreferredCompression(), (int)COMPRESSION_REFPACK);
}

//////////////////////////////////////////////////////////////////////////////
// Size estimators
//////////////////////////////////////////////////////////////////////////////

TEST(max_compressed_size_leaves_room_for_the_header)
{
	for (int c = 0; c < CODEC_COUNT; ++c)
	{
		int est = CompressionManager::getMaxCompressedSize(1024, CODECS[c].type);
		/* Every codec prefixes an 8-byte magic + length header. */
		CHECK(est >= 1024 + 8);
	}

	/* COMPRESSION_NONE has no encoder, so it has no estimate either. */
	CHECK_EQ(CompressionManager::getMaxCompressedSize(1024, COMPRESSION_NONE), 0);
}

TEST(uncompressed_size_reads_the_header_field)
{
	enum { SRC_LEN = 8192 };
	static char src[SRC_LEN];
	fill_text(src, SRC_LEN, 0);

	for (int c = 0; c < CODEC_COUNT; ++c)
	{
		int max_out = CompressionManager::getMaxCompressedSize(SRC_LEN, CODECS[c].type);
		char *packed = (char *)malloc(max_out);

		int packed_len = CompressionManager::compressData(CODECS[c].type, src, SRC_LEN,
		                                                  packed, max_out);
		CHECK(packed_len > 0);
		CHECK_EQ(CompressionManager::getUncompressedSize(packed, packed_len), (int)SRC_LEN);
		free(packed);
	}
}

TEST(uncompressed_size_passes_through_plain_data)
{
	char plain[64];
	memset(plain, 'x', sizeof(plain));

	/* No magic -> the buffer is its own uncompressed form. */
	CHECK_EQ(CompressionManager::getUncompressedSize(plain, sizeof(plain)), (int)sizeof(plain));

	/* Too short to even hold a header. */
	CHECK_EQ(CompressionManager::getUncompressedSize(plain, 4), 4);
	CHECK_EQ(CompressionManager::getUncompressedSize(plain, 0), 0);
}

//////////////////////////////////////////////////////////////////////////////
// Round trips
//////////////////////////////////////////////////////////////////////////////

TEST(every_codec_round_trips_text)
{
	enum { SRC_LEN = 64 * 1024 };
	static char src[SRC_LEN];
	static char back[SRC_LEN];
	fill_text(src, SRC_LEN, 0);

	for (int c = 0; c < CODEC_COUNT; ++c)
	{
		int max_out = CompressionManager::getMaxCompressedSize(SRC_LEN, CODECS[c].type);
		char *packed = (char *)malloc(max_out);

		int packed_len = CompressionManager::compressData(CODECS[c].type, src, SRC_LEN,
		                                                  packed, max_out);
		CHECK(packed_len > 0);
		CHECK(packed_len <= max_out);

		/* The header the encoder stamped has to name the codec back. */
		CHECK_EQ((int)CompressionManager::getCompressionType(packed, packed_len),
		         (int)CODECS[c].type);

		memset(back, 0, SRC_LEN);
		int back_len = CompressionManager::decompressData(packed, packed_len, back, SRC_LEN);
		CHECK_EQ(back_len, (int)SRC_LEN);
		CHECK_MEM(src, back, SRC_LEN);

		free(packed);
	}
}

TEST(every_codec_round_trips_awkward_sizes)
{
	/* 1 byte, either side of the 8-byte header, and a couple of odd sizes.
	   These are where getMaxCompressedSize used to under-estimate: RefPack,
	   BTree and Huff all emit more than uncompressedLen+8 for tiny inputs, so
	   the old estimate handed the encoders a buffer they wrote straight past. */
	static const int SIZES[] = { 1, 2, 3, 4, 7, 8, 9, 63, 64, 65, 1000, 4097 };
	static const int SIZE_COUNT = (int)(sizeof(SIZES) / sizeof(SIZES[0]));

	char src[4097];
	char back[4097];

	for (int s = 0; s < SIZE_COUNT; ++s)
	{
		int len = SIZES[s];
		fill_text(src, len, s);

		for (int c = 0; c < CODEC_COUNT; ++c)
		{
			/* NoxLZH refuses anything under four bytes - see the dedicated
			   test below. */
			if (CODECS[c].type == COMPRESSION_NOXLZH && len < 4)
				continue;

			int max_out = CompressionManager::getMaxCompressedSize(len, CODECS[c].type);
			char *packed = (char *)malloc(max_out);

			int packed_len = CompressionManager::compressData(CODECS[c].type, src, len,
			                                                  packed, max_out);
			CHECK(packed_len > 0);
			CHECK(packed_len <= max_out);

			memset(back, 0, sizeof(back));
			int back_len = CompressionManager::decompressData(packed, packed_len, back, len);
			CHECK_EQ(back_len, len);
			CHECK_MEM(src, back, len);

			free(packed);
		}
	}
}

TEST(noxlzh_refuses_inputs_under_four_bytes)
{
	/* LZHL's block header needs four bytes to describe; below that
	   CompressMemory just returns false and compressData reports 0.  Callers
	   have to be ready to store the payload uncompressed. */
	char src[8];
	char packed[256];
	fill_text(src, sizeof(src), 0);

	for (int len = 1; len < 4; ++len)
		CHECK_EQ(CompressionManager::compressData(COMPRESSION_NOXLZH, src, len,
		                                          packed, sizeof(packed)), 0);

	CHECK(CompressionManager::compressData(COMPRESSION_NOXLZH, src, 4,
	                                       packed, sizeof(packed)) > 0);
}

TEST(max_compressed_size_covers_adversarial_payloads)
{
	/* The estimate is the only bound the EAC encoders get - they take no
	   destination length at all - so it has to hold for payloads that do not
	   compress.  A canary past the estimate catches an encoder that writes
	   further than it promised. */
	enum { SRC_LEN = 32 * 1024, CANARY = 256 };
	static unsigned char src[SRC_LEN];

	unsigned state = 0xdeadbeefu;
	for (int i = 0; i < SRC_LEN; ++i)
	{
		state = state * 1664525u + 1013904223u;
		/* Skewed distribution: the shape that makes Huffman codes longest. */
		src[i] = (unsigned char)(((state >> 24) & 0x0f) ? 0 : (state >> 16));
	}

	for (int c = 0; c < CODEC_COUNT; ++c)
	{
		int max_out = CompressionManager::getMaxCompressedSize(SRC_LEN, CODECS[c].type);
		unsigned char *packed = (unsigned char *)malloc(max_out + CANARY);
		memset(packed + max_out, 0xcd, CANARY);

		int packed_len = CompressionManager::compressData(CODECS[c].type, src, SRC_LEN,
		                                                  packed, max_out);
		CHECK(packed_len > 0);
		CHECK(packed_len <= max_out);

		for (int i = 0; i < CANARY; ++i)
			CHECK_EQ((int)packed[max_out + i], 0xcd);

		free(packed);
	}
}

TEST(every_codec_round_trips_a_pure_run)
{
	enum { SRC_LEN = 16 * 1024 };
	static char src[SRC_LEN];
	static char back[SRC_LEN];
	memset(src, 0, SRC_LEN);

	for (int c = 0; c < CODEC_COUNT; ++c)
	{
		int max_out = CompressionManager::getMaxCompressedSize(SRC_LEN, CODECS[c].type);
		char *packed = (char *)malloc(max_out);

		int packed_len = CompressionManager::compressData(CODECS[c].type, src, SRC_LEN,
		                                                  packed, max_out);
		CHECK(packed_len > 0);
		/* 16K of one byte must actually shrink under every one of these. */
		CHECK(packed_len < SRC_LEN / 4);

		memset(back, 1, SRC_LEN);
		CHECK_EQ(CompressionManager::decompressData(packed, packed_len, back, SRC_LEN),
		         (int)SRC_LEN);
		CHECK_MEM(src, back, SRC_LEN);

		free(packed);
	}
}

TEST(zlib_round_trips_incompressible_noise)
{
	/* Only the ZLib levels publish a real worst-case bound (n*1.1+12+8); the
	   others "guess" at n+8, so noise is not safe to hand them. */
	enum { SRC_LEN = 8192 };
	static unsigned char src[SRC_LEN];
	static unsigned char back[SRC_LEN];

	unsigned state = 0x12345678u;
	for (int i = 0; i < SRC_LEN; ++i)
	{
		state = state * 1664525u + 1013904223u;
		src[i] = (unsigned char)(state >> 24);
	}

	for (int level = COMPRESSION_ZLIB1; level <= COMPRESSION_ZLIB9; ++level)
	{
		CompressionType type = (CompressionType)level;
		int max_out = CompressionManager::getMaxCompressedSize(SRC_LEN, type);
		char *packed = (char *)malloc(max_out);

		int packed_len = CompressionManager::compressData(type, src, SRC_LEN, packed, max_out);
		CHECK(packed_len > 0);
		CHECK(packed_len <= max_out);

		memset(back, 0, SRC_LEN);
		CHECK_EQ(CompressionManager::decompressData(packed, packed_len, back, SRC_LEN),
		         (int)SRC_LEN);
		CHECK_MEM(src, back, SRC_LEN);

		free(packed);
	}
}

//////////////////////////////////////////////////////////////////////////////
// Failure paths
//////////////////////////////////////////////////////////////////////////////

TEST(compress_refuses_a_destination_too_small_for_the_header)
{
	char src[64];
	char dest[64];
	fill_text(src, sizeof(src), 0);

	for (int destLen = 0; destLen < 8; ++destLen)
	{
		for (int c = 0; c < CODEC_COUNT; ++c)
			CHECK_EQ(CompressionManager::compressData(CODECS[c].type, src, sizeof(src),
			                                          dest, destLen), 0);
	}
}

TEST(compress_refuses_an_unknown_codec)
{
	char src[64];
	char dest[256];
	fill_text(src, sizeof(src), 0);

	/* COMPRESSION_NONE has no encoder branch, so it falls through to 0. */
	CHECK_EQ(CompressionManager::compressData(COMPRESSION_NONE, src, sizeof(src),
	                                          dest, sizeof(dest)), 0);
}

TEST(decompress_refuses_short_or_unheadered_input)
{
	char packed[256];
	char dest[256];

	memset(packed, 0, sizeof(packed));
	for (int srcLen = 0; srcLen < 8; ++srcLen)
		CHECK_EQ(CompressionManager::decompressData(packed, srcLen, dest, sizeof(dest)), 0);

	/* A full-length buffer with no magic is not compressed data. */
	memcpy(packed, "just some plain bytes", 21);
	CHECK_EQ(CompressionManager::decompressData(packed, 21, dest, sizeof(dest)), 0);
}

//////////////////////////////////////////////////////////////////////////////
// Name tables
//////////////////////////////////////////////////////////////////////////////

TEST(name_tables_stop_at_compression_max)
{
	/* Both name tables are sized [COMPRESSION_MAX+1] with the rest of the
	   entries commented out, while the enum continues well past MAX.  Indexing
	   them with anything above COMPRESSION_MAX reads off the end - pinned here
	   so the bound is documented rather than rediscovered. */
	CHECK_EQ((int)COMPRESSION_MAX, (int)COMPRESSION_REFPACK);

	CHECK_STR(CompressionManager::getCompressionNameByType(COMPRESSION_NONE), "No compression");
	CHECK_STR(CompressionManager::getCompressionNameByType(COMPRESSION_REFPACK), "RefPack");

	CHECK_STR(CompressionManager::getDecompressionNameByType(COMPRESSION_NONE), "d_None");
	CHECK_STR(CompressionManager::getDecompressionNameByType(COMPRESSION_REFPACK), "d_RefPack");
}
