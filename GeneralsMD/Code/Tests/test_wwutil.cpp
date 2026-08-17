/*
 * Wwutil coverage: cMathUtil and cMiscUtil, the whole library.
 *
 * Both classes are pure static helpers, so everything here is a direct call.
 * The interesting parts are cMathUtil's hand-rolled angle convention (screen
 * coordinates, so +y points down and Angle_To_Vector negates dy on the way
 * out) and the round trip through Vector_To_Angle, which is written as a
 * chain of quadrant fixups rather than an atan2.
 */
#include "test_harness.h"

#include "mathutil.h"
#include "miscutil.h"
#include "wwstring.h"
#include "wwmath.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

namespace
{
	const char *TEMP_FILE = "wwutil_test_tmp.dat";

	void write_temp_file(const char *text)
	{
		FILE *fp = fopen(TEMP_FILE, "wb");
		CHECK(fp != 0);
		fwrite(text, 1, strlen(text), fp);
		fclose(fp);
	}
}

/* ------------------------------------------------------------------ */
/* cMathUtil - angles                                                  */
/* ------------------------------------------------------------------ */

TEST(mathutil_pi_constants)
{
	CHECK_NEAR(cMathUtil::PI_1, 3.14159265358979, 1e-6);
	CHECK_NEAR(cMathUtil::PI_2, cMathUtil::PI_1 / 2.0, 1e-6);
}

TEST(angle_to_vector_cardinals)
{
	/* Screen coordinates: 0 degrees is "up", which is -y. */
	double dx, dy;

	cMathUtil::Angle_To_Vector(0.0, dx, dy);
	CHECK_NEAR(dx, 0.0, 1e-6);
	CHECK_NEAR(dy, -1.0, 1e-6);

	cMathUtil::Angle_To_Vector(90.0, dx, dy);
	CHECK_NEAR(dx, 1.0, 1e-6);
	CHECK_NEAR(dy, 0.0, 1e-6);

	cMathUtil::Angle_To_Vector(180.0, dx, dy);
	CHECK_NEAR(dx, 0.0, 1e-6);
	CHECK_NEAR(dy, 1.0, 1e-6);

	cMathUtil::Angle_To_Vector(270.0, dx, dy);
	CHECK_NEAR(dx, -1.0, 1e-6);
	CHECK_NEAR(dy, 0.0, 1e-6);
}

TEST(angle_to_vector_is_always_unit_length)
{
	/* Each quadrant is a separate branch with its own sin/cos pairing, so
	   walk all four of them. */
	for (int deg = 0; deg < 360; ++deg)
	{
		double dx, dy;
		cMathUtil::Angle_To_Vector(double(deg), dx, dy);
		CHECK_NEAR(::sqrt(dx * dx + dy * dy), 1.0, 1e-4);
	}
}

TEST(angle_to_vector_turns_clockwise_on_screen)
{
	/* Increasing angle sweeps right then down: 45 degrees is right-and-up. */
	double dx, dy;
	cMathUtil::Angle_To_Vector(45.0, dx, dy);
	CHECK(dx > 0.0);
	CHECK(dy < 0.0);

	cMathUtil::Angle_To_Vector(135.0, dx, dy);
	CHECK(dx > 0.0);
	CHECK(dy > 0.0);

	cMathUtil::Angle_To_Vector(225.0, dx, dy);
	CHECK(dx < 0.0);
	CHECK(dy > 0.0);

	cMathUtil::Angle_To_Vector(315.0, dx, dy);
	CHECK(dx < 0.0);
	CHECK(dy < 0.0);
}

TEST(vector_to_angle_round_trips_every_degree)
{
	for (int deg = 0; deg < 360; ++deg)
	{
		double dx, dy, angle;
		cMathUtil::Angle_To_Vector(double(deg), dx, dy);
		cMathUtil::Vector_To_Angle(dx, dy, angle);
		CHECK_NEAR(angle, double(deg), 1e-3);
	}
}

TEST(vector_to_angle_ignores_magnitude)
{
	double angle_unit, angle_long;
	cMathUtil::Vector_To_Angle(3.0, -4.0, angle_unit);
	cMathUtil::Vector_To_Angle(300.0, -400.0, angle_long);
	CHECK_NEAR(angle_unit, angle_long, 1e-6);
}

