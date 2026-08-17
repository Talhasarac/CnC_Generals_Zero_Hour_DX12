/*
 * WWSaveLoad coverage: the pointer swizzler, the subsystem/persist-factory
 * registries, the definition manager and the twiddler.
 *
 * The library is one big set of global registries wired up by static
 * constructors, so the tests below plant their own subsystem, persist
 * factories and definition factory at file scope and then drive the public
 * entry points.  Anything that mutates a global registry puts it back.
 *
 * Chunk ids come out of the PHYSTEST range in saveloadids.h - nothing in the
 * ported libraries claims it, so the test types cannot collide with the real
 * ones.
 */
#include "test_harness.h"

#include "saveload.h"
#include "saveloadids.h"
#include "saveloadstatus.h"
#include "saveloadsubsystem.h"
#include "pointerremap.h"
#include "persist.h"
#include "persistfactory.h"
#include "postloadable.h"
#include "definition.h"
#include "definitionclassids.h"
#include "definitionfactory.h"
#include "definitionfactorymgr.h"
#include "definitionmgr.h"
#include "simpledefinitionfactory.h"
#include "twiddler.h"

#include "chunkio.h"
#include "ramfile.h"
#include "refcount.h"
#include "wwstring.h"
#include "wwhack.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Test fixtures registered with the library's global registries       */
/* ------------------------------------------------------------------ */

enum
{
	CHUNKID_TEST_SUBSYSTEM	= CHUNKID_PHYSTEST_BEGIN + 1,
	CHUNKID_TEST_PERSIST,
	CHUNKID_TEST_DEFINITION,
	CHUNKID_TEST_DEFINITION_B,
};

enum
{
	CLASSID_TEST_DEF		= CLASSID_DUMMY_OBJECTS,			/* is its own superclass */
	CLASSID_TEST_DEF_B	= CLASSID_DUMMY_OBJECTS + 1,		/* same superclass, other class */
};

/*
**	A subsystem registers itself with SaveLoadSystemClass from its constructor,
**	so the single file-scope instance below is the registration.
*/
class TestSubSystemClass : public SaveLoadSubSystemClass
{
public:
	TestSubSystemClass(void)
		:	Value(0), SaveCount(0), LoadCount(0), PostLoadCount(0), HasData(true) {}

	virtual uint32			Chunk_ID(void) const		{ return CHUNKID_TEST_SUBSYSTEM; }
	virtual void			On_Post_Load(void)		{ PostLoadCount++; }

	int	Value;
	int	SaveCount;
	int	LoadCount;
	int	PostLoadCount;
	bool	HasData;

protected:
	enum { VARID_VALUE = 0x01 };

	virtual bool	Contains_Data(void) const	{ return HasData; }
	virtual const char *	Name(void) const		{ return "TestSubSystemClass"; }

	virtual bool Save(ChunkSaveClass &csave)
	{
		SaveCount++;
		WRITE_MICRO_CHUNK(csave, VARID_VALUE, Value);
		return true;
	}

	virtual bool Load(ChunkLoadClass &cload)
	{
		LoadCount++;
		while (cload.Open_Micro_Chunk()) {
			switch (cload.Cur_Micro_Chunk_ID()) {
				READ_MICRO_CHUNK(cload, VARID_VALUE, Value)
			}
			cload.Close_Micro_Chunk();
		}
		return true;
	}
};

TestSubSystemClass TestSubSystem;

/*
**	A persist object plus its factory.  SimplePersistFactoryClass writes the
**	old address, then the object, and registers the old->new pair on load.
*/
class TestPersistClass : public PersistClass
{
public:
	TestPersistClass(void) : Number(0), PostLoads(0), Text("") {}

	virtual const PersistFactoryClass &	Get_Factory(void) const;
	virtual void								On_Post_Load(void)	{ PostLoads++; }

	enum { VARID_NUMBER = 0x01, VARID_TEXT };

	virtual bool Save(ChunkSaveClass &csave)
	{
		WRITE_MICRO_CHUNK(csave, VARID_NUMBER, Number);
		WRITE_MICRO_CHUNK_WWSTRING(csave, VARID_TEXT, Text);
		return true;
	}

	virtual bool Load(ChunkLoadClass &cload)
	{
		while (cload.Open_Micro_Chunk()) {
			switch (cload.Cur_Micro_Chunk_ID()) {
				READ_MICRO_CHUNK(cload, VARID_NUMBER, Number)
				READ_MICRO_CHUNK_WWSTRING(cload, VARID_TEXT, Text)
			}
			cload.Close_Micro_Chunk();
		}
		return true;
	}

	int				Number;
	int				PostLoads;
	StringClass		Text;
};

SimplePersistFactoryClass<TestPersistClass, CHUNKID_TEST_PERSIST>	_TestPersistFactory;

const PersistFactoryClass & TestPersistClass::Get_Factory(void) const
{
	return _TestPersistFactory;
}

/*
**	Two definition types sharing a superclass, so ID_CLASS and ID_SUPERCLASS
**	enumeration can be told apart.
*/
class TestDefinitionClass : public DefinitionClass
{
public:
	DECLARE_EDITABLE(TestDefinitionClass, DefinitionClass);

	virtual uint32							Get_Class_ID(void) const	{ return CLASSID_TEST_DEF; }
	virtual PersistClass *				Create(void) const			{ return NULL; }
	virtual const PersistFactoryClass &	Get_Factory(void) const;
};

SimplePersistFactoryClass<TestDefinitionClass, CHUNKID_TEST_DEFINITION>	_TestDefinitionPersistFactory;

