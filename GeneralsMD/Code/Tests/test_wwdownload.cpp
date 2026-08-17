/*
 * Tests for the wwdownload support library.
 *
 * Scope: the parts that run without a network.  Registry.cpp is fully covered
 * (it only needs a scratch key), urlBuilder.cpp is covered structurally, and
 * Prepare_Directories out of FTP.CPP is covered against a scratch directory.
 *
 * Deliberately NOT covered: CDownload and Cftp.  Both are pure FTP state
 * machines - DownloadFile/Abort/PumpMessages/ConnectToServer/... all either
 * touch a socket or only mutate members that no accessor exposes.  Cftp's one
 * pure helper, GetDownloadFilename, is private with no friend hook.
 *
 * The registry tests never touch the real
 *   SOFTWARE\Electronic Arts\EA Games\Command and Conquer Generals Zero Hour
 * key.  Everything is written under HKCU\Software\CnCGeneralsZH_CMakeTests and
 * deleted again; only the four capitalised wrappers see the real path, and
 * those are exercised read-only against a subkey that cannot exist.
 *
 * Tests named *_DEFECT_* pin behaviour that is wrong but left alone, so a
 * future fix trips the test instead of going unnoticed.
 */
#include "test_harness.h"

#include <string>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "Registry.h"
#include "urlBuilder.h"

/* registry.cpp defines these with external linkage but Registry.h only declares
   the four capitalised wrappers built on top of them. */
bool getStringFromRegistry(HKEY root, std::string path, std::string key, std::string& val);
bool getUnsignedIntFromRegistry(HKEY root, std::string path, std::string key, unsigned int& val);
bool setStringInRegistry(HKEY root, std::string path, std::string key, std::string val);
bool setUnsignedIntInRegistry(HKEY root, std::string path, std::string key, unsigned int val);

/* FTP.CPP declares this at file scope only. */
bool Prepare_Directories(const char *rootdir, const char *filename);


///////////////////////////////////////////////////////////////////////////////
//	Scratch registry key
///////////////////////////////////////////////////////////////////////////////

static const char *SCRATCH = "Software\\CnCGeneralsZH_CMakeTests";

static void scrub(void)
{
	RegDeleteKeyA(HKEY_CURRENT_USER, SCRATCH);
}

/* Writes a value of an arbitrary type and length, so the tests can hand the
   getters the kind of data a real registry can hold but setStringInRegistry
   would never produce. */
static bool raw_set(const char *key, unsigned long type, const void *data, unsigned long len)
{
	HKEY handle;
	if (RegCreateKeyExA(HKEY_CURRENT_USER, SCRATCH, 0, NULL, REG_OPTION_NON_VOLATILE,
	                    KEY_WRITE, NULL, &handle, NULL) != ERROR_SUCCESS) {
		return false;
	}

	LONG result = RegSetValueExA(handle, key, 0, type, (const BYTE *)data, len);
	RegCloseKey(handle);
	return result == ERROR_SUCCESS;
}


///////////////////////////////////////////////////////////////////////////////
//	registry.cpp - round trips
///////////////////////////////////////////////////////////////////////////////

TEST(registry_string_round_trip)
{
	scrub();

	CHECK(setStringInRegistry(HKEY_CURRENT_USER, SCRATCH, "Greeting", "hello world"));

	std::string val = "untouched";
	CHECK(getStringFromRegistry(HKEY_CURRENT_USER, SCRATCH, "Greeting", val));
	CHECK_STR(val.c_str(), "hello world");

	/* A second set overwrites rather than appending a second value. */
	CHECK(setStringInRegistry(HKEY_CURRENT_USER, SCRATCH, "Greeting", "goodbye"));
	CHECK(getStringFromRegistry(HKEY_CURRENT_USER, SCRATCH, "Greeting", val));
	CHECK_STR(val.c_str(), "goodbye");

	scrub();
}

TEST(registry_uint_round_trip)
{
	scrub();

	static const unsigned int values[] = { 0u, 1u, 42u, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu };

	for (int i = 0; i < int(sizeof(values)/sizeof(values[0])); ++i) {
		CHECK(setUnsignedIntInRegistry(HKEY_CURRENT_USER, SCRATCH, "Version", values[i]));

		unsigned int val = 0xDEADBEEFu;
		CHECK(getUnsignedIntFromRegistry(HKEY_CURRENT_USER, SCRATCH, "Version", val));
		CHECK_EQ(val, values[i]);
	}

	scrub();
}

