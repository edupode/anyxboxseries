"""Bounded, read-only GitHub CLI transport and metadata queries."""

from datetime import datetime
import json
import os
from pathlib import Path, PurePosixPath, PureWindowsPath
import re
import shutil
import subprocess
import time
from urllib.parse import urlencode


REPO = "ItsNotRexed/Leonida"
HOME_URL = f"https://github.com/{REPO}"
API = f"repos/{REPO}"
PR_LINK = re.compile(re.escape(HOME_URL) + r"/pull/([1-9][0-9]*)", re.I)
PAGE = "totalCount pageInfo {hasNextPage endCursor}"
CAP = 96
EVENTS = {f"com.github.pull_request.{kind}.v1" for kind in
          ("review.submitted", "head.changed", "state.changed", "check_suite.status.changed")}


class Unknown(Exception):
    pass


def need(condition, message):
    if not condition:
        raise Unknown(message)


def printable(value):
    return bool(value) and all(ch.isprintable() for ch in value)


def path_key(value):
    value = value.replace("\\", "/")
    path = PurePosixPath(value)
    need(printable(value) and not value.startswith("/") and not PureWindowsPath(str(path)).drive
         and ".." not in path.parts and bool(path.parts), "expected a repository-relative file path")
    return str(path)


def pr_number(url):
    match = PR_LINK.fullmatch(url)
    need(match, f"expected a {REPO} pull request URL")
    return int(match[1])


def pr_url(number):
    return f"{HOME_URL}/pull/{number}"


def as_sha(value):
    need(isinstance(value, str) and re.fullmatch("[0-9a-f]{40}", value), "missing commit identity")
    return value


def gh_path():
    executable = shutil.which("gh")
    if not executable and os.name == "nt":
        candidate = Path(os.environ.get("ProgramFiles", r"C:\Program Files")) / "GitHub CLI" / "gh.exe"
        if candidate.is_file():
            executable = str(candidate)
    need(executable, "GitHub CLI was not found")
    return executable


class GitHub:
    def __init__(self):
        self.exe = gh_path()
        self.env = dict(os.environ, GH_HOST="github.com", GH_PROMPT_DISABLED="1",
                        GH_NO_UPDATE_NOTIFIER="1", NO_COLOR="1")
        self.env.pop("GH_DEBUG", None)
        self.calls = 0
        self.tracker_url = None

    def run(self, args, *, data=None, timeout=40):
        # Do not relay auth output, error bodies, patches, or remote free text.
        try:
            result = subprocess.run([self.exe, *args], input=data, capture_output=True,
                                    encoding="utf-8", errors="strict", env=self.env, timeout=timeout)
        except (OSError, subprocess.TimeoutExpired, UnicodeError) as error:
            raise Unknown(f"gh unavailable ({type(error).__name__})") from error
        if result.returncode:
            detail = (result.stderr + result.stdout).lower()
            if re.search(r"\b(403|429)\b|rate.limit|forbidden", detail):
                raise Unknown("GitHub rate limit or forbidden response")
            raise Unknown(f"gh {args[0]} failed (exit {result.returncode})")
        return result.stdout

    def charge(self):
        need(self.calls < CAP, f"API ceiling of {CAP} reached before coverage was complete")
        self.calls += 1

    def api(self, endpoint, projection=None):
        need(endpoint == "user" or endpoint.startswith(API + "/"), "repository scope mismatch")
        self.charge()
        args = ["api", "--hostname", "github.com", "--method", "GET", endpoint]
        if projection:
            args += ["--jq", projection]
        return json.loads(self.run(args))

    def query(self, fields, declarations="", variables=None, projection=None):
        self.charge()
        query = (f"query{declarations} {{ repository(owner:\"ItsNotRexed\",name:\"Leonida\")"
                 f" {{ {fields} }} }}")
        payload = json.dumps({"query": query, "variables": variables or {}})
        args = ["api", "--hostname", "github.com", "graphql", "--input", "-"]
        if projection:
            args += ["--jq", projection]
        response = json.loads(self.run(args, data=payload))
        need(not response.get("errors"), "incomplete GraphQL response")
        repo = response["data"]["repository"]
        need(isinstance(repo, dict), "repository metadata unavailable")
        return repo

    def identity(self):
        self.charge()
        self.run(["auth", "status", "--active", "--hostname", "github.com"])
        login = self.api("user", "{login}")["login"]
        need(printable(login), "authenticated login unavailable")
        return login.casefold()

    def task_prs(self, task):
        # Search only this task and repository when monitoring. gh's projection
        # discards titles and bodies before returning verified candidate numbers.
        # Keep GitHub's search query bounded for long free-form claims. The
        # full task string is still checked locally by the shared predicate.
        term = " ".join(re.findall(r"[\w-]+", task))
        if len(term) > 150:
            term = term[:150].rsplit(" ", 1)[0]
        search = f'repo:{REPO} is:pr is:open in:title,body "{term}"'
        jq = ("{total:.total_count, incomplete:.incomplete_results, items:[.items[]|"
              f"{{url:.repository_url,number,match:({pr_text_match(task)})}}]}}")
        seen, matched, ambiguous = set(), set(), set()
        page, total = 1, None
        while True:
            self.charge()
            params = urlencode({"q": search, "per_page": 100, "page": page})
            envelope = json.loads(self.run(["api", "--hostname", "github.com", "--method", "GET",
                                            f"search/issues?{params}", "--jq", jq]))
            count = envelope["total"]
            need(envelope["incomplete"] is False and type(count) is int and 0 <= count <= 1000
                 and (total is None or count == total), "incomplete task PR search")
            total = count
            for row in envelope["items"]:
                number = row["number"]
                need(row["url"].casefold() == f"https://api.github.com/{API}".casefold()
                     and number not in seen, "inconsistent task PR search")
                seen.add(number)
                if row["match"] == "implementation":
                    matched.add(number)
                elif row["match"] == "ambiguous":
                    ambiguous.add(number)
                else:
                    need(row["match"] == "unrelated", "invalid task association from search")
            if len(seen) == total:
                break
            need(len(envelope["items"]) == 100 and len(seen) < total, "truncated task PR search")
            page += 1
        # GitHub's head: qualifier searches branch prefixes, so it cannot
        # discover e.g. fix/tools-TEST-123-diagnostics by the embedded ID.
        # Enumerate identities only. Fetch review/check/head state for matches.
        fields = f"pullRequests(states:OPEN,first:100,after:$cursor) {{{PAGE} nodes {{number headRefName}}}}"
        heads = pages(lambda cursor: self.query(fields, "($cursor:String)", {"cursor": cursor})["pullRequests"],
                      lambda pr: pr["number"])
        matched.update(pr["number"] for pr in heads if branch_matches(task, pr["headRefName"]))
        return matched, ambiguous - matched