const PersistFactoryClass & TestDefinitionClass::Get_Factory(void) const
{
	return _TestDefinitionPersistFactory;
}

class TestDefinitionBClass : public DefinitionClass
{
public:
	DECLARE_EDITABLE(TestDefinitionBClass, DefinitionClass);

	virtual uint32							Get_Class_ID(void) const	{ return CLASSID_TEST_DEF_B; }
	virtual PersistClass *				Create(void) const			{ return NULL; }
	virtual const PersistFactoryClass &	Get_Factory(void) const;
};

SimplePersistFactoryClass<TestDefinitionBClass, CHUNKID_TEST_DEFINITION_B>	_TestDefinitionBPersistFactory;

const PersistFactoryClass & TestDefinitionBClass::Get_Factory(void) const
{
	return _TestDefinitionBPersistFactory;
}

/* Definition factories also self-register from their constructor. */
DECLARE_DEFINITION_FACTORY(TestDefinitionClass, CLASSID_TEST_DEF, "TestDefinition")		_TestDefinitionFactory;
DECLARE_DEFINITION_FACTORY(TestDefinitionBClass, CLASSID_TEST_DEF_B, "TestDefinitionB")	_TestDefinitionBFactory;

class TestRefObjClass : public RefCountClass
{
public:
	TestRefObjClass(int tag) : Tag(tag) {}
	int Tag;
};

namespace
{
	/* The remap request signature grows file/line under WWDEBUG. */
	void request_remap(PointerRemapClass &remapper, void **pointer)
	{
#ifdef WWDEBUG
		remapper.Request_Pointer_Remap(pointer, __FILE__, __LINE__);
#else
		remapper.Request_Pointer_Remap(pointer);
#endif
	}

	void request_ref_remap(PointerRemapClass &remapper, RefCountClass **pointer)
	{
#ifdef WWDEBUG
		remapper.Request_Ref_Counted_Pointer_Remap(pointer, __FILE__, __LINE__);
#else
		remapper.Request_Ref_Counted_Pointer_Remap(pointer);
#endif
	}

	/* Fabricated "old" addresses.  They are never dereferenced - the remapper
	   only ever compares them. */
	void *fake_old(int i)
	{
		return (void *)(0x10000 + i * 16);
	}

	/* Registers a definition and hands back the pointer so the test can
	   unregister and free it again. */
	TestDefinitionClass *make_def(uint32 id, const char *name)
	{
		TestDefinitionClass *def = new TestDefinitionClass;
		def->Set_Name(name);
		def->Set_ID(id);
		DefinitionMgrClass::Register_Definition(def);
		return def;
	}

	void free_def(DefinitionClass *def)
	{
		DefinitionMgrClass::Unregister_Definition(def);
		delete def;
	}
}

/* ------------------------------------------------------------------ */
/* PointerRemapClass                                                   */
/* ------------------------------------------------------------------ */

TEST(pointer_remap_swizzles_registered_pointers)
{
	PointerRemapClass remapper;

	int targets[4] = { 10, 20, 30, 40 };

	/* Registration order is deliberately scrambled - Process sorts first. */
	remapper.Register_Pointer(fake_old(2), &targets[2]);
	remapper.Register_Pointer(fake_old(0), &targets[0]);
	remapper.Register_Pointer(fake_old(3), &targets[3]);
	remapper.Register_Pointer(fake_old(1), &targets[1]);

	void *slots[4];
	for (int i = 0; i < 4; ++i) {
		slots[i] = fake_old(i);
		request_remap(remapper, &slots[i]);
	}

	remapper.Process();

	for (int i = 0; i < 4; ++i) {
		CHECK_EQ(slots[i], (void *)&targets[i]);
		CHECK_EQ(*(int *)slots[i], (i + 1) * 10);
	}
}

TEST(pointer_remap_handles_many_out_of_order_entries)
{
	/* Enough entries that the sorted merge in Process_Request_Table actually
	   has to walk, and requested in the opposite order to registration. */
	enum { COUNT = 256 };
	static int targets[COUNT];
	static void *slots[COUNT];

	PointerRemapClass remapper;

	for (int i = COUNT - 1; i >= 0; --i) {
		targets[i] = i * 3;
		remapper.Register_Pointer(fake_old(i), &targets[i]);
	}

	for (int j = 0; j < COUNT; ++j) {
		slots[j] = fake_old(COUNT - 1 - j);
		request_remap(remapper, &slots[j]);
	}

	remapper.Process();

	for (int k = 0; k < COUNT; ++k) {
		int expected = COUNT - 1 - k;
		CHECK_EQ(slots[k], (void *)&targets[expected]);
	}
}

TEST(pointer_remap_repeated_requests_for_one_pointer)
{
	PointerRemapClass remapper;

	int target = 77;
	remapper.Register_Pointer(fake_old(5), &target);

	void *a = fake_old(5);
	void *b = fake_old(5);
	void *c = fake_old(5);
	request_remap(remapper, &a);
	request_remap(remapper, &b);
	request_remap(remapper, &c);

	remapper.Process();

	CHECK_EQ(a, (void *)&target);
	CHECK_EQ(b, (void *)&target);
	CHECK_EQ(c, (void *)&target);
}

