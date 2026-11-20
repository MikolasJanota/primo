import importlib.util
import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SCRIPT = Path(__file__).parents[2] / "scripts" / "check_doc_links.py"
SPEC = importlib.util.spec_from_file_location("check_doc_links", SCRIPT)
assert SPEC is not None and SPEC.loader is not None
LINKS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(LINKS)


class DocLinkTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.book = self.root / "html"
        self.book.mkdir()
        self.index = self.book / "index.html"
        self.index.write_text("<h1 id='intro'>Introduction</h1>")

    def check(self, html):
        self.index.write_text(html)
        checker = LINKS.Checker(self.root)
        checker.check_book(self.book)
        return checker

    def git(self, *args):
        return subprocess.check_output(
            ["git", "-C", str(self.root), *args], stderr=subprocess.PIPE, text=True
        ).strip()

    def test_local_targets_anchors_and_url_encoding(self):
        (self.book / "a b.html").write_text(
            '<h1 id="déjà">Heading</h1><a name="old"></a>'
        )
        (self.book / "asset.svg").write_text("<svg/>")
        (self.book / "nested").mkdir()
        (self.book / "nested/index.html").write_text("<h1 id='nested'>Nested</h1>")
        checker = self.check("""<h1 id="intro">Intro</h1>
            <a href="#intro">same page</a>
            <a href="a%20b.html?view=1#d%C3%A9j%C3%A0">encoded</a>
            <a href="a%20b.html#old">legacy anchor</a>
            <a href="/nested/#nested">root relative directory</a>
            <img src="asset.svg?version=1">
            <a href="https://example.org/#remote">external</a>""")
        self.assertEqual(checker.failures, [])
        self.assertEqual(checker.local_links, 5)
        self.assertEqual(checker.external_urls, {"https://example.org/#remote"})

    def test_missing_page_asset_and_fragment_fail(self):
        checker = self.check("""<a href="missing.html">missing</a>
            <img src="missing.svg"><a href="#absent">anchor</a>""")
        self.assertEqual(len(checker.failures), 3)
        self.assertIn("missing HTML anchor", checker.failures[-1]["error"])

    def test_unlinked_source_paths_in_article_prose_fail(self):
        checker = self.check('''<article>
            <p>Builders: <code>src/smt/strategy.hpp</code>.</p>
            <p>Implementation: src/smt/tactic.cpp.</p>
            <p><a id="label">src/core/term.hpp</a></p>
            </article>''')
        self.assertEqual(
            [(failure["line"], failure["uri"]) for failure in checker.failures],
            [(2, "src/smt/strategy.hpp"), (3, "src/smt/tactic.cpp"),
             (4, "src/core/term.hpp")],
        )

    def test_source_links_code_blocks_and_navigation_are_not_omissions(self):
        checker = self.check('''<nav>src/core/term.hpp</nav><article>
            <a href="#owner"><code><span>src/smt/strategy.hpp</span></code></a>
            <pre><code>clang-format src/smt/tactic.cpp</code></pre>
            <script>const file = "src/core/term.hpp";</script>
            <style>/* src/core/term.hpp */</style>
            <h2 id="owner">Owner</h2><p><code>src/smt/</code> is a layer.</p>
            <p>Use <code>src/smt/*.hpp</code> for a shell glob.</p></article>''')
        self.assertEqual(checker.failures, [])
        self.assertEqual(checker.local_links, 1)

    def test_historical_prose_is_preserved_but_its_links_are_checked(self):
        archive = self.book / "archive/original"
        archive.mkdir(parents=True)
        (archive / "old.html").write_text('''<article>
            <code>src/smt/strategy.hpp</code><a href="missing.html">broken</a>
            </article>''')
        checker = self.check('<article>Current prose.</article>')
        self.assertEqual(len(checker.failures), 1)
        self.assertEqual(checker.failures[0]["error"], "local target does not exist")

    def test_cannot_escape_book_even_if_file_exists(self):
        (self.root / "outside.html").write_text("outside")
        checker = self.check('<a href="../outside.html">escape</a>')
        self.assertIn("escapes", checker.failures[0]["error"])

    def test_missing_build_and_missing_git_commit_fail(self):
        checker = LINKS.Checker(self.root)
        checker.check_book(self.root / "absent")
        self.assertEqual(len(checker.failures), 1)
        error = checker.check_git(
            "https://github.com/primo-solver/primo/blob/abcdef0/src/a.cpp#L1"
        )
        self.assertIn("unavailable", error)

    def test_theme_templates_are_not_rendered_pages(self):
        (self.book / "_static").mkdir()
        (self.book / "_static/template.html").write_text('<a href="{{ template() }}">')
        checker = self.check('<a href="_static/template.html">asset</a>')
        self.assertEqual(checker.failures, [])
        self.assertEqual(checker.pages, 1)

    def test_git_uses_pinned_content_and_checks_ranges(self):
        git = self.git
        git("init", "-q")
        (self.root / "source.cpp").write_text("first\nsecond\n")
        git("add", "source.cpp")
        git(
            "-c",
            "user.name=Link Test",
            "-c",
            "user.email=links@example.invalid",
            "-c",
            "commit.gpgsign=false",
            "commit",
            "-qm",
            "fixture",
        )
        revision = git("rev-parse", "HEAD")
        (self.root / "source.cpp").write_text("changed\n")
        checker = LINKS.Checker(self.root)
        base = f"https://github.com/primo-solver/primo/blob/{revision}/source.cpp"
        self.assertIsNone(checker.check_git(base + "#L2"))
        self.assertIsNone(checker.check_git(base + "#L1-L2"))
        for fragment in ("#L0", "#L3", "#L2-L1", "#heading"):
            with self.subTest(fragment=fragment):
                self.assertIsNotNone(checker.check_git(base + fragment))
        self.assertIsNotNone(
            checker.check_git(base.replace("source.cpp", "missing.cpp"))
        )
        self.assertIsNotNone(checker.check_git(base.replace(revision, "main")))
        self.assertIsNone(
            checker.check_git(
                f"https://github.com/primo-solver/primo/commit/{revision}"
            )
        )

    def test_git_rejects_unrelated_history_present_in_local_database(self):
        git = self.git
        git("init", "-q")
        (self.root / "source.cpp").write_text("base\n")
        git("add", "source.cpp")
        identity = ("-c", "user.name=Link Test", "-c", "user.email=links@example.invalid",
                    "-c", "commit.gpgsign=false")
        git(*identity, "commit", "-qm", "base")
        ancestor = git("rev-parse", "HEAD")
        git("checkout", "-qb", "local-only")
        (self.root / "source.cpp").write_text("side history\n")
        git(*identity, "commit", "-qam", "local-only snapshot")
        unrelated = git("rev-parse", "HEAD")
        git("checkout", "-q", "--detach", ancestor)
        (self.root / "source.cpp").write_text("candidate\n")
        git(*identity, "commit", "-qam", "candidate")
        # The side branch keeps the object locally available, unlike a CI clone
        # that fetches only this candidate and the upstream repository's refs.
        self.assertEqual(git("cat-file", "-t", unrelated), "commit")
        checker = LINKS.Checker(self.root)
        for suffix in (f"blob/{unrelated}/source.cpp#L1", f"commit/{unrelated}",
                       f"blob/{unrelated[:8]}/source.cpp#L1"):
            with self.subTest(suffix=suffix):
                self.assertIn("not reachable from HEAD", checker.check_git(
                    "https://github.com/primo-solver/primo/" + suffix))
        for revision in (ancestor, git("rev-parse", "HEAD")):
            with self.subTest(revision=revision):
                self.assertIsNone(checker.check_git(
                    f"https://github.com/primo-solver/primo/blob/{revision}/source.cpp#L1"))

    def test_cli_fails_and_preserves_json_report(self):
        self.index.write_text('''<article><a href="absent.html">broken</a>
            <code>src/smt/strategy.hpp</code></article>''')
        report = self.root / "report.json"
        result = subprocess.run(
            [
                sys.executable,
                str(SCRIPT),
                str(self.book),
                "--repo",
                str(self.root),
                "--report",
                str(report),
            ],
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(result.returncode, 1)
        self.assertEqual(len(json.loads(report.read_text())["failures"]), 2)
        self.assertIn("absent.html", result.stdout)
        self.assertIn("unlinked source filename", result.stdout)


if __name__ == "__main__":
    unittest.main()
