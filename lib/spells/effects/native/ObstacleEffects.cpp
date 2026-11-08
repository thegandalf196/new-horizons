/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ObstaclePlacement.h"
#include "../../ISpellMechanics.h"
#include "../../NewHorizonsDivineRetribution.h"
#include "../../../combatScripts/NativeJsonParameters.h"
#include "../../../GameLibrary.h"
#include "../../../json/JsonBonus.h"
#include "../../../networkPacks/PacksForClient.h"
#include "../../../scripting/ScriptTypeDescription.h"

namespace spells::effects::native
{
namespace
{
class RemoveObstacle final : public SpellEffect
{
	bool canRemove(const CObstacleInstance & obstacle) const
	{
		const auto type = obstacle.obstacleType;
		if(parameters["removeAbsolute"].Bool() && type == CObstacleInstance::ABSOLUTE_OBSTACLE) return true;
		if(parameters["removeUsual"].Bool() && type == CObstacleInstance::USUAL) return true;
		if(parameters["removeAllSpells"].Bool() && type == CObstacleInstance::SPELL_CREATED) return true;
		if(type == CObstacleInstance::SPELL_CREATED)
			for(const auto & spell : parameters["removeSpells"].Vector())
				if(nativeEvents::spell(spell.String()) == SpellID(obstacle.ID).toSpell()) return true;
		return false;
	}
	structure::Obstacles targets(const Mechanics * mechanics, const Target & target, bool alwaysMassive) const
	{
		structure::Obstacles result;
		const auto add = [&](const auto & obstacle)
		{
			if(canRemove(*obstacle) && std::find(result.begin(), result.end(), obstacle) == result.end()) result.push_back(obstacle);
		};
		if(mechanics->isMassive() || alwaysMassive)
		{
			for(const auto & obstacle : mechanics->battle()->battleGetAllObstacles()) add(obstacle);
		}
		else for(const auto & destination : target)
			if(destination.hexValue.isValid())
				for(const auto & obstacle : mechanics->battle()->battleGetAllObstaclesOnPos(destination.hexValue, false)) add(obstacle);
		return result;
	}
public:
	bool applicableGeneral(Problem & problem, const Mechanics * mechanics) const override
	{
		return !targets(mechanics, {}, true).empty() || status::reject(mechanics, &problem);
	}
	bool applicableTarget(Problem &, const Mechanics * mechanics, const Target & target) const override
	{
		return !targets(mechanics, target, false).empty();
	}
	void apply(ServerCallback * server, const Mechanics * mechanics, const Target & target) const override
	{
		const nativeEvents::Context context{*server, *mechanics->battle(), nullptr, nullptr, parameters, {}};
		for(const auto & obstacle : targets(mechanics, target, false)) structure::removeObstacle(context, obstacle);
	}
};

class Moat final : public SpellEffect
{
public:
	void apply(ServerCallback * server, const Mechanics * mechanics, const Target &) const override
	{
		if(!mechanics->isMassive() || !mechanics->battle()->hasMoat()) return;
		const nativeEvents::Context context{*server, *mechanics->battle(), nullptr, nullptr, parameters, {}};
		const auto & assets = parameters["defender"];
		JsonNode flattened;
		for(const auto & patch : parameters["moatHexes"].Vector())
		{
			auto obstacle = structure::obstacle(*mechanics, BattleHex(static_cast<si16>(patch.Vector().at(0).Integer())));
			obstacle.obstacleType = parameters["dispellable"].Bool() ? CObstacleInstance::SPELL_CREATED : CObstacleInstance::MOAT;
			obstacle.casterSide = BattleSide::DEFENDER;
			obstacle.hidden = parameters["hidden"].Bool();
			obstacle.passable = true;
			obstacle.nativeVisible = false;
			obstacle.trap = parameters["trap"].Bool();
			obstacle.removeOnTrigger = parameters["removeOnTrigger"].Bool();
			const auto & trigger = parameters["triggerAbility"].String();
			obstacle.trigger = trigger.empty() ? SpellID(SpellID::NONE) : SpellID(SpellID::decode(trigger));
			obstacle.minimalDamage = parameters["moatDamage"].Integer();
			obstacle.appearSound = AudioPath::builtin(assets["appearSound"].String());
			obstacle.appearAnimation = AnimationPath::builtin(assets["appearAnimation"].String());
			obstacle.animation = AnimationPath::builtin(assets["animation"].String());
			for(const auto & hex : patch.Vector())
			{
				obstacle.customSize.insert(BattleHex(static_cast<si16>(hex.Integer())));
				flattened.Vector().push_back(hex);
			}
			structure::addObstacle(context, std::move(obstacle));
		}
		for(const auto & [name, definition] : parameters["bonus"].Struct())
		{
			auto bonus = definition;
			bonus["duration"].String() = "ONE_BATTLE";
			bonus["sourceType"].String() = "OTHER";
			bonus["limiters"] = JsonNode();
			bonus["limiters"]["type"].String() = "UNIT_ON_HEXES";
			bonus["limiters"]["hexes"] = flattened;
			GiveBonus packet(GiveBonus::ETarget::BATTLE);
			packet.id = mechanics->getBattleID();
			packet.bonus = nativeRules::bonusFromDescriptor(bonus);
			server->apply(packet);
		}
	}
};
}
}

namespace nativeSpells
{
std::shared_ptr<spells::effects::Effect> createObstacleEffect(const ScriptTypeDescription & description)
{
	using namespace spells::effects::native;
	if(description.sourcePath == "spells/obstacle") return std::make_shared<ObstaclePlacement>();
	if(description.sourcePath == "spells/removeObstacle") return std::make_shared<RemoveObstacle>();
	if(description.sourcePath == "spells/moat") return std::make_shared<Moat>();
	return {};
}
}
