# SPDX-License-Identifier: GPL-2.0-or-later
"""Path-aware selected Git credential inspection; no implicit scans or downloads."""
import argparse
import hashlib
import json
from pathlib import Path
import tempfile
import time

import credential_budget as budget
import credential_scope as scope
import credential_payload as payload
import credential_jks_diagnostic as diagnostic
import credential_disposition as disposition
import git_credential_blobs as reader
from scanner_runtime import ScannerError, prepare, run

HERE = Path(__file__).resolve().parent
WHOLE_SECONDS = 240
CODES = frozenset('''scanner-deadline scanner-output-limit scanner-input-incomplete
scanner-process-unavailable unsupported-scanner-platform scanner-archive-unavailable
scanner-archive-identity scanner-archive-members scanner-archive-member-size
scanner-license-identity scanner-byte-completion scanner-report-format scanner-report-count
scanner-exit-report-disagreement scanner-report-fields scanner-report-value scanner-version
scanner-positive-witness scanner-negative-witness scanner-total-finding-limit
index-changed-during-scanner git-inspection-budget-or-process git-inspection-unavailable
git-inspection-failed choose-exactly-one-input-mode invalid-root root-must-be-git-toplevel
unsupported-object-format unmerged-index invalid-index-or-tree-entry incomplete-object-metadata
unreadable-selected-object selected-text-over-16MiB selected-object-size-or-hash-mismatch
nul-in-selected-text selected-text-not-UTF8 selected-entry-budget selected-unique-byte-budget payload-object-identity
payload-membership payload-file-type payload-hash scanner-report-file scanner-incomplete-diagnostic
scanner-cwd-not-empty scanner-resource-status scanner-rss-abort unknown-path-applicability
scanner-network-sandbox scanner-cleanup-incomplete commit-changed-during-scanner
credential-metadata-budget credential-physical-budget scanner-rule-applicability'''.split())


def synthetic_token():
    return 'gh' + 'p_' + hashlib.sha256(b'public synthetic witness only').hexdigest()[:36]


def witness(binary, template, owned, deadline):
    budget.tick(deadline)
    code, output, diagnostic = run([str(binary), 'version'], b'', owned, deadline,
                                    stdout_limit=64, stderr_limit=1024)
    if code or output != b'8.30.1\n' or diagnostic:
        raise ScannerError('scanner-version')
    objects, aliases = {}, []
    for data in (('token="' + synthetic_token() + '"\n').encode(), b'ordinary public documentation\n'):
        budget.tick(deadline)
        oid = hashlib.sha1(b'blob ' + str(len(data)).encode() + b'\0' + data).hexdigest()
        objects[oid] = reader.VerifiedObject(oid, data, hashlib.sha256(data).hexdigest(), 2)
        aliases.append(dict(oid=oid, rules=()))
    result, _ = payload.scan(binary, template, objects, aliases, deadline, witness=True)
    expected = hashlib.sha256(b'github-pat').hexdigest()
    if not any(f['rule_sha256'] == expected for f in result.get((aliases[0]['oid'], ()), [])):
        raise ScannerError('scanner-positive-witness')
    if result.get((aliases[1]['oid'], ())):
        raise ScannerError('scanner-negative-witness')


def map_findings(aliases, findings, deadline):
    result = []
    for alias in aliases:
        budget.tick(deadline)
        for finding in findings.get((alias['oid'], alias['rules']), []):
            budget.tick(deadline)
            if len(result) >= budget.FINDINGS:
                raise ScannerError('scanner-total-finding-limit')
            result.append(dict(finding, oid=alias['oid'], path_sha256=alias['path_sha256']))
    budget.tick(deadline)
    return result


def failed_result(result, manifest_issues, code, object_oid=None):
    issue = {'code': code}
    if object_oid is not None:
        issue['object_oid'] = object_oid
    result.update(issues=manifest_issues + [issue], findings=[], reviewed_nonsecret_findings=[],
                  finding_counts=dict(detected=None, reviewed_nonsecret=None, unresolved=None))


