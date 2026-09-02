from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]


class RepositoryGovernanceTest(unittest.TestCase):
    def test_github_is_tracked_and_worktree_is_ignored(self):
        text = (ROOT / ".gitignore").read_text(encoding="utf-8")
        self.assertNotIn(".github/", text.splitlines())
        self.assertIn(".worktrees/", text.splitlines())

    def test_build_script_allows_external_qt_path_and_probes_local_paths(self):
        text = (ROOT / "build.bat").read_text(encoding="utf-8").lower()
        self.assertIn("if not defined qt_prefix", text)
        self.assertIn("d:\\qt\\6.11.1\\msvc2022_64", text)
        self.assertNotIn("set qt_prefix=c:\\qt", text)

    def test_daily_ci_covers_both_configurations_and_all_gates(self):
        text = (ROOT / ".github/workflows/ci.yml").read_text(encoding="utf-8")
        for token in (
            "push:",
            "pull_request:",
            "workflow_dispatch:",
            "Debug",
            "Release",
            "Qt 6.9.2",
            "python-version: '3.12'",
            "unittest discover -s tools/tests -v",
            "tools/devtools/versioncheck.py",
            "ctest --test-dir",
        ):
            self.assertIn(token, text)