def pages(fetch, key):
    """Check cursors, node identity, and totalCount. Never accept a truncated connection."""
    cursor = None
    cursors = set()
    identities = set()
    output = []
    total = None
    while True:
        connection = fetch(cursor)
        count = connection["totalCount"]
        nodes = connection["nodes"]
        need(type(count) is int and count >= 0 and isinstance(nodes, list), "invalid pagination data")
        need(total is None or count == total, "collection changed during pagination")
        total = count
        for node in nodes:
            identity = key(node)
            need(identity is not None and identity not in identities, "missing or repeated result in pagination")
            identities.add(identity)
            output.append(node)
        info = connection["pageInfo"]
        need(type(info["hasNextPage"]) is bool, "missing pagination status")
        if not info["hasNextPage"]:
            need(len(output) == total, "incomplete paginated results")
            return output
        cursor = info["endCursor"]
        need(nodes and cursor and cursor not in cursors and len(output) < total, "invalid pagination cursor")
        cursors.add(cursor)


PR_STATE = """
number state headRefOid baseRefOid headRefName headRepository {nameWithOwner}
author {login} isDraft reviewDecision reviewRequests {totalCount}
reviews(last:1) {totalCount nodes {state submittedAt}}
statusCheckRollup {state}
"""


def task_pattern(task):
    return r"(?i)(?:^|[^a-z0-9_-])" + re.escape(task) + r"(?:$|[^a-z0-9_-])"


def branch_matches(task, name):
    return re.search(r"(?<![a-z0-9])" + re.escape(task) + r"(?![a-z0-9])", name, re.I) is not None


