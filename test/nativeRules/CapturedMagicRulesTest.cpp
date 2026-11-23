/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../OriginalDataRequirement.h"
#include "../../lib/nativeRules/CapturedRuleRef.h"
#include "../../lib/nativeRules/CompiledMetamagicRules.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/filesystem/ResourcePath.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/battle/BattleInfo.h"
namespace
{
using MagicRef = nativeRules::CapturedRuleRef<nativeRules::CapturedRuleKind::WorldMagic>;
}
TEST(CapturedMagicRules, SharedWorldAndBattleReferencesRoundtripTheSameExactOwner)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	nativeRules::CapturedRulePool pool;
	JsonNode raw(JsonPath::builtin("config/newHorizonsMagic"));
	const auto id = pool.intern(nativeRules::CapturedRuleKind::WorldMagic, raw);
	MagicRef world, battle;
	world.bind(pool.document(id, nativeRules::CapturedRuleKind::WorldMagic));
	battle.bind(world.owner());
	EXPECT_EQ(world.owner(), battle.owner());
	ASSERT_NE(world.owner()->metamagicEnergy(), nullptr);
	CMemorySerializer memory;
	memory.oser & pool;
	{
		nativeRules::CapturedRuleWireScope scope(memory.oser, pool);
		memory.oser & world;
		memory.oser & battle;
	}
	nativeRules::CapturedRulePool decoded;
	memory.iser & decoded;
	MagicRef restoredWorld, restoredBattle;
	{
		nativeRules::CapturedRuleWireScope scope(memory.iser, decoded);
		memory.iser & restoredWorld;
		memory.iser & restoredBattle;
	}
	EXPECT_EQ(restoredWorld.owner(), restoredBattle.owner());
	EXPECT_EQ(restoredWorld.document(), raw);
	EXPECT_EQ(restoredWorld.owner()->metamagicEnergy().get(), restoredBattle.owner()->metamagicEnergy().get());
	EXPECT_NE(restoredWorld.owner(), world.owner());
}
TEST(CapturedMagicRules, MissingTableWrongKindAndInvalidReferenceRejectBeforeReplacingOwner)
{
	for(const int64_t reference : {0LL, 1LL, 100001LL})
	{
		CMemorySerializer memory;
		bool pooled = true;
		memory.oser & pooled;
		memory.oser & reference;
		MagicRef destination{JsonNode()};
		const auto before = destination.owner();
		EXPECT_THROW(memory.iser & destination, std::runtime_error); // missing table rejects before reference interpretation
		EXPECT_EQ(destination.owner(), before);
	}
	nativeRules::CapturedRulePool wrong;
	const auto id = wrong.intern(nativeRules::CapturedRuleKind::Mastery, JsonNode());
	EXPECT_THROW(wrong.document(id, nativeRules::CapturedRuleKind::WorldMagic), std::runtime_error);
	MagicRef reference{JsonNode()};
	const auto before = reference.owner();
	EXPECT_THROW(reference.bind(wrong.document(id, nativeRules::CapturedRuleKind::Mastery)), std::runtime_error);
	EXPECT_EQ(reference.owner(), before);
}

namespace
{
class MismatchingMagicCallback : public CGameState
{
public:
	JsonNode raw;
	std::shared_ptr<const nativeRules::CapturedRuleDocument> forwarded;
	const JsonNode & getMagicRules() const override { return raw; }
	std::shared_ptr<const nativeRules::CapturedRuleDocument> getCapturedMagicRules() const override { return forwarded; }
};
}
TEST(CapturedMagicRules, BattleAdmissionPreservesRawCaptureWhenCustomCallbackForwardsAnotherOwner)
{
	VCMI_TEST_REQUIRE_ORIGINAL_DATA();
	MismatchingMagicCallback callback;
	callback.raw = JsonNode(JsonPath::builtin("config/newHorizonsMagic"));
	callback.forwarded = nativeRules::CapturedRuleDocument::compile(nativeRules::CapturedRuleKind::WorldMagic, callback.raw);
	BattleInfo matching(&callback);
	EXPECT_EQ(matching.getCapturedMagicRules(), callback.forwarded);
	const auto original = callback.raw;
	const auto * originalEnergy = callback.forwarded->metamagicEnergy()->definition(SpellID::BLESS);
	ASSERT_NE(originalEnergy, nullptr);
	// The accepted Arrow price table is fixed. Vary an otherwise configurable
	// spell so this tests a different valid capture, rather than invalid rules.
	for(auto & cost : callback.raw["spells"]["core:bless"]["costs"].Vector()) cost.Integer() += 1;
	for(auto & cost : callback.raw["spells"]["new-horizons:massBless"]["costs"].Vector()) cost.Integer() += 3;
	BattleInfo changed(&callback);
	EXPECT_NE(changed.getCapturedMagicRules(), callback.forwarded);
	EXPECT_TRUE(nativeRules::equalCapturedDocuments(changed.getMagicRules(), callback.raw));
	const auto * changedEnergy = changed.getCapturedMagicRules()->metamagicEnergy()->definition(SpellID::BLESS);
	ASSERT_NE(changedEnergy, nullptr);
	EXPECT_EQ(changedEnergy->listedMana, originalEnergy->listedMana + 1);
	callback.raw = original;
	// Value-equal JSON with changed nested scope is a genuinely different capture.
	callback.raw["spells"]["core:magicArrow"]["costs"][0].setModScope("custom-captured-origin", false);
	BattleInfo scoped(&callback);
	EXPECT_NE(scoped.getCapturedMagicRules(), callback.forwarded);
	EXPECT_TRUE(nativeRules::equalCapturedDocuments(scoped.getMagicRules(), callback.raw));
	EXPECT_FALSE(nativeRules::equalCapturedDocuments(scoped.getMagicRules(), callback.forwarded->document()));
	callback.raw = callback.forwarded->document();
	callback.raw["spells"]["core:magicArrow"]["costs"][0].setOverrideFlag(true);
	BattleInfo overridden(&callback);
	EXPECT_NE(overridden.getCapturedMagicRules(), callback.forwarded);
	EXPECT_TRUE(nativeRules::equalCapturedDocuments(overridden.getMagicRules(), callback.raw));
}
