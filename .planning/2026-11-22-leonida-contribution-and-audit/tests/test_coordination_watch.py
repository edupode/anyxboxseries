"""Characterize the legacy CLI and its module entry point."""

from test_coordination_github import CLAIM, ENTRY_POINTS, LEGACY, MODULE, PR, SHA, TASK, TRACKER, SyntheticGitHubTest


class WatchCompatibilityTests(SyntheticGitHubTest):
    def test_module_exposes_all_legacy_commands(self):
        result, calls = self.run_cli(MODULE, "--help")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("{check,register,watch,heartbeat,release,list-local}", result.stdout)
        self.assertEqual(calls, [])

    def test_required_and_validated_arguments_fail_before_gh(self):
        cases = [
            (["check"], "required: --task-id"),
            (["register", "--task-id", TASK], "required: --paths, --agent, --claim-url"),
            (["watch", "--task-id", TASK, "--interval", "59"], "watch interval must be at least 60 seconds"),
            (["watch", "--task-id", TASK, "--event-subscription", "existing"],
             "event listening requires one explicit PR and a valid existing subscription ID"),
            (["heartbeat", "--lease-id", "bad"], "required: --agent"),
            (["release", "--agent", "agent"], "required: --lease-id"),
            (["list-local", "--task-id", TASK], "unrecognized arguments"),
            (["check", "--task-id", TASK, "--paths", "../outside"], "expected a repository-relative file path"),
        ]
        for args, message in cases:
            with self.subTest(args=args):
                result, calls = self.run_cli(LEGACY, *args)
                self.assertEqual(result.returncode, 2)
                self.assertIn(message, result.stdout + result.stderr)
                self.assertEqual(calls, [])

    def test_register_and_manage_share_the_existing_local_lease(self):
        result, calls = self.run_cli(
            LEGACY, "register", "--task-id", "demo-123", "--paths", r"tools\example.py", "./tools/example.py",
            "--agent", "agent", "--claim-url", CLAIM, scenario="claim",
        )
        lease = self.snapshot(result, 0)
        self.assertEqual(lease["outcome"], "local lease registered")
        self.assertEqual(lease["claim_state"], "claimed by you")
        self.assertIn("tracker-comments", calls)
        lease_args = ("--lease-id", lease["lease_id"], "--agent", "agent")

        result, calls = self.run_cli(MODULE, "list-local")
        self.assertEqual(self.snapshot(result, 0)["leases"], [{
            "task": TASK, "lease_id": lease["lease_id"], "agent": "agent", "claim": CLAIM,
            "paths": ["tools/example.py"], "overrides": {}, "expires": lease["expires"],
        }])
        self.assertEqual(calls, [])

        result, _ = self.run_cli(MODULE, "check", "--task-id", TASK, scenario="claim")
        self.assertEqual(self.snapshot(result, 1)["conflicts"], ["same task or exact path has an active local lease"])

        result, calls = self.run_cli(MODULE, "heartbeat", *lease_args)
        self.assertEqual(self.snapshot(result, 0)["outcome"], "renewed")
        self.assertEqual(calls, [])

        result, calls = self.run_cli(LEGACY, "release", *lease_args)
        self.assertEqual(self.snapshot(result, 0)["outcome"], "released")
        self.assertEqual(calls, [])

        result, calls = self.run_cli(MODULE, "heartbeat", *lease_args)
        self.assertEqual(self.snapshot(result, 1)["outcome"], "conflict")
        self.assertEqual(calls, [])

        result, calls = self.run_cli(LEGACY, "list-local")
        self.assertEqual(self.snapshot(result, 0), {"leases": []})
        self.assertEqual(calls, [])

    def test_watch_observes_only_the_selected_task_and_pr(self):
        for entry in ENTRY_POINTS:
            with self.subTest(entry=entry):
                result, calls = self.run_cli(entry, "watch", "--task-id", TASK, "--pr", PR, PR, watch_once=True)
                snapshot = self.snapshot(result, 130)
                self.assertEqual(snapshot["outcome"], "monitor snapshot")
                self.assertEqual(snapshot["tracker"], TRACKER)
                self.assertEqual(snapshot["prs"], {"7": {
                    "pr": PR, "state": "OPEN", "head": SHA, "draft": False, "review": None,
                    "reviews": 0, "last_review": [], "requested": 0, "checks": "SUCCESS",
                }})
                self.assertEqual(calls, [
                    "auth", "user", "open-trackers", "tracker-body", "tracker-comments", "task-search",
                    "open-pr-branches", "selected-pr", "open-trackers", "tracker-version",
                ])

    def test_watch_listens_to_an_existing_subscription(self):
        result, calls = self.run_cli(
            MODULE, "watch", "--task-id", TASK, "--pr", PR, "--event-subscription", "existing",
            "--interval", "120", watch_once=True,
        )
        self.assertEqual(self.snapshot(result, 130)["outcome"], "monitor snapshot")
        self.assertEqual(calls[-2:], ["subscription-view", "subscription-listen"])

    def test_register_fails_closed_when_the_tracker_changes(self):
        result, calls = self.run_cli(
            LEGACY, "register", "--task-id", TASK, "--paths", "tools/example.py",
            "--agent", "agent", "--claim-url", CLAIM, scenario="stale",
        )
        self.assertEqual(self.snapshot(result, 2), {
            "tracker": TRACKER, "outcome": "unknown",
            "detail": "tracker rolled over or changed during observation",
        })
        self.assertEqual(calls[-2:], ["open-trackers", "tracker-version"])
        self.assertFalse((self.directory / "state").exists())
