/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/nativeRules/CapturedRuleEnvelope.h"
#include "../../lib/serializer/CMemorySerializer.h"

namespace
{
using namespace nativeRules;
using MasteryRef = CapturedRuleRef<CapturedRuleKind::Mastery>;

CapturedRuleSet emptyComposition(CapturedRulePool & pool)
{
	CapturedRuleSet set;
	set.documents = {pool.intern(CapturedRuleKind::Mastery, JsonNode()),
		pool.intern(CapturedRuleKind::Perks, JsonNode()),
		pool.intern(CapturedRuleKind::ResolvedCapability, JsonNode()),
		pool.intern(CapturedRuleKind::ResolvedDevelopment, JsonNode())};
	return set;
}
}

TEST(CapturedRulePool, ExactIdentityIncludesNestedScopeOverrideAndNumericType)
{
	JsonNode original;
	original["child"]["number"].Integer() = 1;
	JsonNode changed = original;
	EXPECT_TRUE(equalCapturedDocuments(original, changed));
	changed["child"].setModScope("another-scope", false);
	EXPECT_EQ(original, changed); // Ordinary JSON equality omits this metadata.
	EXPECT_FALSE(equalCapturedDocuments(original, changed));
	changed = original;
	changed["child"]["number"].setOverrideFlag(true);
	EXPECT_FALSE(equalCapturedDocuments(original, changed));
	changed = original;
	changed["child"]["number"].Float() = 1.0;
	EXPECT_FALSE(equalCapturedDocuments(original, changed));
}

TEST(CapturedRulePool, ComponentsAndCompositionsShareOnlyExactCaptures)
{
	CapturedRulePool pool;
	const auto first = emptyComposition(pool);
	const auto second = emptyComposition(pool);
	EXPECT_EQ(pool.documentCount(), 4);
	EXPECT_EQ(pool.intern(first), pool.intern(second));
	EXPECT_EQ(pool.ruleSetCount(), 1);
	JsonNode scopedNull;
	scopedNull.setModScope("captured-origin", false);
	auto distinct = first;
	distinct.documents[0] = pool.intern(CapturedRuleKind::Mastery, scopedNull);
	EXPECT_NE(pool.intern(first), pool.intern(distinct));
	EXPECT_EQ(pool.documentCount(), 5);
	EXPECT_EQ(pool.ruleSetCount(), 2);
	EXPECT_THROW(pool.document(first.documents[0], CapturedRuleKind::Perks), std::runtime_error);
	EXPECT_THROW(pool.ruleSet(0), std::runtime_error);
}

TEST(CapturedRulePool, PooledReferencesRoundtripAndRestoreHandlerScope)
{
	CapturedRulePool pool;
	const auto id = pool.intern(CapturedRuleKind::Mastery, JsonNode());
	MasteryRef source;
	source.bind(pool.document(id, CapturedRuleKind::Mastery));
	CMemorySerializer memory;
	memory.oser & pool;
	{
		CapturedRuleWireScope scope(memory.oser, pool);
		memory.oser & source;
		memory.oser & source;
	}
	EXPECT_EQ(wireRulePool(memory.oser), nullptr);
	CapturedRulePool decoded;
	memory.iser & decoded;
	MasteryRef first, second;
	{
		CapturedRuleWireScope scope(memory.iser, decoded);
		memory.iser & first;
		memory.iser & second;
	}
	EXPECT_EQ(wireRulePool(memory.iser), nullptr);
	EXPECT_EQ(first.owner(), second.owner());
	EXPECT_EQ(first.owner(), decoded.document(id, CapturedRuleKind::Mastery));
}

TEST(CapturedRulePool, StandaloneReferencesOwnTheirCaptureAfterSourceDestruction)
{
	std::vector<std::byte> bytes;
	{
		MasteryRef source(JsonNode{});
		CMemorySerializer memory;
		memory.oser & source;
		bytes = memory.extractBuffer();
	}
	MasteryRef decoded;
	CMemorySerializer reader(std::move(bytes));
	reader.iser & decoded;
	ASSERT_TRUE(decoded.owner());
	EXPECT_TRUE(decoded.document().isNull());
	EXPECT_EQ(wireRulePool(reader.iser), nullptr);
}

TEST(CapturedRulePool, MissingTableAndWrongKindRejectWithoutReplacingReference)
{
	MasteryRef original(JsonNode{});
	const auto owner = original.owner();
	CMemorySerializer missing;
	missing.oser & true;
	missing.oser & uint32_t{1};
	EXPECT_THROW(missing.iser & original, std::runtime_error);
	EXPECT_EQ(original.owner(), owner);
	CapturedRulePool pool;
	const auto wrong = pool.intern(CapturedRuleKind::ResolvedCapability, JsonNode());
	CMemorySerializer wrongKind;
	wrongKind.oser & true;
	wrongKind.oser & wrong;
	{
		CapturedRuleWireScope scope(wrongKind.iser, pool);
		EXPECT_THROW(wrongKind.iser & original, std::runtime_error);
	}
	EXPECT_EQ(original.owner(), owner);
	EXPECT_EQ(wireRulePool(wrongKind.iser), nullptr);
}

TEST(CapturedRulePool, InvalidTableAdmissionPreservesExistingPool)
{
	CapturedRulePool pool;
	const auto id = pool.intern(CapturedRuleKind::Mastery, JsonNode());
	const auto before = pool.document(id, CapturedRuleKind::Mastery);
	CMemorySerializer invalid;
	invalid.oser & uint32_t{1};
	invalid.oser & static_cast<CapturedRuleKind>(999);
	invalid.oser & JsonNode{};
	invalid.oser & uint32_t{0};
	EXPECT_THROW(invalid.iser & pool, std::runtime_error);
	EXPECT_EQ(pool.documentCount(), 1);
	EXPECT_EQ(pool.document(id, CapturedRuleKind::Mastery), before);
}

TEST(CapturedRulePool, NewFormatMinimumAndWriterAreCoherent)
{
	EXPECT_EQ(ESerializationVersion::CURRENT, ESerializationVersion::NEW_HORIZONS_SHARED_CAPTURED_RULES);
	EXPECT_EQ(ESerializationVersion::MINIMAL, ESerializationVersion::NEW_HORIZONS_SHARED_CAPTURED_RULES);
	EXPECT_LT(ESerializationVersion::NEW_HORIZONS_SORCERY_RETIREMENT, ESerializationVersion::MINIMAL);
}

TEST(CapturedRulePool, WideAndNegativeWireIdentitiesCannotAliasValidSmallReferences)
{
	CapturedRulePool pool;
	pool.intern(CapturedRuleKind::Mastery, JsonNode());
	for(const int64_t invalid : {-1LL, 0LL, 4294967297LL})
	{
		MasteryRef target(JsonNode{});
		const auto original = target.owner();
		CMemorySerializer wire;
		wire.oser & true;
		wire.oser & invalid;
		CapturedRuleWireScope scope(wire.iser, pool);
		EXPECT_THROW(wire.iser & target, std::runtime_error);
		EXPECT_EQ(target.owner(), original);
	}
}
