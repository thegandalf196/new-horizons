/*
 * NewHorizonsMasteryRules.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../../constants/EntityIdentifiers.h"
#include "../../json/JsonNode.h"
#include <optional>
#include <limits>
#include <stdexcept>
#include <array>
#include <cstdint>
#include <string>

namespace newHorizonsHeroes
{
/// The engine's signed varint intentionally rejects uint64_t. Encode two bounded
/// signed halves, preserving every unsigned bit without weakening that guard.
/// Validate both halves before assigning on load (including malformed packets).
template<typename Handler> void serializeMasterySequence(Handler & h, uint64_t & sequence)
{
	constexpr int64_t maximumHalf = std::numeric_limits<uint32_t>::max();
	int64_t high = static_cast<int64_t>(sequence >> 32);
	int64_t low = static_cast<int64_t>(sequence & static_cast<uint64_t>(maximumHalf));
	h & high;
	h & low;
	if(!h.saving)
	{
		if(high < 0 || high > maximumHalf || low < 0 || low > maximumHalf)
			throw std::runtime_error("Invalid mastery sequence wire halves");
		sequence = (static_cast<uint64_t>(high) << 32) | static_cast<uint64_t>(low);
	}
}

enum class MasteryEffect : uint8_t
{
	ARTILLERY_VOLLEY = 0,
	ARTILLERY_PRECISION = 1,
	ARTILLERY_REPAIR = 2,
	LOGISTICS_FORCED_MARCH = 3,
	LOGISTICS_QUARTERMASTER = 4,
	LOGISTICS_PATHFINDER = 5
};

struct DLL_LINKAGE MasteryID
{
	std::string value;
	bool operator==(const MasteryID &) const = default;
	template<typename Handler> void serialize(Handler & h) { h & value; }
};

struct DLL_LINKAGE MasteryOption
{
	MasteryID id;
	MasteryEffect effect = MasteryEffect::ARTILLERY_VOLLEY;
	int magnitude = 0;
	std::string nameTextId;
	std::string descriptionTextId;
	/// Explicit artwork stem; assets are <iconKey>_32 and <iconKey>_64.
	std::string iconKey;
	bool operator==(const MasteryOption &) const = default;

	template<typename Handler> void serialize(Handler & h)
	{
		h & id;
		h & effect;
		h & magnitude;
		h & nameTextId;
		h & descriptionTextId;
		h & iconKey;
	}
};

/// Immutable offered choices, not a request to reconstruct current mod defaults.
/// A new query after load still refers to this saved sequence. Query identity and
/// top-of-stack checks belong to the authoritative query handler, not this DTO.
struct DLL_LINKAGE MasteryOffer
{
	ObjectInstanceID hero;
	PlayerColor player;
	SecondarySkill skill;
	uint32_t level = 0;
	uint64_t sequence = 0;
	std::array<MasteryOption, 3> options;

	template<typename Handler> void serialize(Handler & h)
	{
		h & hero;
		h & player;
		h & skill;
		h & level;
		serializeMasterySequence(h, sequence);
		h & options;
	}
};

enum class MasteryReplyError : uint8_t
{
	NONE,
	WRONG_HERO,
	WRONG_PLAYER,
	STALE_OFFER,
	INVALID_CHOICE,
	NOT_EXPERT,
	ALREADY_CHOSEN
};

/// Empty rules retain legacy semantics. Version1 is Artillery-only; version2
/// adds Logistics. These are explicit choices, never a generic rank4.
DLL_LINKAGE SecondarySkill masteryParentSkill(MasteryEffect effect);
DLL_LINKAGE void validateMasteryRules(const JsonNode & rules);
DLL_LINKAGE std::optional<std::array<MasteryOption, 3>> masteryOptions(
	const JsonNode & rules, SecondarySkill skill);
DLL_LINKAGE void validateMasteryOption(const MasteryOption & option);
/// Substitute only the saved choice magnitude into a resolved localized template.
DLL_LINKAGE std::string formatMasteryDescription(const MasteryOption & option, std::string localizedTemplate);
DLL_LINKAGE void validateMasteryOptions(const std::array<MasteryOption, 3> & options);

/// Structural validation is required before exposing or loading an offer.
DLL_LINKAGE void validateMasteryOffer(const MasteryOffer & offer);
/// Does not consume the offer, mutate skills, grant bonuses or advance a query.
DLL_LINKAGE MasteryReplyError validateMasteryReply(const MasteryOffer & offer,
	ObjectInstanceID hero, PlayerColor player, uint64_t sequence, uint32_t currentLevel,
	int currentSkillRank, bool alreadyChosen, int choice);
}
