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
	Skill,
	Resource,
	Bonus
};

struct SpecialtyArtworkBinding
{
	std::string_view hero;
	SpecialtyArtworkFamily family;
	std::string_view target;
	std::string_view stem;
};
inline constexpr std::array<SpecialtyArtworkBinding, 119> specialtyArtworkBindings = {{
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
	{"core:vokial", SpecialtyArtworkFamily::Creature, "core:vampire", "VokialVampireLine"},
	{"core:krellion", SpecialtyArtworkFamily::Creature, "core:ogre", "KrellionOgreLine"},
	{"core:gundula", SpecialtyArtworkFamily::Skill, "core:offence", "GundulaOffenseCore"},
	{"core:dessa", SpecialtyArtworkFamily::Skill, "core:logistics", "DessaLogisticsCore"},
	{"core:drakon", SpecialtyArtworkFamily::Creature, "core:gnoll", "DrakonGnollLine"},
	{"core:broghild", SpecialtyArtworkFamily::Creature, "core:wyvern", "BroghildWyvernLine"},
	{"core:alkin", SpecialtyArtworkFamily::Creature, "core:gorgon", "AlkinGorgonLine"},
	{"core:korbac", SpecialtyArtworkFamily::Creature, "core:serpentFly", "KorbacSerpentFlyLine"},
	{"core:tazar", SpecialtyArtworkFamily::Skill, "core:armorer", "TazarArmorerCore"},
	{"core:wystan", SpecialtyArtworkFamily::Creature, "core:lizardman", "WystanLizardmanLine"},
	{"core:moandor", SpecialtyArtworkFamily::Creature, "core:lich", "MoandorLichLine"},
	{"core:galthran", SpecialtyArtworkFamily::Creature, "core:skeleton", "GalthranSkeletonLine"},
	{"core:dace", SpecialtyArtworkFamily::Creature, "core:minotaur", "DaceMinotaurLine"},
	{"core:ajit", SpecialtyArtworkFamily::Creature, "core:beholder", "AjitBeholderLine"},
	{"core:shakti", SpecialtyArtworkFamily::Creature, "core:troglodyte", "ShaktiTroglodyteLine"},
	{"core:synca", SpecialtyArtworkFamily::Creature, "core:manticore", "SyncaManticoreLine"},
	{"core:yog", SpecialtyArtworkFamily::Creature, "core:cyclop", "YogCyclopLine"},
	{"core:jabarkas", SpecialtyArtworkFamily::Creature, "core:orc", "JabarkasOrcLine"},
	{"core:charna", SpecialtyArtworkFamily::Creature, "core:wight", "CharnaWightLine"},
	{"core:tamika", SpecialtyArtworkFamily::Creature, "core:blackKnight", "TamikaBlackKnightLine"},
	{"core:bron", SpecialtyArtworkFamily::Creature, "core:basilisk", "BronBasiliskLine"},
	{"core:cragHack", SpecialtyArtworkFamily::Skill, "core:offence", "CragHackOffenseCore"},
	{"core:uland", SpecialtyArtworkFamily::Spell, "core:cure", "UlandCure"},
	{"core:alagar", SpecialtyArtworkFamily::Spell, "core:iceBolt", "AlagarIceBolt"},
	{"core:luna", SpecialtyArtworkFamily::Spell, "core:fireWall", "LunaFireWall"},
	{"core:ciele", SpecialtyArtworkFamily::Spell, "core:magicArrow", "CieleMagicArrow"},
	{"core:adelaide", SpecialtyArtworkFamily::Spell, "core:frostRing", "AdelaideFrostRing"},
	{"core:cuthbert", SpecialtyArtworkFamily::Spell, "new-horizons:frailty", "CuthbertFrailty"},
	{"core:deemer", SpecialtyArtworkFamily::Spell, "core:meteorShower", "DeemerMeteorShower"},
	{"core:xarfax", SpecialtyArtworkFamily::Spell, "core:fireball", "XarfaxFireball"},
	{"core:septienna", SpecialtyArtworkFamily::Spell, "new-horizons:plague", "SeptiennaPlague"},
	{"core:thant", SpecialtyArtworkFamily::Spell, "new-horizons:reanimate", "ThantReanimate"},
	{"core:coronius", SpecialtyArtworkFamily::Spell, "new-horizons:holyWrath", "CoroniusHolyWrath"},
	{"core:loynis", SpecialtyArtworkFamily::Spell, "new-horizons:crusade", "LoynisCrusade"},
	{"core:adela", SpecialtyArtworkFamily::Spell, "core:bless", "AdelaBless"},
	{"core:arlach", SpecialtyArtworkFamily::Creature, "core:ballista", "ArlachBallistaLine"},
	{"core:thunar", SpecialtyArtworkFamily::Creature, "core:earthElemental", "ThunarEarthElementalLine"},
	{"core:lacus", SpecialtyArtworkFamily::Creature, "core:waterElemental", "LacusWaterElementalLine"},
	{"core:ignissa", SpecialtyArtworkFamily::Creature, "core:fireElemental", "IgnissaFireElementalLine"},
	{"core:xyron", SpecialtyArtworkFamily::Spell, "core:inferno", "XyronInferno"},
	{"core:darkstorn", SpecialtyArtworkFamily::Spell, "new-horizons:hexOfPain", "DarkstornHexOfPain"},
	{"core:merist", SpecialtyArtworkFamily::Spell, "new-horizons:hydrasVitality", "MeristHydrasVitality"},
	{"core:sylvia", SpecialtyArtworkFamily::Skill, "core:logistics", "SylviaLogisticsCore"},
	{"core:lordHaart", SpecialtyArtworkFamily::Skill, "core:estates", "LordHaartEstatesCore"},
	{"core:caitlin", SpecialtyArtworkFamily::Resource, "gold", "CaitlinGoldIncome"},
	{"core:clavius", SpecialtyArtworkFamily::Resource, "gold", "ClaviusGoldIncome"},
	{"core:rissa", SpecialtyArtworkFamily::Resource, "mercury", "RissaMercuryIncome"},
	{"core:calid", SpecialtyArtworkFamily::Resource, "sulfur", "CalidSulfurIncome"},
	{"core:saurug", SpecialtyArtworkFamily::Resource, "gems", "SaurugGemsIncome"},
	{"core:sephinroth", SpecialtyArtworkFamily::Resource, "crystal", "SephinrothCrystalIncome"},
	{"core:neela", SpecialtyArtworkFamily::Skill, "core:armorer", "NeelaArmorerCore"},
	{"core:gunnar", SpecialtyArtworkFamily::Skill, "core:logistics", "GunnarLogisticsCore"},
	{"core:gelare", SpecialtyArtworkFamily::Resource, "gold", "GelareGoldIncome"},
	{"core:grindan", SpecialtyArtworkFamily::Resource, "gold", "GrindanGoldIncome"},
	{"core:aine", SpecialtyArtworkFamily::Resource, "gold", "AineGoldIncome"},
	{"core:octavia", SpecialtyArtworkFamily::Resource, "gold", "OctaviaGoldIncome"},
	{"core:nagash", SpecialtyArtworkFamily::Resource, "gold", "NagashGoldIncome"},
	{"core:damacon", SpecialtyArtworkFamily::Resource, "gold", "DamaconGoldIncome"},
	{"core:jenova", SpecialtyArtworkFamily::Resource, "gold", "JenovaGoldIncome"},
	{"core:piquedram", SpecialtyArtworkFamily::Creature, "core:stoneGargoyle", "PiquedramGargoyleLine"},
	{"core:erdamon", SpecialtyArtworkFamily::Creature, "core:earthElemental", "ErdamonEarthElementalLine"},
	{"core:kyrre", SpecialtyArtworkFamily::Skill, "core:logistics", "KyrreLogistics"},
	{"core:alamar", SpecialtyArtworkFamily::Spell, "core:resurrection", "AlamarResurrection"},
	{"core:mephala", SpecialtyArtworkFamily::Skill, "core:armorer", "MephalaArmorer"},
	{"core:vey", SpecialtyArtworkFamily::Creature, "core:ogre", "VeyOgreLine"},
	{"core:thane", SpecialtyArtworkFamily::Creature, "core:genie", "ThaneGenieLine"},
	{"core:josephine", SpecialtyArtworkFamily::Creature, "core:ironGolem", "JosephineGolemLine"},
	{"core:torosar", SpecialtyArtworkFamily::Creature, "core:ballista", "TorosarBallistaLine"},
	{"core:fafner", SpecialtyArtworkFamily::Creature, "core:naga", "FafnerNagaLine"},
	{"core:iona", SpecialtyArtworkFamily::Creature, "core:genie", "IonaGenieLine"},
	{"core:halon", SpecialtyArtworkFamily::Bonus, "METAMAGIC_USES_PER_COMBAT", "HalonMetamagicAdept"},
	{"core:theodorus", SpecialtyArtworkFamily::Creature, "core:mage", "TheodorusMageLine"},
	{"core:solmyr", SpecialtyArtworkFamily::Spell, "new-horizons:masterChainLightning", "SolmyrChainLightning"},
	{"core:lorelei", SpecialtyArtworkFamily::Creature, "core:harpy", "LoreleiHarpyLine"},
	{"core:marius", SpecialtyArtworkFamily::Creature, "core:demon", "MariusDemonLine"},
	{"core:brissa", SpecialtyArtworkFamily::Spell, "core:haste", "BrissaHaste"},
	{"core:aenain", SpecialtyArtworkFamily::Spell, "new-horizons:frailty", "AenainFrailty"},
	{"core:olema", SpecialtyArtworkFamily::Spell, "new-horizons:frailty", "OlemaFrailty"},
	{"core:xsi", SpecialtyArtworkFamily::Spell, "new-horizons:frailty", "XsiFrailty"},
	{"core:astral", SpecialtyArtworkFamily::Spell, "new-horizons:phantomArmy", "AstralPhantomArmy"},
	{"core:cyra", SpecialtyArtworkFamily::Spell, "core:haste", "CyraHaste"},
	{"core:rashka", SpecialtyArtworkFamily::Creature, "core:efreet", "RashkaEfreetLine"},
	{"core:labetha", SpecialtyArtworkFamily::Spell, "new-horizons:guardianSpirit", "LabethaGuardianSpirit"},
}};

/// Presentation only. Call after the captured mechanic has resolved successfully;
/// neither an installed image nor this table establishes a specialty's existence.
inline std::optional<std::string> resolvedSpecialtyArtwork(std::string_view hero,
	SpecialtyArtworkFamily family, std::string_view target)
{
	for(const auto & binding : specialtyArtworkBindings)
		if(binding.hero == hero && binding.family == family && binding.target == target)
			return "newHorizons/heroSpecialties/" + std::string(binding.stem);
	return std::nullopt;
}
}