#ifndef WWDEBUG
TEST(pointer_remap_nulls_out_what_it_cannot_find)
{
	/* Release behaviour only: a WWDEBUG build asserts on the failed lookup
	   before it gets here. */
	PointerRemapClass remapper;

	int target = 1;
	remapper.Register_Pointer(fake_old(0), &target);

	void *known	= fake_old(0);
	void *bogus	= fake_old(99);
	request_remap(remapper, &known);
	request_remap(remapper, &bogus);

	remapper.Process();

	CHECK_EQ(known, (void *)&target);
	CHECK(bogus == NULL);
}
#endif

TEST(pointer_remap_reset_clears_every_table)
{
	PointerRemapClass remapper;

	int target = 5;
	remapper.Register_Pointer(fake_old(0), &target);

	void *slot = fake_old(0);
	request_remap(remapper, &slot);

	remapper.Reset();

	/* With the tables empty Process is a no-op and leaves the slot alone. */
	remapper.Process();
	CHECK_EQ(slot, fake_old(0));

	/* And the remapper is reusable afterwards. */
	remapper.Register_Pointer(fake_old(0), &target);
	request_remap(remapper, &slot);
	remapper.Process();
	CHECK_EQ(slot, (void *)&target);
}

TEST(pointer_remap_on_empty_tables_is_a_no_op)
{
	PointerRemapClass remapper;
	remapper.Process();
	remapper.Reset();
	remapper.Process();
	CHECK(true);
}

TEST(pointer_remap_adds_a_reference_to_ref_counted_pointers)
{
	PointerRemapClass remapper;

	TestRefObjClass *obj_a = new TestRefObjClass(1);
	TestRefObjClass *obj_b = new TestRefObjClass(2);
	CHECK_EQ(obj_a->Num_Refs(), 1);

	remapper.Register_Pointer(fake_old(0), obj_a);
	remapper.Register_Pointer(fake_old(1), obj_b);

	RefCountClass *slot_a = (RefCountClass *)fake_old(0);
	RefCountClass *slot_b = (RefCountClass *)fake_old(1);
	request_ref_remap(remapper, &slot_a);
	request_ref_remap(remapper, &slot_b);

	remapper.Process();

	CHECK_EQ((void *)slot_a, (void *)obj_a);
	CHECK_EQ((void *)slot_b, (void *)obj_b);

	/* The remap took a reference on the caller's behalf. */
	CHECK_EQ(obj_a->Num_Refs(), 2);
	CHECK_EQ(obj_b->Num_Refs(), 2);

	obj_a->Release_Ref();
	obj_a->Release_Ref();
	obj_b->Release_Ref();
	obj_b->Release_Ref();
}

TEST(pointer_remap_keeps_plain_and_ref_counted_tables_apart)
{
	PointerRemapClass remapper;

	TestRefObjClass *obj = new TestRefObjClass(7);
	int plain = 3;

	remapper.Register_Pointer(fake_old(0), &plain);
	remapper.Register_Pointer(fake_old(1), obj);

	void *plain_slot = fake_old(0);
	RefCountClass *ref_slot = (RefCountClass *)fake_old(1);
	request_remap(remapper, &plain_slot);
	request_ref_remap(remapper, &ref_slot);

	remapper.Process();

	CHECK_EQ(plain_slot, (void *)&plain);
	CHECK_EQ((void *)ref_slot, (void *)obj);

	/* Only the ref-counted table adds a reference. */
	CHECK_EQ(obj->Num_Refs(), 2);

	obj->Release_Ref();
	obj->Release_Ref();
}

/* ------------------------------------------------------------------ */
/* saveloadstatus                                                      */
/* ------------------------------------------------------------------ */

TEST(status_count_is_a_plain_counter)
{
	SaveLoadStatus::Reset_Status_Count();
	CHECK_EQ(SaveLoadStatus::Get_Status_Count(), 0);

	for (int i = 1; i <= 5; ++i) {
		SaveLoadStatus::Inc_Status_Count();
		CHECK_EQ(SaveLoadStatus::Get_Status_Count(), i);
	}

	SaveLoadStatus::Reset_Status_Count();
	CHECK_EQ(SaveLoadStatus::Get_Status_Count(), 0);
}

TEST(status_text_round_trips)
{
	StringClass text;

	SaveLoadStatus::Set_Status_Text("loading", 0);
	SaveLoadStatus::Get_Status_Text(text, 0);
	CHECK_STR(text.Peek_Buffer(), "loading");

	SaveLoadStatus::Set_Status_Text("terrain", 1);
	SaveLoadStatus::Get_Status_Text(text, 1);
	CHECK_STR(text.Peek_Buffer(), "terrain");

	/* Setting the top-level text clears the sub-status - that is the whole
	   reason INIT_STATUS and INIT_SUB_STATUS are separate macros. */
	INIT_STATUS("saving");
	SaveLoadStatus::Get_Status_Text(text, 0);
	CHECK_STR(text.Peek_Buffer(), "saving");
	SaveLoadStatus::Get_Status_Text(text, 1);
	CHECK_STR(text.Peek_Buffer(), "");

	INIT_SUB_STATUS("objects");
	SaveLoadStatus::Get_Status_Text(text, 1);
	CHECK_STR(text.Peek_Buffer(), "objects");
	SaveLoadStatus::Get_Status_Text(text, 0);
	CHECK_STR(text.Peek_Buffer(), "saving");
}

/* ------------------------------------------------------------------ */
/* SaveLoadSystemClass - subsystems                                    */
/* ------------------------------------------------------------------ */

