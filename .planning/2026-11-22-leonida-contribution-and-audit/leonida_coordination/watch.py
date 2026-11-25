"""Tracker snapshots and polling for the local coordination CLI."""

from datetime import datetime, timezone
import json
import re
import sqlite3
import time

from .github import (
    EVENTS, HOME_URL, PAGE, PR_LINK, REPO, GitHub, Unknown, as_sha, branch_matches, changed_paths,
    existing_subscription, fork_tips, listen_event, need, open_prs, pages, pr_number, pr_overlap,
    pr_state, pr_url, printable, repo_branches, selected_pr, task_pattern,
)


TRACKER = re.compile(r"^Coordinating who works on what \([0-9]+\)$")
CLAIM_LINK = re.compile(re.escape(HOME_URL) + r"/issues/([1-9][0-9]*)#issuecomment-([1-9][0-9]*)", re.I)
TASK_ID = re.compile(r"[a-z0-9]+(?:-[a-z0-9]+)+", re.I | re.ASCII)
NEW_ITEM = re.compile(r"new(?:[ -]+([a-z][a-z0-9]*))?[ -]+item(?=$|[\s:—–-])", re.I | re.ASCII)
ID_TOKEN = re.compile(r"^(?:`(" + TASK_ID.pattern + r")`|(" + TASK_ID.pattern + r"))(?=$|[\s.,:;(—–])",
                      re.I | re.ASCII)
COMMAND = re.compile(r"^(claiming|status|releasing|done):[ \t]+(.+)$", re.I)
ERRORS = (Unknown, OSError, sqlite3.Error, ValueError, KeyError, TypeError, IndexError, AttributeError)


def stamp(seconds=None):
    return datetime.fromtimestamp(time.time() if seconds is None else seconds,
                                  timezone.utc).isoformat(timespec="seconds")


def emit(**fields):
    print(json.dumps({"observed_at": stamp(), **fields}, ensure_ascii=True), flush=True)


def error_text(error):
    return str(error) if isinstance(error, Unknown) else f"incomplete data or local error ({type(error).__name__})"


def new_item(text):
    """Return the canonical key and any text after an explicit project ID."""
    folded = " ".join(text.casefold().split())
    marker = NEW_ITEM.match(folded)
    if not marker:
        return None
    description = folded[marker.end():].lstrip(" :—–-")
    if not description:
        return None
    label = re.match(r"^id\s*:\s*", description)
    description_after_label = description[label.end():] if label else description
    named = ID_TOKEN.match(description_after_label)
    if named:
        token = named[1] or named[2]
        # A ticket is explicit when it has an ID: label or a matching declared
        # type, as in "new TEST item TEST-123". Bare UTF-8 is ordinary prose.
        declared_type = marker[1]
        if label or (declared_type and re.fullmatch(re.escape(declared_type) + r"-[0-9]+", token)):
            return token.upper(), description_after_label[named.end():]
    prefix = f"new {marker[1]} item" if marker[1] else "new item"
    return f"{prefix} {description}", ""


def task_key(value):
    need(printable(value) and value == value.strip() and len(value) <= 512,
         "invalid task ID or exact new-item description")
    if TASK_ID.fullmatch(value):
        return value.upper()
    parsed = new_item(value)
    need(parsed, "use a task ID or the exact 'new [TYPE] item …' claim description")
    return parsed[0]


def tracker_number(gh):
    fields = f"issues(states:OPEN,first:100,after:$cursor) {{{PAGE} nodes {{number title}}}}"
    rows = pages(lambda cursor: gh.query(fields, "($cursor:String)", {"cursor": cursor})["issues"],
                 lambda row: row["number"])
    matches = [row["number"] for row in rows if TRACKER.fullmatch(row["title"])]
    need(len(matches) == 1, f"found {len(matches)} open coordination trackers")
    gh.tracker_url = f"{HOME_URL}/issues/{matches[0]}"
    return matches[0]


