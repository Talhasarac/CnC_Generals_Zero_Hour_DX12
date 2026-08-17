/*
 * Tests for the debug library.
 *
 * The module is a process-wide singleton wired up from the CRT static
 * initialiser table, so there is nothing to construct: linking it is the setup.
 * Two extension points make the whole thing observable from a test:
 *
 *   - a DebugIOInterface registered with Debug::AddIOFactory captures every
 *     byte the module writes,
 *   - a DebugCmdInterface registered with Debug::AddCommands is handed the
 *     Debug& itself, which is the only way to reach the output stream in a
 *     Release build (DLOG/DASSERT compile to nothing without HAS_LOGS).
 *
 * DebugGetDefaultCommands lives in its own translation unit precisely so a host
 * program can override it; this file does, which both wires the capture class
 * up at startup and exercises the empty-line handling of the loop that consumes
 * it.  The stock implementation therefore cannot be tested here - it returns
 * the literal "!debug.io flat add" and has no logic.
 *
 * Deliberately NOT covered: the con/net/ods/flat I/O classes (window, named
 * pipe, OutputDebugString, log files), DebugStackwalk (needs dbghelp and real
 * frames), the exception filter, and debug.exit (it calls exit(1)).
 */
#include "test_harness.h"

#include <string>
#include <vector>
#include <stdlib.h>

#include "debug.h"


///////////////////////////////////////////////////////////////////////////////
//	Capture I/O class
///////////////////////////////////////////////////////////////////////////////

static std::string captured;
static std::string captured_source;
static int captured_type = -1;
static int flush_signals = 0;

class CaptureIO : public DebugIOInterface
{
public:
	static DebugIOInterface *Create(void) { return new CaptureIO; }

	virtual int Read(char * /*buf*/, int /*maxchar*/) { return 0; }

	virtual void Write(StringType type, const char *src, const char *str)
	{
		if (!str) {
			/* NULL means "flush what you have", not "write nothing". */
			++flush_signals;
			return;
		}
		captured += str;
		captured_source = src ? src : "";
		captured_type = int(type);
	}

	virtual void EmergencyFlush(void) {}
	virtual void Execute(Debug & /*dbg*/, const char * /*cmd*/, bool /*structured*/,
	                     unsigned /*argn*/, const char * const * /*argv*/) {}
	virtual void Delete(void) { delete this; }
};


///////////////////////////////////////////////////////////////////////////////
//	Test command group
///////////////////////////////////////////////////////////////////////////////

static std::string last_cmd;
static int last_mode = -1;
static std::vector<std::string> last_args;
static int execute_calls = 0;
static int startup_calls = 0;

class TestCmds : public DebugCmdInterface
{
public:
	virtual bool Execute(Debug &dbg, const char *cmd, CommandMode cmdmode,
	                     unsigned argn, const char * const *argv);
	virtual void Delete(void) { delete this; }
};

