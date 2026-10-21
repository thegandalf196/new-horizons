/*
 * LuaImplementationIdentity.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "LuaImplementationIdentity.h"
#include "LuaScriptInstance.h"

#include "../lib/filesystem/NhArtSha256.h"

namespace scripting
{
namespace
{
using Implementation = spells::effects::ForecastImplementation;

constexpr unsigned UNIT_EFFECT = 1;
constexpr unsigned BATTLE_LOG = 2;
constexpr unsigned COMBAT_SCRIPT = 4;

// These are reviewed implementation contracts, not generated build metadata.
// A source change must fail closed until its forecast equivalence is reviewed.
constexpr std::string_view DAMAGE_SHA256 = "eaa1d480ac162437bc442c28574e6d79a9f3c9c0123adb826b7a15d1be8c55af";
constexpr std::string_view TIMED_SHA256 = "683dfcc8e294087de5b252592d4a2bd002298547fef78d1609afa76060aaa086";
constexpr std::string_view UNIT_EFFECT_SHA256 = "cba59fd9ea526e021104813ec9602fd4d9eb1e232e46a263d894e9417d3e39a0";
constexpr std::string_view BATTLE_LOG_SHA256 = "a05fb3578afcabce40c2f60bcac17623c7ca3b30a6ae530be2a528b0e4cb8ce7";
constexpr std::string_view FOCUS_MAGIC_SHA256 = "16ec8a55a950e5668c65ac76b2755e82217a911c4270f044121606a38c97340e";
constexpr std::string_view HEX_OF_PAIN_SHA256 = "109149f17e0c8761e532ff32cefbc6b39d58d610eca4b1e32a3d6f05a2356a00";
constexpr std::string_view COMBAT_SCRIPT_SHA256 = "f95edd70b60d3c0893d64b83948b1160391f199b2747ac21d7d146b36e7ac020";

struct ReviewedLayer
{
	std::string_view path;
	std::string_view sha256;
};

constexpr std::array<ReviewedLayer, 3> TIMED_LAYERS = {{
	{"spells/timed", TIMED_SHA256},
	{"spells/patches/timedShield", "1b07ff906170d0263936609b2498e45b4b3f3661632cc4ea0a6fa3999ea4d2d6"},
	{"spells/patches/timedBind", "227ff8633628dc42f827a1c15a72be9fed770af93c0499adcd85a9c335c6336a"}
}};

constexpr std::array<ReviewedLayer, 9> CALCULATOR_LAYERS = {{
	{"damage/damageCalculator", "817acc55e9661d15c9d85d8fb2a771f3897ee3f867ef89ac8c374c33b4ca1970"},
	{"damage/siegeWeapon", "e81a2555552e38c46826532e126bc8d17e205c2ee0147afcf52b10e6770b6240"},
	{"damage/magicElemental", "887c15857c72dc2c4c5d3b5d6ce4589257e97aadd714f64c290ca1672a74bda5"},
	{"damage/psychicElemental", "1535506d256fbe06f68e14388688eaf452717712e88d0017ab2b1fe16fad7884"},
	{"damage/vulnerableFromBack", "86cbb1a75b8ed7126019169f523ffda91489ebfd7334fb11ba95ddbfa2f1ddc8"},
	{"damage/hatesTrait", "ee1ca5fbf07cd3c6eaa9746f446884086d2c90ffb49872ed7ab3b793655da7ef"},
	{"damage/revenge", "577cce13a1521180df94ced2aee5e06688ffd7449e687e068fc296609037c88f"},
	{"damage/enemyAttackReduction", "8206cfc48b88c449a127d8a5fb4dd9afaf5ddd4ac372ce15aa1669d1b634c55b"},
	{"damage/damageReceivedCap", "f1d6fbe1fe25fc58ab6e0f67b6d24c441430a3e93742b5bf4dad2bedcc27aab5"}
}};

std::string digest(std::string_view source)
{
	return nhart::hex(nhart::sha256(std::span<const ui8>(
		reinterpret_cast<const ui8 *>(source.data()), source.size())));
}

bool matchesReviewedLayers(const LuaScriptInstance & script, std::span<const ReviewedLayer> expectedLayers)
{
	const auto & declarations = script.getPatchDeclarations();
	if(script.layers.size() != expectedLayers.size() || declarations.size() != expectedLayers.size() - 1)
		return false;
	for(size_t index = 0; index < expectedLayers.size(); ++index)
	{
		const auto & expected = expectedLayers[index];
		const auto & layer = script.layers[index];
		const auto path = ScriptPath::builtinTODO(std::string(expected.path)).addPrefix("SCRIPTS/");
		if(layer.identifier != "core:" + path.getName() || digest(layer.sourceText) != expected.sha256)
			return false;
		if(index > 0 && (declarations[index - 1].first != "core" || declarations[index - 1].second != expected.path))
			return false;
	}
	return true;
}

unsigned moduleFlag(std::string_view name)
{
	if(name == "spells/unitEffect")
		return UNIT_EFFECT;
	if(name == "battleLog")
		return BATTLE_LOG;
	if(name == "combat/combatScript")
		return COMBAT_SCRIPT;
	return 0;
}
}

LuaImplementationIdentity::LuaImplementationIdentity(std::string_view rootSource, bool hasPatchDeclarations)
	: implementation(Implementation::UNKNOWN)
{
	if(hasPatchDeclarations || rootSource.empty())
		return;
	const auto rootDigest = digest(rootSource);
	if(rootDigest == DAMAGE_SHA256)
		implementation = Implementation::CANONICAL_DAMAGE;
	else if(rootDigest == TIMED_SHA256)
		implementation = Implementation::CANONICAL_TIMED;
	else if(rootDigest == FOCUS_MAGIC_SHA256)
		combatEventImplementation = CombatEventForecastImplementation::CANONICAL_FOCUS_MAGIC;
	else if(rootDigest == HEX_OF_PAIN_SHA256)
		combatEventImplementation = CombatEventForecastImplementation::CANONICAL_HEX_OF_PAIN;
}

LuaImplementationIdentity LuaImplementationIdentity::fromScript(const LuaScriptInstance & script)
{
	if(script.layers.size() == 1 && !script.hasPatchDeclarations())
		return LuaImplementationIdentity(script.layers.front().sourceText, false);

	LuaImplementationIdentity result;
	// Only these two shipping layer chains are reviewed. Arbitrary patches remain
	// unknown, even when a failed load leaves apparently canonical source layers.
	if(matchesReviewedLayers(script, TIMED_LAYERS))
		result.implementation = Implementation::CANONICAL_TIMED;
	else if(matchesReviewedLayers(script, CALCULATOR_LAYERS))
		result.canonicalDamageCalculator = true;
	return result;
}

unsigned LuaImplementationIdentity::expectedModules() const
{
	if(implementation == Implementation::CANONICAL_DAMAGE)
		return UNIT_EFFECT | BATTLE_LOG;
	if(implementation == Implementation::CANONICAL_TIMED)
		return UNIT_EFFECT;
	if(combatEventImplementation == CombatEventForecastImplementation::CANONICAL_FOCUS_MAGIC)
		return COMBAT_SCRIPT;
	if(combatEventImplementation == CombatEventForecastImplementation::CANONICAL_HEX_OF_PAIN)
		return COMBAT_SCRIPT | BATTLE_LOG;
	return 0;
}

void LuaImplementationIdentity::invalidate()
{
	implementation = Implementation::UNKNOWN;
	canonicalDamageCalculator = false;
	combatEventImplementation = CombatEventForecastImplementation::UNKNOWN;
}

void LuaImplementationIdentity::moduleRequested(std::string_view name)
{
	const unsigned flag = moduleFlag(name);
	if(finished || !flag || !(expectedModules() & flag) || (requested & flag))
		invalidate();
	requested |= flag;
}

void LuaImplementationIdentity::moduleLoaded(std::string_view name, std::string_view source)
{
	const unsigned flag = moduleFlag(name);
	if(finished || !flag || !(expectedModules() & flag) || !(requested & flag) || (loaded & flag))
	{
		invalidate();
		return;
	}
	const auto expected = flag == UNIT_EFFECT ? UNIT_EFFECT_SHA256
		: flag == COMBAT_SCRIPT ? COMBAT_SCRIPT_SHA256 : BATTLE_LOG_SHA256;
	if(digest(source) != expected)
		invalidate();
	loaded |= flag;
}

void LuaImplementationIdentity::finish(bool initialized)
{
	if(finished || !initialized || requested != expectedModules() || loaded != expectedModules())
		invalidate();
	finished = true;
}

void LuaImplementationIdentity::executionFailed()
{
	invalidate();
}

spells::effects::ForecastImplementation LuaImplementationIdentity::get() const
{
	return finished ? implementation : Implementation::UNKNOWN;
}

bool LuaImplementationIdentity::hasCanonicalDamageCalculator() const
{
	return finished && canonicalDamageCalculator;
}

CombatEventForecastImplementation LuaImplementationIdentity::getCombatEventImplementation() const
{
	return finished ? combatEventImplementation : CombatEventForecastImplementation::UNKNOWN;
}
}