def pr_text_match(task):
    """Classify PR metadata inside gh. Only the classification leaves the CLI."""
    label = (r"(?i)^\s*(?:[-*+]\s+|#{1,6}\s+)?(?:task(?: id)?|tracker (?:task|item)|claiming|done|"
             r"(?:(?:this|the) (?:pr|pull request|change|patch)\s+)?(?:implements|completes|addresses|fixes|closes))"
             r"[: \t]+")
    operand = re.escape(task)
    relation = (r"(?i)\b(?:separate from|depends on|dependency (?:of|on)|dependenc(?:y|ies)\s*:|follow[- ]up to|related to)"
                r"(?:\s+(?:the\s+)?(?:task|pr|issue|pull request))?\s*[:#]?\s*"
                + operand + r"(?:$|[^a-z0-9_-])")
    # Supply the regexes as JSON strings, including for free-form task names.
    predicates = (
        f'def mentioned: test({json.dumps(task_pattern(task))}); '
        f'def owns: test({json.dumps(label + operand + r"(?:$|[^a-z0-9_-])")}); '
        f'def label_only: test({json.dumps(label + "$")}); '
        f'def exact_operand: test({json.dumps("(?i)^" + operand + "$")}); '
        f'def remove_dependency: gsub({json.dumps(relation)}; " "); '
    )
    # Collect the body by Markdown block before pairing backtick runs. A lazy
    # continuation belongs to a quote only while its last paragraph is open.
    return predicates + r'''
    def unbold: gsub("\\*\\*|__"; "");
    def prose:
      [scan("`+|[^`]+")] as $tokens |
      reduce range(0; $tokens|length) as $i ({skip:-1, text:""};
        if $i <= .skip then .
        elif ($tokens[$i] | startswith("`")) then
          (first(range($i+1; $tokens|length) | select($tokens[.] == $tokens[$i])) // null) as $end |
          if $end == null then .text += $tokens[$i]
          else
            ($tokens[$i+1:$end] | join("") | gsub("\n"; " ") |
             if startswith(" ") and endswith(" ") then .[1:-1] else . end) as $value |
            .skip = $end |
            if (.text | split("\n") | (last // "") | unbold | label_only) and ($value | exact_operand)
            then .text += $value else .text += "\u0000" end
          end
        else .text += $tokens[$i] end) | .text;

    def blank: test("^[ \\t]*$");
    def heading: test("^ {0,3}#{1,6}([ \\t]|$)");
    def thematic: test("^ {0,3}((-[ \\t]*){3,}|(\\*[ \\t]*){3,}|(_[ \\t]*){3,})$");
    def underline: test("^ {0,3}(=+|-+)[ \\t]*$");
    def list_start: test("^ {0,3}([-*+]|1[.)])[ \\t]+[^ \\t]");
    def list_marker: test("^ {0,3}([-*+]|[0-9]{1,9}[.)])([ \\t]|$)");
    def fence_mark:
      ((capture("^ {0,3}(?<mark>`{3,})[^`]*$") //
        capture("^ {0,3}(?<mark>~{3,})") // {}).mark // "");
    def closes($f):
      (fence_mark | startswith($f)) and test("^ {0,3}(`{3,}|~{3,})[ \\t]*$");

    # List indentation and nested markers can change which line closes a fence.
    # Flag these bodies before the paragraph parser can consume a real task or
    # expose an example. Only associations involving the selected task defer.
    def note_list_fence($raw):
      ($raw | sub("^(?:[ \\t]*>[ \\t]?)*"; "")) as $outer |
      ($outer | test("^[ \\t]*(?:[-*+]|[0-9]{1,9}[.)])(?:[ \\t]|$)")) as $item |
      if $item then .in_list = true else . end |
      if .in_list and ($item or ($outer | test("^[ \\t]")) or ($raw | test("^[ \\t]"))) and
         (($raw | sub("^(?:[ \\t]*(?:[-*+]|[0-9]{1,9}[.)])[ \\t]+|[ \\t]*>[ \\t]?)*[ \\t]*"; "") |
           fence_mark) != "")
      then .list_fence = true
      elif ($item | not) and ($raw | test("^[^ \\t>]")) and
           ((.paragraph|length) == 0 or ($raw | heading or thematic or (fence_mark != "")))
      then .in_list = false
      else . end;

    def flush:
      if (.paragraph|length) == 0 then .
      else (.paragraph|join("\n")) as $block | .blocks += [$block] | .paragraph = [] end;
    def blocks:
      gsub("\r\n?"; "\n") | split("\n") |
      reduce .[] as $raw ({blocks:[], paragraph:[], lazy:false, fence:"", depth:0,
                           in_list:false, list_fence:false};
        ($raw | capture("^(?<prefix>(?: {0,3}>[ \\t]?| {0,3}(?:[-*+]|[0-9]{1,9}[.)])(?:[ \\t]+|$))*)(?<line>.*)$")) as $q |
        ([$q.prefix | scan(">")] | length) as $depth |
        (if $depth > 0 then $q.line else $raw end) as $line |
        .fence as $f |
        if $f != "" and $depth >= .depth then
          if $depth == .depth and ($raw | sub("^(?: {0,3}>[ \\t]?)*"; "") | closes($f))
          then .fence = "" else . end
        else
          .fence = "" | note_list_fence($raw) |
          if $depth > 0 then
            flush |
            ($line | fence_mark) as $mark |
            if $mark != "" then .fence = $mark | .depth = $depth | .lazy = false
            elif ($line | blank or heading or thematic) or (.lazy and ($line | underline))
            then .lazy = false
            elif ($line | test("^(    |\\t)")) then .
            else .lazy = true end
          elif ($line | blank) then flush | .lazy = false
          elif ($line | fence_mark) != "" then
            flush | .fence = ($line | fence_mark) | .depth = 0 | .lazy = false
          elif ($line | heading) then flush | .blocks += [$line] | .lazy = false
          elif ($line | thematic) then flush | .lazy = false
          elif ($line | list_start) or
               ((.paragraph[0] // "" | list_marker) and ($line | list_marker))
          then flush | .paragraph = [$line] | .lazy = false
          elif .lazy then .
          elif (.paragraph|length) > 0 and ($line | underline) then flush
          elif (.paragraph|length) == 0 and ($line | test("^(    |\\t)")) then .
          else .paragraph += [$line] end
        end) | flush | {blocks, list_fence};

    def visible_lines: prose | split("\n")[] | unbold | remove_dependency;
    (.body // "") as $body |
    ($body | blocks) as $markup |
    ([.title | visible_lines]) as $title |
    ([$markup.blocks[] | visible_lines]) as $lines |
    if ($title | any(owns)) then "implementation"
    elif $markup.list_fence and
         (($lines | any(mentioned)) or ($body | mentioned) or
          ($body | unbold | gsub("\\s+"; " ") | mentioned)) then "ambiguous"
    elif ($lines | any(owns)) then "implementation"
    elif (($title + $lines) | any(mentioned)) then "ambiguous" else "unrelated" end
    '''


