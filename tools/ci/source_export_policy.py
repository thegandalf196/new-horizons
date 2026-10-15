"""Reviewed contributor documentation in source exports; unknown NH_ files stay private.

Historical revisions without the tracked documentation policy retain the original
blanket NH_ exclusion. Callers determine that opt-in from the exact exported tree,
never from an unrelated or untracked working-tree file.
"""

import argparse
import json
from pathlib import Path
import subprocess

DOCUMENTATION_POLICY = 'docs/NH_DOCUMENTATION_POLICY.md'
MAINTAINED_NH_DOCUMENTS = frozenset({
    'docs/NH_AI_EXCHANGES.md',
    'docs/NH_APPROVED_ART_WORKFLOW.md',
    'docs/NH_ART_ASSET_SPECIFICATIONS.md',
    'docs/NH_ART_DIRECTION.md',
    'docs/NH_CABIR_ART_REFERENCE.md',
    'docs/NH_COMMAND_ACCEPTANCE.md',
    'docs/NH_CONTENT_ACCEPTANCE.md',
    'docs/NH_DELIVERY_PIPELINE.md',
    'docs/NH_DEVELOPMENT_PHASES.md',
    'docs/NH_DOCUMENTATION_POLICY.md',
    'docs/NH_EXTRAS_INTEGRATION.md',
    'docs/NH_FUNCTIONAL_COMPLETION_MATRIX.md',
    'docs/NH_GUILD_SCROLL_EXPORT_AUDIT.md',
    'docs/NH_HERO_ORDERS_UI_REDESIGN.md',
    'docs/NH_HOMM3_UI_STYLE_GUIDE.md',
    'docs/NH_INTEGRATION_GAPS.md',
    'docs/NH_INVESTIGATIONS.md',
    'docs/NH_MARKDOWN_CANONICAL_MIGRATION.md',
    'docs/NH_ORIGINAL_GAPS.md',
    'docs/NH_OVERRIDE_MIGRATION.md',
    'docs/NH_PERFORMANCE_EXPERIMENT.md',
    'docs/NH_PERF_RESULTS.md',
    'docs/NH_PHASE1_EXIT_AUDIT.md',
    'docs/NH_PLAYER_BRANDING.md',
    'docs/NH_PRIMARY_PROGRESSION.md',
    'docs/NH_PROVISIONAL_RULE_REVIEW.md',
    'docs/NH_REGRESSION_GUARDS.md',
    'docs/NH_RELEASE_FAILURES.md',
    'docs/NH_RELEASE_UPDATES.md',
    'docs/NH_SCHOOL_ART_COMPLETENESS.md',
    'docs/NH_SORCERY_STYLE_BRIEF.md',
    'docs/NH_STARTING_ARMIES_PROVISIONAL.md',
    'docs/NH_TEMPLATE_ART_WORKFLOW.md',
    'docs/NH_UI_ASSET_STATUS.md',
    'docs/NH_UI_EDITOR.md',
    'docs/NH_VERSIONING.md',
    'docs/NH_VERSION_1_0_SCOPE.md',
    'docs/NH_WINDOWS_ACCEPTANCE.md',
})


def private_design_path(name):
    if name.startswith('docs/design-sources/'):
        return name != 'docs/design-sources/README.md'
    return name == 'tools/migrate-new-horizons-canonical-docx.py'


def exclude_source_path(name, *, documentation_policy):
    if name == 'CI/deploy_rsa.enc' or private_design_path(name):
        return True
    if name.startswith('docs/NH_'):
        return not documentation_policy or name not in MAINTAINED_NH_DOCUMENTS
    return False


def forbidden_tracked_design(root):
    root = root.resolve(strict=True)
    actual = subprocess.check_output(['git', '-C', str(root), 'rev-parse', '--show-toplevel'], text=True).strip()
    if Path(actual).resolve() != root:
        raise ValueError('--root must be the actual public Git root')
    names = subprocess.check_output(['git', '-C', str(root), 'ls-files', '-z']).decode().split('\0')
    return sorted(name for name in names if private_design_path(name))


def main():
    parser = argparse.ArgumentParser(description='Reject retired private design inputs in the tracked public tree.')
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[2])
    args = parser.parse_args()
    forbidden = forbidden_tracked_design(args.root)
    print(json.dumps({'forbiddenDesignPaths': forbidden}))
    return bool(forbidden)


if __name__ == '__main__':
    raise SystemExit(main())
