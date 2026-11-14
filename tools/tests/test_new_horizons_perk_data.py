#!/usr/bin/env python3
"""Validate the canonical New Horizons Skill/perk registry shape only.

This is deliberately a data-contract test.  Active effects are expected to
have matching runtime coverage; unimplemented registry entries remain planned.
"""
import json
from pathlib import Path
import unittest

from jsonschema import Draft4Validator

ROOT = Path(__file__).resolve().parents[2]
RANKS = ("basic", "advanced", "expert")
ACTIVE_PERKS = {
    "new-horizons:command.battlePlan",
    "new-horizons:command.veteranCommander",
    "new-horizons:command.decisiveOrders",
    *{"new-horizons:authority." + key for key in (
        "massedRanks", "veteranCadre", "familiarStandards", "foreignLegion",
        "officerCorps", "missileCommand", "regimentalOrganization", "combinedArms",
        "grandMarshal", "masterOfLegions")},
    "new-horizons:metamagic.arcaneCatalyst",
    "new-horizons:metamagic.arcaneShield",
    "new-horizons:metamagic.manaConduit",
    "new-horizons:metamagic.lingeringMagic",
    "new-horizons:metamagic.arcaneFeedback",
    "new-horizons:metamagic.spellFracture",
    "new-horizons:metamagic.arcaneWellspring",
    "new-horizons:metamagic.arcaneAttunement",
    "new-horizons:metamagic.arcaneEcho",
    "new-horizons:metamagic.arcaneSingularity",
    "new-horizons:divineMandate.sacredCommand",
    "new-horizons:divineMandate.knightlyValor",
    "new-horizons:divineMandate.chaplainSReserve",
    "new-horizons:divineMandate.shieldOfFaith",
    "new-horizons:divineMandate.righteousDiscipline",
    "new-horizons:divineMandate.unwaveringStandard",
    "new-horizons:divineMandate.inspiringLeadership",
    "new-horizons:divineMandate.consecratedArms",
    "new-horizons:divineMandate.mandateOfHeaven",
    "new-horizons:divineMandate.divineFury",
    "new-horizons:demonicGating.swiftGate",
    "new-horizons:demonicGating.wideGate",
    "new-horizons:demonicGating.hellfireArrival",
    "new-horizons:demonicGating.reinforcedGate",
    "new-horizons:demonicGating.infernalOnslaught",
    "new-horizons:demonicGating.infernalBeacon",
    "new-horizons:demonicGating.hellstride",
    "new-horizons:demonicGating.infernalDiscipline",
    "new-horizons:demonicGating.masterGate",
    "new-horizons:demonicGating.endlessLegion",
    "new-horizons:battlecraft.rapidResponse",
    "new-horizons:discipline.heroicSpirit",
    "new-horizons:diplomacy.mercenaryCaptain",
    "new-horizons:diplomacy.loyalMercenaries",
    "new-horizons:diplomacy.legendaryReputation",
    "new-horizons:luck.opportunist",
    "new-horizons:command.crisisCommand",
    "new-horizons:shadowMagic.plaguebearer",
    "new-horizons:armorer.unyielding",
    "new-horizons:bulwarkOfTheMire.deepBulwark",
    "new-horizons:havocMagic.demolitionist",
    "new-horizons:havocMagic.meteorologist",
    "new-horizons:havocMagic.cataclysm",
    "new-horizons:spellcraft.preciseCasting",
    "new-horizons:command.ironWill",
    "new-horizons:warcasting.arcaneReprisal",
    "new-horizons:recruitment.drillSergeant",
    "new-horizons:recruitment.fieldInstructor",
    "new-horizons:recruitment.reinforcementDrill",
    "new-horizons:warMachines.precisionBombardment",
    "new-horizons:warMachines.breachmaker",
    "new-horizons:warMachines.fieldWorkshop",
    "new-horizons:spellcraft.crossSchoolFormula",
    "new-horizons:spellcraft.concentration",
    "new-horizons:spellcraft.extendSpell",
    "new-horizons:recruitment.recruiterSContacts",
    "new-horizons:elementalRebirth.phoenixSpark",
    "new-horizons:wisdom.spellFamiliarity",
    "new-horizons:learning.prodigy",
    "new-horizons:wisdom.sage",
    "new-horizons:learning.scholar",
    "new-horizons:learning.historian",
    "new-horizons:learning.duelLessons",
    "new-horizons:shroudOfMalassa.deepFlank",
    "new-horizons:shroudOfMalassa.encircledDoom",
    "new-horizons:shroudOfMalassa.vanish",
    "new-horizons:estates.alchemist",
    "new-horizons:learning.seasonedAdventurer",
    "new-horizons:elementalRebirth.elementalConduit",
    "new-horizons:discipline.veteranCohesion",
    "new-horizons:estates.stewardship",
    "new-horizons:elementalRebirth.swiftRebirth",
    "new-horizons:estates.prosperity",
    "new-horizons:estates.merchantPrince",
    "new-horizons:bloodrage.firstBlood",
    "new-horizons:bloodrage.slayer",
    "new-horizons:chaosMagic.realityBreaker",
    "new-horizons:luck.luckyRecovery",
    "new-horizons:shroudOfMalassa.veiledMovement",
    "new-horizons:bloodrage.avatarOfRage",
    "new-horizons:chaosMagic.shapeshifter",
    "new-horizons:chaosMagic.fateDealer",
    "new-horizons:chaosMagic.pandemoniumMaster",
    "new-horizons:logistics.pursuitMarch",
    "new-horizons:natureMagic.worldroot",
    "new-horizons:natureMagic.mireShaper",
    "new-horizons:chaosMagic.confounder",
    "new-horizons:elementalRebirth.rebirthChain",
    "new-horizons:elementalRebirth.elementalAttunement",
    "new-horizons:elementalRebirth.adaptiveElement",
    "new-horizons:elementalRebirth.perfectConvergence",
    "new-horizons:natureMagic.elementalConjurer",
    "new-horizons:discipline.espritDeCorps",
    "new-horizons:luck.serendipity",
    "new-horizons:battlecraft.preEmptiveStrike",
    "new-horizons:chaosMagic.frenziedCurse",
    "new-horizons:elementalRebirth.primalBurst",
    "new-horizons:elementalRebirth.greaterEssence",
    "new-horizons:elementalRebirth.elementalCataclysm",
    "new-horizons:chaosMagic.mindbreaker",
    "new-horizons:diplomacy.recruitmentPact",
    "new-horizons:diplomacy.envoy",
    "new-horizons:diplomacy.peacemaker",
    "new-horizons:diplomacy.tribute",
    "new-horizons:diplomacy.negotiator",
    "new-horizons:diplomacy.commonCause",
    "new-horizons:diplomacy.grandDiplomat",
    "new-horizons:battlecraft.grandTactics",
    "new-horizons:logistics.mountaineer",
    "new-horizons:logistics.rapidEmbarkation",
    "new-horizons:battlecraft.redeployment",
    "new-horizons:battlecraft.tactics",
    "new-horizons:battlecraft.passingLines",
    "new-horizons:command.doubleCommand",
    "new-horizons:bloodrage.bloodScent",
    "new-horizons:bloodrage.rageThroughPain",
    "new-horizons:bloodrage.unrelenting",
    "new-horizons:bloodrage.berserker",
    "new-horizons:bloodrage.furyUnbound",
    "new-horizons:bloodrage.endlessBloodshed",
    "new-horizons:command.commandingPresence",
    "new-horizons:discipline.steadfast",
    "new-horizons:armorer.bastion",
    "new-horizons:havocMagic.mineLayer",
    "new-horizons:natureMagic.geomancer",
    "new-horizons:lightMagic.sanctuaryKeeper",
    "new-horizons:natureMagic.venomancer",
    "new-horizons:havocMagic.pyromancer",
    "new-horizons:havocMagic.cryomancer",
    "new-horizons:havocMagic.controlledBlast",
    "new-horizons:warMachines.quartermaster",
    "new-horizons:warMachines.masterGunner",
    "new-horizons:warMachines.surgeon",
    "new-horizons:warMachines.piercingBolts",
    "new-horizons:warMachines.fortificationEngineer",
    "new-horizons:warMachines.counterBattery",
    "new-horizons:warMachines.battlefieldMedic",
    "new-horizons:spellcraft.counterpressure",
    "new-horizons:spellcraft.arcaneFocus",
    "new-horizons:spellcraft.grandFormula",
    "new-horizons:estates.taxCollector",
    "new-horizons:estates.conscription",
    "new-horizons:estates.architect",
    "new-horizons:estates.mercenaryCompany",
    "new-horizons:estates.banking",
    "new-horizons:estates.landSurveyor",
    "new-horizons:learning.mentor",
    "new-horizons:learning.philosopher",
    "new-horizons:learning.connoisseur",
    "new-horizons:learning.fieldStudy",
    "new-horizons:learning.eagleEye",
    "new-horizons:chaosMagic.paradoxShield",
    "new-horizons:wisdom.manaFasting",
    "new-horizons:wisdom.manaConservation",
    "new-horizons:wisdom.arcaneInsight",
    "new-horizons:wisdom.arcaneMomentum",
    "new-horizons:wisdom.archmage",
    "new-horizons:wisdom.mysticism",
    "new-horizons:command.combinedArms",
    "new-horizons:battlecraft.reserve",
    "new-horizons:battlecraft.overwatch",
    "new-horizons:discipline.standardBearer",
    "new-horizons:discipline.holdFast",
    "new-horizons:discipline.fearless",
    "new-horizons:command.aggressiveCommander",
    "new-horizons:command.defensiveCommander",
    "new-horizons:chaosMagic.blinkmaster",
    "new-horizons:chaosMagic.misfortuneWeaver",
    "new-horizons:shroudOfMalassa.backstab",
    "new-horizons:shroudOfMalassa.shadowHex",
    "new-horizons:shroudOfMalassa.shadowAssault",
    "new-horizons:shroudOfMalassa.noEscape",
    "new-horizons:shroudOfMalassa.evasiveShroud",
    "new-horizons:shroudOfMalassa.exposedWeakness",
    "new-horizons:spellcraft.empowerSpell",
    "new-horizons:spellcraft.spellPenetration",
    "new-horizons:spellcraft.overwhelmingFormula",
    "new-horizons:natureMagic.herbalist",
    "new-horizons:bulwarkOfTheMire.mireborn",
    "new-horizons:bulwarkOfTheMire.thickHide",
    "new-horizons:bulwarkOfTheMire.bogAmbush",
    "new-horizons:bulwarkOfTheMire.toxicSpines",
    "new-horizons:bulwarkOfTheMire.swampRenewal",
    "new-horizons:bulwarkOfTheMire.mireGrip",
    "new-horizons:bulwarkOfTheMire.sharedCover",
    "new-horizons:bulwarkOfTheMire.immovable",
    "new-horizons:bulwarkOfTheMire.vengefulMire",
    "new-horizons:logistics.pathfinding",
    "new-horizons:logistics.forcedMarch",
    "new-horizons:logistics.navigation",
    "new-horizons:logistics.scouting",
    "new-horizons:logistics.roadmaster",
    "new-horizons:logistics.wayfarer",
    "new-horizons:logistics.masterLogistician",
    "new-horizons:wisdom.intelligence",
    "new-horizons:wisdom.spellScholar",
    "new-horizons:lightMagic.benediction",
    "new-horizons:lightMagic.healer",
    "new-horizons:lightMagic.guardian",
    "new-horizons:lightMagic.aegis",
    "new-horizons:lightMagic.litany",
    "new-horizons:lightMagic.purifier",
    "new-horizons:lightMagic.retributionist",
    "new-horizons:lightMagic.miracleWorker",
    "new-horizons:lightMagic.crusader",
    "new-horizons:natureMagic.rootcaller",
    "new-horizons:natureMagic.beastcaller",
    "new-horizons:natureMagic.verdantWarden",
    "new-horizons:natureMagic.verdantCommunion",
    "new-horizons:shadowMagic.malediction",
    "new-horizons:shadowMagic.bloodDrinker",
    "new-horizons:shadowMagic.painweaver",
    "new-horizons:shadowMagic.witheringTouch",
    "new-horizons:shadowMagic.soulBinder",
    "new-horizons:shadowMagic.darkGift",
    "new-horizons:shadowMagic.nightFeeder",
    "new-horizons:shadowMagic.reanimator",
    "new-horizons:shadowMagic.grandMalediction",
    "new-horizons:warcasting.enchantedWeapons",
    "new-horizons:warcasting.spellshock",
    "new-horizons:warcasting.spellward",
    "new-horizons:warcasting.lingeringInspiration",
    "new-horizons:warcasting.battleFocus",
    "new-horizons:warcasting.grandInspiration",
    "new-horizons:warcasting.arcaneSupremacy",
    "new-horizons:warcasting.arcaneValor",
    "new-horizons:warcasting.arcaneBulwark",
    "new-horizons:archery.targetCaller",
    "new-horizons:archery.skirmisher",
    "new-horizons:archery.pointBlankShot",
    "new-horizons:archery.counterfire",
    "new-horizons:archery.armorPiercingShot",
    "new-horizons:archery.suppression",
    "new-horizons:archery.highArc",
    "new-horizons:archery.crossfire",
    "new-horizons:archery.deadeye",
    "new-horizons:archery.rainOfArrows",
    "new-horizons:offense.shockAssault",
    "new-horizons:offense.encirclement",
    "new-horizons:offense.pursuit",
    "new-horizons:offense.executioner",
    "new-horizons:offense.armorPiercer",
    "new-horizons:offense.breakthrough",
    "new-horizons:offense.cleave",
    "new-horizons:offense.vengeance",
    "new-horizons:offense.relentlessAssault",
    "new-horizons:offense.noQuarter",
    "new-horizons:discipline.inspirationalLeader",
    "new-horizons:discipline.rally",
    "new-horizons:discipline.unbreakable",
    "new-horizons:armorer.shieldMaster",
    "new-horizons:armorer.ironDiscipline",
    "new-horizons:armorer.countercharge",
    "new-horizons:armorer.pavise",
    "new-horizons:armorer.formationFighting",
    "new-horizons:armorer.veteran",
    "new-horizons:armorer.defiant",
    "new-horizons:armorer.lastStand",
    "new-horizons:luck.fortuneSFavor",
    "new-horizons:luck.luckyAim",
    "new-horizons:luck.secondChance",
    "new-horizons:luck.gambler",
    "new-horizons:luck.chainOfFortune",
    "new-horizons:luck.twistOfFate",
    "new-horizons:luck.perfectFortune",
    "new-horizons:sorceryMagic.overcharger",
    "new-horizons:sorceryMagic.matterShaper",
    "new-horizons:sorceryMagic.selectiveDispel",
    "new-horizons:sorceryMagic.temporalField",
    "new-horizons:sorceryMagic.temporalist",
    "new-horizons:sorceryMagic.teleporter",
    "new-horizons:sorceryMagic.countermage",
    "new-horizons:sorceryMagic.illusionist",
    "new-horizons:sorceryMagic.chronomancer",
    "new-horizons:sorceryMagic.spellbinder",
    "new-horizons:sylvanLuck.elvenPrecision",
    "new-horizons:sylvanLuck.forestSFavor",
    "new-horizons:sylvanLuck.serendipity",
    "new-horizons:sylvanLuck.luckyRecovery",
    "new-horizons:sylvanLuck.sharedFortune",
    "new-horizons:sylvanLuck.natureSProvidence",
    "new-horizons:sylvanLuck.fortunateAim",
    "new-horizons:sylvanLuck.wildChance",
    "new-horizons:sylvanLuck.perfectMoment",
    "new-horizons:sylvanLuck.cascadingFortune",
    "new-horizons:necromancy.boneCollector",
    "new-horizons:necromancy.corpsePreservation",
    "new-horizons:necromancy.darkConversion",
    "new-horizons:necromancy.blackHarvest",
    "new-horizons:necromancy.soulHarvester",
    "new-horizons:necromancy.masterOfBones",
    "new-horizons:necromancy.deathLord",
    "new-horizons:necromancy.graveKnowledge",
    "new-horizons:necromancy.ossuary",
    "new-horizons:necromancy.lordOfTheDead",
    "new-horizons:bloodrage.warDrums",
    "new-horizons:battlecraft.entrench",
    "new-horizons:battlecraft.battlefieldMastery",
    "new-horizons:recruitment.volunteerNetwork",
    "new-horizons:recruitment.externalRecruiter",
    "new-horizons:recruitment.broadMuster",
    "new-horizons:recruitment.eliteDraft",
    "new-horizons:recruitment.championSCall",
    "new-horizons:recruitment.masterRecruiter",
    "new-horizons:havocMagic.stormcaller",
    "new-horizons:havocMagic.conductor",
    "new-horizons:havocMagic.annihilator",
}
ACTIVE_RANK_SKILLS = {
    "new-horizons:elementalRebirth",
    "new-horizons:divineMandate",
    "new-horizons:diplomacy",
    "new-horizons:warcasting",
    "new-horizons:demonicGating",
    "new-horizons:offense",
    "new-horizons:armorer",
    "new-horizons:archery",
    "new-horizons:battlecraft",
    "new-horizons:command",
    "new-horizons:lightMagic",
    "new-horizons:shadowMagic",
    "new-horizons:natureMagic",
    "new-horizons:havocMagic",
    "new-horizons:sorceryMagic",
    "new-horizons:chaosMagic",
    "new-horizons:warMachines",
    "new-horizons:discipline",
    "new-horizons:recruitment",
    "new-horizons:logistics",
    "new-horizons:estates",
    "new-horizons:learning",
    "new-horizons:luck",
    "new-horizons:wisdom",
    "new-horizons:sylvanLuck",
    "new-horizons:necromancy",
    "new-horizons:bloodrage",
    "new-horizons:metamagic",
    "new-horizons:bulwarkOfTheMire",
    "new-horizons:shroudOfMalassa",
}
EXPECTED_SKILLS = (
    "new-horizons:offense",
    "new-horizons:armorer",
    "new-horizons:archery",
    "new-horizons:battlecraft",
    "new-horizons:warMachines",
    "new-horizons:discipline",
    "new-horizons:recruitment",
    "new-horizons:command",
    "new-horizons:authority",
    "new-horizons:lightMagic",
    "new-horizons:shadowMagic",
    "new-horizons:natureMagic",
    "new-horizons:havocMagic",
    "new-horizons:sorceryMagic",
    "new-horizons:chaosMagic",
    "new-horizons:spellcraft",
    "new-horizons:wisdom",
    "new-horizons:warcasting",
    "new-horizons:logistics",
    "new-horizons:diplomacy",
    "new-horizons:estates",
    "new-horizons:learning",
    "new-horizons:luck",
    "new-horizons:divineMandate",
    "new-horizons:sylvanLuck",
    "new-horizons:metamagic",
    "new-horizons:shroudOfMalassa",
    "new-horizons:demonicGating",
    "new-horizons:necromancy",
    "new-horizons:bloodrage",
    "new-horizons:bulwarkOfTheMire",
    "new-horizons:elementalRebirth",
)