bool TestCmds::Execute(Debug &dbg, const char *cmd, CommandMode cmdmode,
                       unsigned argn, const char * const *argv)
{
	++execute_calls;
	last_cmd = cmd;
	last_mode = int(cmdmode);
	last_args.clear();
	for (unsigned i = 0; i < argn; ++i) {
		last_args.push_back(argv[i]);
	}

	if (!strcmp(cmd, "startup")) {
		++startup_calls;
		return true;
	}

	if (!strcmp(cmd, "noop")) {
		return true;
	}

	if (!strcmp(cmd, "echo")) {
		for (unsigned i = 0; i < argn; ++i) {
			if (i) dbg << "|";
			dbg << argv[i];
		}
		return true;
	}

	if (!strcmp(cmd, "repeat")) {
		Debug::RepeatChar rc('=', argn ? atoi(argv[0]) : 0);
		dbg << rc;
		return true;
	}

	if (!strcmp(cmd, "fmt")) {
		Debug::Format f("[%s:%d]", argn ? argv[0] : "", argn > 1 ? atoi(argv[1]) : 0);
		dbg << f;
		return true;
	}

	if (!strcmp(cmd, "fmtlong")) {
		/* 600 characters through a 512 byte buffer. */
		std::string huge(600, 'F');
		Debug::Format f("%s", huge.c_str());
		dbg << f;
		return true;
	}

	if (!strcmp(cmd, "nul")) {
		dbg << (const char *)0;
		return true;
	}

	if (!strcmp(cmd, "empty")) {
		/* An empty string returns early, so it must not consume the width. */
		Debug::Width w(4);
		dbg << w << "" << "ab";
		return true;
	}

	if (!strcmp(cmd, "ints")) {
		dbg << int(-42) << " " << unsigned(4000000000u) << " " << long(-7L)
		    << " " << (unsigned long)(0xFFFFFFFFul) << " " << short(-3)
		    << " " << (unsigned short)(65535) << " " << (__int64)(-5000000000i64)
		    << " " << (unsigned __int64)(10000000000ui64) << " " << true << " " << false;
		return true;
	}

	if (!strcmp(cmd, "radix")) {
		Debug::Hex hex;
		Debug::Dec dec;
		Debug::Bin bin;
		dbg << hex << 255 << " " << bin << 5 << " " << dec << 255;
		return true;
	}

	if (!strcmp(cmd, "radix_sticky")) {
		/* The radix is module state, so it survives across insertions. */
		Debug::Hex hex;
		dbg << hex << 255 << " " << 16;
		return true;
	}

	if (!strcmp(cmd, "width")) {
		Debug::Width w(6);
		Debug::Width w2(6);
		dbg << w << "ab" << "cd" << w2 << "ef";
		return true;
	}

	if (!strcmp(cmd, "fill")) {
		Debug::FillChar f('0');
		Debug::FillChar back(' ');
		Debug::Width w(5);
		dbg << f << w << "42" << back;
		return true;
	}

	if (!strcmp(cmd, "negwidth")) {
		Debug::Width w(-5);
		dbg << w << "ab";
		return true;
	}

	if (!strcmp(cmd, "widthprefix")) {
		/* The radix prefix is written straight to the buffer, so the padding
		   lands between the prefix and the digits. */
		Debug::Hex hex;
		Debug::Dec dec;
		Debug::Width w(6);
		dbg << hex << w << 255 << dec;
		return true;
	}

	if (!strcmp(cmd, "floats")) {
		dbg << 1.5f << " " << 2.25;
		return true;
	}

	if (!strcmp(cmd, "ptr")) {
		dbg << (const void *)0;
		return true;
	}

	if (!strcmp(cmd, "hresult")) {
		dbg << Debug::HResult(0x80004005L);
		return true;
	}

	if (!strcmp(cmd, "mem")) {
		static const char blob[] = "ABCDEFGHIJKLMNOP";
		dbg << Debug::MemDump::CharRel(blob, 16);
		return true;
	}

	if (!strcmp(cmd, "memraw")) {
		static const char blob[] = "ABCD";
		dbg << Debug::MemDump::RawRel(blob, 4);
		return true;
	}

	/* Everything else, "help" included, is unknown - which is what makes the
	   module's own error messages reachable. */
	return false;
}

/* Both run as ordinary static initialisers, i.e. after Debug::PreStaticInit
   (.CRT$XCB) and before Debug::PostStaticInit (.CRT$XCY). */
static bool registered_io = Debug::AddIOFactory("capture", "test capture", CaptureIO::Create);
static bool registered_cmds = Debug::AddCommands("test", new TestCmds);

/* Overrides the library's own definition.  The leading, doubled and trailing
   newlines are the point: the loop that consumes this only advanced past
   non-empty lines, so any one of them used to hang the process before main. */
const char *DebugGetDefaultCommands(void)
{
	return "\n!debug.io capture add\n\ntest.startup\n\n";
}


///////////////////////////////////////////////////////////////////////////////
//	Helpers
///////////////////////////////////////////////////////////////////////////////

static std::string run(const char *cmd)
{
	captured.clear();
	captured_source.clear();
	captured_type = -1;
	Debug::Command(cmd);
	return captured;
}


///////////////////////////////////////////////////////////////////////////////
//	Debug::SimpleMatch
///////////////////////////////////////////////////////////////////////////////

