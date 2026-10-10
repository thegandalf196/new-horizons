"""Focused popup source/data contract; not native or rendered UI acceptance."""
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


class HeroSelectionClassOrderTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.prototypes = json.loads((ROOT / 'config/heroes/tower.json').read_text())
        cls.classes = json.loads((ROOT / 'config/heroClasses.json').read_text())
        cls.swaps = json.loads((ROOT / 'Mods/new-horizons/Content/config/heroes/classSwaps.json').read_text())
        cls.legacy = sorted(cls.prototypes, key=lambda name: cls.prototypes[name]['index'])

    def resolved_class(self, name, use_swaps=True):
        return self.swaps.get('core:' + name, {}).get('class', 'core:' + self.prototypes[name]['class']) if use_swaps else 'core:' + self.prototypes[name]['class']

    def order(self, allowed, use_swaps=True):
        return sorted(allowed, key=lambda name: self.classes[self.resolved_class(name, use_swaps).split(':')[1]]['index'])

    def test_popup_filters_then_groups_resolved_classes_and_renders_same_vector(self):
        text = (ROOT / 'client/lobby/OptionsTab.cpp').read_text()
        block = text.split('void OptionsTab::SelectionWindow::genContentHeroes()', 1)[1].split('void OptionsTab::SelectionWindow::genContentBonus()', 1)[0]
        collect = block.index('for(auto & elem : allowedHeroes)')
        faction = block.index('if(type->heroClass->faction != selectedFaction)')
        append = block.index('heroes.push_back(elem);')
        sort = block.index('std::stable_sort(heroes.begin(), heroes.end()')
        render = block.index('for(const auto & elem : heroes)')
        self.assertLess(collect, faction)
        self.assertLess(faction, append)
        self.assertLess(append, sort)
        self.assertLess(sort, render)
        self.assertIn('left.toHeroType()->heroClass->getId() < right.toHeroType()->heroClass->getId()', block)
        self.assertEqual(block.count('heroes.push_back(elem);'), 1)
        self.assertLess(block.index('set.hero = HeroTypeID::RANDOM;'), collect)
        self.assertIn('if(unusableHeroes.count(elem))', block)
        self.assertNotIn('thane', block.lower())
        self.assertNotIn('halon', block.lower())
        selection = text.split('void OptionsTab::SelectionWindow::setElement(', 1)[1]
        self.assertIn('set.hero = heroes[elem];', selection)

    def test_actual_swapped_tower_roster_has_two_contiguous_class_groups(self):
        result = self.order(self.legacy)
        self.assertEqual(result, ['piquedram', 'josephine', 'neela', 'torosar', 'fafner', 'rissa', 'iona', 'halon',
                                  'thane', 'astral', 'serena', 'daremyth', 'theodorus', 'solmyr', 'cyra', 'aine'])
        self.assertEqual([self.resolved_class(name) for name in result],
                         ['core:alchemist'] * 8 + ['core:wizard'] * 8)
        for class_id in ('core:alchemist', 'core:wizard'):
            self.assertEqual([name for name in result if self.resolved_class(name) == class_id],
                             [name for name in self.legacy if self.resolved_class(name) == class_id])

    def test_allowed_subset_keeps_filtering_and_relative_order(self):
        allowed = [name for name in self.legacy if name not in ('josephine', 'astral', 'rissa')]
        result = self.order(allowed)
        self.assertEqual(set(result), set(allowed))
        self.assertNotIn('josephine', result)
        self.assertLess(result.index('halon'), result.index('thane'))
        for class_id in ('core:alchemist', 'core:wizard'):
            self.assertEqual([name for name in result if self.resolved_class(name) == class_id],
                             [name for name in allowed if self.resolved_class(name) == class_id])
        self.assertEqual(self.order([]), [])

    def test_unchanged_legacy_classes_keep_original_roster_order(self):
        self.assertEqual(self.order(self.legacy, use_swaps=False), self.legacy)
        self.assertEqual(self.prototypes['thane']['index'], 33)
        self.assertEqual(self.prototypes['halon']['index'], 41)

    def test_random_and_unusable_gate_use_resolved_selection_without_unchecked_index(self):
        text = (ROOT / 'client/lobby/OptionsTab.cpp').read_text()
        block = text.split('void OptionsTab::SelectionWindow::setElement(', 1)[1].split('if(type == SelType::BONUS)', 1)[0]
        hero = block.split('if(type == SelType::HERO)', 1)[1]
        self.assertIn('if(elem >= heroes.size())\n\t\t\t\treturn;', hero)
        self.assertIn('set.hero = heroes[elem];', hero)
        self.assertIn('set.hero = HeroTypeID::RANDOM;', hero)
        self.assertIn('if(doApply && unusableHeroes.count(set.hero))', hero)
        self.assertNotIn('unusableHeroes.count(heroes[elem])', hero)
        self.assertEqual(hero.count('heroes[elem]'), 1)
        # Random does not index the list, even if empty or its first hero is taken.
        random = -1

        def select(index, concrete, unavailable):
            if index > 0:
                index -= 1
                if index >= len(concrete):
                    return None
                selected = concrete[index]
            else:
                selected = random
            return None if selected in unavailable else selected

        self.assertEqual(select(0, [], set()), random)
        self.assertEqual(select(0, ['halon'], {'halon'}), random)
        self.assertIsNone(select(1, [], set()))
        self.assertIsNone(select(1, ['halon'], {'halon'}))
        self.assertEqual(select(1, ['thane'], {'halon'}), 'thane')


if __name__ == '__main__':
    unittest.main()