def file_fields(size, cursor=""):
    return f"files(first:{size}{cursor}) {{{PAGE} nodes {{path changeType}}}}"


def open_prs(gh, task, paths):
    files = f"changedFiles {file_fields(30)}" if paths else ""
    fields = (f"pullRequests(states:OPEN,first:30,after:$cursor) {{{PAGE} "
              f"nodes {{{PR_STATE} title body {files}}}}}")
    # PR descriptions may include unrelated output. Keep only their classification.
    jq = (".data.repository.pullRequests.nodes |= map(. + {matches: "
          f"({pr_text_match(task)})}} | del(.body,.title))")
    return pages(lambda cursor: gh.query(fields, "($cursor:String)", {"cursor": cursor}, jq)["pullRequests"],
                 lambda pr: pr["number"])


def selected_pr(gh, number):
    pr = gh.query(f"pullRequest(number:$n) {{{PR_STATE}}}", "($n:Int!)", {"n": number})["pullRequest"]
    need(pr and pr["number"] == number, f"unavailable PR #{number}")
    return pr


def pr_state(pr):
    review = pr["reviews"]
    return {"pr": pr_url(pr["number"]), "state": pr["state"], "head": as_sha(pr["headRefOid"]),
            "draft": pr["isDraft"], "review": pr["reviewDecision"], "reviews": review["totalCount"],
            "last_review": review["nodes"], "requested": pr["reviewRequests"]["totalCount"],
            "checks": (pr["statusCheckRollup"] or {}).get("state")}


def changed_paths(gh, base, head, paths):
    """Compare file identity and mode at the merge base and head; never request blob data.

    GitHub includes patches only on the *first* REST comparison page. A later page
    supplies the merge base. Commit.file then returns tree-entry metadata only.
    """
    base, head = as_sha(base), as_sha(head)
    meta = gh.api(f"{API}/compare/{base}...{head}?page=2&per_page=1",
                  "{base:.base_commit.sha, merge:.merge_base_commit.sha, files:(.files // [] | length)}")
    need(meta["base"] == base and meta["files"] == 0, "comparison metadata incomplete")
    merge = as_sha(meta["merge"])
    if merge == head:
        return set()
    ordered = sorted(paths)
    entries = " ".join(f"p{i}:file(path:{json.dumps(path, ensure_ascii=False)}) {{oid mode}}"
                       for i, path in enumerate(ordered))
    fields = (f"base:object(oid:$base) {{... on Commit {{oid {entries}}}}} "
              f"head:object(oid:$head) {{... on Commit {{oid {entries}}}}}")
    trees = gh.query(fields, "($base:GitObjectID!,$head:GitObjectID!)", {"base": merge, "head": head})
    need(trees["base"] and trees["head"] and trees["base"]["oid"] == merge and trees["head"]["oid"] == head,
         "unavailable commit tree metadata")
    return {path for i, path in enumerate(ordered) if trees["base"][f"p{i}"] != trees["head"][f"p{i}"]}