TEST(simplematch_literals)
{
	CHECK(Debug::SimpleMatch("", ""));
	CHECK(Debug::SimpleMatch("abc", "abc"));
	CHECK(!Debug::SimpleMatch("abc", "abd"));
	CHECK(!Debug::SimpleMatch("abc", "ab"));
	CHECK(!Debug::SimpleMatch("ab", "abc"));
	CHECK(!Debug::SimpleMatch("abc", ""));
	CHECK(!Debug::SimpleMatch("", "abc"));

	/* Case sensitive, and no metacharacter other than '*'. */
	CHECK(!Debug::SimpleMatch("ABC", "abc"));
	CHECK(!Debug::SimpleMatch("abc", "a?c"));
	CHECK(Debug::SimpleMatch("a?c", "a?c"));
	CHECK(Debug::SimpleMatch("a.c", "a.c"));
}

TEST(simplematch_star)
{
	CHECK(Debug::SimpleMatch("abc", "*"));
	CHECK(Debug::SimpleMatch("abc", "*c"));
	CHECK(Debug::SimpleMatch("abc", "*bc"));
	CHECK(Debug::SimpleMatch("abc", "*abc"));
	CHECK(Debug::SimpleMatch("abc", "a*c"));
	CHECK(Debug::SimpleMatch("abc", "*b*"));
	CHECK(Debug::SimpleMatch("abcdef", "a*f"));
	CHECK(Debug::SimpleMatch("abcdef", "*c*e*"));
	CHECK(Debug::SimpleMatch("abcdef", "**f"));

	CHECK(!Debug::SimpleMatch("abc", "*d"));
	CHECK(!Debug::SimpleMatch("abc", "b*"));
	CHECK(!Debug::SimpleMatch("abcdef", "*f*g"));
}

TEST(simplematch_DEFECT_star_never_matches_zero_chars_at_the_end)
{
	/* The loop only enters the '*' branch while there is input left, so a
	   trailing '*' with nothing to consume falls through to a plain
	   *str=='*' comparison.  Every caller passes patterns from debug
	   commands, so this is pinned rather than changed. */
	CHECK(!Debug::SimpleMatch("", "*"));
	CHECK(!Debug::SimpleMatch("a", "a*"));
	CHECK(!Debug::SimpleMatch("abc", "abc*"));

	/* ...but a '*' with at least one character to eat works as expected. */
	CHECK(Debug::SimpleMatch("ab", "a*"));
	CHECK(Debug::SimpleMatch("abcd", "abc*"));
}

TEST(simplematch_realistic_log_group_patterns)
{
	CHECK(Debug::SimpleMatch("debug_debug", "debug_*g"));
	CHECK(Debug::SimpleMatch("gameengine", "*engine"));
	CHECK(Debug::SimpleMatch("w3dview.cpp", "*.cpp"));
	CHECK(!Debug::SimpleMatch("w3dview.h", "*.cpp"));
	CHECK(Debug::SimpleMatch("net/socket", "net/*t"));
}

TEST(simplematch_backtracking)
{
	/* The recursion has to retry every start position, not just the first. */
	CHECK(Debug::SimpleMatch("aaab", "*ab"));
	CHECK(Debug::SimpleMatch("xaxbxc", "*a*b*c"));
	CHECK(!Debug::SimpleMatch("xaxbxc", "*c*b*a"));
	CHECK(Debug::SimpleMatch("aaaaaaaaab", "*a*a*a*b"));
}


///////////////////////////////////////////////////////////////////////////////
//	Startup path
///////////////////////////////////////////////////////////////////////////////

TEST(startup_ran_the_default_commands)
{
	/* Reaching this point at all proves the empty lines in the override did not
	   spin forever, and both non-empty lines must have executed. */
	CHECK_EQ(startup_calls, 1);
	CHECK(registered_io);
	CHECK(registered_cmds);

	/* If the capture class had not been added, nothing below could see output. */
	CHECK(run("test.noop").length() > 0);
}


///////////////////////////////////////////////////////////////////////////////
//	Command dispatch and reply framing
///////////////////////////////////////////////////////////////////////////////

TEST(command_normal_reply_framing)
{
	std::string out = run("test.echo a b");

	CHECK_STR(out.c_str(), "> test.echo a b\na|b\n");
	CHECK_STR(captured_source.c_str(), "test.echo");
	CHECK_EQ(captured_type, int(DebugIOInterface::CmdReply));
	CHECK_EQ(last_mode, int(DebugCmdInterface::Normal));
}