TEST(subsystem_save_load_round_trip)
{
	static char storage[4096];
	int written = 0;

	TestSubSystem.Value			= 0x1234abcd;
	TestSubSystem.HasData		= true;
	TestSubSystem.SaveCount		= 0;
	TestSubSystem.LoadCount		= 0;

	{
		RAMFileClass file(storage, sizeof(storage));
		file.Open(FileClass::WRITE);
		ChunkSaveClass csave(&file);
		CHECK(SaveLoadSystemClass::Save(csave, TestSubSystem));
		written = file.Size();
		file.Close();
	}

	CHECK_EQ(TestSubSystem.SaveCount, 1);
	CHECK(written > 0);

	TestSubSystem.Value = 0;

	{
		RAMFileClass file(storage, written);
		file.Open(FileClass::READ);
		ChunkLoadClass cload(&file);
		CHECK(SaveLoadSystemClass::Load(cload));
		file.Close();
	}

	CHECK_EQ(TestSubSystem.LoadCount, 1);
	CHECK_EQ((unsigned)TestSubSystem.Value, 0x1234abcdu);
}

TEST(subsystem_that_contains_no_data_writes_nothing)
{
	static char storage[4096];
	int written = 0;

	TestSubSystem.HasData	= false;
	TestSubSystem.SaveCount	= 0;

	{
		RAMFileClass file(storage, sizeof(storage));
		file.Open(FileClass::WRITE);
		ChunkSaveClass csave(&file);
		CHECK(SaveLoadSystemClass::Save(csave, TestSubSystem));
		written = file.Size();
		file.Close();
	}

	CHECK_EQ(TestSubSystem.SaveCount, 0);
	CHECK_EQ(written, 0);

	TestSubSystem.HasData = true;
}

TEST(load_counts_every_chunk_it_walks)
{
	static char storage[4096];
	int written = 0;

	TestSubSystem.HasData	= true;
	TestSubSystem.LoadCount	= 0;

	{
		RAMFileClass file(storage, sizeof(storage));
		file.Open(FileClass::WRITE);
		ChunkSaveClass csave(&file);
		SaveLoadSystemClass::Save(csave, TestSubSystem);
		SaveLoadSystemClass::Save(csave, TestSubSystem);
		written = file.Size();
		file.Close();
	}

	SaveLoadStatus::Reset_Status_Count();

	{
		RAMFileClass file(storage, written);
		file.Open(FileClass::READ);
		ChunkLoadClass cload(&file);
		CHECK(SaveLoadSystemClass::Load(cload));
		file.Close();
	}

	CHECK_EQ(SaveLoadStatus::Get_Status_Count(), 2);
	CHECK_EQ(TestSubSystem.LoadCount, 2);
}

TEST(load_skips_chunks_no_subsystem_claims)
{
	static char storage[4096];
	int written = 0;

	TestSubSystem.LoadCount = 0;

	{
		RAMFileClass file(storage, sizeof(storage));
		file.Open(FileClass::WRITE);
		ChunkSaveClass csave(&file);

		/* A chunk id nothing is registered for, then a real one. */
		csave.Begin_Chunk(CHUNKID_PHYSTEST_BEGIN + 0x700);
		int junk = 0xdeadbeef;
		csave.Write(&junk, sizeof(junk));
		csave.End_Chunk();

		SaveLoadSystemClass::Save(csave, TestSubSystem);
		written = file.Size();
		file.Close();
	}

	{
		RAMFileClass file(storage, written);
		file.Open(FileClass::READ);
		ChunkLoadClass cload(&file);
		CHECK(SaveLoadSystemClass::Load(cload));
		file.Close();
	}

	/* The unknown chunk is stepped over, the known one still loads. */
	CHECK_EQ(TestSubSystem.LoadCount, 1);
}

TEST(load_of_an_empty_stream_succeeds)
{
	static char storage[64];
	memset(storage, 0, sizeof(storage));

	TestSubSystem.LoadCount = 0;

	RAMFileClass file(storage, 0);
	file.Open(FileClass::READ);
	ChunkLoadClass cload(&file);
	CHECK(SaveLoadSystemClass::Load(cload));
	file.Close();

	CHECK_EQ(TestSubSystem.LoadCount, 0);
}

/* ------------------------------------------------------------------ */
/* SaveLoadSystemClass - post load callbacks                           */
/* ------------------------------------------------------------------ */

TEST(post_load_callbacks_fire_once_each)
{
	TestPersistClass a, b;

	SaveLoadSystemClass::Register_Post_Load_Callback(&a);
	SaveLoadSystemClass::Register_Post_Load_Callback(&b);
	CHECK(a.Is_Post_Load_Registered());
	CHECK(b.Is_Post_Load_Registered());

	SaveLoadSystemClass::Post_Load_Processing(NULL);

	CHECK_EQ(a.PostLoads, 1);
	CHECK_EQ(b.PostLoads, 1);

	/* The list is drained, so a second pass does nothing. */
	CHECK(!a.Is_Post_Load_Registered());
	CHECK(!b.Is_Post_Load_Registered());
	SaveLoadSystemClass::Post_Load_Processing(NULL);
	CHECK_EQ(a.PostLoads, 1);
	CHECK_EQ(b.PostLoads, 1);
}

TEST(post_load_registration_is_idempotent)
{
	TestPersistClass obj;

	SaveLoadSystemClass::Register_Post_Load_Callback(&obj);
	SaveLoadSystemClass::Register_Post_Load_Callback(&obj);
	SaveLoadSystemClass::Register_Post_Load_Callback(&obj);

	SaveLoadSystemClass::Post_Load_Processing(NULL);
	CHECK_EQ(obj.PostLoads, 1);
}