TEST(vector_to_angle_handles_the_axes)
{
	double angle;

	/* dx == 0 is its own branch and never reaches the atan. */
	cMathUtil::Vector_To_Angle(0.0, -1.0, angle);
	CHECK_NEAR(angle, 0.0, 1e-6);

	cMathUtil::Vector_To_Angle(0.0, 1.0, angle);
	CHECK_NEAR(angle, 180.0, 1e-6);

	/* The degenerate zero vector reports straight up rather than failing. */
	cMathUtil::Vector_To_Angle(0.0, 0.0, angle);
	CHECK_NEAR(angle, 0.0, 1e-6);

	/* Looser here: these two go through the atan/quadrant fixup chain, which
	   mixes PI_1 and PI_2.  PI_2 is a separately truncated literal rather than
	   PI_1/2, and the 1.5e-7 difference lands as ~1e-5 degrees of drift. */
	cMathUtil::Vector_To_Angle(1.0, 0.0, angle);
	CHECK_NEAR(angle, 90.0, 1e-4);

	cMathUtil::Vector_To_Angle(-1.0, 0.0, angle);
	CHECK_NEAR(angle, 270.0, 1e-4);
}

TEST(vector_to_angle_stays_in_range)
{
	for (int deg = 0; deg < 360; ++deg)
	{
		double dx, dy, angle;
		cMathUtil::Angle_To_Vector(double(deg), dx, dy);
		cMathUtil::Vector_To_Angle(dx, dy, angle);
		CHECK(angle >= 0.0);
		CHECK(angle < 360.0);
	}
}

/* ------------------------------------------------------------------ */
/* cMathUtil - distance, rounding, rotation                            */
/* ------------------------------------------------------------------ */

TEST(simple_distance)
{
	CHECK_NEAR(cMathUtil::Simple_Distance(0.0, 0.0, 3.0, 4.0), 5.0, 1e-9);
	CHECK_NEAR(cMathUtil::Simple_Distance(3.0, 4.0, 0.0, 0.0), 5.0, 1e-9);
	CHECK_NEAR(cMathUtil::Simple_Distance(-1.0, -1.0, -1.0, -1.0), 0.0, 1e-9);
	CHECK_NEAR(cMathUtil::Simple_Distance(-2.0, 5.0, 1.0, 1.0), 5.0, 1e-9);
}

TEST(round_goes_away_from_zero)
{
	CHECK_EQ(cMathUtil::Round(0.0), 0);
	CHECK_EQ(cMathUtil::Round(0.4), 0);
	CHECK_EQ(cMathUtil::Round(0.5), 1);
	CHECK_EQ(cMathUtil::Round(1.49), 1);
	CHECK_EQ(cMathUtil::Round(1.5), 2);
	CHECK_EQ(cMathUtil::Round(2.5), 3);

	/* Not truncation-toward-negative-infinity: -2.5 goes to -3, not -2. */
	CHECK_EQ(cMathUtil::Round(-0.4), 0);
	CHECK_EQ(cMathUtil::Round(-0.5), -1);
	CHECK_EQ(cMathUtil::Round(-1.5), -2);
	CHECK_EQ(cMathUtil::Round(-2.5), -3);
}

TEST(round_has_a_dead_zone_at_the_origin)
{
	/* Anything inside +/-MISCUTIL_EPSILON short-circuits to exactly 0. */
	CHECK_EQ(cMathUtil::Round(MISCUTIL_EPSILON / 2.0), 0);
	CHECK_EQ(cMathUtil::Round(-MISCUTIL_EPSILON / 2.0), 0);
	CHECK_EQ(cMathUtil::Round(0.0), 0);
}

TEST(rotate_vector_quarter_turns)
{
	double vx = 1.0, vy = 0.0;

	cMathUtil::Rotate_Vector(vx, vy, 90.0);
	CHECK_NEAR(vx, 0.0, 1e-6);
	CHECK_NEAR(vy, 1.0, 1e-6);

	cMathUtil::Rotate_Vector(vx, vy, 90.0);
	CHECK_NEAR(vx, -1.0, 1e-6);
	CHECK_NEAR(vy, 0.0, 1e-6);

	cMathUtil::Rotate_Vector(vx, vy, 180.0);
	CHECK_NEAR(vx, 1.0, 1e-6);
	CHECK_NEAR(vy, 0.0, 1e-6);
}

