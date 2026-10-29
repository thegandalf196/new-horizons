#!/usr/bin/env python3
"""Source/data bindings only; actual pixels and integrated damage need native acceptance."""
from pathlib import Path
import json
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]


def jsonc(path):
    text = (ROOT / path).read_text()
    text = re.sub(r'"(?:\\.|[^"\\])*"|//[^\n]*|/\*[\s\S]*?\*/',
                  lambda m: m[0] if m[0].startswith('"') else '', text)
    text = re.sub(r'"(?:\\.|[^"\\])*"|,(?=\s*[}\]])',
                  lambda m: m[0] if m[0].startswith('"') else '', text)
    return json.loads(text)


class LivingArmorPresentationContract(unittest.TestCase):
    def test_physical_metadata_visible_and_value_is_dynamic(self):
        row = jsonc('config/bonuses.json')['PHYSICAL_DAMAGE_REDUCTION_BASIS_POINTS']
        self.assertFalse(row.get('hidden', False))
        self.assertIn('${val.basisPoints}', row['description'])
        self.assertNotIn('75', row['description'])
        handler = (ROOT / 'lib/CBonusTypeHandler.cpp').read_text()
        self.assertIn('TextOperations::formatBasisPoints(bonusValue)', handler)
        self.assertIn('replace_all(text, "${val.basisPoints}"', handler)

    def test_mechanical_values_unchanged_and_icons_are_creature_local(self):
        row = jsonc('Mods/new-horizons/Content/config/creatures/livingArmor.json')['livingArmor']
        self.assertEqual(row['abilities']['physicalResistance']['val'], 7500)
        self.assertEqual(row['abilities']['magicalVulnerability']['val'], 100)
        self.assertEqual(row['abilities']['construct']['type'], 'NON_LIVING')
        self.assertEqual(row['graphics']['animation'], 'CWSKEL.DEF')
        for key, path in [('physicalResistance', 'NH_spell_holy_armor_44.png'),
                          ('magicalVulnerability', 'NH_spell_disintegrate-44.png'),
                          ('construct', 'newHorizons/heroSpecialties/JosephineGolemLine/icon-44.png')]:
            self.assertEqual(row['abilities'][key]['icon'], path)

    def test_temporary_row_has_selected_hourglass(self):
        help = (ROOT / 'client/windows/NewHorizonsCreatureAbilityHelp.h').read_text()
        self.assertIn('summonLifetimePresentation::tooltip(summonRounds), "NH_spell_time_stop-44.png"', help)

    def test_portraits_reuse_exact_battle_identity_instead_of_a_second_blue_rule(self):
        portrait = (ROOT / 'client/widgets/CreaturePic.cpp').read_text()
        self.assertIn('battleStackColorPresentation::appearance(nullptr, cre->getJsonKey(), false, {}).color', portrait)
        self.assertNotIn('new-horizons:livingArmor', portrait)
        self.assertNotIn('114, 186, 218', portrait)
        self.assertIn('CCreaturePic::show', portrait)

    def test_every_creature_preview_draw_sets_instance_color(self):
        image = (ROOT / 'client/widgets/Images.cpp').read_text()
        header = (ROOT / 'client/widgets/Images.h').read_text()
        self.assertIn('ColorRGBA effectColor{0, 0, 0, 0}', header)
        self.assertIn('if(flags & CREATURE_PREVIEW_MODE)\n\t\t\timg->setEffectColor(effectColor)', image)
        self.assertIn('void setEffectColor(const ColorRGBA & color)', header)
        self.assertIn('img->setAlpha(alpha)', image)

    def test_repeated_preview_colors_use_current_palette_guard(self):
        backend = (ROOT / 'clientsdl3/render/ScalableImage.cpp').read_text()
        self.assertIn('paletteUpdate::setColorIfChanged(palette, i, shiftedColor)', backend)
        self.assertNotIn('SDL_SetPaletteColors(palette, &shiftedColor, i, 1)', backend)
        helper = (ROOT / 'clientsdl3/render/PaletteUpdate.h').read_text()
        for channel in 'rgba':
            self.assertIn(f'current.{channel} != color.{channel}', helper)
        sdl2 = (ROOT / 'clientsdl2/render/ScalableImage.cpp').read_text()
        self.assertIn('palette->colors[i] = CSDL_Ext::toSDL(shifter.shiftColor', sdl2)

    def test_one_shared_percent_body_and_complete_widget_registrations(self):
        status = (ROOT / 'client/battle/NewHorizonsBattleStatus.h').read_text()
        self.assertIn('return TextOperations::formatBasisPoints(basisPoints)', status)
        for path in ('widgets/CreaturePic.cpp', 'widgets/CreaturePreviewAnimation.cpp'):
            self.assertEqual((ROOT / 'client/CMakeLists.txt').read_text().count(path), 1)
        self.assertEqual((ROOT / 'lib/CMakeLists.txt').read_text().count('texts/PercentageFormatting.h'), 1)
        self.assertNotIn('CCreaturePic::CCreaturePic(', (ROOT / 'client/widgets/MiscWidgets.cpp').read_text())
        self.assertNotIn('CCreatureAnim::CCreatureAnim(', (ROOT / 'client/widgets/Images.cpp').read_text())


if __name__ == '__main__':
    unittest.main()