/* ------------------------------------------------------------------ */
/* Persist factories                                                   */
/* ------------------------------------------------------------------ */

TEST(persist_factories_register_themselves)
{
	PersistFactoryClass *factory = SaveLoadSystemClass::Find_Persist_Factory(CHUNKID_TEST_PERSIST);
	CHECK(factory != NULL);
	CHECK_EQ(factory->Chunk_ID(), (uint32)CHUNKID_TEST_PERSIST);

	CHECK(SaveLoadSystemClass::Find_Persist_Factory(CHUNKID_TEST_DEFINITION) != NULL);

	/* Nothing claims this id. */
	CHECK(SaveLoadSystemClass::Find_Persist_Factory(CHUNKID_PHYSTEST_BEGIN + 0x7ff) == NULL);
}

TEST(persist_object_round_trips_through_its_factory)
{
	static char storage[4096];
	int written = 0;

	TestPersistClass original;
	original.Number	= -12345;
	original.Text		= "persisted string";

	{
		RAMFileClass file(storage, sizeof(storage));
		file.Open(FileClass::WRITE);
		ChunkSaveClass csave(&file);
		original.Get_Factory().Save(csave, &original);
		written = file.Size();
		file.Close();
	}

	CHECK(written > 0);

	{
		RAMFileClass file(storage, written);
		file.Open(FileClass::READ);
		ChunkLoadClass cload(&file);

		PersistFactoryClass *factory = SaveLoadSystemClass::Find_Persist_Factory(CHUNKID_TEST_PERSIST);
		CHECK(factory != NULL);
		PersistClass *loaded = factory->Load(cload);
		file.Close();

		CHECK(loaded != NULL);
		TestPersistClass *typed = (TestPersistClass *)loaded;
		CHECK_EQ(typed->Number, -12345);
		CHECK_STR(typed->Text.Peek_Buffer(), "persisted string");

		/* The loaded object is a different instance, not the original. */
		CHECK(typed != &original);
		CHECK(&typed->Get_Factory() == &original.Get_Factory());

		delete loaded;
	}
}

TEST(persist_round_trip_of_default_state)
{
	static char storage[1024];
	int written = 0;

	TestPersistClass original;

	{
		RAMFileClass file(storage, sizeof(storage));
		file.Open(FileClass::WRITE);
		ChunkSaveClass csave(&file);
		original.Get_Factory().Save(csave, &original);
		written = file.Size();
		file.Close();
	}

	RAMFileClass file(storage, written);
	file.Open(FileClass::READ);
	ChunkLoadClass cload(&file);
	PersistClass *loaded = SaveLoadSystemClass::Find_Persist_Factory(CHUNKID_TEST_PERSIST)->Load(cload);
	file.Close();

	TestPersistClass *typed = (TestPersistClass *)loaded;
	CHECK_EQ(typed->Number, 0);
	CHECK_EQ((int)typed->Text.Get_Length(), 0);
	delete loaded;
}

/* ------------------------------------------------------------------ */
/* DefinitionFactoryMgrClass                                           */
/* ------------------------------------------------------------------ */

TEST(definition_factories_are_findable_by_id_and_name)
{
	DefinitionFactoryClass *factory = DefinitionFactoryMgrClass::Find_Factory(CLASSID_TEST_DEF);
	CHECK(factory != NULL);
	CHECK_EQ(factory->Get_Class_ID(), (uint32)CLASSID_TEST_DEF);
	CHECK_STR(factory->Get_Name(), "TestDefinition");
	CHECK(factory->Is_Displayed());

	/* Name lookup is case insensitive. */
	CHECK_EQ(DefinitionFactoryMgrClass::Find_Factory("testdefinition"), factory);
	CHECK_EQ(DefinitionFactoryMgrClass::Find_Factory("TESTDEFINITION"), factory);

	CHECK(DefinitionFactoryMgrClass::Find_Factory("no such factory") == NULL);
	CHECK(DefinitionFactoryMgrClass::Find_Factory((uint32)0x7fffffff) == NULL);
}

TEST(definition_factory_creates_its_own_type)
{
	DefinitionFactoryClass *factory = DefinitionFactoryMgrClass::Find_Factory(CLASSID_TEST_DEF);
	CHECK(factory != NULL);

	DefinitionClass *def = factory->Create();
	CHECK(def != NULL);
	CHECK_EQ(def->Get_Class_ID(), (uint32)CLASSID_TEST_DEF);
	CHECK_EQ(def->Get_ID(), 0u);
	delete def;
}

TEST(definition_factory_enumeration_by_superclass)
{
	/* Both test factories live under the same superclass, so walking that
	   superclass has to hand back exactly those two. */
	int found_a = 0;
	int found_b = 0;

	for (	DefinitionFactoryClass *f = DefinitionFactoryMgrClass::Get_First(CLASSID_DUMMY_OBJECTS);
			f != NULL;
			f = DefinitionFactoryMgrClass::Get_Next(f, CLASSID_DUMMY_OBJECTS))
	{
		CHECK_EQ(SuperClassID_From_ClassID(f->Get_Class_ID()), (uint32)CLASSID_DUMMY_OBJECTS);
		if (f->Get_Class_ID() == CLASSID_TEST_DEF)	found_a++;
		if (f->Get_Class_ID() == CLASSID_TEST_DEF_B)	found_b++;
	}

	CHECK_EQ(found_a, 1);
	CHECK_EQ(found_b, 1);
}

