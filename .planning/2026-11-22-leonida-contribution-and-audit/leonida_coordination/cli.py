"""Read-only Leonida preflight and same-machine leases. Requires Python 3.10+ and gh.

Exit codes: 0 clear or successful local operation, 1 conflict, 2 unknown/error.
A watch is a narrower monitor of its tracker/task and selected PRs, never a preflight.
One observation allows at most 96 explicit API calls, with pages of at most 100.
A lease expires two hours after registration or its most recent heartbeat.
"""


import argparse
from contextlib import closing, contextmanager
import json
import os
from pathlib import Path
import re
import sqlite3
import time
import uuid

from .github import HOME_URL, GitHub, need, path_key, pr_number, pr_url, printable
from .watch import CLAIM_LINK, ERRORS, emit, error_text, remote, stamp, task_key, watch


TTL = 2 * 60 * 60


def state_path():
    if os.name == "nt":
        root = os.environ.get("LOCALAPPDATA")
        need(root and Path(root).is_absolute(), "LOCALAPPDATA unavailable")
        return Path(root) / "Leonida" / "coordination.sqlite3"
    root = Path(os.environ.get("XDG_STATE_HOME") or Path.home() / ".local" / "state")
    need(root.is_absolute(), "state directory must be absolute")
    return root / "leonida" / "coordination.sqlite3"


@contextmanager
def database(*, write=False, create=False):
    path = state_path()
    if create:
        path.parent.mkdir(mode=0o700, parents=True, exist_ok=True)
    elif not path.exists():
        yield None
        return
    mode = "rwc" if create else ("rw" if write else "ro")
    with closing(sqlite3.connect(f"{path.as_uri()}?mode={mode}", uri=True,
                                 timeout=15, isolation_level=None)) as db:
        db.row_factory = sqlite3.Row
        if write:
            db.execute("BEGIN IMMEDIATE")
        try:
            if create:
                db.execute("""CREATE TABLE IF NOT EXISTS leases (
                    task TEXT PRIMARY KEY, lease_id TEXT NOT NULL UNIQUE, agent TEXT NOT NULL,
                    claim TEXT NOT NULL, paths TEXT NOT NULL, overrides TEXT NOT NULL,
                    expires INTEGER NOT NULL)""")
            yield db
            if write:
                db.commit()
        except BaseException:
            if write:
                db.rollback()
            raise


def live_leases(db, now):
    if db is None:
        return []
    return [{"task": row["task"], "lease_id": row["lease_id"], "agent": row["agent"],
             "claim": row["claim"], "paths": json.loads(row["paths"]),
             "overrides": json.loads(row["overrides"]), "expires": stamp(row["expires"])}
            for row in db.execute("SELECT * FROM leases WHERE expires > ? ORDER BY task", (now,))]


def local_matches(leases, task, paths):
    return [row for row in leases if row["task"] == task or paths.intersection(row["paths"])]


def read_matching_leases(args):
    with database() as db:
        return local_matches(live_leases(db, int(time.time())), args.task_id, args.paths)


def report_state(snapshot, leases, args):
    snapshot["leases"] = leases
    if snapshot["leases"]:
        snapshot["conflicts"].append("same task or exact path has an active local lease")
    for url in snapshot["pr_overlaps"]:
        if url not in args.overrides:
            snapshot["conflicts"].append(f"exact path overlap: {url}")
    for url in args.overrides:
        if url not in snapshot["pr_overlaps"]:
            snapshot["unknown"].append(f"override does not match a verified PR path overlap: {url}")
    return 2 if snapshot["unknown"] else (1 if snapshot["conflicts"] else 0)


def preflight(gh, args):
    snapshot = remote(gh, args)
    code = report_state(snapshot, read_matching_leases(args), args)
    if args.command == "register" and code == 0:
        # Acquire the local writer lock only after the remote preflight. Re-read
        # all active leases under the lock, including exact paths on other tasks.
        with database(write=True, create=True) as db:
            now = int(time.time())
            db.execute("DELETE FROM leases WHERE expires <= ?", (now,))
            matches = local_matches(live_leases(db, now), args.task_id, args.paths)
            if matches:
                snapshot["leases"] = matches
                snapshot["conflicts"].append("a concurrent registration holds the task or an exact path")
                code = 1
            else:
                lease_id = str(uuid.uuid4())
                db.execute("""INSERT INTO leases (task, lease_id, agent, claim, paths, overrides, expires)
                              VALUES (?,?,?,?,?,?,?)""",
                           (args.task_id, lease_id, args.agent, args.claim_url, json.dumps(sorted(args.paths)),
                            json.dumps(args.overrides), now + TTL))
                snapshot.update(lease_id=lease_id, expires=stamp(now + TTL), overrides=args.overrides)
    outcome = ["local lease registered" if args.command == "register" else "clear snapshot",
               "conflict", "unknown"][code]
    emit(task=args.task_id, outcome=outcome, **snapshot,
         notice="This observation grants no public reservation. Post the required tracker claim before editing.")
    return code


