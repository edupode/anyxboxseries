"""Exercise the CLI's read-only GitHub calls with a synthetic gh executable."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


TOOL_DIR = Path(__file__).resolve().parents[1]
REPO = "ItsNotRexed/Leonida"
TRACKER = f"https://github.com/{REPO}/issues/123"
CLAIM = f"{TRACKER}#issuecomment-42"
PR = f"https://github.com/{REPO}/pull/7"
TASK = "DEMO-123"
SHA = "a" * 40
UPDATED = "2026-01-01T00:00:00Z"
REMOTE_TEXT = "synthetic remote free text"
LEGACY = (str(TOOL_DIR / "coordination_watch.py"),)
MODULE = ("-m", "leonida_coordination")
ENTRY_POINTS = (LEGACY, MODULE)

FAKE_GH = r'''
import json
import os
from pathlib import Path
import sys
from urllib.parse import parse_qs, urlsplit

scenario = os.environ["TEST_GH_SCENARIO"]
repo = "ItsNotRexed/Leonida"
url = f"https://github.com/{repo}/issues/123"
task = "DEMO-123"
title = "Coordinating who works on what (123)"
updated = "2026-01-01T00:00:00Z"
sha = "a" * 40
noise = "synthetic remote free text"
request = Path(sys.argv[0]).name
args = sys.argv[1:]
variables = {}
tracker = {"number": 123, "title": title}
author = "Other" if scenario == "conflict" else "Operator"
comments = [{"url": f"{url}#issuecomment-42", "author": {"login": author},
             "body": f"claiming: {task}\n{noise}", "createdAt": updated}]
if scenario not in {"claim", "conflict", "stale"}:
    comments = []


def page(nodes, count=None, cursor=None):
    return {"totalCount": len(nodes) if count is None else count, "nodes": nodes,
            "pageInfo": {"hasNextPage": cursor is not None, "endCursor": cursor}}


if request == "auth":
    assert args == ["status", "--active", "--hostname", "github.com"]
    result = noise
elif request == "event":
    kind = "com.github.pull_request.head.changed.v1"
    if args == ["subscription", "view", "existing", "--json", "id,resourceUrl,eventTypes,expiresAt"]:
        request = "subscription-view"
        result = {"id": "existing", "resourceUrl": f"https://github.com/{repo}/pull/7",
                  "eventTypes": [kind], "expiresAt": "2099-01-01T00:00:00Z"}
    else:
        assert args == ["listen", "--subscription", "existing", "--count", "1", "--timeout", "120s",
                        "--jq", ".type"]
        request = "subscription-listen"
        result = kind
elif request == "api":
    assert args[:2] == ["--hostname", "github.com"]
    if args[2:5] == ["graphql", "--input", "-"]:
        payload = json.load(sys.stdin)
        query, variables = payload["query"], payload["variables"]
        assert f'repository(owner:"ItsNotRexed",name:"Leonida")' in query
        if "issues(states:OPEN,first:100" in query:
            request = "open-trackers"
            if scenario == "ceiling":
                number = int(variables["cursor"] or 0) + 1
                inventory = page([{"number": number, "title": "Other"}], 100, str(number))
            elif scenario == "truncated":
                inventory = page([tracker], 2)
            else:
                inventory = page([tracker])
            result = {"issues": inventory}
        elif "issue(number:$n)" in query:
            assert variables["n"] == 123
            if "comments(first:100" in query:
                request = "tracker-comments"
                result = {"issue": {"comments": page(comments)}}
            else:
                issue = {"state": "OPEN", "title": title, "updatedAt": updated,
                         "comments": {"totalCount": len(comments)}}
                if " body" in query:
                    request = "tracker-body"
                    row = "" if scenario == "absent" else f"| {task} | {noise} |\n"
                    issue["body"] = (
                        f"## Up for grabs\n| ID | Task |\n| --- | --- |\n{row}\n"
                        "## Fork branches ahead of MAIN without a PR\nNone.\n"
                    ).replace("MAIN", chr(96) + "main" + chr(96))
                else:
                    request = "tracker-version"
                    if scenario == "stale":
                        issue["updatedAt"] = "2026-01-01T01:00:00Z"
                result = {"issue": issue}
        elif "pullRequests(states:OPEN" in query:
            if "first:30" in query:
                request = "open-prs"
                assert args[5] == "--jq" and "del(.body,.title)" in args[6]
            elif "headRefName" in query:
                request = "open-pr-branches"
            else:
                request = "open-pr-fingerprint"
            result = {"pullRequests": page([])}
        elif "pullRequest(number:$n)" in query:
            request = "selected-pr"
            assert variables["n"] == 7
            result = {"pullRequest": {
                "number": 7, "state": "OPEN", "headRefOid": sha, "isDraft": False,
                "reviewDecision": None, "reviews": {"totalCount": 0, "nodes": []},
                "reviewRequests": {"totalCount": 0}, "statusCheckRollup": {"state": "SUCCESS"},
            }}
        elif "defaultBranchRef" in query:
            request = "default-branch"
            result = {"defaultBranchRef": {"name": "main", "target": {"oid": sha}}}
        elif 'refs(refPrefix:"refs/heads/",first:100' in query:
            request = "repository-branches"
            result = {"refs": page([{"name": "main", "target": {"oid": sha}}])}
        else:
            raise AssertionError("unexpected GraphQL fields")
        result = {"data": {"repository": result}}
    else:
        assert args[2:4] == ["--method", "GET"]
        endpoint = args[4]
        if endpoint == "user":
            request = "user"
            assert args[5:] == ["--jq", "{login}"]
            result = {"login": "Operator"}
        else:
            request = "task-search"
            parts = urlsplit(endpoint)
            assert parts.path == "search/issues"
            assert parse_qs(parts.query) == {
                "q": [f'repo:{repo} is:pr is:open in:title,body "{task}"'],
                "per_page": ["100"], "page": ["1"],
            }
            assert args[5] == "--jq" and "number,match:" in args[6]
            result = {"total": 0, "incomplete": False, "items": []}
else:
    raise AssertionError("unexpected gh command")

with Path(os.environ["TEST_GH_TRACE"]).open("a", encoding="utf-8") as trace:
    trace.write(request + "\n")
if scenario == "rate" and request == "user":
    print("HTTP 429: " + noise, file=sys.stderr)
    sys.exit(1)
if isinstance(result, str):
    print(result)
else:
    print(json.dumps(result))
'''

# Stop the watch after its initial cycle, using its normal interrupt exit path.
WATCH_ONCE = """
import runpy
import sys
import time

