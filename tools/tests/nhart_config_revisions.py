"""Finite accepted nongraphics config history for selected artwork proofs.

No Git, network, installed defaults or permissive future-change filtering.
"""
import copy
import hashlib
import json

# Exact reviewed records; extending accepted history requires a reviewed update.
APPROVED_CONFIG_PROOFS = {
    'Mods/new-horizons/Content/config/spells/newHorizons.json': '50ad20b5a76fc3f7eb5cd32e18766353bf618ead5dc906ca7eb219bf0ac9c9a1',
    'Mods/new-horizons/Content/config/spells/massVariants.json': 'cd88c590bbb225f32f8d601f0e31c7d9efa58088d9e76db671a905a5f3885327',
}


def _digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def _field(config, pointer):
    if not isinstance(pointer, str) or not pointer.startswith('/'):
        raise ValueError('Config proof requires an absolute object JSON pointer')
    parts = [part.replace('~1', '/').replace('~0', '~') for part in pointer[1:].split('/')]
    node = config
    for part in parts[:-1]:
        if not isinstance(node, dict) or part not in node:
            raise ValueError('Config proof pointer does not resolve')
        node = node[part]
    if not isinstance(node, dict):
        raise ValueError('Config proof target must be an object member')
    return node, parts[-1]


def _projection(config, excluded):
    projected = copy.deepcopy(config)
    for pointer in excluded:
        node, key = _field(projected, pointer)
        if key not in node:
            raise ValueError('Selected graphics pointer is missing')
        del node[key]
    return _digest(projected)


def config_at_revision(config, path, proofs, revision):
    """Verify the exact current capture, then reverse only recorded edits."""
    if not isinstance(proofs, dict) or set(proofs) != {'format', 'files'} or type(proofs['format']) is not int or proofs['format'] != 1:
        raise ValueError('Unsupported config proof format')
    if not isinstance(proofs['files'], dict) or set(proofs['files']) != set(APPROVED_CONFIG_PROOFS) or path not in APPROVED_CONFIG_PROOFS:
        raise ValueError('Unknown config proof source')
    for source, expected in APPROVED_CONFIG_PROOFS.items():
        if _digest(proofs['files'][source]) != expected:
            raise ValueError('Accepted config revision metadata changed')
    record = proofs['files'][path]
    revisions = record['revisions']
    if type(revision) is not int or not 0 <= revision <= len(revisions):
        raise ValueError('Unrecorded config revision')
    historical = copy.deepcopy(config)
    excluded = record['excludedGraphics']
    if _projection(historical, excluded) != record['currentProjectionSha256']:
        raise ValueError('Current nongraphics config projection changed')
    for change_set in reversed(revisions[revision:]):
        if _projection(historical, excluded) != change_set['toProjectionSha256']:
            raise ValueError('Config revision after-projection changed')
        for change in reversed(change_set['changes']):
            node, key = _field(historical, change['pointer'])
            if key not in node or node[key] != change['after']:
                raise ValueError('Config revision after-value changed')
            if change['operation'] == 'add':
                del node[key]
            elif change['operation'] == 'replace':
                node[key] = copy.deepcopy(change['before'])
            else:
                raise ValueError('Unsupported config revision operation')
        if _projection(historical, excluded) != change_set['fromProjectionSha256']:
            raise ValueError('Config revision before-projection changed')
    expected = revisions[revision - 1]['toProjectionSha256'] if revision else record['baselineProjectionSha256']
    if _projection(historical, excluded) != expected:
        raise ValueError('Requested config projection changed')
    return historical


def wave_config_revision(wave, path):
    selected = wave.get('configRevisions', {})
    if not isinstance(selected, dict) or set(selected) - set(APPROVED_CONFIG_PROOFS):
        raise ValueError('Unknown wave config revision source')
    if any(type(value) is not int or value < 0 for value in selected.values()):
        raise ValueError('Invalid wave config revision value')
    return selected.get(path, 0)