TEST(definition_factory_full_enumeration_reaches_both)
{
	int count = 0;
	int found_a = 0;

	for (	DefinitionFactoryClass *f = DefinitionFactoryMgrClass::Get_First();
			f != NULL;
			f = DefinitionFactoryMgrClass::Get_Next(f))
	{
		count++;
		if (f->Get_Class_ID() == CLASSID_TEST_DEF) found_a++;
	}

	CHECK(count >= 2);
	CHECK_EQ(found_a, 1);
}

TEST(superclass_id_buckets_by_range)
{
	CHECK_EQ(SuperClassID_From_ClassID(CLASSID_TEST_DEF), (uint32)CLASSID_DUMMY_OBJECTS);
	CHECK_EQ(SuperClassID_From_ClassID(CLASSID_TEST_DEF_B), (uint32)CLASSID_DUMMY_OBJECTS);
	CHECK_EQ(SuperClassID_From_ClassID(CLASSID_DUMMY_OBJECTS + DEF_CLASSID_RANGE - 1),
	         (uint32)CLASSID_DUMMY_OBJECTS);
	CHECK_EQ(SuperClassID_From_ClassID(CLASSID_DUMMY_OBJECTS + DEF_CLASSID_RANGE),
	         (uint32)CLASSID_BUILDINGS);
	CHECK_EQ(SuperClassID_From_ClassID(CLASSID_TERRAIN), (uint32)CLASSID_TERRAIN);
}

/* ------------------------------------------------------------------ */
/* DefinitionMgrClass                                                  */
/* ------------------------------------------------------------------ */

TEST(definitions_are_stored_sorted_by_id)
{
	/* Registered out of order on purpose - Register_Definition binary
	   searches for the insertion point. */
	DefinitionClass *c = make_def(300, "Charlie");
	DefinitionClass *a = make_def(100, "Alpha");
	DefinitionClass *d = make_def(400, "Delta");
	DefinitionClass *b = make_def(200, "Bravo");

	DefinitionClass *cur = DefinitionMgrClass::Get_First();
	CHECK_EQ(cur, a);
	cur = DefinitionMgrClass::Get_Next(cur);
	CHECK_EQ(cur, b);
	cur = DefinitionMgrClass::Get_Next(cur);
	CHECK_EQ(cur, c);
	cur = DefinitionMgrClass::Get_Next(cur);
	CHECK_EQ(cur, d);
	CHECK(DefinitionMgrClass::Get_Next(cur) == NULL);

	free_def(a);
	free_def(b);
	free_def(c);
	free_def(d);
	CHECK(DefinitionMgrClass::Get_First() == NULL);
}

TEST(find_definition_by_id)
{
	DefinitionClass *defs[8];
	for (int i = 0; i < 8; ++i) {
		char name[32];
		sprintf(name, "Def%d", i);
		defs[i] = make_def(1000 + i * 7, name);
	}

	for (int j = 0; j < 8; ++j) {
		CHECK_EQ(DefinitionMgrClass::Find_Definition(1000 + j * 7), defs[j]);
	}

	/* Ids that fall between, before and after the registered ones. */
	CHECK(DefinitionMgrClass::Find_Definition(1001) == NULL);
	CHECK(DefinitionMgrClass::Find_Definition(1) == NULL);
	CHECK(DefinitionMgrClass::Find_Definition(999999) == NULL);

	for (int k = 0; k < 8; ++k)
		free_def(defs[k]);
}

TEST(find_definition_with_a_single_entry)
{
	DefinitionClass *only = make_def(42, "Only");

	CHECK_EQ(DefinitionMgrClass::Find_Definition(42), only);
	CHECK(DefinitionMgrClass::Find_Definition(41) == NULL);
	CHECK(DefinitionMgrClass::Find_Definition(43) == NULL);

	free_def(only);
	CHECK(DefinitionMgrClass::Find_Definition(42) == NULL);
}

TEST(find_named_definition_is_case_insensitive)
{
	DefinitionClass *a = make_def(10, "Alpha Unit");
	DefinitionClass *b = make_def(20, "Bravo Unit");

	CHECK_EQ(DefinitionMgrClass::Find_Named_Definition("Alpha Unit"), a);
	CHECK_EQ(DefinitionMgrClass::Find_Named_Definition("alpha unit"), a);
	CHECK_EQ(DefinitionMgrClass::Find_Named_Definition("BRAVO UNIT"), b);
	CHECK(DefinitionMgrClass::Find_Named_Definition("Charlie") == NULL);

	free_def(a);
	free_def(b);
}

TEST(find_typed_definition_matches_class_and_superclass)
{
	DefinitionClass *a = new TestDefinitionClass;
	((TestDefinitionClass *)a)->Set_Name("Typed A");
	a->Set_ID(500);
	DefinitionMgrClass::Register_Definition(a);

	DefinitionClass *b = new TestDefinitionBClass;
	((TestDefinitionBClass *)b)->Set_Name("Typed B");
	b->Set_ID(501);
	DefinitionMgrClass::Register_Definition(b);

	CHECK_EQ(DefinitionMgrClass::Find_Typed_Definition("Typed A", CLASSID_TEST_DEF), a);
	CHECK_EQ(DefinitionMgrClass::Find_Typed_Definition("typed b", CLASSID_TEST_DEF_B), b);

	/* The superclass matches too - both classes share CLASSID_DUMMY_OBJECTS. */
	CHECK_EQ(DefinitionMgrClass::Find_Typed_Definition("Typed B", CLASSID_DUMMY_OBJECTS), b);

	/* Wrong class id for that name.  This is the path that used to walk the
	   cache vector's uninitialized tail - see the Count()/Length() fix in
	   definitionmgr.cpp. */
	CHECK(DefinitionMgrClass::Find_Typed_Definition("Typed A", CLASSID_LIGHT) == NULL);
	CHECK(DefinitionMgrClass::Find_Typed_Definition("No Such Name", CLASSID_TEST_DEF) == NULL);

	free_def(a);
	free_def(b);
}