def inspect(root, *, staged=False, revision=None, archive, diagnostic_inherited_jks=False):
    deadline = time.monotonic() + WHOLE_SECONDS
    result = dict(schema=3, status='issue', scope=scope.contract(), issues=[], findings=[], reviewed_nonsecret_findings=[],
                  finding_counts=dict(detected=None, reviewed_nonsecret=None, unresolved=None),
                  evaluation_state='not-started',
                  counts=dict(total_entries=None, text_aliases=None, excluded_entries=None,
                              denied_entries=None, presence_entries=None,
                              evaluated_text_aliases=0, evaluated_unique_objects=0))
    manifest_issues = []
    if diagnostic_inherited_jks:
        result.update(gate_pass=False, text_status='not-started', uninspected=[])
        result['scope']['diagnostic'] = diagnostic.contract()
    try:
        budget.tick(deadline)
        scope.validate_manifest(json.loads((HERE / 'path-predicates.lock.json').read_text()))
        options = dict(staged=staged, revision=revision, deadline=deadline)
        if diagnostic_inherited_jks:
            options['diagnostic_inherited_jks'] = True
        selected = reader.read_credentials(root, **options)
        if diagnostic_inherited_jks:
            manifest_issues = list(selected['issues'])
            result.update(counts=selected['counts'], selection_sha256=selected['selection_sha256'], issues=manifest_issues)
        budget.tick(deadline)
        result.update(counts=selected['counts'], selection_sha256=selected['selection_sha256'])
        if diagnostic_inherited_jks:
            reader.recheck(selected['root'], staged, selected['manifest'], deadline, selected['commit'])
            result['uninspected'] = diagnostic.authorize(selected, deadline)
            result['counts'].update(uninspected_binary_aliases=2, uninspected_binary_objects=2)
            result['evaluation_state'] = 'incomplete'
            result['counts'].update(evaluated_text_aliases=None, evaluated_unique_objects=None)
            identifiers = {alias['oid'].encode() for alias in selected['aliases']}
            selected['objects'] = reader.fetch_objects(selected['root'], selected['object_format'], identifiers, deadline)
        elif selected['issues']:
            result['issues'] = selected['issues']
            budget.tick(deadline)
            return result
        with tempfile.TemporaryDirectory(prefix='nh-credentialv2-') as directory:
            owned = Path(directory)
            budget.tick(deadline)
            binary, template = prepare(archive, owned)
            budget.tick(deadline)
            cwd = owned / 'cwd'
            cwd.mkdir(mode=0o700)
            witness(binary, template, cwd, deadline)
            result['evaluation_state'] = 'incomplete'
            result['counts'].update(evaluated_text_aliases=None, evaluated_unique_objects=None)
            findings, counts = payload.scan(binary, template, selected['objects'], selected['aliases'], deadline)
            result['evaluation_state'] = 'complete'
            result['counts'].update(counts, unique_objects=len(selected['objects']),
                                    evaluated_unique_objects=len(selected['objects']),
                                    evaluated_text_aliases=len(selected['aliases']),
                                    normalized_unique_bytes=sum(len(o.data) for o in selected['objects'].values()),
                                    original_unique_bytes=sum(o.source_bytes for o in selected['objects'].values()))
            result['findings'] = map_findings(selected['aliases'], findings, deadline)
            reader.recheck(selected['root'], staged, selected['manifest'], deadline, selected['commit'])
        budget.tick(deadline)
        result['findings'], result['reviewed_nonsecret_findings'], result['finding_counts'] = disposition.partition(
            result['findings'], deadline)
        if diagnostic_inherited_jks:
            result['text_status'] = 'findings' if result['findings'] else 'clean'
        else:
            result['status'] = 'findings' if result['findings'] else 'clean'
    except (ScannerError, reader.InspectionError) as error:
        code = str(error) if str(error) in CODES or str(error) in {
            'diagnostic-jks-binding-mismatch', 'diagnostic-jks-provenance-mismatch'} else 'credential-inspection-failed'
        failed_result(result, manifest_issues, code,
                      error.object_oid if isinstance(error, reader.SelectedTextError) else None)
    except UnicodeError:
        failed_result(result, manifest_issues, 'selected-text-not-UTF8')
    except (OSError, ValueError, RecursionError, TypeError, KeyError):
        failed_result(result, manifest_issues, 'credential-inspection-failed')
    if diagnostic_inherited_jks and result['text_status'] == 'not-started' and result['evaluation_state'] != 'not-started':
        result['text_status'] = 'failed'
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, required=True)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--staged', action='store_true')
    mode.add_argument('--revision')
    parser.add_argument('--gitleaks-archive', '--archive', dest='archive', type=Path, required=True)
    parser.add_argument('--diagnostic-inherited-jks', action='store_true')
    args = parser.parse_args()
    result = inspect(args.root, staged=args.staged, revision=args.revision, archive=args.archive,
                     diagnostic_inherited_jks=args.diagnostic_inherited_jks)
    print(json.dumps(result, sort_keys=True))
    return {'clean':0, 'findings':1, 'issue':2}[result['status']]


if __name__ == '__main__':
    raise SystemExit(main())