TEST(registry_set_creates_the_key)
{
	scrub();

	/* Nothing exists yet, so this proves setStringInRegistry creates the key and
	   not just the value. */
	std::string val;
	CHECK(!getStringFromRegistry(HKEY_CURRENT_USER, SCRATCH, "Fresh", val));

	CHECK(setStringInRegistry(HKEY_CURRENT_USER, SCRATCH, "Fresh", "made"));
	CHECK(getStringFromRegistry(HKEY_CURRENT_USER, SCRATCH, "Fresh", val));
	CHECK_STR(val.c_str(), "made");

	scrub();
}

TEST(registry_multiple_values_are_independent)
{
	scrub();

	CHECK(setStringInRegistry(HKEY_CURRENT_USER, SCRATCH, "Language", "german"));
	CHECK(setUnsignedIntInRegistry(HKEY_CURRENT_USER, SCRATCH, "Version", 7));
	CHECK(setStringInRegistry(HKEY_CURRENT_USER, SCRATCH, "BaseURL", "http://x/"));

	std::string s;
	unsigned int u = 0;
	CHECK(getStringFromRegistry(HKEY_CURRENT_USER, SCRATCH, "Language", s));
	CHECK_STR(s.c_str(), "german");
	CHECK(getUnsignedIntFromRegistry(HKEY_CURRENT_USER, SCRATCH, "Version", u));
	CHECK_EQ(u, 7u);
	CHECK(getStringFromRegistry(HKEY_CURRENT_USER, SCRATCH, "BaseURL", s));
	CHECK_STR(s.c_str(), "http://x/");

	scrub();
}


///////////////////////////////////////////////////////////////////////////////
//	registry.cpp - failure paths
///////////////////////////////////////////////////////////////////////////////

TEST(registry_missing_key_fails_and_leaves_val_alone)
{
	scrub();

	std::string s = "sentinel";
	CHECK(!getStringFromRegistry(HKEY_CURRENT_USER, "Software\\CnCGeneralsZH_NoSuchKeyEver", "x", s));
	CHECK_STR(s.c_str(), "sentinel");

	unsigned int u = 12345u;
	CHECK(!getUnsignedIntFromRegistry(HKEY_CURRENT_USER, "Software\\CnCGeneralsZH_NoSuchKeyEver", "x", u));
	CHECK_EQ(u, 12345u);
}

TEST(registry_missing_value_fails_and_leaves_val_alone)
{
	scrub();

	CHECK(setStringInRegistry(HKEY_CURRENT_USER, SCRATCH, "Present", "yes"));

	std::string s = "sentinel";
	CHECK(!getStringFromRegistry(HKEY_CURRENT_USER, SCRATCH, "Absent", s));
	CHECK_STR(s.c_str(), "sentinel");

	unsigned int u = 12345u;
	CHECK(!getUnsignedIntFromRegistry(HKEY_CURRENT_USER, SCRATCH, "Absent", u));
	CHECK_EQ(u, 12345u);

	scrub();
}

TEST(registry_string_write_to_hklm_is_denied_for_a_normal_user)
{
	/* Not an assertion about the machine's ACLs - just that the low-level setter
	   reports the failure instead of claiming success.  HKLM\SOFTWARE requires
	   elevation; if this process happens to be elevated the write succeeds and
	   the key is cleaned up either way. */
	bool ok = setStringInRegistry(HKEY_LOCAL_MACHINE, "Software\\CnCGeneralsZH_CMakeTests", "x", "y");

	std::string s;
	bool readable = getStringFromRegistry(HKEY_LOCAL_MACHINE, "Software\\CnCGeneralsZH_CMakeTests", "x", s);
	CHECK_EQ(ok, readable);

	if (ok) {
		CHECK_STR(s.c_str(), "y");
		RegDeleteKeyA(HKEY_LOCAL_MACHINE, "Software\\CnCGeneralsZH_CMakeTests");
	}
}


///////////////////////////////////////////////////////////////////////////////
//	registry.cpp - buffer edges
///////////////////////////////////////////////////////////////////////////////

