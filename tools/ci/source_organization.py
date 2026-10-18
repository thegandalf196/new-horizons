#!/usr/bin/env python3
"""Inventory physical sizes and reject growth outside independently reviewed scopes.

`inventory` requires reviewed coverage, never size approvals. `check` uses a reviewed baseline
and a Git base. Review identifiers document human review; this tool cannot verify
the identity or independence of a reviewer. It does not measure semantic complexity.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from source_organization_inventory import git, inventory, span


REVIEW = {'file': 500, 'function': 80}
CEILING = {'file': 1000, 'function': 150}


def units(report: dict) -> dict:
    result = {}
    for file in report['files']:
        if file['classification'] != 'production':
            continue
        result[file['path']] = {**file, 'kind': 'file'}
        for function in file['functions']:
            key = file['path'] + '::' + function['symbol']
            if key in result:
                raise ValueError('Duplicate unit: ' + key)
            result[key] = {**function, 'kind': 'function', 'path': file['path']}
    return result


def reviewed(entry: dict) -> bool:
    return all(isinstance(entry.get(key), str) and entry[key].strip()
               for key in ('review', 'rationale'))


def proposal(report: dict) -> dict:
    """Unreviewed candidate only; empty review fields deliberately fail the gate."""
    entries = {}
    for key, unit in units(report).items():
        if unit['lines'] > REVIEW[unit['kind']]:
            entries[key] = {'lines': unit['lines'], 'sha256': unit['sha256'],
                            'kind': 'legacy', 'review': '', 'rationale': ''}
    gaps = {}
    for file in report['files']:
        if file['classification'] in {'production', 'test'} and (
                file['measurement'] == 'manual-required' or file['diagnostics']):
            gaps[file['path']] = {'sha256': file['sha256'], 'owner': '',
                                  'reason': 'Unsupported language' if file['measurement'] == 'manual-required'
                                  else 'Parser recovery; complete named spans require review'}
    return {'schema': 1, 'units': entries, 'manual_functions': {}, 'coverage_pending': gaps}


def apply_manual(report: dict, baseline: dict, read_source, previous: dict | None = None,
                 old_baseline: dict | None = None, *, historical_baseline: bool = False) -> list[str]:
    """Apply span records, not size exceptions.

    Current coverage includes tests; historical comparison stays production-only.
    Only the previous Git baseline may retain now-resolved frozen evidence.
    """
    errors = []
    manual = baseline.get('manual_functions', {})
    pending = baseline.get('coverage_pending', {})
    old_files = {file['path']: file for file in (previous or report)['files']}
    for file in report['files']:
        if file['classification'] not in ({'production'} if historical_baseline
                                          else {'production', 'test'}):
            continue
        needs_manual = file['measurement'] == 'manual-required' or bool(file['diagnostics'])
        entry = manual.get(file['path'])
        gap = pending.get(file['path'], {})
        valid_gap = (gap.get('sha256') == file['sha256']
                     and all(isinstance(gap.get(key), str) and gap[key].strip() for key in ('owner', 'reason')))
        if file['path'] in pending and (entry is not None or not needs_manual
                                        and not (historical_baseline and valid_gap)):
            errors.append(file['path'] + ': remove resolved/overlapping pending coverage entry')
        if not needs_manual and entry is None:
            continue
        if (needs_manual and entry is None and valid_gap
                and old_files.get(file['path'], {}).get('sha256') == file['sha256']
                and (old_baseline is None or old_baseline.get('coverage_pending', {}).get(file['path']) == gap)):
            file['measurement'] = 'coverage-pending-frozen'
            continue
        if entry is None or not reviewed(entry) or entry.get('sha256') != file['sha256']:
            errors.append(file['path'] + ': missing/stale reviewed manual function inventory')
            continue
        functions = entry.get('functions')
        if not isinstance(functions, list):
            errors.append(file['path'] + ': manual functions must be an explicit list')
            continue
        data = read_source(file['path'])
        measured = []
        for item in functions:
            if (not isinstance(item.get('symbol'), str) or not item['symbol'].strip()
                    or not isinstance(item.get('start'), int) or not isinstance(item.get('end'), int)
                    or not 1 <= item['start'] <= item['end'] <= file['lines']):
                errors.append(file['path'] + ': invalid manual function span')
                continue
            measured.append(span(item['symbol'], item['start'], item['end'], data))
        # A recovery override supplies a complete reviewed inventory, not a union
        # whose duplicate/missing parser nodes could obscure the actual functions.
        file['functions'] = measured
        file['measurement'] = 'manual-reviewed'
    for path in manual.keys() - {file['path'] for file in report['files']}:
        errors.append(path + ': stale manual inventory for missing source')
    for path in pending.keys() - {file['path'] for file in report['files']}:
        errors.append(path + ': stale coverage gap for missing source')
    return errors


def check(current: dict, previous: dict, baseline: dict, old_baseline: dict) -> dict:
    errors, changes = [], []
    now, before = units(current), units(previous)
    entries = baseline.get('units', {})
    old_entries = old_baseline.get('units', {})
    for key in sorted(now.keys() | before.keys()):
        unit, old = now.get(key), before.get(key)
        if unit is None:
            changes.append({'unit': key, 'before': old['lines'], 'after': None})
            continue
        changed = old is None or unit['sha256'] != old['sha256']
        if changed:
            changes.append({'unit': key, 'before': old['lines'] if old else None,
                            'after': unit['lines'], 'start': unit.get('start'), 'end': unit.get('end')})
        trigger, ceiling = REVIEW[unit['kind']], CEILING[unit['kind']]
        if unit['lines'] <= trigger:
            if key in entries:
                errors.append(key + ': remove obsolete review entry (ratchet)')
            continue
        entry = entries.get(key, {})
        if (not reviewed(entry) or entry.get('lines') != unit['lines']
                or entry.get('sha256') != unit['sha256']):
            errors.append(key + ': missing/stale exact-size review entry')
            continue
        kind = entry.get('kind')
        if kind not in {'legacy', 'review', 'exception'}:
            errors.append(key + ': unknown review kind')
        if (old is not None and old['lines'] > trigger and unit['lines'] >= old['lines'] and changed
                and (kind != 'exception' or not entry.get('deferral'))):
            errors.append(key + ': edited existing hotspot must shrink or have bounded exception')
        prior_entry = old_entries.get(key)
        updated_review = entry != prior_entry and entry.get('review') != (prior_entry or {}).get('review')
        if changed and kind != 'legacy' and not updated_review:
            errors.append(key + ': changed hotspot requires a new review identifier')
        if kind == 'legacy':
            if old is None or (changed and unit['lines'] >= old['lines']):
                errors.append(key + ': legacy hotspot must shrink when edited')
            elif changed and not updated_review:
                errors.append(key + ': extraction requires updated independent review')
        elif unit['lines'] > ceiling:
            if kind != 'exception' or not isinstance(entry.get('deferral'), str) or not entry['deferral'].strip():
                errors.append(key + ': ceiling requires scoped technical exception and bounded deferral')
        if old is None and unit['lines'] > ceiling and kind != 'exception':
            errors.append(key + ': new/moved unit exceeds ceiling')
    for key in entries.keys() - now.keys():
        errors.append(key + ': stale baseline unit; remove after extraction/removal')
    return {'schema': 1, 'errors': sorted(set(errors)), 'changes': changes}


def load_json(path: Path) -> dict:
    result = json.loads(path.read_text())
    if result.get('schema') != 1:
        raise ValueError('Unsupported schema: ' + path.name)
    return result


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=('inventory', 'proposal', 'check'))
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument('--revision', help='Inventory a pinned Git tree instead of working files')
    parser.add_argument('--include-untracked', action='store_true',
                        help='Diagnostic inventory/proposal including unpublished nonignored source files')
    parser.add_argument('--base-ref', help='Required comparison tree for check; never inferred')
    parser.add_argument('--baseline', type=Path, default=Path('tools/ci/source_organization_baseline.json'))
    parser.add_argument('--output', type=Path)
    args = parser.parse_args(argv)
    try:
        root = args.root.resolve()
        if args.include_untracked and (args.command == 'check' or args.revision):
            parser.error('--include-untracked is only for working-tree inventory/proposal diagnostics')
        current = inventory(root, args.revision, args.include_untracked)
        if args.command == 'proposal':
            result = proposal(current)
        elif args.command == 'inventory':
            baseline_path = args.baseline if args.baseline.is_absolute() else root / args.baseline
            baseline = load_json(baseline_path)
            read_source = (lambda path: git(root, 'show', args.revision + ':' + path)) if args.revision else (
                lambda path: (root / path).read_bytes())
            errors = apply_manual(current, baseline, read_source)
            pending = [file['path'] for file in current['files']
                       if file['measurement'] == 'coverage-pending-frozen']
            current['errors'] = sorted(set(errors + [path + ': unresolved function coverage' for path in pending]))
            current['coverage_pending'] = pending
            result = current
        else:
            if not args.base_ref or args.revision:
                parser.error('check requires --base-ref and measures the current working tree')
            baseline_path = args.baseline if args.baseline.is_absolute() else root / args.baseline
            baseline = load_json(baseline_path)
            previous = inventory(root, args.base_ref)
            relative = baseline_path.relative_to(root).as_posix()
            try:
                old_baseline = json.loads(git(root, 'show', args.base_ref + ':' + relative))
            except Exception as error:
                raise ValueError('Base tree must contain reviewed baseline; bootstrap needs independent review') from error
            errors = apply_manual(current, baseline, lambda path: (root / path).read_bytes(), previous, old_baseline)
            errors += apply_manual(previous, old_baseline,
                                   lambda path: git(root, 'show', args.base_ref + ':' + path),
                                   historical_baseline=True)
            result = check(current, previous, baseline, old_baseline)
            result['errors'] = sorted(set(result['errors'] + errors))
            result['coverage_pending'] = [file['path'] for file in current['files']
                                          if file['measurement'] == 'coverage-pending-frozen']
            result['selection'] = current['selection']
            result['untracked_source_paths'] = current['untracked_source_paths']
        encoded = json.dumps(result, indent=2, ensure_ascii=True) + '\n'
        if args.output:
            args.output.write_text(encoded)
        else:
            sys.stdout.write(encoded)
        return 1 if result.get('errors') else 0
    except (OSError, ValueError, ImportError) as error:
        print('source organization: ' + str(error), file=sys.stderr)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