def tracker_version(gh, number, body=False):
    field = "body" if body else ""
    row = gh.query(f"issue(number:$n) {{state title updatedAt comments {{totalCount}} {field}}}",
                   "($n:Int!)", {"n": number})["issue"]
    need(row and row["state"] == "OPEN" and TRACKER.fullmatch(row["title"]), "tracker changed")
    return row["updatedAt"], row["comments"]["totalCount"], row.get("body")


def sections(markdown):
    headings = list(re.finditer(r"^#{1,6} +(.+?)\s*$", markdown, re.M))
    return [(match[1].strip(), markdown[match.end():headings[i + 1].start() if i + 1 < len(headings) else None])
            for i, match in enumerate(headings)]


def grabs(section_list, task):
    blocks = [block for heading, block in section_list if heading.casefold() == "up for grabs"]
    need(len(blocks) == 1, "missing or ambiguous up-for-grabs section")
    rows = [line.split("|") for line in blocks[0].splitlines() if line.startswith("|")]
    need(len(rows) >= 2 and rows[0][1].strip().casefold() == "id"
         and re.fullmatch("[- :]+", rows[1][1]), "unrecognized up-for-grabs table")
    found = []
    for row in rows[2:]:
        cell = row[1].strip().strip("⭐️ ").strip("` ")
        need(TASK_ID.fullmatch(cell), "unrecognized task ID in up-for-grabs table")
        if cell.upper() == task:
            found.append("|".join(row))
    need(len(found) <= 1, "repeated task in up-for-grabs table")
    return found[0] if found else None


def command_lines(body):
    # Ignore quoted examples, indented code and fenced blocks.
    fence = ""
    for line in body.splitlines():
        delimiter = re.match(r"^ {0,3}(`{3,}|~{3,})", line)
        if fence:
            if delimiter and delimiter[1].startswith(fence) and not line[delimiter.end():].strip(" \t"):
                fence = ""
            continue
        if delimiter:
            fence = delimiter[1]
            continue
        match = COMMAND.match(line)
        if match:
            yield match[1].lower(), match[2].strip()


def command_target(text, task, action, prior_names):
    folded = " ".join(text.casefold().split())
    parsed = new_item(text)
    if parsed:
        key, tail = parsed
        if action == "claiming" or TASK_ID.fullmatch(key) or key in prior_names:
            return (True, tail) if key == task else None
        if not TASK_ID.fullmatch(task):
            candidates = {}
            for name in prior_names:
                if not key.startswith(name):
                    continue
                suffix = key[len(name):]
                note = action in {"status", "releasing"} and re.match(r"^(?: [—–-] |: )\S", suffix)
                completed = action == "done" and suffix.startswith(" ") and implementation_pr(suffix)
                if note or completed:
                    candidates[name] = suffix
            need(len(candidates) <= 1 or (task not in candidates and task != key),
                 "tracker command could refer to multiple free-form claims")
            if task in candidates:
                return False, candidates[task]
    else:
        match = ID_TOKEN.match(folded)
        if match and (match[1] or match[2]).upper() == task:
            tail = folded[match.end():]
            marker = tail.lstrip(" :(—–-").partition(")")[0]
            return bool(NEW_ITEM.match(marker)), tail
    return None


def implementation_pr(tail):
    """The public done command is 'done: TASK #PR'. Only consume its PR operand."""
    operand = tail.lstrip(" \t:—–-")
    operand = re.sub(r"^pr\b[\s:]+", "", operand, flags=re.I)
    link = re.match(r"^\[[^\]]*\]\(([^)]+)\)", operand)
    if link:
        match = PR_LINK.fullmatch(link[1])
    else:
        match = re.match(rf"^(?:{re.escape(HOME_URL)}/pull/|#)([1-9][0-9]*)(?=$|[\s,.);])",
                         operand, re.I)
    return int(match[1]) if match else None