def manage(args):
    if args.command == "list-local":
        with database() as db:
            emit(leases=live_leases(db, int(time.time())))
        return 0
    need(printable(args.agent), "missing lease owner label")
    lease = str(uuid.UUID(args.lease_id))
    with database(write=True) as db:
        now = int(time.time())
        if db is None:
            affected = 0
        else:
            where = " WHERE lease_id=? AND agent=? AND expires>?"
            params = (lease, args.agent, now)
            if args.command == "heartbeat":
                affected = db.execute("UPDATE leases SET expires=?" + where, (now + TTL, *params)).rowcount
            else:
                affected = db.execute("DELETE FROM leases" + where, params).rowcount
    if not affected:
        emit(outcome="conflict", detail="no active lease with that UUID and owner label")
        return 1
    emit(outcome="renewed" if args.command == "heartbeat" else "released", lease_id=lease,
         **({"expires": stamp(now + TTL)} if args.command == "heartbeat" else {}))
    return 0


def arguments():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter,
                                     epilog="""Synthetic read-only examples:
  coordination_watch.py check --task-id DEMO-123 --paths tools/example.py
  coordination_watch.py watch --task-id DEMO-123 --interval 120""")
    subs = parser.add_subparsers(dest="command", required=True)
    for name in ("check", "register", "watch"):
        sub = subs.add_parser(name)
        sub.add_argument("--task-id", required=True)
        sub.add_argument("--paths", nargs="+", action="extend", default=[], required=name == "register")
        sub.add_argument("--pr", nargs="+", action="extend", default=[], metavar="PR_URL")
        if name == "register":
            sub.add_argument("--agent", required=True)
            sub.add_argument("--claim-url", required=True)
            sub.add_argument("--allow-overlap", nargs=2, action="append", default=[],
                             metavar=("PR_URL", "REASON"), help="override only this PR's verified exact path overlap")
        elif name == "watch":
            sub.add_argument("--interval", type=int, default=60, help="seconds between observations (minimum 60)")
            sub.add_argument("--event-subscription", metavar="EXISTING_ID",
                             help="listen using an existing subscription for exactly one selected PR")
    for name in ("heartbeat", "release"):
        sub = subs.add_parser(name)
        sub.add_argument("--lease-id", required=True)
        sub.add_argument("--agent", required=True, help="original local owner label")
    subs.add_parser("list-local")
    args = parser.parse_args()
    if hasattr(args, "task_id"):
        args.task_id = task_key(args.task_id)
        args.paths = {path_key(path) for path in args.paths}
        args.pr = sorted({pr_number(link) for link in args.pr})
        args.overrides = {}
    if args.command == "register":
        match = CLAIM_LINK.fullmatch(args.claim_url)
        need(printable(args.agent) and match,
             "register needs a local owner and a canonical Leonida tracker comment URL")
        args.claim_url = f"{HOME_URL}/issues/{match[1]}#issuecomment-{match[2]}"
        for link, reason in args.allow_overlap:
            url = pr_url(pr_number(link))
            need(url not in args.overrides and printable(reason.strip()), "expected a unique PR and overlap reason")
            args.overrides[url] = reason.strip()
    if args.command == "watch":
        need(args.interval >= 60, "watch interval must be at least 60 seconds")
        if args.event_subscription:
            need(len(args.pr) == 1 and re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_-]*", args.event_subscription),
                 "event listening requires one explicit PR and a valid existing subscription ID")
    return args


def main():
    gh = None
    try:
        args = arguments()
        if args.command in {"list-local", "heartbeat", "release"}:
            return manage(args)
        if args.command == "watch":
            return watch(args, read_matching_leases)
        gh = GitHub()
        return preflight(gh, args)
    except ERRORS as error:
        emit(outcome="unknown", tracker=gh.tracker_url if gh else None, detail=error_text(error))
        return 2
    except KeyboardInterrupt:
        return 130
