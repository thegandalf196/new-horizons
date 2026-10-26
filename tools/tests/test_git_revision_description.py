"""Exercise Git revision metadata without Git repositories or compilation.

CMAKE_EXECUTABLE selects CMake; otherwise it is discovered on PATH.
NH_TEST_GIT_REVISION_MODULE_DIR optionally selects private module afterimages
for pre-application testing. Ordinary runs use this checkout's cmake_modules.
"""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]
SHA = "1" * 40
OTHER_SHA = "2" * 40


class GitRevisionDescriptionTest(unittest.TestCase):
    def setUp(self):
        self.cmake = os.environ.get("CMAKE_EXECUTABLE") or shutil.which("cmake")
        if not self.cmake:
            self.skipTest("CMake is unavailable")
        self.modules = Path(os.environ.get(
            "NH_TEST_GIT_REVISION_MODULE_DIR", REPO_ROOT / "cmake_modules"))
        self.assertTrue((self.modules / "GetGitRevisionDescription.cmake").is_file())
        self.scratch = tempfile.TemporaryDirectory(prefix="git-revision-")
        self.addCleanup(self.scratch.cleanup)
        self.root = Path(self.scratch.name).resolve()
        self.assertFalse(self.root.is_relative_to(REPO_ROOT))

    def write(self, path, contents):
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(contents, encoding="utf-8")

    def check_revision(self, source, expected_ref, expected_hash):
        output = self.root / "output"
        # Failed resolution must never reuse copied refs or caller variables.
        self.write(output / "CMakeFiles/git-data/head-ref", OTHER_SHA + "\n")
        runner = self.root / "probe.cmake"
        module = self.modules / "GetGitRevisionDescription.cmake"
        result = self.root / "result.txt"
        self.write(runner, f'''cmake_minimum_required(VERSION 3.16)
include([==[{module.as_posix()}]==])
set(CMAKE_CURRENT_LIST_DIR [==[{source.as_posix()}]==])
set(CMAKE_CURRENT_BINARY_DIR [==[{output.as_posix()}]==])
set(ref "stale")
set(hash "stale")
set(HEAD_REF "cached-stale-ref" CACHE INTERNAL "" FORCE)
set(HEAD_HASH "cached-stale-hash" CACHE INTERNAL "" FORCE)
get_git_head_revision(ref hash)
file(WRITE [==[{result.as_posix()}]==] "${{ref}}\\n${{hash}}\\n")
''')
        process = subprocess.run([self.cmake, "-P", str(runner)],
                                 capture_output=True, text=True, timeout=15)
        self.assertEqual(process.returncode, 0, process.stdout + process.stderr)
        self.assertEqual(result.read_text(encoding="utf-8").splitlines(),
                         [expected_ref, expected_hash])

    def test_ordinary_loose_and_exact_packed_refs(self):
        source = self.root / "source with spaces"
        metadata = source / ".git"
        ref = "refs/heads/topic.v1"
        self.write(metadata / "HEAD", f"ref: {ref}\n")
        self.write(metadata / ref, SHA + "\n")
        with self.subTest(storage="loose, searched from child"):
            self.check_revision(source / "nested", ref, SHA)
        (metadata / ref).unlink()
        self.write(metadata / "packed-refs", "# pack-refs with: peeled sorted\n"
                   f"{OTHER_SHA} refs/heads/topicXv1\n"
                   f"{OTHER_SHA} {ref}-more\n{SHA} {ref}\n^{OTHER_SHA}\n")
        with self.subTest(storage="packed, exact ref"):
            self.check_revision(source, ref, SHA)

    def test_absolute_and_relative_gitdir_files(self):
        for absolute in (False, True):
            with self.subTest(absolute=absolute):
                base = self.root / str(absolute)
                source, metadata = base / "source", base / "metadata"
                target = metadata.as_posix() if absolute else "../metadata"
                self.write(source / ".git", f"gitdir: {target}\n")
                self.write(metadata / "HEAD", "ref: refs/heads/main\n")
                self.write(metadata / "refs/heads/main", SHA + "\n")
                self.check_revision(source, "refs/heads/main", SHA)

    def test_absolute_and_relative_commondir_loose_and_packed(self):
        for absolute in (False, True):
            for packed in (False, True):
                with self.subTest(absolute=absolute, packed=packed):
                    base = self.root / f"{absolute}-{packed}"
                    source, common = base / "source", base / "common"
                    metadata = common / "worktrees/linked"
                    self.write(source / ".git", f"gitdir: {metadata.as_posix()}\n")
                    target = common.as_posix() if absolute else "../.."
                    self.write(metadata / "commondir", target + "\n")
                    self.write(metadata / "HEAD", "ref: refs/heads/main\n")
                    if packed:
                        self.write(common / "packed-refs", f"{SHA} refs/heads/main\n")
                    else:
                        self.write(common / "refs/heads/main", SHA + "\n")
                    self.check_revision(source, "refs/heads/main", SHA)
                    # A stale local branch must not shadow either shared storage.
                    self.write(metadata / "refs/heads/main", OTHER_SHA + "\n")
                    self.check_revision(source, "refs/heads/main", SHA)

    def test_private_ref_namespaces_never_fall_back_to_common(self):
        for namespace in ("bisect", "worktree", "rewritten"):
            with self.subTest(namespace=namespace):
                base = self.root / namespace
                source, common = base / "source", base / "common"
                metadata = common / "worktrees/linked"
                ref = f"refs/{namespace}/current"
                self.write(source / ".git", f"gitdir: {metadata.as_posix()}\n")
                self.write(metadata / "commondir", "../..\n")
                self.write(metadata / "HEAD", f"ref: {ref}\n")
                self.write(metadata / ref, SHA + "\n")
                self.write(common / ref, OTHER_SHA + "\n")
                self.write(common / "packed-refs", f"{OTHER_SHA} {ref}\n")
                self.check_revision(source, ref, SHA)
                (metadata / ref).unlink()
                self.check_revision(source, ref, "")
                (common / ref).unlink()
                self.check_revision(source, ref, "")

    def test_detached_head_in_directory_and_worktree(self):
        for worktree in (False, True):
            with self.subTest(worktree=worktree):
                source = self.root / str(worktree) / "source"
                metadata = source / ".git"
                if worktree:
                    metadata = source.parent / "common/worktrees/linked"
                    self.write(source / ".git", f"gitdir: {metadata.as_posix()}\n")
                    self.write(metadata / "commondir", "../..\n")
                self.write(metadata / "HEAD", SHA + "\n")
                self.check_revision(source, "", SHA)

    def test_missing_metadata_does_not_reuse_stale_hash(self):
        source = self.root / "source"
        metadata = source / ".git"
        metadata.mkdir(parents=True)
        with self.subTest(missing="HEAD"):
            self.check_revision(source, "", "")
        self.write(metadata / "HEAD", "ref: refs/heads/unborn\n")
        with self.subTest(missing="loose and packed refs"):
            self.check_revision(source, "refs/heads/unborn", "")
        self.write(metadata / "packed-refs", f"{SHA} refs/heads/another\n")
        with self.subTest(missing="packed entry"):
            self.check_revision(source, "refs/heads/unborn", "")

    def test_gitdir_file_missing_target_and_malformed(self):
        source = self.root / "source"
        for contents in ("gitdir: ../missing\n", "invalid metadata\n"):
            with self.subTest(contents=contents):
                self.write(source / ".git", contents)
                self.check_revision(source, "", "")

    def test_source_archive(self):
        source = self.root / "source"
        source.mkdir()
        self.check_revision(source, "GITDIR-NOTFOUND", "GITDIR-NOTFOUND")


if __name__ == "__main__":
    unittest.main()
