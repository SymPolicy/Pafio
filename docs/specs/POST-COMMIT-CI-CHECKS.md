# Post-Commit CI Checks

**Purpose:** Define the required workflow for checking GitHub Actions after a local commit is pushed, including what must be verified before committing and what must be watched after pushing.

**Last updated:** 2026-09-08

## Scope

This spec applies to agent and maintainer work on `Pafio` branches. It covers local pre-commit verification, post-push GitHub Actions monitoring, and failure recovery for repository-local and cross-repository gates.

Use the current request and existing approvals to determine authority. This workflow does not itself authorize a commit, push, merge, release, or governance change. Repository review and approval requirements remain effective; do not ask again for an action already covered by the same scope and risk boundary. Resolve discoverable facts and ordinary in-scope implementation issues directly, report material findings, and pause only work that needs a new decision.

## Commit-Time Verification

Before creating an authorized commit, run the closest local checks affected by the change. Use focused checks during implementation and reuse passing evidence while its inputs remain unchanged. The commands below are a scope-dependent catalog, not a requirement to run every gate before every commit.

Local checks for the affected surfaces:

```bash
ctest --test-dir build-codex --output-on-failure -R '<affected-test-pattern>'
python3 scripts/repo-hygiene-gate.py --mode tracked
python3 scripts/docs-audit.py
```

The GitHub Actions CI floor is the repository-local `local-ci-gate` workflow.
It owns the range-aware docs/repo hygiene checks, native submit gate,
extractability gate, performance smoke gate, and delivery package gate that
were previously split across `styio-ci`, `repo-hygiene`, and `Submit Gate`.
`local-ci-gate` is the pafio repository's own CI surface; it is not the shared
Styio ecosystem resource gate modeled by upstream `styio-ci-gate`.
Inside that workflow, `Linux / Debian 13 trixie gate` is the blocking repository
gate for shared-source `nightly` promotion. `macOS / latest native smoke` is an
independent platform adaptation lane. `Windows / latest native smoke` runs only
through explicit `workflow_dispatch`; Windows release owners execute it from
the Windows release flow and must resolve any failure before publishing Windows
artifacts. Neither a shared-source merge nor another platform's passing result
constitutes Windows release evidence.

During authorized Windows process-adaptation work, dispatch the same workflow
with `native_process_only=true` for focused validation. It builds the CLI and runs the
independent `PortableProcess.*` contracts plus CLI probes. The default dispatch
still runs the full workflow; focused process evidence does not replace the
platform's complete release acceptance.

For cross-repository checkouts, CI first tries the Pafio branch name and falls
back independently to each sibling repository's `nightly` branch when that
branch does not exist there. A Pafio-only change therefore does not require
creating empty matching branches in Styio or Vityo.

Cross-repository documentation changes run the ecosystem CLI docs gate:

```bash
cd <pafio-workspace>
python3 scripts/ecosystem-cli-doc-gate.py --require-workspace --workspace-root <workspace-root>
```

The docs gate validates the Pafio-owned surface locally by default. Its explicit
workspace form checks fixed Pafio, Styio, Styio Platform, and Vityo
documentation; it never discovers or delegates to a mutable sibling checkout.
The commit message body should record the checks that were actually run.

## Final Regression

Run the required complete regression after all changes, source review, in-scope repairs, and focused verification are finished. Coordinate local and CI evidence for the same candidate; required CI checks still run after an authorized push. If the regression fails, diagnose the failing stage and repair ordinary defects within the authorized scope. Run the focused checks needed to verify those repairs, then repeat only the affected checks and any broader regression needed to establish acceptance, reusing still-valid evidence. Request a developer decision only when the necessary repair would change the objective, a published contract, or the risk boundary. Keep acceptance unresolved until the required checks pass.

## Post-Push Verification

After pushing a commit, the agent must actively check GitHub Actions while the current work turn remains open.

Required steps:

1. Resolve the current branch and pushed commit.
2. Query GitHub Actions for the repository and branch.
3. Observe the relevant run for the exact pushed commit using bounded tool waits and progress updates. An expired observation window does not cancel the run or establish its result.
4. If a check fails, inspect its diagnostics and report a privacy-safe cause and the smallest repair and verification proposal. Follow the Final Regression decision rule for complete-regression failures. Ordinary focused-check failures may be repaired within existing authority; a follow-up commit or push must also be covered by that authority.
5. If observation is blocked or the turn ends before the run completes, report the run URL, commit, unresolved status, and resume command as an incomplete verification handoff.

Preferred commands:

```bash
gh run list --branch "$(git branch --show-current)" --limit 10
gh run view <run-id> --json headSha,status,conclusion,url
gh run view <run-id> --log-failed
```

If `gh` is unavailable or unauthenticated, the agent must state that GitHub Actions could not be checked directly and include the local gates that were run instead.

## Cross-Repository Work

When one delivery spans several sibling repositories, post-push verification
applies to every pushed repository. The agent should check each repository's GitHub
Actions status, not only the repository that received the last commit.

Cross-repository gates must use the same workspace checkout set that will be visible to CI. If a gate consumes another repository's branch, perform an already-authorized dependency push first; otherwise prepare the required handoff and report the revision mismatch. A gate dependency does not grant permission to publish another repository.

## Delivery Ruleset Governance

Required GitHub merge gates are maintained through GitHub Rulesets, not legacy classic branch protection. Protected downstream branches should require `secret-scan` and the blocking Linux `local-ci-gate` job as the stable repository-local status-check surface. macOS results apply only to macOS adaptation. Windows is a separately dispatched release lane and must not become a shared-source merge requirement or inherit another platform's acceptance evidence.

Gate audits must inspect effective branch rules, for example:

```bash
gh api repos/<owner>/<repo>/rules/branches/ai-dev
```

Do not use `branches/ai-dev/protection/required_status_checks` as the authority for this repository. That legacy classic endpoint can return 404 even when the Ruleset gate is active.

## Completion Criteria

A delivery requiring remote verification is complete only when the required checks pass for the exact delivered commit and all authorized acceptance conditions are satisfied. After an approved repair and push, use the replacement commit's results.

Queued, running, failed, cancelled, or unobservable checks remain unresolved verification. A status URL and recovery command make the handoff actionable; they do not make the delivery complete. A local-only request is complete against its local acceptance conditions and does not require an unsolicited push.
