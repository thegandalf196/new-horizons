#!/usr/bin/env python3
"""Source-only contract checks; not native input/rendering acceptance."""
from pathlib import Path

root = Path(__file__).resolve().parents[2]
source = (root / 'client/windows/CSpellWindow.cpp').read_text()

def check(text):
    required = [
        'AnimationPath::builtin("SPELTAB"), EImageBlitMode::COLORKEY',
        'const Rect allBookmark(0, 236, 83, 57);',
        'CPicture>(tabs->getImage(0), allBookmark, 524 + offR, 324)',
        'CPicture>(tabs->getImage(4), allBookmark, 524 + offR, 324)',
        'Rect(534 + offR + pos.x, 318 + pos.y, 64, 64)',
        'std::bind(&CSpellWindow::selectSchool, this, SpellSchool::ANY), 458, this)',
        'allSchoolsSelected->setEnabled(school == SpellSchool::ANY);',
        'allSchoolsInactive->setEnabled(school != SpellSchool::ANY);',
        'allSchoolsSelected->setEnabled(selectedTab == SpellSchool::ANY);',
        'allSchoolsInactive->setEnabled(selectedTab != SpellSchool::ANY);',
        'getSchoolBookmarkPath()',
        'CPicture>(background->getSurface(), Rect(524 + offR, 88, 83, 294), 524 + offR, 88)',
        'schoolTabPanel->removeUsedEvents(LCLICK | SHOW_POPUP);',
        'allSchoolsInactive->removeUsedEvents(LCLICK | SHOW_POPUP);',
        'allSchoolsSelected->removeUsedEvents(LCLICK | SHOW_POPUP);',
    ]
    return all(item in text for item in required) and 'NH_spells_button' not in text

assert check(source)
for old, new in [
    ('allSchoolsSelected->removeUsedEvents(LCLICK | SHOW_POPUP);', ''),
    ('allSchoolsInactive->removeUsedEvents(LCLICK | SHOW_POPUP);', ''),
    ('background->getSurface()', 'allSchoolsSelected->getSurface()'),
    ('schoolTabPanel->removeUsedEvents(LCLICK | SHOW_POPUP);', ''),
    ('tabs->getImage(4)', 'tabs->getImage(0)'),
    ('allBookmark(0, 236, 83, 57)', 'allBookmark(0, 0, 83, 57)'),
    ('318 + pos.y, 64, 64', '318 + pos.y, 45, 35'),
    ('setEnabled(school != SpellSchool::ANY)', 'setEnabled(school == SpellSchool::ANY)'),
]:
    assert old in source
    assert not check(source.replace(old, new)), old
print('PASS: native All resource/crop/state/hit/help source contract; eight negative controls')
print('NOT compilation, widget lifecycle, rendered fit or gameplay acceptance')