TEST(rotate_vector_preserves_length)
{
	for (int deg = 0; deg < 360; deg += 7)
	{
		double vx = 3.0, vy = -4.0;
		cMathUtil::Rotate_Vector(vx, vy, double(deg));
		CHECK_NEAR(::sqrt(vx * vx + vy * vy), 5.0, 1e-6);
	}
}

TEST(rotate_vector_by_zero_and_full_turn_is_identity)
{
	double vx = 1.25, vy = -7.5;

	cMathUtil::Rotate_Vector(vx, vy, 0.0);
	CHECK_NEAR(vx, 1.25, 1e-9);
	CHECK_NEAR(vy, -7.5, 1e-9);

	cMathUtil::Rotate_Vector(vx, vy, 360.0);
	CHECK_NEAR(vx, 1.25, 1e-5);
	CHECK_NEAR(vy, -7.5, 1e-5);
}

TEST(rotate_vector_composes)
{
	double vx = 2.0, vy = 1.0;
	cMathUtil::Rotate_Vector(vx, vy, 30.0);
	cMathUtil::Rotate_Vector(vx, vy, 45.0);

	double wx = 2.0, wy = 1.0;
	cMathUtil::Rotate_Vector(wx, wy, 75.0);

	CHECK_NEAR(vx, wx, 1e-6);
	CHECK_NEAR(vy, wy, 1e-6);
}

/* ------------------------------------------------------------------ */
/* cMathUtil - probability density helpers                             */
/* ------------------------------------------------------------------ */

TEST(uniform_pdf_double_stays_in_range)
{
	bool saw_low = false;
	bool saw_high = false;

	for (int i = 0; i < 2000; ++i)
	{
		double x = cMathUtil::Get_Uniform_Pdf_Double(-5.0, 15.0);
		CHECK(x >= -5.0 - MISCUTIL_EPSILON);
		CHECK(x <= 15.0 + MISCUTIL_EPSILON);
		if (x < 0.0) saw_low = true;
		if (x > 10.0) saw_high = true;
	}

	/* Not a distribution test, just proof it is not pinned to one endpoint. */
	CHECK(saw_low);
	CHECK(saw_high);
}

TEST(uniform_pdf_double_with_empty_range)
{
	double x = cMathUtil::Get_Uniform_Pdf_Double(7.0, 7.0);
	CHECK_NEAR(x, 7.0, 1e-9);
}

TEST(normalized_uniform_pdf_double_is_zero_to_one)
{
	for (int i = 0; i < 2000; ++i)
	{
		double x = cMathUtil::Get_Normalized_Uniform_Pdf_Double();
		CHECK(x >= 0.0);
		CHECK(x <= 1.0);
	}
}

TEST(uniform_pdf_int_covers_its_whole_range)
{
	int seen[6] = { 0, 0, 0, 0, 0, 0 };

	for (int i = 0; i < 4000; ++i)
	{
		int x = cMathUtil::Get_Uniform_Pdf_Int(10, 15);
		CHECK(x >= 10);
		CHECK(x <= 15);
		seen[x - 10] = 1;
	}

	/* Both endpoints are inclusive - the +1 in the modulo is load bearing. */
	for (int j = 0; j < 6; ++j)
		CHECK_EQ(seen[j], 1);
}

TEST(uniform_pdf_int_with_single_value_range)
{
	for (int i = 0; i < 16; ++i)
		CHECK_EQ(cMathUtil::Get_Uniform_Pdf_Int(3, 3), 3);
}

TEST(hat_pdf_double_stays_in_range)
{
	for (int i = 0; i < 2000; ++i)
	{
		double x = cMathUtil::Get_Hat_Pdf_Double(-2.0, 6.0);
		CHECK(x >= -2.0 - MISCUTIL_EPSILON);
		CHECK(x <= 6.0 + MISCUTIL_EPSILON);
	}
}

TEST(hat_pdf_double_with_empty_range)
{
	double x = cMathUtil::Get_Hat_Pdf_Double(4.0, 4.0);
	CHECK_NEAR(x, 4.0, 1e-9);
}

TEST(normalized_hat_pdf_double_is_zero_to_one)
{
	for (int i = 0; i < 2000; ++i)
	{
		double x = cMathUtil::Get_Normalized_Hat_Pdf_Double();
		CHECK(x >= 0.0);
		CHECK(x <= 1.0);
	}
}

TEST(hat_pdf_int_stays_in_range)
{
	for (int i = 0; i < 2000; ++i)
	{
		int x = cMathUtil::Get_Hat_Pdf_Int(0, 10);
		CHECK(x >= 0);
		CHECK(x <= 10);
	}
}