def claims_for(gh, number, task, expected_count):
    fields = (f"issue(number:$n) {{comments(first:100,after:$cursor) {{{PAGE} "
              "nodes {url author {login} body createdAt}}}")
    def fetch(cursor):
        return gh.query(fields, "($n:Int!,$cursor:String)", {"n": number, "cursor": cursor})["issue"]["comments"]
    comments = pages(fetch, lambda row: row["url"])
    need(len(comments) == expected_count, "tracker comment count changed")
    active, finished, seen_prs = {}, {}, set()
    prior_names = {}
    for row in sorted(comments, key=lambda item: (item["createdAt"], int(item["url"].rsplit("-", 1)[1]))):
        link = CLAIM_LINK.fullmatch(row["url"])
        need(link and int(link[1]) == number, "tracker comment origin mismatch")
        for action, text in command_lines(row["body"]):
            author = (row["author"] or {}).get("login")
            who = author.casefold() if author else None
            names = prior_names.setdefault(who, set())
            if action == "claiming":
                parsed = new_item(text)
                if parsed and not TASK_ID.fullmatch(parsed[0]):
                    names.add(parsed[0])
            target = command_target(text, task, action, names)
            if target is None:
                continue
            is_new, tail = target
            need(who, "matching tracker command has an unknown author")
            if action == "done":
                number_ref = implementation_pr(tail)
                if number_ref:
                    seen_prs.add(number_ref)
            if action == "releasing":
                active.pop(who, None)
            elif action == "done":
                active.pop(who, None)
                finished[who] = row["url"]
            elif action == "claiming":
                active[who] = {"claim": row["url"], "new_item": is_new, "status": None}
            else:
                active.setdefault(who, {"claim": None, "new_item": False, "status": None})["status"] = row["url"]
    return active, finished, seen_prs


def indexed_branches(section_list):
    found = [block for title, block in section_list if re.fullmatch(
        r"Fork branches ahead of `[^`]+` without a PR", title, re.I)]
    need(len(found) == 1 and found[0].strip(), "missing, empty or ambiguous tracker branch index")
    branches, omissions = {}, []
    for line in found[0].splitlines():
        if not line.strip():
            continue
        match = re.fullmatch(r"- `([A-Za-z0-9-]+):([^`]+)`(?:\s.*)?", line)
        if match:
            key = (match[1].casefold(), match[2])
            need(printable(match[2]) and key not in branches, "invalid branch index entry")
            branches[key] = line
        elif re.fullmatch(r"(?:none\.?|no (?:such )?branches\.?)", line.strip(), re.I):
            continue
        else:
            omissions.append("tracker branch index omits entries or has an unrecognized row")
    return branches, list(dict.fromkeys(omissions))


def audit_branches(gh, sections_, task, paths, linked, conflicts, unknown):
    indexed, omissions = indexed_branches(sections_)
    unknown.extend(omissions)
    default, repo_heads = repo_branches(gh)
    base = as_sha(default["target"]["oid"])
    upstream = REPO.split("/")[0].casefold()
    for name in repo_heads:
        indexed.setdefault((upstream, name), "")
    labels = [f"{owner}:{name}" for owner, name in indexed
              if paths and owner != upstream and (owner, name) not in linked]
    forks = fork_tips(gh, base, labels)
    for (owner, name), description in indexed.items():
        if (owner, name) in linked:
            continue
        label = f"{owner}:{name}"
        # A slash, dash or underscore commonly separates a task ID from a branch slug.
        if branch_matches(task, name) or re.search(task_pattern(task), description):
            conflicts.append(f"same-task branch without an open PR: {label}")
        if not paths:
            continue
        head = repo_heads.get(name) if owner == upstream else forks[label]
        need(head, f"unavailable branch tip for {label}")
        overlap = changed_paths(gh, base, head, paths)
        if overlap:
            conflicts.append(f"branch {label} changes {', '.join(sorted(overlap))}")
    return default, repo_heads, forks


def verify_branch_tips(gh, fingerprint):
    default, repo_heads, forks = fingerprint
    need(repo_branches(gh) == (default, repo_heads), "repository branches or default branch changed; refresh required")
    need(fork_tips(gh, default["target"]["oid"], forks) == forks, "fork branch tips changed; refresh required")


