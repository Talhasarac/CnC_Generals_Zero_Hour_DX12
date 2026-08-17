/*
 * Tests for the two remaining leaf support libraries, benchmark and
 * eabrowserdispatch.  Neither has enough surface to deserve a binary of its
 * own: one is a stub with a single entry point, the other is MIDL output.
 *
 * What is actually worth pinning here:
 *
 *   - RunBenchmark's contract with its one caller (W3DShaderManager ->
 *     GameLODManager): all three indices written, NULLs tolerated, 0 returned.
 *     The values themselves are the calibration knob documented in
 *     benchmark_stub.c, so they are checked as "positive and equal", not as
 *     magic numbers a future tuning pass would have to come and edit here.
 *   - that the MIDL step really produced the GUIDs from BrowserDispatch.idl and
 *     that the generated interface is callable through its vtable, which is how
 *     GameEngine's WebBrowser.h uses it.  A silently misgenerated header would
 *     otherwise only show up in Phase 4.
 *
 * Not covered: gamespy.  It is vendored third party (TheSuperHackers/GamespySDK)
 * and ships its own test suite, which the port turns off with GS_BUILD_TESTS=OFF
 * because those tests want a live GameSpy backend that has been dead since 2014.
 */
#include "test_harness.h"

#include <windows.h>

#include "benchmark.h"
#include "EABrowserDispatch/BrowserDispatch.h"


///////////////////////////////////////////////////////////////////////////////
//	benchmark
///////////////////////////////////////////////////////////////////////////////

TEST(benchmark_fills_in_every_result)
{
	float f = -1.0f, i = -1.0f, m = -1.0f;

	CHECK_EQ(RunBenchmark(0, 0, &f, &i, &m), 0);

	/* GameLOD only ever takes ratios of these, so what matters is that they are
	   usable numbers - not which ones. */
	CHECK(f > 0.0f);
	CHECK(i > 0.0f);
	CHECK(m > 0.0f);
	CHECK_EQ(f, i);
	CHECK_EQ(i, m);
}

TEST(benchmark_is_deterministic)
{
	float f1 = 0.0f, i1 = 0.0f, m1 = 0.0f;
	float f2 = 0.0f, i2 = 0.0f, m2 = 0.0f;

	RunBenchmark(0, 0, &f1, &i1, &m1);
	RunBenchmark(0, 0, &f2, &i2, &m2);

	/* The stub stands in for a measurement, so this pins the substitution: two
	   calls must agree, which the real BYTEmark would not have guaranteed. */
	CHECK_EQ(f1, f2);
	CHECK_EQ(i1, i2);
	CHECK_EQ(m1, m2);
}

TEST(benchmark_tolerates_null_results)
{
	float f = 0.0f;

	CHECK_EQ(RunBenchmark(0, 0, 0, 0, 0), 0);
	CHECK_EQ(RunBenchmark(0, 0, &f, 0, 0), 0);
	CHECK(f > 0.0f);
}

TEST(benchmark_ignores_its_argv)
{
	char arg0[] = "whatever";
	char *argv[] = { arg0, 0 };
	float f = 0.0f, i = 0.0f, m = 0.0f;
	float f2 = 0.0f, i2 = 0.0f, m2 = 0.0f;

	RunBenchmark(0, 0, &f, &i, &m);
	RunBenchmark(1, argv, &f2, &i2, &m2);

	CHECK_EQ(f, f2);
	CHECK_EQ(i, i2);
	CHECK_EQ(m, m2);
}


///////////////////////////////////////////////////////////////////////////////
//	eabrowserdispatch
///////////////////////////////////////////////////////////////////////////////

/* The two UUIDs written out in BrowserDispatch.idl, spelled independently of
   the generated _i.c so that a regenerated-from-a-different-idl mismatch shows
   up as a failure rather than as agreement with itself. */
static const GUID idl_libid =
	{ 0xC92D8250, 0xA628, 0x4CE5, { 0x82, 0x3F, 0x1A, 0x1F, 0x11, 0x6E, 0xFC, 0xC9 } };
static const GUID idl_iid =
	{ 0xBC834510, 0xC5BC, 0x4B90, { 0x8C, 0x9A, 0x0E, 0x4B, 0x19, 0x98, 0x79, 0x6F } };

TEST(browserdispatch_guids_match_the_idl)
{
	CHECK(IsEqualGUID(LIBID_BROWSERDISPATCHLib, idl_libid));
	CHECK(IsEqualGUID(IID_IBrowserDispatch, idl_iid));

	/* Different objects, so a copy-paste in the .idl would be caught too. */
	CHECK(!IsEqualGUID(LIBID_BROWSERDISPATCHLib, IID_IBrowserDispatch));
}

TEST(browserdispatch_guids_come_from_the_library)
{
	/* MIDL emits the GUIDs as selectany data in BrowserDispatch_i.c, i.e. in the
	   eabrowserdispatch archive - the header only declares them.  Taking their
	   address is what makes this a link-time check that the archive is really on
	   the line and not quietly empty. */
	CHECK(&IID_IBrowserDispatch != 0);
	CHECK(&LIBID_BROWSERDISPATCHLib != 0);
}

/* Minimal implementation of the generated interface.  WebBrowser.h talks to
   IBrowserDispatch through exactly this shape. */
class TestBrowserDispatch : public IBrowserDispatch
{
public:
	TestBrowserDispatch(void) : m_refs(1), m_lastNum(0), m_calls(0) {}

	STDMETHOD(QueryInterface)(REFIID riid, void **ppv)
	{
		if (IsEqualGUID(riid, IID_IUnknown) || IsEqualGUID(riid, IID_IBrowserDispatch)) {
			*ppv = this;
			AddRef();
			return S_OK;
		}
		*ppv = 0;
		return E_NOINTERFACE;
	}
	STDMETHOD_(ULONG, AddRef)(void) { return ++m_refs; }
	STDMETHOD_(ULONG, Release)(void) { return --m_refs; }

	STDMETHOD(TestMethod)(int num1)
	{
		++m_calls;
		m_lastNum = num1;
		return S_OK;
	}

	ULONG m_refs;
	int m_lastNum;
	int m_calls;
};

TEST(browserdispatch_interface_is_callable)
{
	TestBrowserDispatch obj;
	IBrowserDispatch *p = &obj;

	CHECK_EQ(p->TestMethod(42), S_OK);
	CHECK_EQ(obj.m_calls, 1);
	CHECK_EQ(obj.m_lastNum, 42);

	/* IUnknown first, TestMethod at slot 3: the vtable layout the game relies on. */
	void **vtbl = *(void ***)p;
	CHECK(vtbl[0] != 0);
	CHECK(vtbl[1] != 0);
	CHECK(vtbl[2] != 0);
	CHECK(vtbl[3] != 0);
}

TEST(browserdispatch_queryinterface_follows_the_iid)
{
	TestBrowserDispatch obj;
	IUnknown *unk = &obj;
	void *out = 0;

	CHECK_EQ(unk->QueryInterface(IID_IBrowserDispatch, &out), S_OK);
	CHECK(out == (void *)&obj);
	CHECK_EQ(obj.m_refs, 2u);
	((IBrowserDispatch *)out)->Release();

	out = (void *)1;
	CHECK_EQ(unk->QueryInterface(LIBID_BROWSERDISPATCHLib, &out), E_NOINTERFACE);
	CHECK(out == 0);
	CHECK_EQ(obj.m_refs, 1u);
}