TEST(definition_enumeration_by_class_and_superclass)
{
	DefinitionClass *a1 = new TestDefinitionClass;
	a1->Set_ID(600);
	DefinitionMgrClass::Register_Definition(a1);

	DefinitionClass *b1 = new TestDefinitionBClass;
	b1->Set_ID(601);
	DefinitionMgrClass::Register_Definition(b1);

	DefinitionClass *a2 = new TestDefinitionClass;
	a2->Set_ID(602);
	DefinitionMgrClass::Register_Definition(a2);

	/* ID_CLASS sees only the exact class... */
	int class_count = 0;
	for (	DefinitionClass *d = DefinitionMgrClass::Get_First(CLASSID_TEST_DEF, DefinitionMgrClass::ID_CLASS);
			d != NULL;
			d = DefinitionMgrClass::Get_Next(d, CLASSID_TEST_DEF, DefinitionMgrClass::ID_CLASS))
	{
		CHECK_EQ(d->Get_Class_ID(), (uint32)CLASSID_TEST_DEF);
		class_count++;
	}
	CHECK_EQ(class_count, 2);

	/* ...ID_SUPERCLASS sees the whole family. */
	int super_count = 0;
	for (	DefinitionClass *d = DefinitionMgrClass::Get_First(CLASSID_DUMMY_OBJECTS, DefinitionMgrClass::ID_SUPERCLASS);
			d != NULL;
			d = DefinitionMgrClass::Get_Next(d, CLASSID_DUMMY_OBJECTS, DefinitionMgrClass::ID_SUPERCLASS))
	{
		super_count++;
	}
	CHECK_EQ(super_count, 3);

	CHECK(DefinitionMgrClass::Get_First(CLASSID_LIGHT, DefinitionMgrClass::ID_CLASS) == NULL);

	free_def(a1);
	free_def(b1);
	free_def(a2);
}

TEST(registering_a_duplicate_id_is_refused)
{
	DefinitionClass *first = make_def(900, "First");

	DefinitionClass *dup = new TestDefinitionClass;
	((TestDefinitionClass *)dup)->Set_Name("Duplicate");
	dup->Set_ID(900);
	DefinitionMgrClass::Register_Definition(dup);

	/* The second registration is dropped, the first one still owns the id. */
	CHECK_EQ(DefinitionMgrClass::Find_Definition(900), first);
	CHECK_EQ(DefinitionMgrClass::Get_First(), first);
	CHECK(DefinitionMgrClass::Get_Next(first) == NULL);

	delete dup;
	free_def(first);
}

TEST(a_definition_with_id_zero_is_not_registered)
{
	DefinitionClass *def = new TestDefinitionClass;
	((TestDefinitionClass *)def)->Set_Name("Unset");
	DefinitionMgrClass::Register_Definition(def);

	CHECK(DefinitionMgrClass::Get_First() == NULL);

	delete def;
}

TEST(changing_an_id_relinks_the_definition)
{
	DefinitionClass *a = make_def(10, "A");
	DefinitionClass *b = make_def(20, "B");

	/* Set_ID re-registers, so the sort order has to follow. */
	a->Set_ID(30);

	CHECK_EQ(DefinitionMgrClass::Get_First(), b);
	CHECK_EQ(DefinitionMgrClass::Get_Next(b), a);
	CHECK_EQ(DefinitionMgrClass::Find_Definition(30), a);
	CHECK(DefinitionMgrClass::Find_Definition(10) == NULL);

	free_def(a);
	free_def(b);
}

TEST(unregistering_reindexes_the_rest)
{
	DefinitionClass *a = make_def(10, "A");
	DefinitionClass *b = make_def(20, "B");
	DefinitionClass *c = make_def(30, "C");

	free_def(b);

	CHECK_EQ(DefinitionMgrClass::Get_First(), a);
	CHECK_EQ(DefinitionMgrClass::Get_Next(a), c);
	CHECK(DefinitionMgrClass::Get_Next(c) == NULL);
	CHECK(DefinitionMgrClass::Find_Definition(20) == NULL);

	/* Unregistering twice is harmless. */
	DefinitionMgrClass::Unregister_Definition(c);
	DefinitionMgrClass::Unregister_Definition(c);
	CHECK_EQ(DefinitionMgrClass::Get_First(), a);
	delete c;

	free_def(a);
}

TEST(get_new_id_walks_the_class_id_range)
{
	/* Each class owns a block of 10000 ids starting at its offset from
	   DEF_CLASSID_START. */
	const uint32 range_start = (CLASSID_TEST_DEF - DEF_CLASSID_START) * 10000;

	CHECK_EQ(DefinitionMgrClass::Get_New_ID(CLASSID_TEST_DEF), range_start + 1);

	DefinitionClass *first = make_def(range_start + 1, "First");
	CHECK_EQ(DefinitionMgrClass::Get_New_ID(CLASSID_TEST_DEF), range_start + 2);

	DefinitionClass *second = make_def(range_start + 2, "Second");
	CHECK_EQ(DefinitionMgrClass::Get_New_ID(CLASSID_TEST_DEF), range_start + 3);

	/* A hole in the middle gets filled before the tail is extended. */
	DefinitionClass *fourth = make_def(range_start + 4, "Fourth");
	CHECK_EQ(DefinitionMgrClass::Get_New_ID(CLASSID_TEST_DEF), range_start + 3);

	free_def(first);
	free_def(second);
	free_def(fourth);
}

