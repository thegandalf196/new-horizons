/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once
#include "NativeBenchmarkEvidence.h"
#include "../server/battles/BattleTestFixture.h"
#include "../../AI/BattleAI/BattleAI.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/constants/EntityIdentifiers.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include <optional>

namespace nativeBenchmark
{
class EncounterEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit EncounterEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
class EncounterCallback final : public CBattleCallback
{
public:
	std::vector<BattleAction> pending;
	explicit EncounterCallback(PlayerColor player) : CBattleCallback(player, nullptr) {}
	void battleMakeUnitAction(const BattleID &, const BattleAction & action) override { pending.push_back(action); }
	void battleMakeSpellAction(const BattleID &, const BattleAction & action) override { pending.push_back(action); }
	std::optional<BattleAction> makeSurrenderRetreatDecision(const BattleID &, const BattleStateInfoForRetreat &) override
	{
		return std::nullopt;
	}
};
class EncounterServer final : public RecordingGameServer
{
public:
	std::optional<BattleResult> result;
	JsonNode terminalUnits;
	uint64_t packets = 0;
	void applyPack(CPackForClient & pack) override
	{
		++packets;
		if(const auto * finished = dynamic_cast<const BattleResult *>(&pack))
		{
			result = *finished;
			const auto * current = gameState->getBattle(finished->battleID);
			for(const auto * unit : current->battleGetAllUnits(false))
			{
				JsonNode row;
				row["id"].Integer() = unit->unitId();
				row["side"].Integer() = static_cast<int>(unit->unitSide());
				row["availableHealth"].Integer() = unit->getAvailableHealth();
				row["summoned"].Bool() = unit->isSummoned();
				row["state"] = unit->acquireState()->save();
				terminalUnits.Vector().push_back(std::move(row));
			}
		}
		RecordingGameServer::applyPack(pack);
	}
};
inline JsonNode armyEvidence(const CArmedInstance & army)
{
	JsonNode result;
	for(const auto & [slot, stack] : army.Slots())
	{
		JsonNode row;
		row["slot"].Integer() = slot.getNum();
		row["creature"].String() = stack->getCreature()->getJsonKey();
		row["count"].Integer() = stack->getCount();
		result.Vector().push_back(std::move(row));
	}
	return result;
}
inline JsonNode heroEvidence(const CGHeroInstance & hero)
{
	JsonNode result;
	result["type"].String() = HeroTypeID::encode(hero.getHeroTypeID().getNum());
	result["perks"] = hero.getPerkState().toJson();
	result["mana"].Integer() = hero.getManaAvailable();
	result["normalMana"].Integer() = hero.getNormalSpellPoints();
	result["bufferMana"].Integer() = hero.getBufferSpellPoints();
	result["manaCapacity"].Integer() = hero.manaLimit();
	for(const auto spell : hero.getSpellsInSpellbook())
		result["knownSpells"].Vector().emplace_back(SpellID::encode(spell.getNum()));
	for(const auto skill : {PrimarySkill::ATTACK, PrimarySkill::DEFENSE, PrimarySkill::SPELL_POWER, PrimarySkill::KNOWLEDGE})
		result["primary"].Vector().emplace_back(hero.getPrimSkillLevel(skill));
	for(const auto & [skill, rank] : hero.secSkills)
	{
		JsonNode row;
		row["skill"].String() = SecondarySkill::encode(skill.getNum());
		row["rank"].Integer() = rank;
		result["skills"].Vector().push_back(std::move(row));
	}
	return result;
}
}
