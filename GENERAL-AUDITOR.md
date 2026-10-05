# General-Auditor

CI uses `Unka-Malloc/General-Auditor@only` and audits this repository only.
The native `styio-audit` status context is retained where branch protection requires it.
Keyword findings are advisory and require contextual review by the contributor's local Agent.

For local checks, use a trusted checkout of General-Auditor's `only` branch.
Select its absolute directory with `--audit-root` where supported, otherwise
`GENERAL_AUDITOR_ROOT`, or repository-local `git config --local generalAuditor.root`.
An explicit argument overrides the environment, which overrides local Git configuration.
The directory must contain `action_entry.py` and this repository's exact
`profiles/SymPolicy/Pafio.json`; missing profiles are errors.
There is no PATH lookup or common-only fallback.

```sh
git config --local generalAuditor.root "$GENERAL_AUDITOR_ROOT"
```

The command below uses an explicit environment value; hooks and delivery wrappers
also accept the local Git configuration. The local scope commands require the
scope implementation from [General-Auditor PR #1](https://github.com/Unka-Malloc/General-Auditor/pull/1).
Do not fetch or execute Auditor code from a contributor's branch.

```sh
python3 -I "$GENERAL_AUDITOR_ROOT/action_entry.py" scan \
  --policy-root "$GENERAL_AUDITOR_ROOT" --repository "SymPolicy/Pafio" \
  --directory . --scope staged --output "$(git rev-parse --git-path general-auditor/staged.json)"
```

Use `--scope worktree` for tracked and nonignored untracked working files,
`--scope history` for all commits reachable from HEAD, or `--scope range --base <commit>`
for outgoing commits. Reports remain under Git's private metadata directory;
review redacted findings with the local Agent before publishing. A successful
scanner exit is not a contextual privacy approval. Configuration failures fail the command.
Existing application checks and delivery schedulers remain independent.
