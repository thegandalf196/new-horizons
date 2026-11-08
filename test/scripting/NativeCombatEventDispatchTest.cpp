/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "../../Global.h"
#include <gtest/gtest.h>
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/combatScripts/ICombatEventScript.h"
#include "../../lib/combatScripts/NativeCombatEvents.h"
#include "../../lib/json/JsonNode.h"
#include <algorithm>
#include <stdexcept>
#include <vector>

// Deliberately requires no resource loader, game library, or Lua context.
// Dispatch membership and forecast identity are properties of the fixed factory.
TEST(NativeCombatEventDispatchTest, EveryShippedIdentityHasExactlyItsDeclaredEventsAndForecast)
{
	using Event = CombatEventType;
	using Forecast = CombatEventForecastImplementation;
	struct Identity
	{
		const char * path;
		std::vector<Event> events;
		Forecast forecast = Forecast::UNKNOWN;
	};
	// The pinned combat Lua declarations, including the inert base/status scripts.
	const Identity identities[] = {
		{"combat/deathStare", {Event::AFTER_ATTACK}},
		{"combat/destruction", {Event::AFTER_ATTACK}},
		{"combat/transmutation", {Event::AFTER_ATTACK}},
		{"combat/lifeDrain", {Event::AFTER_ATTACK}},
		{"combat/soulSteal", {Event::AFTER_ATTACK}},
		{"combat/vampirism", {Event::AFTER_ATTACK}},
		{"combat/fireShield", {Event::AFTER_ATTACKED}},
		{"combat/hexOfPain", {Event::AFTER_ATTACK}, Forecast::CANONICAL_HEX_OF_PAIN},
		{"combat/shadowGift", {Event::AFTER_ATTACK}},
		{"combat/enchanted", {Event::BATTLE_START, Event::ROUND_START}},
		{"combat/arrowTowerDamage", {Event::BATTLE_SETUP}},
		{"combat/summonGuardians", {Event::BATTLE_START}},
		{"combat/focusMagic", {Event::AFTER_ATTACK}, Forecast::CANONICAL_FOCUS_MAGIC},
		{"combat/arcaneBreach", {}},
		{"combat/plagueStatus", {}},
		{"combat/soulChainStatus", {}},
		{"combat/combatScript", {}}};
	const CPlayerBattleCallback battle(nullptr, PlayerColor(0));
	for(const auto & identity : identities)
	{
		SCOPED_TRACE(identity.path);
		const auto implementation = nativeEvents::create(identity.path);
		ASSERT_NE(implementation, nullptr);
		EXPECT_EQ(implementation->forecastImplementation(battle), identity.forecast);
		for(int value = static_cast<int>(Event::INVALID); value <= static_cast<int>(Event::BATTLE_SETUP); ++value)
		{
			const auto requested = static_cast<Event>(value);
			SCOPED_TRACE(value);
			const bool handled = std::find(identity.events.begin(), identity.events.end(), requested) != identity.events.end();
			EXPECT_EQ(implementation->handlesEvent(battle, requested), handled);
			// Inert identities and every unhandled event reject before touching any
			// server/unit pointers; they cannot silently mutate a hypothetical battle.
			if(!handled)
			{
				EXPECT_THROW(implementation->run(nullptr, battle, requested, nullptr, nullptr, JsonNode(), CombatEventPayload()), std::runtime_error);
			}
		}
	}
	// The commander script is a patch of deathStare, never a standalone identity.
	EXPECT_EQ(nativeEvents::create("combat/deathStareCommander"), nullptr);
	EXPECT_EQ(nativeEvents::create("combat/unknown"), nullptr);
	EXPECT_EQ(nativeEvents::create("combat/DeathStare"), nullptr);
}