TEST(command_structured_reply_drops_the_prompt)
{
	std::string out = run("!test.echo a");

	CHECK_STR(out.c_str(), "!test.echo a\na\n");
	CHECK_STR(captured_source.c_str(), "test.echo");
	CHECK_EQ(captured_type, int(DebugIOInterface::StructuredCmdReply));
	CHECK_EQ(last_mode, int(DebugCmdInterface::Structured));
}

TEST(command_bang_is_only_special_at_offset_zero)
{
	std::string out = run("test.echo !x");

	CHECK_STR(out.c_str(), "> test.echo !x\n!x\n");
	CHECK_EQ(last_mode, int(DebugCmdInterface::Normal));
}

TEST(command_empty_produces_nothing)
{
	int before = execute_calls;

	CHECK_STR(run("").c_str(), "");
	CHECK_STR(run("   ").c_str(), "");
	CHECK_EQ(execute_calls, before);
}

TEST(command_group_selection_alone_is_a_no_op)
{
	int before = execute_calls;
	std::string out = run("test.");

	CHECK_STR(out.c_str(), "> test.\n");
	CHECK_EQ(execute_calls, before);

	/* ...but the group sticks, so the next bare command lands in it. */
	CHECK_STR(run("noop").c_str(), "> noop\n");
	CHECK_EQ(execute_calls, before + 1);
	CHECK_STR(last_cmd.c_str(), "noop");
}

TEST(command_unknown_group_is_reported)
{
	std::string out = run("nosuchgroup.whatever");

	/* Reachable only since the two searches stopped shadowing each other. */
	CHECK_STR(out.c_str(), "> nosuchgroup.whatever\nUnknown command group nosuchgroup\n");
}

TEST(command_unknown_command_is_reported)
{
	std::string out = run("test.nosuchcommand");

	CHECK_STR(out.c_str(), "> test.nosuchcommand\nUnknown command\n");
	CHECK_STR(last_cmd.c_str(), "nosuchcommand");
}

TEST(command_help_without_args_stays_silent)
{
	/* help is broadcast to every interface in the group and gets no error
	   message of its own when nobody answers. */
	std::string out = run("test.help");

	CHECK_STR(out.c_str(), "> test.help\n");
}

TEST(command_help_with_args_reports_the_miss)
{
	std::string out = run("test.help sometopic");

	CHECK_STR(out.c_str(), "> test.help sometopic\nUnknown command, help not available\n");
}

TEST(command_structured_mode_suppresses_the_error_messages)
{
	std::string out = run("!test.nosuchcommand");

	CHECK_STR(out.c_str(), "!test.nosuchcommand\n");
}


///////////////////////////////////////////////////////////////////////////////
//	Command tokenizer
///////////////////////////////////////////////////////////////////////////////

TEST(tokenizer_splits_on_spaces_and_tabs)
{
	run("test.noop a b\tc \t d");

	CHECK_EQ(last_args.size(), size_t(4));
	CHECK_STR(last_args[0].c_str(), "a");
	CHECK_STR(last_args[1].c_str(), "b");
	CHECK_STR(last_args[2].c_str(), "c");
	CHECK_STR(last_args[3].c_str(), "d");
}

TEST(tokenizer_does_not_treat_cr_as_whitespace)
{
	run("test.noop a\r");

	CHECK_EQ(last_args.size(), size_t(1));
	CHECK_STR(last_args[0].c_str(), "a\r");
}

TEST(tokenizer_quotes)
{
	run("test.noop \"a b\" 'c d' e");

	CHECK_EQ(last_args.size(), size_t(3));
	CHECK_STR(last_args[0].c_str(), "a b");
	CHECK_STR(last_args[1].c_str(), "c d");
	CHECK_STR(last_args[2].c_str(), "e");
}

TEST(tokenizer_empty_quotes_are_a_real_argument)
{
	run("test.noop \"\" x");

	CHECK_EQ(last_args.size(), size_t(2));
	CHECK_STR(last_args[0].c_str(), "");
	CHECK_STR(last_args[1].c_str(), "x");
}

TEST(tokenizer_quote_inside_a_word_is_literal)
{
	run("test.noop a\"b c");

	CHECK_EQ(last_args.size(), size_t(2));
	CHECK_STR(last_args[0].c_str(), "a\"b");
	CHECK_STR(last_args[1].c_str(), "c");
}