def tracker_prs(section_list, task):
    """Look up explicit PR-column entries for the already-selected task."""
    result = set()
    for _, block in section_list:
        columns = []
        for line in block.splitlines():
            if not line.startswith("|"):
                columns = []
                continue
            cells = [cell.strip() for cell in line.strip().strip("|").split("|")]
            if cells[0].casefold() == "id":
                columns = [cell.casefold() for cell in cells]
            elif cells[0].strip("` ").upper() == task and "pr" in columns:
                index = columns.index("pr")
                need(index < len(cells), "malformed tracker PR row")
                value = cells[index]
                # The destination owns the identity of a Markdown link; its
                # visible text can be "implementation", a number or anything else.
                def from_link(match):
                    return f" #{pr_number(match[1])} "
                value = re.sub(r"\[[^\]]*\]\(<?([^\s)>]+)>?\)", from_link, value)
                value = PR_LINK.sub(lambda match: f" #{match[1]} ", value)
                for token in re.split(r"[\s,;<>]+", value.strip()):
                    if token.casefold() in {"", "pr", "prs", "—", "–", "-", "none"}:
                        continue
                    reference = re.fullmatch(r"#([1-9][0-9]*)", token)
                    need(reference, "ambiguous reference in the task's tracker PR column")
                    result.add(int(reference[1]))
    return result


def remote(gh, args, monitor=False):
    login = gh.identity()
    number = tracker_number(gh)
    version, count, body = tracker_version(gh, number, body=True)
    parts = sections(body)
    row = grabs(parts, args.task_id)
    active, finished, refs = claims_for(gh, number, args.task_id, count)
    refs |= tracker_prs(parts, args.task_id)
    mine = active.get(login, {})
    conflicts = [f"active claim/status by {who}" for who in active if who != login]
    conflicts += [f"done reported by {who}" for who in finished]
    unknown = []
    if not monitor and row is None and not any(record["claim"] and record["new_item"] for record in active.values()):
        unknown.append("task absent from up-for-grabs and no active exact new-item claim")
    if not monitor and row and re.search(r"first refusal|first dibs|first right|@[\w-]+[^|]*\bfirst\b", row, re.I):
        unknown.append("up-for-grabs row includes priority or first-refusal terms; inspect the tracker")
    if getattr(args, "claim_url", None) and args.claim_url != mine.get("claim"):
        unknown.append("claim URL does not identify your active exact claim on the current tracker")
    pr_states, known_overlaps = {}, {}
    snapshot = {"tracker": gh.tracker_url, "listed_up_for_grabs": bool(row), "claims": active,
                "done": finished, "claim_state": "claimed by you" if mine.get("claim") else "no active claim by you",
                "prs": pr_states, "pr_overlaps": known_overlaps, "conflicts": conflicts, "unknown": unknown}
    chosen = set(args.pr)
    if monitor:
        matched, ambiguous = gh.task_prs(args.task_id)
        for n in sorted(ambiguous - refs):
            unknown.append(f"PR mentions the task without identifying its implementation: {pr_url(n)}")
        for n in sorted(chosen | refs | matched):
            pr_states[n] = pr_state(selected_pr(gh, n))
    else:
        prs = open_prs(gh, args.task_id, args.paths)
        linked = set()
        for pr in prs:
            n = pr["number"]
            need(pr["state"] == "OPEN", "open PR inventory changed")
            fork = pr["headRepository"]
            need(fork and fork["nameWithOwner"], f"PR #{n} head repository unavailable")
            linked.add((fork["nameWithOwner"].split("/")[0].casefold(), pr["headRefName"]))
            association = pr["matches"]
            need(association in {"implementation", "ambiguous", "unrelated"}, "invalid PR task association")
            match = association == "implementation" or branch_matches(args.task_id, pr["headRefName"]) or n in refs
            if not match and association == "ambiguous":
                unknown.append(f"PR mentions the task without identifying its implementation: {pr_url(n)}")
            if match or n in chosen:
                pr_states[n] = pr_state(pr)
            if match:
                author = (pr["author"] or {}).get("login")
                need(author, f"PR #{n} author unavailable")
                if author.casefold() != login:
                    conflicts.append(f"another user's matching open PR: {pr_url(n)}")
            overlap = pr_overlap(gh, pr, args.paths)
            if overlap:
                known_overlaps[pr_url(n)] = sorted(overlap)
        for n in sorted((chosen | refs) - set(pr_states)):
            pr = selected_pr(gh, n)
            pr_states[n] = pr_state(pr)
            need(pr["state"] != "OPEN", "open PR inventory changed")
            if n in refs and pr["state"] == "MERGED":
                conflicts.append(f"matching tracker PR is merged: {pr_url(n)}")
        try:
            branch_tips = audit_branches(gh, parts, args.task_id, args.paths, linked, conflicts, unknown)
        except ERRORS as error:
            # These are partial observations. Surface known PR links/claims
            # together with unknown; never register or keep calling a rejected API.
            unknown.append(error_text(error))
            return snapshot
        # A push/rebase during a paginated inventory invalidates file-overlap evidence.
        tips = f"pullRequests(states:OPEN,first:100,after:$cursor) {{{PAGE} nodes {{number headRefOid baseRefOid}}}}"
        ends = pages(lambda cursor: gh.query(tips, "($cursor:String)", {"cursor": cursor})["pullRequests"],
                     lambda pr: pr["number"])
        fingerprint = lambda rows: {(p["number"], p["headRefOid"], p["baseRefOid"]) for p in rows}
        need(fingerprint(ends) == fingerprint(prs), "open PRs or their heads/bases changed; refresh required")
        if not unknown and not conflicts:
            verify_branch_tips(gh, branch_tips)
    need(tracker_number(gh) == number and tracker_version(gh, number)[:2] == (version, count),
         "tracker rolled over or changed during observation")
    return snapshot