TEST(definition_save_load_round_trip)
{
	static char storage[2048];
	int written = 0;

	TestDefinitionClass original;
	original.Set_Name("Saved Definition");
	original.Set_ID(4242);
	original.Set_User_Data(0x99);

	{
		RAMFileClass file(storage, sizeof(storage));
		file.Open(FileClass::WRITE);
		ChunkSaveClass csave(&file);
		original.Get_Factory().Save(csave, &original);
		written = file.Size();
		file.Close();
	}

	RAMFileClass file(storage, written);
	file.Open(FileClass::READ);
	ChunkLoadClass cload(&file);
	PersistClass *loaded = SaveLoadSystemClass::Find_Persist_Factory(CHUNKID_TEST_DEFINITION)->Load(cload);
	file.Close();

	CHECK(loaded != NULL);
	DefinitionClass *def = (DefinitionClass *)(TestDefinitionClass *)loaded;
	CHECK_EQ(def->Get_ID(), 4242u);
	CHECK_STR(def->Get_Name(), "Saved Definition");

	/* Only the id and the name are persisted - the user data and the save
	   flag are runtime-only and come back at their defaults. */
	CHECK_EQ(def->Get_User_Data(), 0u);
	CHECK(def->Is_Save_Enabled());

	delete loaded;
}

/* ------------------------------------------------------------------ */
/* TwiddlerClass                                                       */
/* ------------------------------------------------------------------ */

TEST(twiddler_module_is_linked_and_registered)
{
	/* twiddler.obj holds nothing anyone references, so without the force
	   link its static factories never make it into the executable. */
	FORCE_LINK(Twiddler);

	CHECK(SaveLoadSystemClass::Find_Persist_Factory(CHUNKID_TWIDDLER) != NULL);

	DefinitionFactoryClass *factory = DefinitionFactoryMgrClass::Find_Factory(CLASSID_TWIDDLERS);
	CHECK(factory != NULL);
	CHECK_STR(factory->Get_Name(), "Twiddler");
}

TEST(an_empty_twiddler_twiddles_to_nothing)
{
	FORCE_LINK(Twiddler);

	TwiddlerClass twiddler;
	CHECK_EQ(twiddler.Get_Class_ID(), (uint32)CLASSID_TWIDDLERS);
	CHECK_EQ(twiddler.Get_Indirect_Class_ID(), 0u);

	/* With no entries in its preset list there is nothing to pick. */
	CHECK(twiddler.Twiddle() == NULL);
	CHECK(twiddler.Create() == NULL);

	twiddler.Set_Indirect_Class_ID(CLASSID_TEST_DEF);
	CHECK_EQ(twiddler.Get_Indirect_Class_ID(), (uint32)CLASSID_TEST_DEF);
}

TEST(find_definition_indirects_through_a_twiddler)
{
	FORCE_LINK(Twiddler);

	TwiddlerClass *twiddler = new TwiddlerClass;
	twiddler->Set_Name("Empty Twiddler");
	twiddler->Set_ID(7000);
	DefinitionMgrClass::Register_Definition(twiddler);

	/* twiddle=true asks the twiddler for a victim, and an empty one has
	   none, so the lookup comes back NULL even though the definition is
	   there.  twiddle=false hands back the twiddler itself. */
	CHECK(DefinitionMgrClass::Find_Definition(7000, true) == NULL);
	CHECK_EQ(DefinitionMgrClass::Find_Definition(7000, false), (DefinitionClass *)twiddler);

	CHECK(DefinitionMgrClass::Find_Named_Definition("Empty Twiddler", true) == NULL);
	CHECK_EQ(DefinitionMgrClass::Find_Named_Definition("empty twiddler", false),
	         (DefinitionClass *)twiddler);

	free_def(twiddler);
}

TEST(twiddler_round_trips_through_its_persist_factory)
{
	FORCE_LINK(Twiddler);

	static char storage[2048];
	int written = 0;

	TwiddlerClass original;
	original.Set_Name("Round Trip Twiddler");
	original.Set_ID(7100);
	original.Set_Indirect_Class_ID(CLASSID_TEST_DEF);

	{
		RAMFileClass file(storage, sizeof(storage));
		file.Open(FileClass::WRITE);
		ChunkSaveClass csave(&file);
		original.Get_Factory().Save(csave, &original);
		written = file.Size();
		file.Close();
	}

	CHECK(written > 0);

	RAMFileClass file(storage, written);
	file.Open(FileClass::READ);
	ChunkLoadClass cload(&file);
	PersistClass *loaded = SaveLoadSystemClass::Find_Persist_Factory(CHUNKID_TWIDDLER)->Load(cload);
	file.Close();

	CHECK(loaded != NULL);
	TwiddlerClass *twiddler = (TwiddlerClass *)loaded;
	CHECK_EQ(twiddler->Get_ID(), 7100u);
	CHECK_STR(twiddler->Get_Name(), "Round Trip Twiddler");
	CHECK_EQ(twiddler->Get_Indirect_Class_ID(), (uint32)CLASSID_TEST_DEF);

	delete loaded;
}