TEST(tokenizer_unterminated_quote_swallows_the_rest_of_the_line)
{
	run("test.noop \"a b c");

	CHECK_EQ(last_args.size(), size_t(1));
	CHECK_STR(last_args[0].c_str(), "a b c");
}

TEST(tokenizer_semicolon_ends_the_command)
{
	run("test.noop a ; b");
	CHECK_EQ(last_args.size(), size_t(1));
	CHECK_STR(last_args[0].c_str(), "a");

	/* No separator needed - the semicolon terminates the token too. */
	run("test.noop a;b");
	CHECK_EQ(last_args.size(), size_t(1));
	CHECK_STR(last_args[0].c_str(), "a");

	/* And there is no second command: only one Execute happens. */
	int before = execute_calls;
	run("test.noop x ; test.noop y");
	CHECK_EQ(execute_calls, before + 1);
	CHECK_STR(last_args[0].c_str(), "x");
}

TEST(tokenizer_drops_arguments_past_the_fixed_array)
{
	/* parts[100] holds the command plus 99 arguments; the rest are dropped
	   silently, which is documented in the source as intentional. */
	std::string cmd = "test.noop";
	for (int i = 0; i < 150; ++i) {
		cmd += " a";
	}

	run(cmd.c_str());
	CHECK_EQ(last_args.size(), size_t(99));
}

TEST(tokenizer_only_the_first_dot_splits_the_group)
{
	run("test.noop.extra");

	CHECK_STR(last_cmd.c_str(), "noop.extra");
	/* Unknown to the handler, so it reports a miss rather than running noop. */
	CHECK_STR(captured.c_str(), "> test.noop.extra\nUnknown command\n");
}


///////////////////////////////////////////////////////////////////////////////
//	Output stream
///////////////////////////////////////////////////////////////////////////////

TEST(stream_null_string)
{
	CHECK_STR(run("test.nul").c_str(), "> test.nul\n[NULL]\n");
}

TEST(stream_empty_string_does_not_consume_the_width)
{
	CHECK_STR(run("test.empty").c_str(), "> test.empty\n  ab\n");
}

TEST(stream_integers)
{
	CHECK_STR(run("test.ints").c_str(),
	          "> test.ints\n-42 4000000000 -7 4294967295 -3 65535 -5000000000 10000000000 true false\n");
}

TEST(stream_radix_prefixes)
{
	CHECK_STR(run("test.radix").c_str(), "> test.radix\n0xff %101 255\n");
}

TEST(stream_radix_is_sticky_until_changed)
{
	CHECK_STR(run("test.radix_sticky").c_str(), "> test.radix_sticky\n0xff 0x10\n");
}

TEST(stream_width_applies_to_one_insertion)
{
	CHECK_STR(run("test.width").c_str(), "> test.width\n    abcd    ef\n");
}

TEST(stream_fill_character)
{
	CHECK_STR(run("test.fill").c_str(), "> test.fill\n00042\n");
}

TEST(stream_negative_width_pads_nothing)
{
	/* The padding loop compares against an unsigned, so a negative width used
	   to wrap round into four billion fill characters. */
	CHECK_STR(run("test.negwidth").c_str(), "> test.negwidth\nab\n");
}

TEST(stream_DEFECT_width_pads_after_the_radix_prefix)
{
	/* The prefix goes straight into the buffer while the digits go through the
	   width-aware path, so padding lands in the middle of the number. */
	CHECK_STR(run("test.widthprefix").c_str(), "> test.widthprefix\n0x    ff\n");
}

TEST(stream_floats)
{
	CHECK_STR(run("test.floats").c_str(), "> test.floats\n1.500000 2.250000\n");
}

TEST(stream_null_pointer)
{
	CHECK_STR(run("test.ptr").c_str(), "> test.ptr\nptr:NULL\n");
}

TEST(stream_hresult_without_translators)
{
	CHECK_STR(run("test.hresult").c_str(), "> test.hresult\nHResult:0x80004005\n");
}