TEST(registry_255_char_string_is_the_longest_that_fits)
{
	scrub();

	/* getStringFromRegistry hands RegQueryValueEx a 256 byte buffer, so 255
	   characters plus the terminator is exactly the limit. */
	std::string longest(255, 'L');
	CHECK(setStringInRegistry(HKEY_CURRENT_USER, SCRATCH, "Long", longest));

	std::string val;
	CHECK(getStringFromRegistry(HKEY_CURRENT_USER, SCRATCH, "Long", val));
	CHECK_EQ(val.length(), size_t(255));
	CHECK_STR(val.c_str(), longest.c_str());

	/* One more character needs 257 bytes -> ERROR_MORE_DATA -> false, and the
	   caller's string is left alone rather than half filled. */
	std::string toolong(256, 'L');
	CHECK(setStringInRegistry(HKEY_CURRENT_USER, SCRATCH, "TooLong", toolong));

	std::string untouched = "sentinel";
	CHECK(!getStringFromRegistry(HKEY_CURRENT_USER, SCRATCH, "TooLong", untouched));
	CHECK_STR(untouched.c_str(), "sentinel");

	scrub();
}

TEST(registry_empty_value_reads_as_empty_string)
{
	scrub();

	/* Zero length REG_SZ: RegQueryValueEx succeeds without writing a single byte.
	   Before the buffer was zero initialised this handed back uninitialised
	   stack. */
	CHECK(raw_set("Empty", REG_SZ, "", 0));

	std::string val = "sentinel";
	CHECK(getStringFromRegistry(HKEY_CURRENT_USER, SCRATCH, "Empty", val));
	CHECK_EQ(val.length(), size_t(0));

	scrub();
}

TEST(registry_unterminated_data_is_truncated_not_overread)
{
	scrub();

	/* 256 bytes of 'A' with no NUL anywhere: the value fills the buffer exactly
	   and the terminator has to be forced in, or the std::string runs off the
	   end of the stack array. */
	unsigned char blob[256];
	memset(blob, 'A', sizeof(blob));
	CHECK(raw_set("Blob", REG_BINARY, blob, sizeof(blob)));

	std::string val;
	CHECK(getStringFromRegistry(HKEY_CURRENT_USER, SCRATCH, "Blob", val));
	CHECK_EQ(val.length(), size_t(255));
	CHECK_EQ(val.find_first_not_of('A'), std::string::npos);

	scrub();
}

TEST(registry_short_unterminated_data_stops_at_its_own_length)
{
	scrub();

	/* Same idea below the buffer size: only the bytes the registry supplied may
	   show up, the rest of the buffer must read as zero. */
	CHECK(raw_set("Short", REG_BINARY, "AB", 2));

	std::string val;
	CHECK(getStringFromRegistry(HKEY_CURRENT_USER, SCRATCH, "Short", val));
	CHECK_STR(val.c_str(), "AB");

	scrub();
}


///////////////////////////////////////////////////////////////////////////////
//	registry.cpp - the getters ignore the value type
///////////////////////////////////////////////////////////////////////////////

TEST(registry_DEFECT_getters_ignore_the_value_type)
{
	scrub();

	/* Both getters pass &type to RegQueryValueEx and then never look at it, so a
	   value of the wrong type is happily reinterpreted.  Nothing in the game
	   depends on the confusion, so this is pinned rather than fixed. */

	/* REG_DWORD read as a string: four bytes, no terminator of its own. */
	CHECK(setUnsignedIntInRegistry(HKEY_CURRENT_USER, SCRATCH, "Number", 0x41414141u));
	std::string as_string;
	CHECK(getStringFromRegistry(HKEY_CURRENT_USER, SCRATCH, "Number", as_string));
	CHECK_STR(as_string.c_str(), "AAAA");

	/* REG_SZ read as a DWORD: "abc" is exactly four bytes with its terminator,
	   so the whole word is defined and comes back little endian. */
	CHECK(setStringInRegistry(HKEY_CURRENT_USER, SCRATCH, "Text", "abc"));
	unsigned int as_uint = 0;
	CHECK(getUnsignedIntFromRegistry(HKEY_CURRENT_USER, SCRATCH, "Text", as_uint));
	CHECK_EQ(as_uint, 0x00636261u);

	scrub();
}

TEST(registry_uint_read_of_a_long_string_fails)
{
	scrub();

	/* More than four bytes does not fit the DWORD buffer -> ERROR_MORE_DATA. */
	CHECK(setStringInRegistry(HKEY_CURRENT_USER, SCRATCH, "Text", "abcdefgh"));

	unsigned int val = 999u;
	CHECK(!getUnsignedIntFromRegistry(HKEY_CURRENT_USER, SCRATCH, "Text", val));
	CHECK_EQ(val, 999u);

	scrub();
}