def load(path):
    return json.loads((ROOT / path).read_text(encoding="utf-8"))


class NewHorizonsPerkDataTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.rules = load("config/newHorizonsPerks.json")
        cls.schema = load("config/schemas/newHorizonsPerks.json")
        cls.historical = load(
            "test/testdata/vcmi-test/content/test/newHorizonsHistoricalFactionPerks.json")

    def test_registry_validates_against_schema(self):
        Draft4Validator(self.schema).validate(self.rules)

    def test_parent_first_policy_has_configurable_valid_normalization(self):
        self.assertEqual(self.rules['offerWeightPolicyVersion'], 2)
        self.assertEqual(self.rules['perkOfferNormalization'], {
            'maximumPreferenceRatio': 2.0, 'mathVersion': 1})
        self.assertEqual(self.rules['maxPerksPerSkill'], 3)
        validator = Draft4Validator(self.schema)
        for ratio in (1.0, 1.5, 2.0, 3.0):
            with self.subTest(ratio=ratio):
                candidate = json.loads(json.dumps(self.rules))
                candidate['perkOfferNormalization']['maximumPreferenceRatio'] = ratio
                validator.validate(candidate)
        for normalization in (
                {'maximumPreferenceRatio': 0.99, 'mathVersion': 1},
                {'maximumPreferenceRatio': None, 'mathVersion': 1},
                {'maximumPreferenceRatio': 2.0, 'mathVersion': 2},
                {'maximumPreferenceRatio': 2.0},
                {'maximumPreferenceRatio': 2.0, 'mathVersion': 1, 'unknown': 1}):
            with self.subTest(invalid=normalization):
                candidate = json.loads(json.dumps(self.rules))
                candidate['perkOfferNormalization'] = normalization
                self.assertTrue(list(validator.iter_errors(candidate)))

    def test_hero_luck_reworks_keep_saved_ids_ranks_and_sylvan_identity(self):
        perks = {p['id']: p for p in self.rules['skills']['new-horizons:luck']['perks']}
        for identity, name, rank in (
                ('luckyRecovery', 'Lucky Day', 'advanced'),
                ('gambler', 'Erratic Mana', 'advanced'),
                ('twistOfFate', 'Druidic Luck', 'expert')):
            perk = perks['new-horizons:luck.' + identity]
            self.assertEqual((perk['name'], perk['requires']), (name, rank))
            self.assertEqual(perk['effect']['status'], 'active')
            self.assertEqual(perk['description'], perk['effect']['description'])
        self.assertNotIn('new-horizons:luck.luckyDay', perks)
        self.assertNotIn('new-horizons:luck.erraticMana', perks)
        self.assertNotIn('new-horizons:luck.druidicLuck', perks)
        sylvan = next(p for p in self.rules['skills']['new-horizons:sylvanLuck']['perks']
                      if p['id'] == 'new-horizons:sylvanLuck.luckyRecovery')
        description = 'Moving at least 1 hex, up to half Speed, before a melee attack grants +10% damage.'
        self.assertEqual((sylvan['name'], sylvan['requires']), ('Measured Advance', 'basic'))
        self.assertEqual(sylvan['description'], description)
        self.assertEqual(sylvan['effect'], {'status': 'active', 'description': description})

    def test_sylvan_tactics_policy_is_optional_and_strict(self):
        validator = Draft4Validator(self.schema)
        self.assertEqual(self.rules['sylvanTacticsPolicy'], 1)
        validator.validate(self.rules)
        historical = json.loads(json.dumps(self.rules))
        historical.pop('sylvanTacticsPolicy')
        validator.validate(historical)
        self.assertNotIn('sylvanTacticsPolicy', historical)
        for invalid in (None, False, True, 0, -1, 2, 1.0, '1', {}, []):
            with self.subTest(invalid=invalid):
                candidate = json.loads(json.dumps(self.rules))
                candidate['sylvanTacticsPolicy'] = invalid
                self.assertTrue(list(validator.iter_errors(candidate)))

    def test_sylvan_tactics_preserves_all_saved_perk_ids_and_tiers(self):
        sylvan = self.rules['skills']['new-horizons:sylvanLuck']
        self.assertEqual(sylvan['name'], 'Sylvan Tactics')
        expected = (
            ('elvenPrecision', 'Fleetfoot', 'basic'),
            ('forestSFavor', 'Patient Hunter', 'basic'),
            ('serendipity', 'Dappled Cover', 'basic'),
            ('luckyRecovery', 'Measured Advance', 'basic'),
            ('sharedFortune', 'Covering Fire', 'advanced'),
            ('natureSProvidence', 'Crossfire', 'advanced'),
            ('fortunateAim', 'Disruptive Strike', 'advanced'),
            ('wildChance', 'Quick Redeployment', 'advanced'),
            ('perfectMoment', 'Perfect Moment', 'expert'),
            ('cascadingFortune', 'Wild Hunt', 'expert'),
        )
        self.assertEqual([(p['id'], p['name'], p['requires']) for p in sylvan['perks']],
                         [('new-horizons:sylvanLuck.' + key, name, tier)
                          for key, name, tier in expected])
        perfect = next(p for p in sylvan['perks'] if p['id'].endswith('.perfectMoment'))
        self.assertEqual(perfect['description'], 'Creature Lucky Strikes deal 3.5× damage.')
        self.assertEqual(perfect['effect']['description'], perfect['description'])

    def test_revised_order_perks_preserve_saved_ids_and_tiers(self):
        expected = (
            ('offense', 'encirclement', 'Spearhead', 'basic',
             'Charge! grants an additional +5 percentage points of damage.'),
            ('armorer', 'shieldMaster', 'Shield Master', 'basic',
             'Hold the Line! grants an additional +5 percentage points of physical damage reduction.'),
            ('command', 'combinedArms', 'Combined Arms', 'advanced',
             'Focus Fire! grants friendly melee attacks 50% of its damage bonus against the designated target. '
             'Feint! grants an additional +5 percentage points of retaliation damage reduction, up to its final 60% cap.'),
            ('lightMagic', 'crusader', 'Exalted Champion', 'expert',
             'The Chosen lasts 1 additional round, added after its nominal duration cap.'),
        )
        for skill_id, perk_id, name, tier, description in expected:
            skill = self.rules['skills']['new-horizons:' + skill_id]
            identity = 'new-horizons:' + skill_id + '.' + perk_id
            with self.subTest(identity=identity):
                matches = [perk for perk in skill['perks'] if perk['id'] == identity]
                self.assertEqual(len(matches), 1)
                perk = matches[0]
                self.assertEqual((perk['name'], perk['requires']), (name, tier))
                self.assertEqual(perk.get('details', perk['description']), description)
                self.assertEqual(perk['effect']['status'], 'active')
                self.assertEqual(perk['effect']['description'], perk['description'])

    def test_necromancy_revision_keeps_ids_and_correct_tier_alternatives(self):
        skill = self.rules['skills']['new-horizons:necromancy']
        perks = {p['id'].split('.', 1)[1]: p for p in skill['perks']}
        expected = (
            ('graveKnowledge', 'Grave Knowledge', 'basic', "restore 35% of this hero's own eligible fallen"),
            ('darkConversion', 'Boneward', 'basic', '50% Magic Resistance'),
            ('blackHarvest', 'Soul Drinker', 'basic', 'recover 1 Mana for each new Skeleton or Skeleton Warrior'),
            ('corpsePreservation', 'Death March', 'basic', '+2 Speed. Initiative is unchanged.'),
            ('masterOfBones', 'Bone and Steel', 'advanced', 'upgraded form'),
            ('boneCollector', 'Bone Collector', 'advanced', '25% more new Skeletons and Skeleton Warriors'),
            ('deathLord', 'Graveplate', 'advanced', '20% less physical damage'),
            ('ossuary', "Dead Man's Luck", 'advanced', '+3 Luck'),
            ('soulHarvester', 'Tomb Guard', 'expert', 'strike twice'),
            ('lordOfTheDead', 'Lord of the Dead', 'expert', '90% total Leadership Requirement reduction'),
        )
        self.assertEqual(len(perks), len(expected))
        for identity, name, tier, effect in expected:
            with self.subTest(perk=identity):
                perk = perks[identity]
                self.assertEqual((perk['name'], perk['requires']), (name, tier))
                self.assertIn(effect, perk.get('details', perk['description']))
                self.assertEqual(perk['effect'], {
                    'status': 'active', 'description': perk['description'],
                    **({'parameters': {'skeletonRequirementDiscountPercent': 90}} if identity == 'lordOfTheDead' else {})})
        self.assertIn('not damage reduction', perks['darkConversion']['details'])
        self.assertIn('Retaliations are unchanged', perks['soulHarvester']['details'])
        self.assertIn('Multiple Double Strike effects do not add more strikes', perks['soulHarvester']['details'])
        self.assertIn('up to 30 Mana per battle', perks['blackHarvest']['details'])
        self.assertIn('Zombies and Grave Knowledge recovery do not count', perks['blackHarvest']['details'])
        self.assertIn('Grave Knowledge recovery is unchanged', perks['boneCollector']['description'])
        self.assertIn('rounded down separately for each form', perks['graveKnowledge']['details'])
        self.assertIn('Destroyed remains, summoned creatures and clones are excluded',
                      perks['graveKnowledge']['details'])
        self.assertNotIn('enemy', perks['graveKnowledge']['details'])
        self.assertNotIn('Construct', perks['deathLord']['description'])
        self.assertEqual(perks['lordOfTheDead']['effect']['parameters'], {'skeletonRequirementDiscountPercent': 90})
        self.assertNotIn('boneAndSteel', perks)
        self.assertNotIn('deathMarch', perks)
        for rank, raising in zip(RANKS, (10, 20, 30)):
            description = skill['ranks'][rank].get('details', skill['ranks'][rank]['description'])
            self.assertIn(f'{raising}% of eligible living enemy casualties', description)
            self.assertIn('Leadership Requirement reduction', description)
            self.assertIn('ordinary seven-slot limit', description)
        self.assertIn('eligible enemy casualties', skill['description'])
        self.assertIn('explicitly destroyed or invalid remains are excluded', skill['description'])

    def test_necromancy_policy_is_optional_strict_and_configurable(self):
        policy = self.rules['necromancyPolicy']
        self.assertEqual(policy, {
            'version': 4,
            'skeletonLeadershipReductionPercent': [0, 50, 60, 70],
            'lordOfTheDeadLeadershipReductionPercent': 90,
            'deathMarchSpeed': 2,
            'bonewardMagicalReductionPercent': 0,
            'tombGuardAdditionalRetaliations': 0,
            'graveplatePhysicalReductionPercent': 20,
            'graveKnowledgeRecoveryPercent': 35,
            'bonewardMagicResistancePercent': 50,
            'tombGuardDoubleStrike': True,
            'deadMansLuckBonus': 3,
            'boneCollectorRaiseBonusPercent': 25,
            'soulHarvesterManaPerRaisedCreature': 1,
            'soulHarvesterManaCap': 30,
        })
        validator = Draft4Validator(self.schema)
        absent = json.loads(json.dumps(self.rules))
        del absent['necromancyPolicy']
        self.assertFalse(validator.is_valid(absent))
        absent.pop('commandAuthorityPolicy')
        absent.pop('warfarePolicyVersion')
        absent.pop('unifiedSkillProgressionVersion')
        absent['commandPolicyVersion'] = 1
        absent['skills'].pop('new-horizons:authority')
        validator.validate(absent)
        version1 = {
            'version': 1,
            'skeletonLeadershipReductionPercent': [0, 50, 60, 70],
            'lordOfTheDeadLeadershipReductionPercent': 90,
            'deathMarchSpeed': 2,
            'bonewardMagicalReductionPercent': 30,
            'tombGuardAdditionalRetaliations': 1,
            'graveplatePhysicalReductionPercent': 20,
            'graveKnowledgeRecoveryPercent': 20,
        }
        validator.validate({**absent, 'necromancyPolicy': version1})
        version2 = {**policy, 'version': 2}
        validator.validate({**absent, 'necromancyPolicy': version2})
        legacy = {key: value for key, value in self.rules.items() if key != 'unifiedSkillProgressionVersion'}
        validator.validate({**legacy, 'necromancyPolicy': version2})
        self.assertFalse(validator.is_valid({**self.rules, 'necromancyPolicy': version2}))
        configurable = json.loads(json.dumps(self.rules))
        configurable['necromancyPolicy'].update({
            'lordOfTheDeadLeadershipReductionPercent': 50,
            'deathMarchSpeed': 0,
            'graveplatePhysicalReductionPercent': 100,
            'graveKnowledgeRecoveryPercent': 0,
            'bonewardMagicResistancePercent': 100,
            'tombGuardDoubleStrike': False,
            'deadMansLuckBonus': 10,
            'boneCollectorRaiseBonusPercent': 100,
            'soulHarvesterManaPerRaisedCreature': 100,
            'soulHarvesterManaCap': 1000000,
        })
        validator.validate(configurable)
        invalid = [None, {}, {**policy, 'unknown': 1}, {**policy, 'version': 5},
                   {**version1, 'tombGuardDoubleStrike': True}]
        invalid.extend({k: v for k, v in policy.items() if k != omitted} for omitted in policy)
        invalid.extend({**policy, 'skeletonLeadershipReductionPercent': value} for value in (
            [0, 50, 60], [0, 50, 60, 70, 80], [1, 50, 60, 70],
            [0, -1, 60, 70], [0, 50, 60, 100], [0, 50.5, 60, 70], '50'))
        invalid.extend({**policy, key: value} for key, value in (
            ('lordOfTheDeadLeadershipReductionPercent', -1),
            ('deathMarchSpeed', -1), ('bonewardMagicalReductionPercent', 1),
            ('tombGuardAdditionalRetaliations', 1),
            ('graveplatePhysicalReductionPercent', -1), ('graveplatePhysicalReductionPercent', 101),
            ('graveKnowledgeRecoveryPercent', -1), ('graveKnowledgeRecoveryPercent', 101),
            ('graveKnowledgeRecoveryPercent', 20.5), ('bonewardMagicResistancePercent', 101),
            ('tombGuardDoubleStrike', 1), ('deadMansLuckBonus', 11),
            ('boneCollectorRaiseBonusPercent', -1), ('boneCollectorRaiseBonusPercent', 101),
            ('soulHarvesterManaPerRaisedCreature', 101), ('soulHarvesterManaCap', 1000001),
            ('version', True)))
        for value in invalid:
            with self.subTest(invalid=value):
                candidate = {**self.rules, 'necromancyPolicy': value}
                self.assertTrue(list(validator.iter_errors(candidate)))

    def test_heroic_spirit_is_an_expert_captured_hero_morale_efficiency_perk(self):
        perk = next(p for p in self.rules['skills']['new-horizons:discipline']['perks']
                    if p['id'] == 'new-horizons:discipline.heroicSpirit')
        self.assertEqual((perk['name'], perk['requires']), ('Heroic Spirit', 'expert'))
        self.assertEqual(perk['effect']['status'], 'active')
        self.assertEqual(perk['description'], perk['effect']['description'])
        self.assertIn('+5 percentage points', perk['description'])
        self.assertIn('capped at +30 percentage points', perk['description'])
        self.assertIn('issuing hero', perk['details'])
        self.assertIn('no additional Hero Action', perk['details'])
        self.assertNotIn('retaliation', perk['description'])

    def test_provenance_metadata_and_runtime_selection_limits(self):
        self.assertEqual(self.rules["schemaVersion"], 1)
        self.assertEqual(self.rules["rulesetVersion"], 2)
        self.assertIsInstance(self.rules["sourceDocument"], str)
        self.assertTrue(self.rules["sourceDocument"].strip())
        self.assertRegex(self.rules["sourceSha256"], r"\A[0-9a-f]{64}\Z")
        self.assertEqual(self.rules["maxSkillChoices"], 2)
        self.assertEqual(self.rules["maxPerkChoices"], 2)
        self.assertEqual(self.rules["maxPerksPerSkill"], 3)

    def test_current_32_skill_roster(self):
        self.assertEqual(tuple(self.rules["skills"]), EXPECTED_SKILLS)
        self.assertEqual(len(self.rules["skills"]), 32)

    def test_cleave_is_active_advanced_offense_perk(self):
        cleave = next(
            perk
            for perk in self.rules["skills"]["new-horizons:offense"]["perks"]
            if perk["id"] == "new-horizons:offense.cleave"
        )
        description = (
            "After destroying a stack in melee, automatically strike the adjacent enemy "
            "stack with the highest current aggregate HP for 50% normal damage. Ties use "
            "deterministic hex order. Once per activation; Cleave cannot trigger itself."
        )
        self.assertEqual(cleave["name"], "Cleave")
        self.assertEqual(cleave["requires"], "advanced")
        self.assertEqual(cleave["details"], description)
        self.assertEqual(cleave["effect"]["status"], "active")
        self.assertEqual(cleave["effect"]["description"], cleave["description"])

    def test_vengeance_is_active_advanced_offense_perk(self):
        vengeance = next(
            perk
            for perk in self.rules["skills"]["new-horizons:offense"]["perks"]
            if perk["id"] == "new-horizons:offense.vengeance"
        )
        description = "While Riposte! is active, each affected stack gains one additional retaliation for the round."
        self.assertEqual(vengeance["name"], "Vengeance")
        self.assertEqual(vengeance["requires"], "advanced")
        self.assertEqual(vengeance["description"], description)
        self.assertEqual(vengeance["effect"]["status"], "active")
        self.assertEqual(vengeance["effect"]["description"], description)

    def test_relentless_assault_is_active_expert_offense_perk(self):
        perk = next(
            perk
            for perk in self.rules["skills"]["new-horizons:offense"]["perks"]
            if perk["id"] == "new-horizons:offense.relentlessAssault"
        )
        description = (
            "Consecutive activations attacking the same enemy stack gain +10% damage, "
            "stacking to +30%. Attacking another target resets the bonus."
        )
        self.assertEqual(perk["name"], "Relentless Assault")
        self.assertEqual(perk["requires"], "expert")
        self.assertEqual(perk["description"], description)
        self.assertEqual(perk["effect"]["status"], "active")
        self.assertEqual(perk["effect"]["description"], description)

    def test_no_quarter_is_active_expert_offense_perk(self):
        perk = next(
            perk
            for perk in self.rules["skills"]["new-horizons:offense"]["perks"]
            if perk["id"] == "new-horizons:offense.noQuarter"
        )
        description = (
            "If a melee attack leaves an enemy below 25% maximum HP, it loses all remaining "
            "retaliations for the round and suffers -2 Morale until the end of its next activation."
        )
        self.assertEqual(perk["name"], "No Quarter")
        self.assertEqual(perk["requires"], "expert")
        self.assertEqual(perk["details"], description)
        self.assertEqual(perk["effect"]["status"], "active")
        self.assertEqual(perk["effect"]["description"], perk["description"])

    def test_all_ten_archery_perks_are_active_at_their_canonical_ranks(self):
        skill = self.rules["skills"]["new-horizons:archery"]
        canonical = {
            "new-horizons:archery.targetCaller": "basic",
            "new-horizons:archery.skirmisher": "basic",
            "new-horizons:archery.pointBlankShot": "basic",
            "new-horizons:archery.counterfire": "basic",
            "new-horizons:archery.armorPiercingShot": "advanced",
            "new-horizons:archery.suppression": "advanced",
            "new-horizons:archery.highArc": "advanced",
            "new-horizons:archery.crossfire": "advanced",
            "new-horizons:archery.deadeye": "expert",
            "new-horizons:archery.rainOfArrows": "expert",
        }
        perks = {perk["id"]: perk for perk in skill["perks"]}
        self.assertEqual(set(perks), set(canonical))
        for perk_id, rank in canonical.items():
            with self.subTest(perk=perk_id):
                self.assertEqual(perks[perk_id]["requires"], rank)
                self.assertEqual(perks[perk_id]["effect"]["status"], "active")

    def test_seven_implemented_bulwark_perks_including_deep_bulwark_are_active(self):
        skill = self.rules["skills"]["new-horizons:bulwarkOfTheMire"]
        perks = {perk["id"]: perk for perk in skill["perks"]}
        canonical = {
            "new-horizons:bulwarkOfTheMire.toxicSpines": "basic",
            "new-horizons:bulwarkOfTheMire.swampRenewal": "advanced",
            "new-horizons:bulwarkOfTheMire.mireGrip": "advanced",
            "new-horizons:bulwarkOfTheMire.sharedCover": "advanced",
            "new-horizons:bulwarkOfTheMire.immovable": "expert",
            "new-horizons:bulwarkOfTheMire.vengefulMire": "expert",
        }
        self.assertEqual(set(canonical) | {"new-horizons:bulwarkOfTheMire.deepBulwark"}
                         | {
                             "new-horizons:bulwarkOfTheMire.mireborn",
                             "new-horizons:bulwarkOfTheMire.thickHide",
                             "new-horizons:bulwarkOfTheMire.bogAmbush",
                         }, set(perks))
        for perk_id, rank in canonical.items():
            with self.subTest(perk=perk_id):
                self.assertEqual(perks[perk_id]["requires"], rank)
                self.assertEqual(perks[perk_id]["effect"]["status"], "active")
        deep_bulwark = perks["new-horizons:bulwarkOfTheMire.deepBulwark"]
        self.assertEqual(deep_bulwark["requires"], "advanced")
        self.assertEqual(deep_bulwark["effect"]["status"], "active")

    def test_each_skill_has_ranked_effects_and_4_4_2_perks(self):
        all_perk_ids = []
        for skill_id, skill in self.rules["skills"].items():
            with self.subTest(skill=skill_id):
                self.assertEqual(skill["id"], skill_id)
                self.assertEqual(set(skill["ranks"]), set(RANKS))
                for rank in RANKS:
                    effect = skill["ranks"][rank]["effect"]
                    expected_status = "active" if (skill_id in ACTIVE_RANK_SKILLS or
                                                   skill_id in {"new-horizons:spellcraft", "new-horizons:authority"}) else "planned"
                    self.assertEqual(effect["status"], expected_status)
                    self.assertTrue(effect["description"])
                    self.assertEqual(skill["ranks"][rank]["description"], effect["description"])

                perks = skill["perks"]
                self.assertEqual(len(perks), 10)
                self.assertEqual(
                    {rank: sum(perk["requires"] == rank for perk in perks) for rank in RANKS},
                    {"basic": 4, "advanced": 4, "expert": 2},
                )
                for perk in perks:
                    self.assertTrue(perk["id"].startswith(skill_id + "."))
                    self.assertEqual(perk["description"], perk["effect"]["description"])
                    expected_status = "active" if perk["id"] in ACTIVE_PERKS else "planned"
                    self.assertEqual(perk["effect"]["status"], expected_status)
                    self.assertTrue(perk["name"])
                    self.assertTrue(perk["description"])
                    all_perk_ids.append(perk["id"])

        self.assertEqual(len(all_perk_ids), 320)
        self.assertEqual(len(set(all_perk_ids)), 320)

    def test_historical_metamagic_text_describes_round_long_optional_spell_actions(self):
        metamagic = self.historical["skills"]["new-horizons:metamagic"]
        for rank in RANKS:
            with self.subTest(rank=rank):
                description = metamagic["ranks"][rank]["description"]
                self.assertIn("optional Spell Action usable until the round ends", description)
                self.assertIn("Casting with it spends", description)
                self.assertIn("cannot trigger Metamagic again", description)

        grand = next(
            perk for perk in metamagic["perks"]
            if perk["id"] == "new-horizons:metamagic.grandMetamagic"
        )["description"]
        self.assertIn("first additional Spell of the third used Metamagic sequence", grand)
        self.assertIn("Both opportunities expire at the end of the round", grand)
        self.assertIn("neither can trigger Metamagic", grand)

    def test_historical_metamagic_rewards_distinguish_normal_restoration_from_buffer(self):
        perks = {
            perk["id"]: perk for perk in self.historical["skills"]["new-horizons:metamagic"]["perks"]
        }
        self.assertNotIn("new-horizons:metamagic.spellEcho", perks)
        self.assertNotIn("new-horizons:metamagic.countersequence", perks)
        acquisition = perks["new-horizons:metamagic.arcaneAcquisition"]
        self.assertEqual(acquisition["name"], "Arcane Acquisition")
        self.assertEqual(acquisition["effect"]["status"], "active")
        self.assertIn("each living enemy stack", acquisition["description"])
        self.assertIn("Check each target when it is damaged", acquisition["description"])
        buffer = perks["new-horizons:metamagic.spellBuffer"]
        self.assertEqual(buffer["requires"], "advanced")
        self.assertIn("Once per combat", buffer["description"])
        self.assertIn("expires unused at the end of the round", buffer["description"])
        self.assertIn("6 Buffer Spell Points", buffer["description"])
        self.assertIn("does not consume a Metamagic use", buffer["description"])
        reserve = perks["new-horizons:metamagic.formulaReserve"]["description"]
        self.assertIn("After each Metamagic sequence", reserve)
        self.assertIn("at least one additional Spell", reserve)
        self.assertIn("3 Normal Spell Points", reserve)

    def test_historical_spell_point_perks_keep_runtime_pool_terminology(self):
        perks = {
            perk["name"]: perk["description"]
            for perk in self.historical["skills"]["new-horizons:metamagic"]["perks"]
        }
        self.assertIn("Normal Spell Points", perks["Formula Reserve"])
        self.assertIn("Buffer Spell Points", perks["Spell Buffer"])
        intelligence = next(
            perk for perk in self.rules["skills"]["new-horizons:wisdom"]["perks"]
            if perk["name"] == "Intelligence"
        )["details"]
        self.assertIn("floor(1.30 × effective Knowledge)", intelligence)

    def test_current_faction_policies_are_strict(self):
        validator = Draft4Validator(self.schema)
        for key, expected in (
            ('metamagicPolicy', {'version': 3, 'adeptAdditionalPoints': 1}),
            ('divineMandatePolicy', {'version': 2}),
            ('demonicGatingPolicy', {'version': 2}),
        ):
            with self.subTest(policy=key):
                self.assertEqual(self.rules[key], expected)
                validator.validate(self.rules)
                for invalid in (None, {}, {'version': 1}, {'version': 4},
                                {'version': '3'}, {'version': True},
                                {**expected, 'unexpected': 1}):
                    self.assertTrue(list(validator.iter_errors({**self.rules, key: invalid})))
        for invalid in ({'version': 2}, {'version': 3, 'adeptAdditionalMarks': 1},
                        {'version': 3, 'adeptAdditionalPoints': -1},
                        {'version': 3, 'adeptAdditionalPoints': 13},
                        {'version': 3, 'adeptAdditionalPoints': True},
                        {'version': 3, 'adeptAdditionalPoints': 1.5}):
            self.assertTrue(list(validator.iter_errors({**self.rules, 'metamagicPolicy': invalid})))

    def test_current_metamagic_uses_round_points_and_complete_tier_pool(self):
        skill = self.rules['skills']['new-horizons:metamagic']
        self.assertIn('12 points', skill['description'])
        self.assertNotIn('Arcane Mark', json.dumps(skill))
        for rank, amount in zip(RANKS, (1, 2, 3)):
            description = skill['ranks'][rank]['description']
            self.assertIn('Start combat with 1 Metamagic point', description)
            self.assertIn('gain 1 each later round, up to 12', description)
            self.assertIn(f'+{amount}% spell effectiveness and Magic Penetration', description)
        perks = {p['id'].split('.')[-1]: p for p in skill['perks']}
        self.assertEqual(tuple(perks), (
            'arcaneCatalyst', 'arcaneShield', 'manaConduit', 'lingeringMagic',
            'arcaneFeedback', 'spellFracture', 'arcaneWellspring',
            'arcaneAttunement', 'arcaneEcho', 'arcaneSingularity'))
        self.assertIn('3 Metamagic points instead of 1', perks['arcaneWellspring']['description'])
        self.assertIn('25% damage', perks['arcaneEcho']['description'])
        self.assertIn('10 Metamagic points', perks['arcaneSingularity']['description'])
        self.assertIn('25% less Mana', perks['arcaneSingularity']['description'])
        names = [p['name'] for s in self.rules['skills'].values() for p in s['perks']]
        for perk in perks.values():
            self.assertEqual(names.count(perk['name']), 1)
            self.assertLessEqual(len(perk['description'].split()), 25)

    def test_current_dungeon_and_conflux_replacements_keep_other_perks(self):
        expected = {
            'shroudOfMalassa': ('shadowHex', 'exposedWeakness', 'ambusher', 'nightProwler'),
            'elementalRebirth': ('elementalConduit', 'elementalCataclysm', 'elementalMemory', 'elementalWard'),
        }
        for skill, (basic, advanced, removed_basic, removed_advanced) in expected.items():
            pool = {p['id'].split('.')[-1]: p for p in self.rules['skills']['new-horizons:' + skill]['perks']}
            self.assertEqual(pool[basic]['requires'], 'basic')
            self.assertEqual(pool[advanced]['requires'], 'advanced')
            self.assertNotIn(removed_basic, pool)
            self.assertNotIn(removed_advanced, pool)
            self.assertEqual(len(pool), 10)
            self.assertEqual([sum(p['requires'] == rank for p in pool.values()) for rank in RANKS], [4, 4, 2])

    def test_current_divine_mandate_describes_automatic_known_light_support(self):
        skill = self.rules['skills']['new-horizons:divineMandate']
        for rank, efficiency, count in zip(RANKS, (5, 10, 15), (1, 2, 3)):
            description = skill['ranks'][rank].get('details', skill['ranks'][rank]['description'])
            self.assertIn(f'+{efficiency} percentage points on attribute-derived effects', description)
            self.assertIn(f'Up to {count} Orders per combat', description)
            self.assertIn('useful known beneficial Light spell', description)
            self.assertIn('free of Mana and additional Hero Actions', description)
            self.assertIn('No useful spell spends no activation', description)
        perks = {p['id'].split('.')[-1]: p for p in skill['perks']}
        self.assertEqual(tuple(perks), (
            'sacredCommand', 'knightlyValor', 'chaplainSReserve', 'shieldOfFaith',
            'righteousDiscipline', 'unwaveringStandard', 'inspiringLeadership',
            'consecratedArms', 'mandateOfHeaven', 'divineFury'))
        self.assertIn('restores three Mana', perks['chaplainSReserve']['description'])
        self.assertIn('+2 Speed', perks['inspiringLeadership']['description'])
        self.assertIn('Initiative is unchanged', perks['inspiringLeadership']['description'])
        self.assertIn('five at Expert rank', perks['mandateOfHeaven']['description'])
        self.assertIn('10% chance to cast free Holy Wrath', perks['divineFury']['description'])
        self.assertIn('melee attacks, ranged attacks and retaliations', perks['divineFury']['details'])

    def test_current_gating_describes_temporary_original_stack_reinforcements(self):
        skill = self.rules['skills']['new-horizons:demonicGating']
        self.assertIn('no reserve troops are required or consumed', skill['description'])
        self.assertIn('Summoned stacks cannot Gate', skill['description'])
        for rank, categories in zip(RANKS, ('Core', 'Core and Elite', 'Core, Elite and Champion')):
            description = skill['ranks'][rank].get('details', skill['ranks'][rank]['description'])
            self.assertIn(f'Original {categories} Inferno stacks may Gate once per combat', description)
            self.assertIn('spending their creature activation', description)
            self.assertIn('start of the next round', description)
            self.assertIn('within three hexes', description)
            self.assertIn('30% of current creatures, rounded down with a minimum of one', description)
            self.assertIn('disappear after combat and cannot Gate', description)
        perks = {p['id'].split('.')[-1]: p for p in skill['perks']}
        self.assertEqual(tuple(perks), (
            'swiftGate', 'wideGate', 'hellfireArrival', 'reinforcedGate',
            'infernalOnslaught', 'infernalBeacon', 'hellstride', 'infernalDiscipline',
            'masterGate', 'endlessLegion'))
        self.assertIn('cannot be healed or resurrected', perks['reinforcedGate']['description'])
        self.assertIn('does not increase Hellfire Arrival damage', perks['infernalOnslaught']['description'])
        self.assertIn('Initiative is unchanged', perks['hellstride']['description'])
        self.assertIn('does not spend', perks['masterGate']['description'])
        self.assertIn('50%', perks['endlessLegion']['description'])

    def test_historical_faction_fixture_keeps_retired_identities_and_no_new_policies(self):
        historical = json.loads(json.dumps(self.rules))
        for key in ('metamagicPolicy', 'divineMandatePolicy', 'demonicGatingPolicy'):
            historical.pop(key)
            self.assertNotIn(key, self.historical)
        historical['skills'].update(self.historical['skills'])
        Draft4Validator(self.schema).validate(historical)
        for skill, retained, retired in (
            ('metamagic', 'grandMetamagic', 'arcanePerfection'),
            ('divineMandate', 'knightlySequence', 'knightlyValor'),
            ('demonicGating', 'reserveDiscipline', 'infernalDiscipline'),
        ):
            perks = historical['skills'][f'new-horizons:{skill}']['perks']
            ids = {p['id'].split('.')[-1] for p in perks}
            self.assertIn(retained, ids)
            self.assertNotIn(retired, ids)
            self.assertEqual(len(ids), 10)
            self.assertEqual({rank: sum(p['requires'] == rank for p in perks) for rank in RANKS},
                             {'basic': 4, 'advanced': 4, 'expert': 2})

    def test_game_settings_schema_exposes_registry_without_activating_it(self):
        settings = load("config/schemas/gameSettings.json")
        heroes = settings["properties"]["heroes"]["properties"]
        self.assertEqual(heroes["newHorizonsPerks"], {"$ref": "newHorizonsPerks.json"})

    def test_default_module_carries_canonical_registry(self):
        module = load("Mods/new-horizons/mod.json")
        self.assertEqual(module["version"], load("config/newHorizonsVersion.json")["version"])
        self.assertEqual(module["settings"]["heroes"]["newHorizonsPerks"], self.rules)


if __name__ == "__main__":
    unittest.main()