TEST(stream_repeatchar_keeps_the_remainder)
{
	/* The old loop decremented before testing, so 79 came out as 70 and every
	   count below 10 after a >=10 count was lost. */
	CHECK_STR(run("test.repeat 0").c_str(), "> test.repeat 0\n");
	CHECK_STR(run("test.repeat 1").c_str(), "> test.repeat 1\n=\n");
	CHECK_STR(run("test.repeat 9").c_str(), "> test.repeat 9\n=========\n");
	CHECK_STR(run("test.repeat 10").c_str(), "> test.repeat 10\n==========\n");

	std::string out = run("test.repeat 79");
	CHECK_EQ(out.length(), strlen("> test.repeat 79\n") + 79 + 1);
	CHECK_EQ(out.find_first_not_of('=', strlen("> test.repeat 79\n")),
	         strlen("> test.repeat 79\n") + 79);

	out = run("test.repeat 25");
	CHECK_EQ(out.length(), strlen("> test.repeat 25\n") + 25 + 1);

	/* A negative count writes nothing rather than looping. */
	CHECK_STR(run("test.repeat -3").c_str(), "> test.repeat -3\n");
}

TEST(stream_format)
{
	CHECK_STR(run("test.fmt abc 7").c_str(), "> test.fmt abc 7\n[abc:7]\n");
}

TEST(stream_format_truncates_and_stays_terminated)
{
	/* _vsnprintf writes no NUL when it fills the buffer, and m_buffer has no
	   initialiser - this used to run off the end of the object. */
	std::string out = run("test.fmtlong");
	std::string payload = out.substr(strlen("> test.fmtlong\n"));

	CHECK_EQ(payload.length(), size_t(511 + 1));
	CHECK_EQ(payload.find_first_not_of('F'), size_t(511));
}

TEST(stream_memdump_with_characters)
{
	CHECK_STR(run("test.mem").c_str(),
	          "> test.mem\n"
	          "00000000 41 42 43 44 45 46 47 48 49 4a 4b 4c 4d 4e 4f 50 ABCDEFGHIJKLMNOP\n");
}

TEST(stream_memdump_pads_a_short_line)
{
	/* Four bytes with 21 items per line: the rest of the row is blanks. */
	std::string out = run("test.memraw");

	CHECK_EQ(out.compare(0, strlen("> test.memraw\n00000000 41 42 43 44 "),
	                     "> test.memraw\n00000000 41 42 43 44 "), 0);
	CHECK_EQ(out.find_first_not_of(' ', strlen("> test.memraw\n00000000 41 42 43 44")),
	         out.length() - 1);
	CHECK_EQ(out[out.length()-1], '\n');
}


///////////////////////////////////////////////////////////////////////////////
//	HRESULT translators
///////////////////////////////////////////////////////////////////////////////

static std::string translator_log;

static bool translator_low(Debug &dbg, long hresult, void *user)
{
	translator_log += "low";
	if (hresult != 0x80004005L) return false;
	dbg << "LOW" << (const char *)user;
	return true;
}

static bool translator_high(Debug &dbg, long hresult, void *user)
{
	translator_log += "high";
	if (hresult != 0x80004005L) return false;
	dbg << "HIGH" << (const char *)user;
	return true;
}

TEST(hresult_translators_run_in_priority_order)
{
	translator_log.clear();

	Debug::AddHResultTranslator(1, translator_low, (void *)"1");
	Debug::AddHResultTranslator(10, translator_high, (void *)"2");

	CHECK_STR(run("test.hresult").c_str(), "> test.hresult\nHIGH2\n");
	CHECK_STR(translator_log.c_str(), "high");

	Debug::RemoveHResultTranslator(translator_high, (void *)"2");
	translator_log.clear();
	CHECK_STR(run("test.hresult").c_str(), "> test.hresult\nLOW1\n");
	CHECK_STR(translator_log.c_str(), "low");

	Debug::RemoveHResultTranslator(translator_low, (void *)"1");
	CHECK_STR(run("test.hresult").c_str(), "> test.hresult\nHResult:0x80004005\n");
}

TEST(hresult_removing_an_unregistered_translator_is_harmless)
{
	Debug::RemoveHResultTranslator(translator_low, (void *)"nope");
	CHECK_STR(run("test.hresult").c_str(), "> test.hresult\nHResult:0x80004005\n");
}


///////////////////////////////////////////////////////////////////////////////
//	The built-in 'debug' command group
///////////////////////////////////////////////////////////////////////////////

