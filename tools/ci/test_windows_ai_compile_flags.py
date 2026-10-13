"""Synthetic, process-free contract controls; no compiler or game invocation."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("windows_ai_compile_flags", Path(__file__).with_name("windows_ai_compile_flags.py"))
subject = importlib.util.module_from_spec(spec)
spec.loader.exec_module(subject)


class WindowsAIFlagTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.entries = [{"file": "PRIVATE_USER_TOKEN/" + name, "directory": str(self.root),
                         "arguments": ["sccache", "cl.exe", "/O2", "/Ob1", "/DNDEBUG", "/I", "PRIVATE_ENV_TOKEN", name]}
                        for name in subject.SOURCES]
        (self.root / "CMakeCache.txt").write_text("CMAKE_CXX_FLAGS_RELWITHDEBINFO:STRING=/O2 /Ob1 /DNDEBUG /Z7\nPRIVATE_USER_TOKEN:STRING=SECRET\n", encoding="utf-8")
        self.write_database()

    def write_database(self):
        (self.root / "compile_commands.json").write_text(json.dumps(self.entries), encoding="utf-8")

    def collect(self, environment=None):
        with patch("subprocess.Popen", side_effect=AssertionError("process forbidden")):
            return subject.collect(self.root, {} if environment is None else environment)

    def test_order_privacy_and_exact_sources(self):
        report = self.collect({"CL": "/Ob0 /I PRIVATE_ENV_TOKEN", "_CL_": "-Ob2", "UNRELATED": "SECRET"})
        self.assertEqual(len(report["translation_units"]), 2)
        row = report["translation_units"][0]
        self.assertEqual(row["ordered_explicit_flags"], ["/Ob0", "/O2", "/Ob1", "/DNDEBUG", "-Ob2"])
        serialized = json.dumps(report)
        for private in ("PRIVATE_USER_TOKEN", "PRIVATE_ENV_TOKEN", "SECRET", str(self.root)):
            self.assertNotIn(private, serialized)
        self.assertEqual(set(report["cache_flags"]), {"CMAKE_CXX_FLAGS_RELWITHDEBINFO"})

    def test_windows_quotes_and_link_tail(self):
        words = subject.split_windows(r'"C:\Program Files\cl.exe" /Ob1 @"flags with spaces.rsp" /link /Ob3')
        self.assertEqual(words[0], r"C:\Program Files\cl.exe")
        self.assertEqual(words[2], "@flags with spaces.rsp")
        self.assertEqual(subject.flags(words), ["/Ob1"])
        with self.assertRaises(ValueError):
            subject.split_windows('"unfinished')

    def test_nested_utf16_response_expansion(self):
        (self.root / "outer.rsp").write_bytes('/O2 @"inner flags.rsp" /DNDEBUG'.encode("utf-16"))
        (self.root / "inner flags.rsp").write_text('/Ob2 /I "PRIVATE_ENV_TOKEN"', encoding="utf-8")
        self.entries[0]["arguments"] = ["cl.exe", "@outer.rsp", subject.SOURCES[0]]
        self.write_database()
        self.assertEqual(self.collect()["translation_units"][0]["command_flags"], ["/O2", "/Ob2", "/DNDEBUG"])

    def test_dash_link_tail_and_environment_boundaries(self):
        for boundary in ("/link", "-link", "-LINK"):
            with self.subTest(boundary=boundary):
                self.assertEqual(subject.flags(["/Ob1", boundary, "/Ob3"]), ["/Ob1"])
                for variable in ("CL", "_CL_"):
                    with self.assertRaisesRegex(ValueError, "environment linker boundary"):
                        self.collect({variable: boundary + " /Ob3"})

    def test_repeated_response_has_cumulative_token_bound(self):
        (self.root / "tokens.rsp").write_text("/Ob1 " * 32, encoding="utf-8")
        self.assertEqual(len(subject.expand(["@tokens.rsp"] * 1024, self.root, self.root)), 32768)
        with self.assertRaisesRegex(ValueError, "response token bound"):
            subject.expand(["@tokens.rsp"] * 1025, self.root, self.root)

    def test_escape_cycle_and_missing_response_refuse(self):
        for response in ("@../outside.rsp", "@missing.rsp", "@cycle.rsp"):
            with self.subTest(response=response):
                (self.root / "cycle.rsp").write_text("@cycle.rsp", encoding="utf-8")
                self.entries[0]["arguments"] = ["cl.exe", response]
                self.write_database()
                with self.assertRaises((ValueError, FileNotFoundError)):
                    self.collect()

    def test_response_size_and_depth_refuse(self):
        (self.root / "large.rsp").write_bytes(b"x" * (1024 * 1024 + 1))
        with self.assertRaises(ValueError):
            subject.expand(["@large.rsp"], self.root, self.root)
        for index in range(6):
            (self.root / f"{index}.rsp").write_text(f"@{index + 1}.rsp" if index < 5 else "/Ob1", encoding="utf-8")
        with self.assertRaises(ValueError):
            subject.expand(["@0.rsp"], self.root, self.root)

    def test_duplicate_source_and_shell_compiler_refuse(self):
        self.entries.append(dict(self.entries[0]))
        self.write_database()
        with self.assertRaises(ValueError):
            self.collect()
        self.entries.pop()
        self.entries[0]["arguments"] = ["cl.exe", "&&", "cl.exe", "/Ob1"]
        self.write_database()
        with self.assertRaises(ValueError):
            self.collect()


if __name__ == "__main__":
    unittest.main()
