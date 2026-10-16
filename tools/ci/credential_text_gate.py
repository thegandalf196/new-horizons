#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""CI prevention for supported text, never a binary or privacy clearance gate."""
import argparse
import hashlib
import json
from pathlib import Path
import re

import credential_content as content
import credential_jks_diagnostic as legacy
import credential_scope as scope
import credential_budget as budget
import credential_disposition as disposition


def required_issues():
    return [dict(code='unsupported-relevant-input', oid=oid,
                 path_sha256=hashlib.sha256(path.encode()).hexdigest(), rule_sha256=None,
                 location='manifest-only-no-content-location') for path, oid in legacy.BINDINGS.items()]


def valid_counts(counts):
    if not isinstance(counts, dict):
        return False
    positive = ('total_entries', 'text_aliases', 'evaluated_text_aliases', 'evaluated_unique_objects',
                'unique_objects', 'representations', 'physical_bytes', 'original_unique_bytes',
                'normalized_unique_bytes')
    zero_or_more = ('presence_entries', 'denied_entries', 'excluded_entries', 'external_gitlinks',
                    'known_binary_entries', 'known_binary_objects', 'uninspected_binary_aliases',
                    'uninspected_binary_objects')
    if any(type(counts.get(k)) is not int or counts[k] <= 0 for k in positive):
        return False
    if any(type(counts.get(k)) is not int or counts[k] < 0 for k in zero_or_more):
        return False
    if not (counts['text_aliases'] == counts['evaluated_text_aliases']
            and counts['unique_objects'] == counts['evaluated_unique_objects']
            and counts['unique_objects'] <= counts['representations'] <= counts['text_aliases']
            and counts['normalized_unique_bytes'] <= counts['original_unique_bytes'] <= budget.UNIQUE_BYTES
            and counts['normalized_unique_bytes'] <= counts['physical_bytes'] <= budget.PHYSICAL_BYTES
            and counts['total_entries'] <= budget.ENTRIES
            and counts['presence_entries'] == 0 and counts['denied_entries'] == 2
            and counts['uninspected_binary_aliases'] == counts['uninspected_binary_objects'] == 2
            and counts['known_binary_entries'] == counts['known_binary_objects'] <= len(scope.KNOWN_BINARY_BINDINGS)):
        return False
    categories = ('text_aliases', 'presence_entries', 'denied_entries', 'excluded_entries',
                  'external_gitlinks', 'known_binary_entries')
    suffixes, classes = counts.get('excluded_by_suffix'), counts.get('supported_by_applicability')
    if not isinstance(suffixes, dict) or not isinstance(classes, dict):
        return False
    if (any(k not in scope.EXCLUDED_SUFFIXES or type(v) is not int or v <= 0 for k, v in suffixes.items())
            or any(k not in {'generic', *(rules[0] for rules in scope.SUFFIX if rules)}
                   or type(v) is not int or v <= 0 for k, v in classes.items())):
        return False
    return (sum(counts[k] for k in categories) == counts['total_entries']
            and sum(suffixes.values()) == counts['excluded_entries']
            and sum(classes.values()) == counts['text_aliases'])


def accepts_supported_text(result):
    """Validate our in-process transaction; arbitrary exit2/report files are not inputs."""
    if not isinstance(result, dict):
        return False
    expected_scope = scope.contract() | {'diagnostic': legacy.contract()}
    required = required_issues()
    uninspected = [dict(issue, disposition=legacy.DISPOSITION, inspected=False) for issue in required]
    canonical = lambda rows: sorted(json.dumps(row, sort_keys=True) for row in rows)
    if (result.get('schema') != 3 or result.get('status') != 'issue' or result.get('gate_pass') is not False
            or result.get('evaluation_state') != 'complete' or result.get('text_status') != 'clean'
            or result.get('findings') != [] or result.get('scope') != expected_scope
            or not isinstance(result.get('selection_sha256'), str)
            or not re.fullmatch('[0-9a-f]{64}', result['selection_sha256'])
            or not isinstance(result.get('issues'), list) or not isinstance(result.get('uninspected'), list)):
        return False
    try:
        return (canonical(result['issues']) == canonical(required)
                and canonical(result['uninspected']) == canonical(uninspected)
                and disposition.valid_partition(result['findings'], result.get('reviewed_nonsecret_findings'),
                                                result.get('finding_counts'))
                and valid_counts(result.get('counts')))
    except (TypeError, ValueError, RecursionError):
        return False


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    parser.add_argument('--revision', required=True)
    parser.add_argument('--gitleaks-archive', type=Path, required=True)
    args = parser.parse_args(argv)
    # authorize() checks full path/mode/OID aliases and actual ancestor metadata
    # before any fetch. Complete/clean can only follow witness, scan and postcheck.
    result = content.inspect(args.root, revision=args.revision, archive=args.gitleaks_archive,
                             diagnostic_inherited_jks=True)
    accepted = accepts_supported_text(result)
    result['supported_text_ci'] = dict(accepted=accepted, binary_safety_established=False,
                                       privacy_or_history_clearance=False)
    print(json.dumps(result, sort_keys=True))
    return 0 if accepted else 2


if __name__ == '__main__':
    raise SystemExit(main())
