"""Pure workflow contract; never dispatches to any external repository."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = ROOT / ".github/workflows/on-release.yml"


class ReleaseNotificationTest(unittest.TestCase):
    def test_repository_and_prerelease_matrix(self):
        source = WORKFLOW.read_text()
        condition = re.search(r"^    if: \$\{\{ (.+) \}\}$", source, re.MULTILINE)
        self.assertIsNotNone(condition)
        # Parse the actual limited expression, not eval or a duplicated guard.
        guard = re.fullmatch(r"github\.repository == '([^']+)' && !github\.event\.release\.prerelease", condition.group(1))
        self.assertIsNotNone(guard)
        self.assertEqual(guard.group(1), "vcmi/vcmi")
        for repository, prerelease, expected in (
            ("vcmi/vcmi", False, True),
            ("vcmi/vcmi", True, False),
            ("example/new-horizons", False, False),
            ("example/new-horizons", True, False),
        ):
            with self.subTest(repository=repository, prerelease=prerelease):
                self.assertEqual(repository == guard.group(1) and not prerelease, expected)

    def test_upstream_release_dispatch_structure_is_preserved(self):
        source = WORKFLOW.read_text()
        for fragment in (
            "types: [published]",
            "  notify_repos:",
            "    runs-on: ubuntu-latest",
            "uses: peter-evans/repository-dispatch@v4",
            "token: ${{ secrets.HOMEBREW_TOKEN_SECRET }}",
            "repository: vcmi/homebrew-vcmi",
            "event-type: on-release",
            "client-payload: '{\"release\": ${{ toJson(github.event.release) }}}'",
        ):
            with self.subTest(fragment=fragment):
                self.assertIn(fragment, source)


if __name__ == "__main__":
    unittest.main()