def pr_overlap(gh, pr, paths):
    if not paths:
        return set()
    fields = f"pullRequest(number:$n) {{headRefOid baseRefOid {file_fields(100, ',after:$cursor')}}}"
    def fetch(cursor):
        if cursor is None:
            return pr["files"]
        part = gh.query(fields, "($n:Int!,$cursor:String)", {"n": pr["number"], "cursor": cursor})["pullRequest"]
        need(part["headRefOid"] == pr["headRefOid"] and part["baseRefOid"] == pr["baseRefOid"],
             f"PR #{pr['number']} changed during file pagination")
        return part["files"]
    files = pages(fetch, lambda item: path_key(item["path"]))
    need(len(files) == pr["changedFiles"], f"incomplete changed-file inventory for PR #{pr['number']}")
    overlap = paths.intersection(path_key(item["path"]) for item in files)
    # GitHub's GraphQL changed-file entry has no previous filename. Check the
    # requested paths directly when a rename could have hidden its old side.
    if paths - overlap and any(item["changeType"] == "RENAMED" for item in files):
        overlap |= changed_paths(gh, pr["baseRefOid"], pr["headRefOid"], paths - overlap)
    return overlap


def repo_branches(gh):
    default = gh.query("defaultBranchRef {name target {oid}}")["defaultBranchRef"]
    fields = f'refs(refPrefix:"refs/heads/",first:100,after:$cursor) {{{PAGE} nodes {{name target {{oid}}}}}}'
    rows = pages(lambda cursor: gh.query(fields, "($cursor:String)", {"cursor": cursor})["refs"],
                 lambda row: row["name"])
    tips = {row["name"]: as_sha(row["target"]["oid"]) for row in rows}
    need(default and tips.get(default["name"]) == default["target"]["oid"], "default branch changed")
    del tips[default["name"]]
    return default, tips


def fork_tips(gh, base, labels):
    heads = {}
    labels = sorted(labels)
    for start in range(0, len(labels), 20):
        batch = labels[start:start + 20]
        fields = " ".join(f"f{i}:compare(headRef:$r{i}) {{baseTarget {{oid}} headTarget {{oid}}}}"
                          for i in range(len(batch)))
        declarations = "(" + ",".join(f"$r{i}:String!" for i in range(len(batch))) + ")"
        values = {f"r{i}": label for i, label in enumerate(batch)}
        comparisons = gh.query(f"defaultBranchRef {{{fields}}}", declarations, values)["defaultBranchRef"]
        for i, label in enumerate(batch):
            pair = comparisons[f"f{i}"]
            need(pair and pair["baseTarget"]["oid"] == base, f"unavailable base/head for {label}")
            heads[label] = as_sha(pair["headTarget"]["oid"])
    return heads


def existing_subscription(gh, subscription, number):
    gh.charge()
    fields = "id,resourceUrl,eventTypes,expiresAt"
    value = json.loads(gh.run(["event", "subscription", "view", subscription, "--json", fields]))
    resources = {pr_url(number).casefold(), f"https://api.github.com/{API}/pulls/{number}".casefold()}
    need(value["id"] == subscription and value["resourceUrl"].casefold() in resources
         and value["eventTypes"] and set(value["eventTypes"]) <= EVENTS
         and datetime.fromisoformat(value["expiresAt"].replace("Z", "+00:00")).timestamp() > time.time(),
         "subscription must be active and tied to the selected PR and supported event types")


def listen_event(gh, subscription, interval):
    # --resource can create a subscription. Only attach using an existing ID.
    # The CLI exits 8 on timeout.
    gh.charge()
    return gh.run(["event", "listen", "--subscription", subscription, "--count", "1",
                   "--timeout", f"{interval}s", "--jq", ".type"], timeout=interval + 15)