TEST(debug_group_help_lists_commands)
{
	std::string out = run("debug.help");

	CHECK(out.find("alwaysflush") != std::string::npos);
	CHECK(out.find("timestamp") != std::string::npos);
	CHECK(out.find("io") != std::string::npos);
	CHECK(out.find("list") != std::string::npos);
}

TEST(debug_group_io_lists_only_the_active_classes)
{
	std::string out = run("debug.io");

	CHECK(out.find("Active:") != std::string::npos);
	CHECK(out.find("capture (test capture)") != std::string::npos);
	/* Registered by PostStaticInit, but none of them was ever added. */
	CHECK(out.find("flat") == std::string::npos);
	CHECK(out.find("ods") == std::string::npos);
}

TEST(debug_group_io_question_mark_lists_every_registered_class)
{
	std::string out = run("debug.io ?");

	CHECK(out.find("Possible:") != std::string::npos);
	CHECK(out.find("capture") != std::string::npos);
	CHECK(out.find("flat") != std::string::npos);
	CHECK(out.find("con") != std::string::npos);
	CHECK(out.find("net") != std::string::npos);
	CHECK(out.find("ods") != std::string::npos);
}

TEST(debug_group_unknown_io_class_is_reported)
{
	std::string out = run("debug.io nosuchclass add");

	CHECK(out.find("Unknown I/O class") != std::string::npos);
}

TEST(debug_group_alwaysflush_toggles)
{
	int before = flush_signals;

	CHECK(run("debug.alwaysflush +").find("alwaysflush") != std::string::npos);
	run("test.noop");
	CHECK(flush_signals > before);

	before = flush_signals;
	run("debug.alwaysflush -");
	run("test.noop");
	CHECK_EQ(flush_signals, before);
}

TEST(debug_group_flag_without_a_sign_only_reports)
{
	run("debug.alwaysflush -");
	int before = flush_signals;

	/* Only '+' and '-' change anything; anything else just prints the state. */
	CHECK(run("debug.alwaysflush x").find("alwaysflush") != std::string::npos);
	run("test.noop");
	CHECK_EQ(flush_signals, before);

	CHECK(run("debug.alwaysflush").find("alwaysflush") != std::string::npos);
	run("test.noop");
	CHECK_EQ(flush_signals, before);
}

/* Kept last: it changes the shape of every reply while it is on. */
TEST(debug_group_timestamp_prefixes_every_line)
{
	run("debug.timestamp +");

	std::string out = run("test.echo a");
	run("debug.timestamp -");

	CHECK_EQ(out[0], '[');
	CHECK(out.find("] > test.echo a\n") != std::string::npos);
	/* Each line gets its own stamp, so the payload carries a second one. */
	CHECK_EQ(out.find('['), size_t(0));
	CHECK(out.rfind('[') > size_t(0));

	/* And the shape is back to normal afterwards. */
	CHECK_STR(run("test.echo a").c_str(), "> test.echo a\na\n");
}


///////////////////////////////////////////////////////////////////////////////
//	Internal allocator
///////////////////////////////////////////////////////////////////////////////

void *DebugAllocMemory(unsigned numBytes);
void *DebugReAllocMemory(void *oldPtr, unsigned newSize);
void DebugFreeMemory(void *ptr);

TEST(debug_memory_alloc_and_free)
{
	void *p = DebugAllocMemory(64);
	CHECK(p != 0);
	memset(p, 0xAB, 64);
	CHECK_EQ(((unsigned char *)p)[63], (unsigned char)0xAB);
	DebugFreeMemory(p);

	/* Freeing NULL is a no-op rather than a crash. */
	DebugFreeMemory(0);
}

TEST(debug_memory_realloc_truth_table)
{
	CHECK_EQ(DebugReAllocMemory(0, 0), (void *)0);

	void *p = DebugReAllocMemory(0, 16);
	CHECK(p != 0);

	memcpy(p, "0123456789abcde", 16);
	p = DebugReAllocMemory(p, 4096);
	CHECK(p != 0);
	/* Growing keeps the contents, whichever of the two paths it took. */
	CHECK_STR((const char *)p, "0123456789abcde");

	p = DebugReAllocMemory(p, 8);
	CHECK(p != 0);
	CHECK_MEM(p, "01234567", 8);

	CHECK_EQ(DebugReAllocMemory(p, 0), (void *)0);
}
