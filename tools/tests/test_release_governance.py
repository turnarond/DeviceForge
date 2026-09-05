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
        lower_text = text.lower()
        self.assertIn("workflow_dispatch:", text)
        self.assertNotIn("push:", text)
        self.assertNotIn("pull_request:", text)
        for forbidden in (
            "gh release",
            "git tag",
            "actions/create-release",
            "softprops/action-gh-release",
            "contents: write",
        ):
            self.assertNotIn(forbidden, lower_text)
        for token in ("version:", "versioncheck.py", "ctest --test-dir",
                      "windeployqt", "Compress-Archive", "Expand-Archive",
                      "smoke.py", "upload-artifact@v4"):
            self.assertIn(token, text)

    def test_release_validation_fails_fast_and_uses_validated_inputs(self):
        text = (ROOT / ".github/workflows/release-validation.yml").read_text(encoding="utf-8")
        lower_text = text.lower()
        self.assertEqual(text.count("${{ inputs.version }}"), 1)
        self.assertIn("REQUESTED_VERSION: ${{ inputs.version }}", text)
        self.assertIn("'^[0-9]+\\.[0-9]+\\.[0-9]+$'", text)
        self.assertEqual(
            text.count("run: |"),
            text.count("$ErrorActionPreference = 'Stop'"),
        )
        self.assertNotIn("continue-on-error", lower_text)
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
            "& $pythonPath tools/devtools/smoke.py $smokeExe",
        ):
            self.assertRegex(
                text,
                re.escape(command) + r"[^\n]*\n\s*if \(\$LASTEXITCODE -ne 0\) \{ exit \$LASTEXITCODE \}",
            )
        self.assertIn("contents: read", text)
        self.assertEqual(text.count("if-no-files-found: error"), 1)

    def test_release_validation_only_builds_and_uploads_portable_zip(self):
        text = (ROOT / ".github/workflows/release-validation.yml").read_text(encoding="utf-8")
        lower_text = text.lower()

        for forbidden in (
            "choco install nsis",
            "makensis",
            "setup.exe",
            "installer",
            "upload installer",
            "-setup",
        ):
            self.assertNotIn(forbidden, lower_text)
        self.assertNotRegex(lower_text, r"\bnsis\b")

        self.assertEqual(text.count("actions/upload-artifact@v4"), 1)
        self.assertEqual(text.count("if-no-files-found: error"), 1)
        self.assertIn(
            'Compress-Archive -Path build\\Release\\* -DestinationPath $portableZip -Force',
            text,
        )
        self.assertIn(
            "path: dist\\DeviceForge-v${{ env.REQUESTED_VERSION }}-win64.zip",
            text,
        )

    def test_release_validation_smokes_extracted_zip_with_hermetic_qt_environment(self):
        text = (ROOT / ".github/workflows/release-validation.yml").read_text(encoding="utf-8")

        expected_tokens = (
            "$pythonPath = (Get-Command python -ErrorAction Stop).Source",
            '$smokeRoot = Join-Path $env:RUNNER_TEMP "deviceforge-portable-smoke-$([guid]::NewGuid().ToString(\'N\'))"',
            "New-Item -ItemType Directory -Path $smokeRoot",
            "Expand-Archive -LiteralPath $portableZip -DestinationPath $smokeRoot",
            "$smokeExe = Join-Path $smokeRoot 'DeviceForge.exe'",
            "Test-Path -LiteralPath $smokeExe",
            "$qtRoot = [IO.Path]::GetFullPath($env:QT_ROOT_DIR).TrimEnd('\\')",
            "$candidatePath.Equals($qtRoot, [StringComparison]::OrdinalIgnoreCase)",
            '$candidatePath.StartsWith("$qtRoot\\", [StringComparison]::OrdinalIgnoreCase)',
            "$env:PATH = (@($smokeRoot) + $sanitizedPathEntries) -join [IO.Path]::PathSeparator",
            "$env:QT_PLUGIN_PATH = $null",
            "$env:QT_QPA_PLATFORM_PLUGIN_PATH = $null",
            "$env:QML2_IMPORT_PATH = $null",
            "$env:QML_IMPORT_PATH = $null",
            "& $pythonPath tools/devtools/smoke.py $smokeExe",
        )
        for token in expected_tokens:
            self.assertIn(token, text)

        self.assertLess(text.index("Compress-Archive"), text.index("Expand-Archive"))
        self.assertLess(text.index("Get-Command python"), text.index("$env:PATH ="))
        self.assertLess(text.index("Expand-Archive"), text.index("smoke.py $smokeExe"))
        self.assertNotIn("smoke.py build\\Release\\DeviceForge.exe", text)