def stop_after_snapshot(seconds):
    raise KeyboardInterrupt

time.sleep = stop_after_snapshot
entry = sys.argv.pop(1)
if entry == "-m":
    runpy.run_module(sys.argv.pop(1), run_name="__main__", alter_sys=True)
else:
    runpy.run_path(entry, run_name="__main__")
"""


class SyntheticGitHubTest(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.directory = Path(temp.name)
        self.trace = self.directory / "gh-trace"
        # Use a native executable so Windows passes GraphQL and jq arguments
        # directly, without batch-shell parsing.
        gh = self.directory / ("gh.exe" if os.name == "nt" else "gh")
        if os.name == "nt":
            # A Windows venv's python.exe is a redirector that needs its pyvenv.cfg.
            interpreter = getattr(sys, "_base_executable", sys.executable)
            shutil.copy2(interpreter, gh)
            for dll in Path(sys.base_prefix).glob("*.dll"):
                shutil.copy2(dll, self.directory / dll.name)
        else:
            gh.symlink_to(sys.executable)
        for name in ("api", "auth", "event"):
            (self.directory / name).write_text(FAKE_GH, encoding="utf-8")
        self.env = {name: os.environ[name] for name in ("SYSTEMROOT", "WINDIR")
                    if name in os.environ}
        self.env.update(
            PATH=str(self.directory), ProgramFiles=str(self.directory),
            LOCALAPPDATA=str(self.directory / "state"), XDG_STATE_HOME=str(self.directory / "state"),
            PYTHONHOME=sys.base_prefix, PYTHONPATH=str(TOOL_DIR), PYTHONDONTWRITEBYTECODE="1",
            TEST_GH_TRACE=str(self.trace),
        )

    def run_cli(self, entry, *args, scenario="clear", watch_once=False):
        self.trace.unlink(missing_ok=True)
        env = dict(self.env, TEST_GH_SCENARIO=scenario)
        prefix = ("-c", WATCH_ONCE) if watch_once else ()
        result = subprocess.run(
            [sys.executable, *prefix, *entry, *args],
            cwd=self.directory, env=env, capture_output=True, text=True, encoding="utf-8", timeout=35,
        )
        calls = self.trace.read_text(encoding="utf-8").splitlines() if self.trace.exists() else []
        self.assertNotIn(REMOTE_TEXT, result.stdout + result.stderr)
        return result, calls

    def snapshot(self, result, expected_code):
        self.assertEqual(result.returncode, expected_code, result.stdout + result.stderr)
        self.assertEqual(result.stderr, "")
        snapshot = json.loads(result.stdout)
        self.assertRegex(snapshot.pop("observed_at"), r"^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d\+00:00$")
        return snapshot


@unittest.skipUnless(os.name == "nt", "Windows venvs use a redirector executable")
class WindowsVirtualEnvironmentTests(unittest.TestCase):
    def test_fake_gh_handles_registration_from_a_venv(self):
        with tempfile.TemporaryDirectory() as directory:
            subprocess.run([sys.executable, "-B", "-m", "venv", "--without-pip", directory],
                           check=True, timeout=60)
            python = Path(directory) / "Scripts" / "python.exe"
            result = subprocess.run(
                [python, "-B", "-m", "unittest", "test_coordination_watch.WatchCompatibilityTests."
                 "test_register_fails_closed_when_the_tracker_changes"],
                cwd=TOOL_DIR / "tests", capture_output=True, text=True, timeout=60,
            )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


class GitHubCompatibilityTests(SyntheticGitHubTest):
    def test_check_clear_conflict_and_unknown_match_on_both_entry_points(self):
        cases = [
            ("clear", 0, "clear snapshot", [], []),
            ("conflict", 1, "conflict", ["active claim/status by other"], []),
            ("absent", 2, "unknown", [], ["task absent from up-for-grabs and no active exact new-item claim"]),
        ]
        for scenario, code, outcome, conflicts, unknown in cases:
            with self.subTest(scenario=scenario):
                observations = []
                for entry in ENTRY_POINTS:
                    result, calls = self.run_cli(entry, "check", "--task-id", "demo-123", scenario=scenario)
                    snapshot = self.snapshot(result, code)
                    self.assertEqual(snapshot["task"], TASK)
                    self.assertEqual(snapshot["tracker"], TRACKER)
                    self.assertEqual(snapshot["outcome"], outcome)
                    self.assertEqual(snapshot["conflicts"], conflicts)
                    self.assertEqual(snapshot["unknown"], unknown)
                    if scenario == "conflict":
                        self.assertEqual(snapshot["claims"]["other"]["claim"], CLAIM)
                    self.assertEqual(calls[:5], ["auth", "user", "open-trackers", "tracker-body", "tracker-comments"])
                    self.assertEqual(calls[-2:], ["open-trackers", "tracker-version"])
                    observations.append((snapshot, calls))
                self.assertEqual(*observations)

    def test_check_rejects_incomplete_inventory_and_stops_at_api_ceiling(self):
        cases = [
            ("truncated", 3, "incomplete paginated results"),
            ("ceiling", 96, "API ceiling of 96 reached before coverage was complete"),
            ("rate", 2, "GitHub rate limit or forbidden response"),
        ]
        for scenario, count, detail in cases:
            with self.subTest(scenario=scenario):
                result, calls = self.run_cli(LEGACY, "check", "--task-id", TASK, scenario=scenario)
                self.assertEqual(self.snapshot(result, 2), {
                    "outcome": "unknown", "tracker": None, "detail": detail,
                })
                self.assertEqual(len(calls), count)