///////////////////////////////////////////////////////////////////////////////
//	registry.cpp - the capitalised wrappers
///////////////////////////////////////////////////////////////////////////////

TEST(registry_wrappers_append_the_path_and_report_misses)
{
	/* Read only on purpose: the wrappers hard code the real game key and this
	   test must not write there.  A subkey that cannot exist proves the path is
	   appended (an unappended lookup would hit the real key and could succeed). */
	std::string s = "sentinel";
	CHECK(!GetStringFromRegistry("\\NoSuchSubkeyForCMakeTests", "BaseURL", s));
	CHECK_STR(s.c_str(), "sentinel");

	unsigned int u = 4242u;
	CHECK(!GetUnsignedIntFromRegistry("\\NoSuchSubkeyForCMakeTests", "Version", u));
	CHECK_EQ(u, 4242u);
}


///////////////////////////////////////////////////////////////////////////////
//	urlBuilder.cpp
///////////////////////////////////////////////////////////////////////////////

/* The four URLs are built from whatever the machine's real EA registry key
   holds, so only the structure is assertable, not the contents. */
TEST(urlbuilder_shapes)
{
	std::string game, maps, config, motd;
	FormatURLFromRegistry(game, maps, config, motd);

	/* Every URL is <baseURL><something>.txt, and configURL is the one with a
	   fixed tail - so it pins the base the other three must share. */
	CHECK(config.length() > strlen("config.txt"));
	std::string base = config.substr(0, config.length() - strlen("config.txt"));
	CHECK_EQ(config.compare(base.length(), std::string::npos, "config.txt"), 0);

	CHECK_EQ(game.compare(0, base.length(), base), 0);
	CHECK_EQ(maps.compare(0, base.length(), base), 0);
	CHECK_EQ(motd.compare(0, base.length(), base), 0);

	CHECK_EQ(game.compare(game.length()-4, 4, ".txt"), 0);
	CHECK_EQ(maps.compare(maps.length()-4, 4, ".txt"), 0);
	CHECK_EQ(motd.compare(motd.length()-4, 4, ".txt"), 0);

	/* maps-<version>.txt and MOTD-<language>.txt are literal prefixes. */
	CHECK_EQ(maps.compare(base.length(), 5, "maps-"), 0);
	CHECK_EQ(motd.compare(base.length(), 5, "MOTD-"), 0);

	/* The game patch URL is <language>-<version>.txt, and the language is the
	   same one the MOTD URL carries. */
	std::string language = motd.substr(base.length() + 5);
	language.erase(language.length() - 4);		/* drop ".txt" */
	CHECK(language.length() > 0);
	CHECK_EQ(game.compare(base.length(), language.length(), language), 0);
	CHECK_EQ(game[base.length() + language.length()], '-');

	/* The version tails are decimal. */
	std::string version = game.substr(base.length() + language.length() + 1);
	version.erase(version.length() - 4);
	CHECK(version.length() > 0);
	CHECK_EQ(version.find_first_not_of("0123456789"), std::string::npos);

	std::string mapversion = maps.substr(base.length() + 5);
	mapversion.erase(mapversion.length() - 4);
	CHECK(mapversion.length() > 0);
	CHECK_EQ(mapversion.find_first_not_of("0123456789"), std::string::npos);

	/* _snprintf(buf, 256, ...) is the ceiling on every one of them. */
	CHECK(game.length() < 256);
	CHECK(maps.length() < 256);
	CHECK(config.length() < 256);
	CHECK(motd.length() < 256);
}

TEST(urlbuilder_is_deterministic)
{
	std::string a1, a2, a3, a4;
	std::string b1, b2, b3, b4;

	FormatURLFromRegistry(a1, a2, a3, a4);
	FormatURLFromRegistry(b1, b2, b3, b4);

	CHECK_STR(a1.c_str(), b1.c_str());
	CHECK_STR(a2.c_str(), b2.c_str());
	CHECK_STR(a3.c_str(), b3.c_str());
	CHECK_STR(a4.c_str(), b4.c_str());
}

