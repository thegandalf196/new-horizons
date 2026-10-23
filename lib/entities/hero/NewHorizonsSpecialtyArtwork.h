/*
 * NewHorizonsSpecialtyArtwork.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace newHorizonsHeroes::detail
{
enum class SpecialtyArtworkFamily
{
	Creature,
	Spell,
	Skill
};

/// Presentation only. Call after the captured mechanic has resolved successfully;
/// neither an installed image nor this table establishes a specialty's existence.
inline std::optional<std::string> resolvedSpecialtyArtwork(std::string_view hero,
	SpecialtyArtworkFamily family, std::string_view target)
{
	struct Binding
	{
		std::string_view hero;
		SpecialtyArtworkFamily family;
		std::string_view target;
		std::string_view stem;
	};
	static constexpr std::array<Binding, 36> bindings = {{
		{"core:monere", SpecialtyArtworkFamily::Creature, "new-horizons:wisp", "MonereWispLine"},
		{"core:pasis", SpecialtyArtworkFamily::Creature, "new-horizons:wisp", "PasisWispLine"},
		{"core:gerwulf", SpecialtyArtworkFamily::Creature, "core:ballista", "GerwulfBallistaLine"},
		{"core:christian", SpecialtyArtworkFamily::Creature, "core:ballista", "ChristianBallistaLine"},
		{"core:melodia", SpecialtyArtworkFamily::Spell, "core:bless", "MelodiaBless"},
		{"core:pyre", SpecialtyArtworkFamily::Creature, "core:ballista", "PyreBallistaLine"},
		{"core:ash", SpecialtyArtworkFamily::Spell, "core:fireball", "AshFireball"},
		{"core:aislinn", SpecialtyArtworkFamily::Spell, "core:meteorShower", "AislinnMeteorShower"},
		{"core:jeddite", SpecialtyArtworkFamily::Spell, "core:resurrection", "JedditeResurrection"},
		{"core:gurnisson", SpecialtyArtworkFamily::Creature, "core:ballista", "GurnissonBallistaLine"},
		{"core:terek", SpecialtyArtworkFamily::Spell, "core:haste", "TerekHaste"},
		{"core:mirlanda", SpecialtyArtworkFamily::Spell, "new-horizons:frailty", "MirlandaFrailty"},
		{"core:voy", SpecialtyArtworkFamily::Skill, "core:logistics", "VoyLogistics"},
		{"core:inteus", SpecialtyArtworkFamily::Spell, "new-horizons:crusade", "InteusCrusade"},
		{"core:kalt", SpecialtyArtworkFamily::Creature, "core:waterElemental", "KaltWaterElementalLine"},
		{"core:fiur", SpecialtyArtworkFamily::Creature, "core:fireElemental", "FiurFireElementalLine"},
		{"core:orrin", SpecialtyArtworkFamily::Skill, "core:archery", "OrrinArchery"},
		{"core:valeska", SpecialtyArtworkFamily::Creature, "core:archer", "ValeskaArcherLine"},
		{"core:edric", SpecialtyArtworkFamily::Creature, "core:griffin", "EdricGriffinLine"},
		{"core:sorsha", SpecialtyArtworkFamily::Creature, "core:swordsman", "SorshaSwordsmanLine"},
		{"core:tyris", SpecialtyArtworkFamily::Creature, "core:cavalier", "TyrisCavalierLine"},
		{"core:ivor", SpecialtyArtworkFamily::Creature, "core:woodElf", "IvorElfLine"},
		{"core:ufretin", SpecialtyArtworkFamily::Creature, "core:dwarf", "UfretinDwarfLine"},
		{"core:ryland", SpecialtyArtworkFamily::Creature, "core:dendroidGuard", "RylandDendroidLine"},
		{"core:clancy", SpecialtyArtworkFamily::Creature, "core:unicorn", "ClancyUnicornLine"},
		{"core:aeris", SpecialtyArtworkFamily::Creature, "core:pegasus", "AerisPegasusLine"},
		{"core:ingham", SpecialtyArtworkFamily::Creature, "core:monk", "InghamMonkLine"},
		{"core:fiona", SpecialtyArtworkFamily::Creature, "core:hellHound", "FionaHellHoundLine"},
		{"core:ignatius", SpecialtyArtworkFamily::Creature, "core:imp", "IgnatiusImpLine"},
		{"core:calh", SpecialtyArtworkFamily::Creature, "core:gog", "CalhGogLine"},
		{"core:nymus", SpecialtyArtworkFamily::Creature, "core:pitFiend", "NymusPitFiendLine"},
		{"core:straker", SpecialtyArtworkFamily::Creature, "core:walkingDead", "StrakerWalkingDeadLine"},
		{"core:zubin", SpecialtyArtworkFamily::Spell, "new-horizons:focusMagic", "ZubinFocusMagic"},
		{"core:gretchin", SpecialtyArtworkFamily::Creature, "core:goblin", "GretchinGoblinLine"},
		{"core:shiva", SpecialtyArtworkFamily::Creature, "core:roc", "ShivaRocLine"},
		{"core:tyraxor", SpecialtyArtworkFamily::Creature, "core:goblinWolfRider", "TyraxorGoblinWolfRiderLine"},
	}};
	for(const auto & binding : bindings)
		if(binding.hero == hero && binding.family == family && binding.target == target)
			return "newHorizons/heroSpecialties/" + std::string(binding.stem);
	return std::nullopt;
}
}
