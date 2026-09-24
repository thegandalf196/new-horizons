/*
 * Reward.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#include "StdInc.h"
#include "Reward.h"

#include "../mapObjects/CGHeroInstance.h"
#include "../entities/hero/NewHorizonsHeroRules.h"
#include "../spells/NewHorizonsMagic.h"
#include "../serializer/JsonSerializeFormat.h"
#include "../constants/StringConstants.h"
#include "../CSkillHandler.h"

void Rewardable::RewardRevealTiles::serializeJson(JsonSerializeFormat & handler)
{
	handler.serializeBool("hide", hide);
	handler.serializeInt("scoreSurface", scoreSurface);
	handler.serializeInt("scoreSubterra", scoreSubterra);
	handler.serializeInt("scoreWater", scoreWater);
	handler.serializeInt("scoreRock", scoreRock);
	handler.serializeInt("radius", radius);
}

Rewardable::Reward::Reward()
	: heroExperience(0)
	, heroLevel(0)
	, manaDiff(0)
	, manaBuffer(0)
	, manaPercentage(-1)
	, manaOverflowFactor(0)
	, movePoints(0)
	, movePercentage(-1)
	, moveOverflowFactor(100)
	, primary(4, 0)
	, removeObject(false)
	, spellCast(SpellID::NONE, MasteryLevel::NONE)
{
}

Rewardable::Reward::~Reward() = default;

si32 Rewardable::Reward::calculateManaPoints(const CGHeroInstance * hero) const
{
	const bool spellPointRules = newHorizonsMagic::spellPointRulesActive(hero->getMagicRules());
	int64_t manaScaled = hero->getNormalSpellPoints();
	if (manaPercentage >= 0)
		manaScaled = static_cast<int64_t>(hero->manaLimit()) * manaPercentage / 100;

	if(spellPointRules)
	{
		const int64_t normalCapacity = hero->manaLimit();
		manaScaled = std::clamp<int64_t>(manaScaled, 0, normalCapacity);

		// In the two-pool rules, a negative fixed mana reward is a cost. It drains
		// Buffer first, so only the part of the cost left after Buffer can lower
		// the displayed/returned Normal value.
		if(manaDiff < 0)
		{
			const int64_t cost = -static_cast<int64_t>(manaDiff);
			const int64_t bufferSpent = std::min<int64_t>(cost, hero->getBufferSpellPoints());
			manaScaled = std::max<int64_t>(0, manaScaled - (cost - bufferSpent));
			return static_cast<si32>(manaScaled);
		}
	}

	const int64_t manaMissing = std::max<int64_t>(0, static_cast<int64_t>(hero->manaLimit()) - manaScaled);
	const int64_t manaGranted = std::min<int64_t>(manaMissing, manaDiff);
	const int64_t manaOverflow = static_cast<int64_t>(manaDiff) - manaGranted;
	const int64_t manaOverLimit = manaOverflow * manaOverflowFactor / 100;
	int64_t manaOutput = manaScaled + manaGranted + manaOverLimit;
	if(spellPointRules)
		manaOutput = std::min<int64_t>(manaOutput, hero->manaLimit());

	const int64_t minimum = spellPointRules ? 0 : std::numeric_limits<si32>::min();
	return static_cast<si32>(std::clamp<int64_t>(manaOutput, minimum, std::numeric_limits<si32>::max()));
}

si32 Rewardable::Reward::calculateMovePoints(const CGHeroInstance * hero) const
{
	si32 moveScaled = hero->movementPointsRemaining();
	si32 moveLimit = hero->movementPointsLimit();

	if (movePercentage >= 0)
		moveScaled = moveLimit * movePercentage / 100;

	si32 moveMissing   = std::max(0, moveLimit - moveScaled);
	si32 moveGranted   = std::min(moveMissing, movePoints);
	si32 moveOverflow  = movePoints - moveGranted;
	si32 moveOverLimit = moveOverflow * moveOverflowFactor / 100;
	si32 moveOutput    = moveScaled + moveGranted + moveOverLimit;

	return std::max(0, moveOutput);
}

Component Rewardable::Reward::getDisplayedComponent(const CGHeroInstance * h) const
{
	std::vector<Component> comps;
	loadComponents(comps, h);

	if (!comps.empty())
		return comps.front();

	// Rewardable requested component that represent such rewards, to be used as button in UI selection dialog, e.g. Chest with its experience / money pick
	// However reward is either completely empty OR has no rewards that target hero can receive OR these rewards have no visible component (e.g. movement)
	// Such cases are unreachable in H3, however can be reached by mods
	logMod->warn("Failed to find displayed component for reward!");
	return Component(ComponentType::NONE, 0);
}

void Rewardable::Reward::loadComponents(std::vector<Component> & comps, const CGHeroInstance * h) const
{
	for (auto comp : extraComponents)
		comps.push_back(comp);
	
	for (auto & bonus : heroBonuses)
	{
		if (bonus->type == BonusType::MORALE)
			comps.emplace_back(ComponentType::MORALE, bonus->val);
		if (bonus->type == BonusType::LUCK)
			comps.emplace_back(ComponentType::LUCK, bonus->val);
	}
	
	if (heroExperience)
		comps.emplace_back(ComponentType::EXPERIENCE, static_cast<si32>(h ? h->calculateXp(heroExperience) : heroExperience));

	if (heroLevel)
		comps.emplace_back(ComponentType::LEVEL, heroLevel);

	if (manaDiff || manaPercentage >= 0 || manaBuffer != 0)
	{
		const int64_t normalChange = h ? static_cast<int64_t>(calculateManaPoints(h)) - h->getNormalSpellPoints() : manaDiff;
		const int64_t bufferSpent = h && newHorizonsMagic::spellPointRulesActive(h->getMagicRules()) && manaDiff < 0
			? std::min<int64_t>(-static_cast<int64_t>(manaDiff), h->getBufferSpellPoints())
			: 0;
		const int64_t totalChange = normalChange - bufferSpent + manaBuffer;
		comps.emplace_back(ComponentType::MANA, static_cast<si32>(std::clamp<int64_t>(totalChange,
			std::numeric_limits<si32>::min(), std::numeric_limits<si32>::max())));
	}

	for (size_t i=0; i<primary.size(); i++)
	{
		if (primary[i] != 0)
			comps.emplace_back(ComponentType::PRIM_SKILL, PrimarySkill(i), primary[i]);
	}

	for(const auto & entry : secondary)
	{
		const auto replaced = h ? newHorizonsMagic::replacementSkill(h->getMagicRules(), entry.first) : entry.first;
		const auto skillID = h
			? newHorizonsHeroes::normalizeRewardSkill(h->getPrimaryGrowthRules(), replaced)
			: std::optional<SecondarySkill>(replaced);
		if(!skillID)
			continue;
		int levelsGained = entry.second;
		int currentLevel = h ? h->getSecSkillLevel(*skillID) : 0;
		int finalLevel = std::clamp<int>(currentLevel + levelsGained, MasteryLevel::NONE, MasteryLevel::EXPERT);
		if (finalLevel == MasteryLevel::NONE)
			comps.emplace_back(ComponentType::SEC_SKILL, *skillID);
		else
			comps.emplace_back(ComponentType::SEC_SKILL, *skillID, finalLevel);
	}

	for(const auto & entry : grantedArtifacts)
		comps.emplace_back(ComponentType::ARTIFACT, entry);

	for(const auto & entry : takenArtifacts)
		comps.emplace_back(ComponentType::ARTIFACT, entry);

	for(const auto & entry : takenArtifactSlots)
	{
		if (h)
		{
			const auto & slotContent = h->getSlot(entry);
			if (slotContent->artifactID.hasValue())
				comps.emplace_back(ComponentType::ARTIFACT, slotContent->getArt()->getTypeId());
		}
	}

	for(const SpellID & spell : grantedScrolls)
		comps.emplace_back(ComponentType::SPELL, spell);

	for(const SpellID & spell : takenScrolls)
		comps.emplace_back(ComponentType::SPELL, spell);

	for(const auto & entry : spells)
	{
		bool learnable = !h || h->canLearnSpell(entry.toEntity(LIBRARY), true);
		comps.emplace_back(ComponentType::SPELL, entry, learnable ?	0 : -1);
	}

	for(const auto & entry : creatures)
		comps.emplace_back(ComponentType::CREATURE, entry.getId(), entry.getCount());

	for (size_t i=0; i<resources.size(); i++)
	{
		if (resources[i] !=0)
			comps.emplace_back(ComponentType::RESOURCE, GameResID(i), resources[i]);
	}
}

void Rewardable::Reward::serializeJson(JsonSerializeFormat & handler)
{
	if(handler.saving && manaBuffer < 0)
		throw std::runtime_error("Buffer reward cannot be negative");
	resources.serializeJson(handler, "resources");
	handler.serializeBool("removeObject", removeObject);
	handler.serializeInt("manaPercentage", manaPercentage);
	handler.serializeInt("movePercentage", movePercentage);
	handler.serializeInt("manaBuffer", manaBuffer, 0);
	if(!handler.saving && manaBuffer < 0)
		throw std::runtime_error("Buffer reward cannot be negative");
	handler.serializeInt("heroExperience", heroExperience);
	handler.serializeInt("heroLevel", heroLevel);
	handler.serializeInt("manaDiff", manaDiff);
	handler.serializeInt("manaOverflowFactor", manaOverflowFactor);
	handler.serializeInt("movePoints", movePoints);
	handler.serializeInt("moveOverflowFactor", manaOverflowFactor);
	handler.serializeIdArray("artifacts", grantedArtifacts);
	handler.serializeIdArray("takenArtifacts", takenArtifacts);
	handler.serializeIdArray("takenArtifactSlots", takenArtifactSlots);
	handler.serializeIdArray("scrolls", grantedScrolls);
	handler.serializeIdArray("takenScrolls", takenScrolls);
	handler.serializeIdArray("spells", spells);
	handler.enterArray("creatures").serializeStruct(creatures);
	handler.enterArray("primary").serializeArray(primary);
	{
		auto a = handler.enterArray("secondary");
		std::vector<std::pair<SecondarySkill, si32>> fieldValue(secondary.begin(), secondary.end());
		a.serializeStruct<std::pair<SecondarySkill, si32>>(fieldValue, [](JsonSerializeFormat & h, std::pair<SecondarySkill, si32> & e)
		{
			h.serializeId("skill", e.first);
			h.serializeId("level", e.second, 0, [](const std::string & i){return vstd::find_pos(NSecondarySkill::levels, i);}, [](si32 i){return NSecondarySkill::levels.at(i);});
		});
		a.syncSize(fieldValue);
		secondary = std::map<SecondarySkill, si32>(fieldValue.begin(), fieldValue.end());
	}
	
	{
		auto a = handler.enterArray("creaturesChange");
		std::vector<std::pair<CreatureID, CreatureID>> fieldValue(creaturesChange.begin(), creaturesChange.end());
		a.serializeStruct<std::pair<CreatureID, CreatureID>>(fieldValue, [](JsonSerializeFormat & h, std::pair<CreatureID, CreatureID> & e)
		{
			h.serializeId("creature", e.first, CreatureID{});
			h.serializeId("amount", e.second, CreatureID{});
		});
		creaturesChange = std::map<CreatureID, CreatureID>(fieldValue.begin(), fieldValue.end());
	}
	
	{
		auto a = handler.enterStruct("spellCast");
		a->serializeId("spell", spellCast.first, SpellID{});
		a->serializeInt("level", spellCast.second);
	}
}