TEST(urlbuilder_overwrites_whatever_it_was_handed)
{
	std::string game = "stale", maps = "stale", config = "stale", motd = "stale";
	FormatURLFromRegistry(game, maps, config, motd);

	CHECK(game != "stale");
	CHECK(maps != "stale");
	CHECK(config != "stale");
	CHECK(motd != "stale");
}


///////////////////////////////////////////////////////////////////////////////
//	FTP.CPP - Prepare_Directories
///////////////////////////////////////////////////////////////////////////////

static std::string scratch_dir(void)
{
	char temp[MAX_PATH];
	GetTempPathA(sizeof(temp), temp);

	std::string root = temp;
	root += "wwdl_prepdirs_test";
	return root;
}

static void remove_scratch_dirs(const std::string &root)
{
	RemoveDirectoryA((root + "\\a\\b").c_str());
	RemoveDirectoryA((root + "\\a").c_str());
	RemoveDirectoryA((root + "\\solo").c_str());
	RemoveDirectoryA(root.c_str());
}

static bool is_dir(const std::string &path)
{
	DWORD attr = GetFileAttributesA(path.c_str());
	return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

TEST(prepare_directories_creates_every_level)
{
	std::string root = scratch_dir();
	remove_scratch_dirs(root);
	CHECK(CreateDirectoryA(root.c_str(), NULL) != 0);

	CHECK(Prepare_Directories(root.c_str(), "a\\b\\patch.bin"));
	CHECK(is_dir(root + "\\a"));
	CHECK(is_dir(root + "\\a\\b"));

	/* The file itself is never created - only the directories leading to it. */
	CHECK(!is_dir(root + "\\a\\b\\patch.bin"));
	CHECK_EQ(GetFileAttributesA((root + "\\a\\b\\patch.bin").c_str()), DWORD(INVALID_FILE_ATTRIBUTES));

	remove_scratch_dirs(root);
}

TEST(prepare_directories_is_idempotent)
{
	std::string root = scratch_dir();
	remove_scratch_dirs(root);
	CHECK(CreateDirectoryA(root.c_str(), NULL) != 0);

	CHECK(Prepare_Directories(root.c_str(), "a\\b\\one.bin"));
	/* Second file into the same tree: every CreateDirectory now fails with
	   ERROR_ALREADY_EXISTS, which used to be reported as an error. */
	CHECK(Prepare_Directories(root.c_str(), "a\\b\\two.bin"));
	CHECK(Prepare_Directories(root.c_str(), "a\\b\\one.bin"));
	CHECK(is_dir(root + "\\a\\b"));

	remove_scratch_dirs(root);
}

TEST(prepare_directories_flat_name_creates_nothing)
{
	std::string root = scratch_dir();
	remove_scratch_dirs(root);
	CHECK(CreateDirectoryA(root.c_str(), NULL) != 0);

	/* No backslash means no loop iterations at all. */
	CHECK(Prepare_Directories(root.c_str(), "patch.bin"));
	CHECK(!is_dir(root + "\\patch.bin"));

	CHECK(Prepare_Directories(root.c_str(), ""));

	remove_scratch_dirs(root);
}

TEST(prepare_directories_single_level)
{
	std::string root = scratch_dir();
	remove_scratch_dirs(root);
	CHECK(CreateDirectoryA(root.c_str(), NULL) != 0);

	CHECK(Prepare_Directories(root.c_str(), "solo\\patch.bin"));
	CHECK(is_dir(root + "\\solo"));

	remove_scratch_dirs(root);
}

TEST(prepare_directories_trailing_separator_creates_the_whole_path)
{
	std::string root = scratch_dir();
	remove_scratch_dirs(root);
	CHECK(CreateDirectoryA(root.c_str(), NULL) != 0);

	/* A name ending in a separator has no file part, so the last component is
	   treated as a directory too. */
	CHECK(Prepare_Directories(root.c_str(), "a\\b\\"));
	CHECK(is_dir(root + "\\a\\b"));

	remove_scratch_dirs(root);
}

TEST(prepare_directories_unreachable_root_fails)
{
	/* A drive that cannot exist: CreateDirectory fails with something other than
	   ERROR_ALREADY_EXISTS, so the failure has to propagate. */
	CHECK(!Prepare_Directories("\\\\?\\Q:\\no_such_volume_for_tests", "a\\patch.bin"));

	/* ...but only when there is a directory to create at all. */
	CHECK(Prepare_Directories("\\\\?\\Q:\\no_such_volume_for_tests", "patch.bin"));
}
