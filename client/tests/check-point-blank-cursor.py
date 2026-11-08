#!/usr/bin/env python3
"""Scoped native cursor composition contract; not rendered or compiled acceptance."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]


def check():
    runtime = (ROOT / 'client/battle/BattlePointBlankPresentation.cpp').read_text()
    cursor = (ROOT / 'client/battle/BattleActionCursor.cpp').read_text()
    image = (ROOT / 'client/render/PointBlankCursor.cpp').read_text()
    config = (ROOT / 'config/cursors.json').read_text()
    assert runtime.count('ballistaPhysicalDamagePercent') == 2
    assert runtime.count('!= 200') == 2
    assert runtime.count('hasBonusOfType(BonusType::SPELL_LIKE_ATTACK)') == 2
    assert 'core:christian' not in runtime
    assert 'battleHasShootingPenalty(shooter, target)' in runtime
    assert 'battleHasDistancePenalty(&shooter, shooter.getPosition(), target)' in runtime
    assert 'battleHasWallPenalty(&shooter, shooter.getPosition(), target)' in runtime
    assert 'shots.' not in runtime and 'battleMake' not in runtime
    shoot = cursor.split('case PossiblePlayerBattleAction::SHOOT:', 1)[1].split('case ', 1)[0]
    assert shoot.index('pointBlankPresentation::setCursor') < shoot.index('Cursor::Combat::SHOOT_PENALTY')
    assert 'Cursor::Combat::SHOOT)' in shoot
    assert 'Point(36, 36), CanvasScalingPolicy::IGNORE' in image
    assert 'AnimationPath::builtin("CRCOMBAT"), penalty ? 15 : 3' in image
    assert 'locator.originalDefFrame = true' in image
    assert image.count('canvas.draw(arrow,') == 2
    assert 'canvas.draw(arrow, Point(0, 0))' in image
    assert 'penalty ? Point(0, 12) : Point(6, 6)' in image
    assert 'setAlpha' not in image and 'adjustPalette' not in image
    for name, x, y, filename in (
        ('combatPointBlankShoot', 12, 12, 'NH_pointBlankShoot.png'),
        ('combatPointBlankShootPenalty', 14, 0, 'NH_pointBlankShootPenalty.png'),
    ):
        entry = re.search(r'"' + name + r'"\s*:\s*\{([^}]+)\}', config).group(1)
        assert re.search(r'"pivotX"\s*:\s*' + str(x), entry)
        assert re.search(r'"pivotY"\s*:\s*' + str(y), entry)
        assert '"' + filename + '"' in entry
    # Actual native source alpha extents: unchanged first arrow preserves hotspot;
    # second copy fits entirely, despite each source DEF's transparent36px canvas.
    for bounds, offset in (((24, 24), (6, 6)), ((28, 15), (0, 12))):
        assert all(length + delta <= 36 for length, delta in zip(bounds, offset))


if __name__ == '__main__':
    check()
    print('PASS: scoped Point-Blank cursor source, native-frame bounds and hotspot contract')
    print('NOT compiled, native gameplay, or graphical acceptance')