/* ------------------------------------------------------------------ */
/* cMiscUtil - time                                                    */
/* ------------------------------------------------------------------ */

TEST(get_text_time_is_a_stripped_ctime_string)
{
	LPCSTR text = cMiscUtil::Get_Text_Time();
	CHECK(text != 0);

	/* ctime's fixed layout "Www Mmm dd hh:mm:ss yyyy" minus the newline. */
	CHECK_EQ((int)strlen(text), 24);
	CHECK(strchr(text, '\n') == 0);
	CHECK_EQ((int)text[13], (int)':');
	CHECK_EQ((int)text[16], (int)':');
}

TEST(seconds_to_hms_splits_correctly)
{
	int h, m, s;

	cMiscUtil::Seconds_To_Hms(0.0f, h, m, s);
	CHECK_EQ(h, 0); CHECK_EQ(m, 0); CHECK_EQ(s, 0);

	cMiscUtil::Seconds_To_Hms(59.0f, h, m, s);
	CHECK_EQ(h, 0); CHECK_EQ(m, 0); CHECK_EQ(s, 59);

	cMiscUtil::Seconds_To_Hms(60.0f, h, m, s);
	CHECK_EQ(h, 0); CHECK_EQ(m, 1); CHECK_EQ(s, 0);

	cMiscUtil::Seconds_To_Hms(3599.0f, h, m, s);
	CHECK_EQ(h, 0); CHECK_EQ(m, 59); CHECK_EQ(s, 59);

	cMiscUtil::Seconds_To_Hms(3600.0f, h, m, s);
	CHECK_EQ(h, 1); CHECK_EQ(m, 0); CHECK_EQ(s, 0);

	cMiscUtil::Seconds_To_Hms(3661.0f, h, m, s);
	CHECK_EQ(h, 1); CHECK_EQ(m, 1); CHECK_EQ(s, 1);

	/* Hours are unbounded - no wrap at 24. */
	cMiscUtil::Seconds_To_Hms(86399.0f, h, m, s);
	CHECK_EQ(h, 23); CHECK_EQ(m, 59); CHECK_EQ(s, 59);

	cMiscUtil::Seconds_To_Hms(360000.0f, h, m, s);
	CHECK_EQ(h, 100); CHECK_EQ(m, 0); CHECK_EQ(s, 0);
}

TEST(seconds_to_hms_truncates_fractions)
{
	int h, m, s;

	cMiscUtil::Seconds_To_Hms(59.9f, h, m, s);
	CHECK_EQ(h, 0); CHECK_EQ(m, 0); CHECK_EQ(s, 59);

	cMiscUtil::Seconds_To_Hms(0.5f, h, m, s);
	CHECK_EQ(h, 0); CHECK_EQ(m, 0); CHECK_EQ(s, 0);
}

TEST(seconds_to_hms_recomposes)
{
	for (int total = 0; total < 200000; total += 997)
	{
		int h, m, s;
		cMiscUtil::Seconds_To_Hms(float(total), h, m, s);
		CHECK(m >= 0 && m < 60);
		CHECK(s >= 0 && s < 60);
		CHECK_EQ(h * 3600 + m * 60 + s, total);
	}
}

/* ------------------------------------------------------------------ */
/* cMiscUtil - string helpers                                          */
/* ------------------------------------------------------------------ */

TEST(string_compare_is_case_insensitive)
{
	CHECK(cMiscUtil::Is_String_Same("Hello", "hello"));
	CHECK(cMiscUtil::Is_String_Same("", ""));
	CHECK(cMiscUtil::Is_String_Same("MiXeD123", "mixed123"));
	CHECK(!cMiscUtil::Is_String_Same("hello", "hello "));
	CHECK(!cMiscUtil::Is_String_Same("abc", "abd"));

	CHECK(!cMiscUtil::Is_String_Different("Hello", "hello"));
	CHECK(cMiscUtil::Is_String_Different("abc", "abd"));

	/* The two are strict complements. */
	CHECK_NE(cMiscUtil::Is_String_Same("a", "b"),
	         cMiscUtil::Is_String_Different("a", "b"));
}

