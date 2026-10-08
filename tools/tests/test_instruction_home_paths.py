# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic privacy-policy fixtures; no production identity or history."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

SCRIPT = Path(__file__).resolve().parents[1] / "check_instruction_home_paths.py"
SPEC = importlib.util.spec_from_file_location("home_path_policy", SCRIPT)
POLICY = importlib.util.module_from_spec(SPEC)
import sys
sys.modules[SPEC.name] = POLICY
SPEC.loader.exec_module(POLICY)


class HomePathPolicyTest(unittest.TestCase):
    def test_literal_unix_mac_windows_and_common_encoded_forms(self):
        for text in (
            "/home/sample-account/project", "/Users/sample-account/project",
            r"C:\Users\sample-account\project", "C:/Users/sample-account/project",
            r"C:\\Users\\sample-account\\project", r"\/home\/sample-account\/project",
            "%2Fhome%2Fsample-account%2Fproject", "%252FUsers%252Fsample-account%252Fproject",
            "&#47;home&#47;sample-account&#47;project",
            r"\u002fhome\u002fsample-account\u002fproject",
            r"\x2fUsers\x2fsample-account\x2fproject",
        ):
            with self.subTest(text=text):
                self.assertTrue(POLICY.contains_literal_home_path(text))

    def test_portable_variables_placeholders_and_unrelated_paths(self):
        for text in (
            "$HOME/project", "~/project", "%USERPROFILE%/project",
            "/home/<user>/project", "/Users/<username>/project", r"C:\Users\<user>\project",
            "/home/${USER}/project", "/home/$USER/project", r"C:\Users\%USERNAME%\project",
            "/usr/share/project", "/tmp/work/project", "relative/home/sample-account/project",
        ):
            with self.subTest(text=text):
                self.assertFalse(POLICY.contains_literal_home_path(text))

    def test_project_instruction_scope(self):
        for path in ("AGENTS.md", "docs/design.md", ".agents/skills/work/SKILL.md", "README.TXT"):
            self.assertTrue(POLICY.project_document(path))
        for path in ("src/code.cpp", "assets/image.png", "vendor/reference.md", "third_party/README.txt"):
            self.assertFalse(POLICY.project_document(path))

    def test_diff_checks_additions_not_removed_or_historical_lines(self):
        diff = "--- a/docs/sample.md\n+++ b/docs/sample.md\n@@ -4,2 +4,2 @@\n-/home/sample-account/removed\n+portable $HOME/new\n unchanged /home/sample-account/old\n@@ -20,0 +21 @@\n+/Users/sample-account/new\n"
        additions = list(POLICY.added_lines(diff))
        self.assertEqual(additions, [(4, "portable $HOME/new"), (21, "/Users/sample-account/new")])
        self.assertEqual([number for number, text in additions if POLICY.contains_literal_home_path(text)], [21])

    def test_staged_reads_git_patch_not_worktree_and_excludes_code(self):
        calls = []
        def fake_git(repo, *arguments):
            calls.append(arguments)
            if "--name-only" in arguments:
                return b"docs/sample.md\0src/code.cpp\0"
            return b"@@ -0,0 +1 @@\n+/home/sample-account/new\n"
        with patch.object(POLICY, "git", side_effect=fake_git):
            findings = POLICY.inspect(Path("."), staged=True)
        self.assertEqual(findings, [POLICY.Finding("docs/sample.md", 1)])
        self.assertTrue(all("--cached" in call for call in calls))
        self.assertEqual(len(calls), 2)

    def test_base_is_resolved_before_diff(self):
        calls = []
        def fake_git(repo, *arguments):
            calls.append(arguments)
            return b"a" * 40 + b"\n" if arguments[0] == "rev-parse" else b""
        with patch.object(POLICY, "git", side_effect=fake_git):
            self.assertEqual(POLICY.inspect(Path("."), base="HEAD"), [])
        self.assertIn("--end-of-options", calls[0])
        self.assertIn("a" * 40, calls[1])

    def test_default_scans_only_tracked_docs_and_does_not_follow_symlinks(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / "docs").mkdir()
            (root / "docs/sample.md").write_text("safe\n/home/sample-account/project\n")
            (root / "docs/untracked.md").write_text("/home/sample-account/untracked\n")
            (root / "docs/link.md").symlink_to(root / "docs/untracked.md")
            with patch.object(POLICY, "git", return_value=b"docs/sample.md\0docs/link.md\0"):
                findings = POLICY.inspect(root)
            self.assertEqual(findings, [POLICY.Finding("docs/sample.md", 2)])

    def test_diagnostics_do_not_echo_personal_path_or_source_snippet(self):
        with patch.object(POLICY, "inspect", return_value=[POLICY.Finding("docs/sample.md", 9)]), \
                patch.object(sys, "stderr") as stderr:
            self.assertEqual(POLICY.main([]), 1)
        output = "".join(call.args[0] for call in stderr.write.call_args_list)
        self.assertIn("docs/sample.md:9", output)
        self.assertNotIn("sample-account", output)

    def test_os_error_does_not_echo_absolute_path(self):
        with patch.object(POLICY, "inspect", side_effect=OSError("/home/sample-account/private")), \
                patch.object(sys, "stderr") as stderr:
            with self.assertRaises(SystemExit) as result:
                POLICY.main([])
        self.assertEqual(result.exception.code, 2)
        output = "".join(call.args[0] for call in stderr.write.call_args_list)
        self.assertNotIn("sample-account", output)


if __name__ == "__main__":
    unittest.main()