def watch(args, read_matching_leases):
    last_good, last_error = None, None
    failures = 0
    subscription = args.event_subscription
    event_error = None
    while True:
        gh = None
        try:
            gh = GitHub()
            snapshot = remote(gh, args, monitor=True)
            need(not snapshot["unknown"], "; ".join(snapshot["unknown"]))
            leases = read_matching_leases(args)
            current = {"tracker": snapshot["tracker"], "claims": snapshot["claims"], "done": snapshot["done"],
                       "listed_up_for_grabs": snapshot["listed_up_for_grabs"],
                       "claim_state": snapshot["claim_state"], "prs": snapshot["prs"], "leases": leases}
            changed = {k: v for k, v in current.items() if last_good is None or v != last_good[k]}
            if changed or last_error:
                emit(task=args.task_id, outcome="monitor snapshot", **changed)
            last_good, last_error, failures = current, None, 0
        except ERRORS as error:
            message = error_text(error)
            if message != last_error:
                emit(task=args.task_id, tracker=gh.tracker_url if gh else None, outcome="unknown",
                     detail=message, retained_last_snapshot=last_good is not None)
            last_error, failures = message, failures + 1
        if failures:
            time.sleep(max(args.interval, min(3600, args.interval * 2 ** min(failures, 10))))
            continue
        started = time.monotonic()
        wait = args.interval
        if subscription:
            try:
                existing_subscription(gh, subscription, args.pr[0])
                event = listen_event(gh, subscription, args.interval)
                if event.strip() in EVENTS:
                    wait = 60
                event_error = None
            except ERRORS as error:
                message = error_text(error)
                if message != "gh event failed (exit 8)" and message != event_error:
                    emit(outcome="event listener unavailable", detail=message, polling=True)
                    event_error = message
                # Polling survives expired subscriptions and disconnected listeners.
                if message != "gh event failed (exit 8)":
                    subscription = None
        time.sleep(max(0, wait - (time.monotonic() - started)))