TEST(character_classes_match_the_c_library)
{
	for (int i = 0; i < 128; ++i)
	{
		char c = (char)i;
		CHECK_EQ(cMiscUtil::Is_Alphabetic(c), isalpha(i) != 0);
		CHECK_EQ(cMiscUtil::Is_Numeric(c), isdigit(i) != 0);
		CHECK_EQ(cMiscUtil::Is_Alphanumeric(c), isalnum(i) != 0);
	}
}

TEST(is_whitespace_is_space_and_tab_only)
{
	CHECK(cMiscUtil::Is_Whitespace(' '));
	CHECK(cMiscUtil::Is_Whitespace('\t'));

	/* Newline and carriage return are deliberately not whitespace here, and
	   Trim_Trailing_Whitespace inherits that. */
	CHECK(!cMiscUtil::Is_Whitespace('\n'));
	CHECK(!cMiscUtil::Is_Whitespace('\r'));
	CHECK(!cMiscUtil::Is_Whitespace('\0'));
	CHECK(!cMiscUtil::Is_Whitespace('x'));
}

TEST(trim_trailing_whitespace)
{
	char buffer[64];

	strcpy(buffer, "hello   ");
	cMiscUtil::Trim_Trailing_Whitespace(buffer);
	CHECK_STR(buffer, "hello");

	strcpy(buffer, "hello\t \t");
	cMiscUtil::Trim_Trailing_Whitespace(buffer);
	CHECK_STR(buffer, "hello");

	strcpy(buffer, "  spaced  out  ");
	cMiscUtil::Trim_Trailing_Whitespace(buffer);
	CHECK_STR(buffer, "  spaced  out");

	strcpy(buffer, "clean");
	cMiscUtil::Trim_Trailing_Whitespace(buffer);
	CHECK_STR(buffer, "clean");

	strcpy(buffer, "     ");
	cMiscUtil::Trim_Trailing_Whitespace(buffer);
	CHECK_STR(buffer, "");

	strcpy(buffer, "");
	cMiscUtil::Trim_Trailing_Whitespace(buffer);
	CHECK_STR(buffer, "");

	/* A trailing newline blocks the trim - it is not in the whitespace set. */
	strcpy(buffer, "line \n");
	cMiscUtil::Trim_Trailing_Whitespace(buffer);
	CHECK_STR(buffer, "line \n");
}

/* ------------------------------------------------------------------ */
/* cMiscUtil - files                                                   */
/* ------------------------------------------------------------------ */

TEST(file_exists_and_remove_file)
{
	cMiscUtil::Remove_File(TEMP_FILE);
	CHECK(!cMiscUtil::File_Exists(TEMP_FILE));

	write_temp_file("wwutil");
	CHECK(cMiscUtil::File_Exists(TEMP_FILE));

	cMiscUtil::Remove_File(TEMP_FILE);
	CHECK(!cMiscUtil::File_Exists(TEMP_FILE));

	/* Removing something that is not there is a no-op, not a failure. */
	cMiscUtil::Remove_File(TEMP_FILE);
	CHECK(!cMiscUtil::File_Exists(TEMP_FILE));

	CHECK(!cMiscUtil::File_Exists("no_such_directory_here\\no_such_file.xyz"));
}

TEST(file_is_read_only)
{
	write_temp_file("wwutil");
	CHECK(!cMiscUtil::File_Is_Read_Only(TEMP_FILE));

	/* A missing file reports "not read only" rather than failing. */
	cMiscUtil::Remove_File(TEMP_FILE);
	CHECK(!cMiscUtil::File_Is_Read_Only(TEMP_FILE));
}

TEST(get_file_id_string_is_name_size_and_stamp)
{
	write_temp_file("0123456789");

	StringClass id;
	cMiscUtil::Get_File_Id_String(TEMP_FILE, id);

	/* Upper-cased basename, byte count, then the PE timestamp (0 for a file
	   that has no image header). */
	CHECK_STR(id.Peek_Buffer(), "WWUTIL_TEST_TMP.DAT 10 0");

	cMiscUtil::Remove_File(TEMP_FILE);
}

TEST(get_file_id_string_strips_the_directory)
{
	write_temp_file("abc");

	char with_dot[128];
	sprintf(with_dot, ".\\%s", TEMP_FILE);

	StringClass id;
	cMiscUtil::Get_File_Id_String(with_dot, id);
	CHECK_STR(id.Peek_Buffer(), "WWUTIL_TEST_TMP.DAT 3 0");

	cMiscUtil::Remove_File(TEMP_FILE);
}
