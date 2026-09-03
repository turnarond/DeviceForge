from pathlib import Path
import re
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

    def test_发布验收只允许手动触发且不发布Release(self):
        text = (ROOT / ".github/workflows/release-validation.yml").read_text(encoding="utf-8")
        self.assertIn("workflow_dispatch:", text)
        self.assertNotIn("push:", text)
        self.assertNotIn("pull_request:", text)
        self.assertNotIn("gh release", text.lower())
        for token in ("version:", "versioncheck.py", "ctest --test-dir",
                      "windeployqt", "smoke.py", "makensis", "upload-artifact@v4"):
            self.assertIn(token, text)

    def test_release_validation_fails_fast_and_uses_validated_inputs(self):
        text = (ROOT / ".github/workflows/release-validation.yml").read_text(encoding="utf-8")
        self.assertEqual(text.count("${{ inputs.version }}"), 1)
        self.assertIn("REQUESTED_VERSION: ${{ inputs.version }}", text)
        self.assertIn("'^[0-9]+\\.[0-9]+\\.[0-9]+$'", text)
        self.assertIn("${env:ProgramFiles(x86)}\\NSIS\\makensis.exe", text)
        self.assertIn("Test-Path -LiteralPath $makensisPath", text)
        self.assertIn(
            "windeployqt --release --no-translations --plugindir build\\Release\\plugins "
            "--include-plugins qwindows,qsqlite build\\Release\\DeviceForge.exe",
            text)
        for plugin in ("plugins\\platforms\\qwindows.dll", "plugins\\sqldrivers\\qsqlite.dll"):
            self.assertIn(plugin, text)
        for command in (
            "python -m unittest discover -s tools/tests -v",
            "python tools/devtools/versioncheck.py",
            "cmake -S . -B build",
            "cmake --build build --config Release --parallel",
            "ctest --test-dir build -C Release --output-on-failure",
            "windeployqt --release --no-translations",
            "python tools/devtools/smoke.py build\\Release\\DeviceForge.exe",
            "choco install nsis -y --no-progress",
            "& $makensisPath",
        ):
            self.assertRegex(
                text,
                re.escape(command) + r"[^\n]*\n\s*if \(\$LASTEXITCODE -ne 0\) \{ exit \$LASTEXITCODE \}",
            )
        self.assertIn("contents: read", text)
        self.assertEqual(text.count("if-no-files-found: error"), 2)
