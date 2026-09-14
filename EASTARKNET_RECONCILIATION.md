# EastArkNet Trunk Recorder Reconciliation — September 14, 2026

This file tracks the gap between the canonical GitHub fork and the newer accepted/experimental work currently present in SDR-host worktrees.

It is intentionally **not** a claim that the September DMR/CAP+ source has already been promoted to the canonical GitHub branch.

## Current GitHub authority

The default `customizations` branch is still anchored at:

`5164458eb57a91056e3ff0cb737529b4282d5186`

That branch documents the August EastArkNet fork features:

- per-system SDR source restrictions;
- source-aware control-channel setup/retuning;
- source-aware voice-recorder selection/fallback;
- global `recordDenyTalkgroups`;
- patched-talkgroup duplicate output;
- unknown-talkgroup output separation;
- `stat_socket` build support.

The remote `ean-dmr-port-20260910` branch does not yet represent the full accepted September DMR work from the SDR host.

## SDR-host work that must be classified before promotion

Recent isolated work has included:

- the September DMR port to the newer upstream Trunk Recorder line;
- per-system source restriction preservation on the newer source base;
- conventional/trunked DMR behavior and DMR recorder fixes;
- missing-TLC investigation/repair;
- bounded DMR tag handling;
- persistent P25 source work;
- CAP+ parser/transition investigations and memory-isolation diagnostics.

Some of this work is accepted or production-relevant; some remains diagnostic or experimental. The distinction must be preserved during repository cleanup.

## Required reconciliation

- [ ] Inventory every current SDR-host Trunk Recorder worktree, branch and immutable HEAD.
- [ ] Classify each local-only commit as accepted, superseded, diagnostic, or experimental.
- [ ] Identify the exact upstream base used for the accepted September port.
- [ ] Push the accepted DMR port commits to a durable GitHub branch.
- [ ] Preserve EastArkNet per-system source restrictions and source-aware recorder behavior.
- [ ] Include accepted conventional/trunked DMR fixes only from their accepted immutable source.
- [ ] Include missing-TLC and bounded-tag repairs only after their isolated acceptance gates pass.
- [ ] Include persistent P25 source changes only after their isolated acceptance gate passes.
- [ ] Keep CAP+ parser/transition work isolated until separately accepted.
- [ ] Update `FORK_CHANGES.md` to describe the complete accepted fork, not only the August customization set.
- [ ] Reconcile README/configuration documentation for accepted DMR/source-selection behavior.
- [ ] Record the exact accepted production binary/source commit after promotion.
- [ ] Remove superseded local/remote branches only after all accepted commits are durably reachable from GitHub.

## CAP+ boundary

CAP+ transition/parser work remains an experimental workstream until explicitly accepted.

Do not merge CAP+ changes into the canonical EastArkNet fork merely because a diagnostic worktree exists or a bounded test passes. Memory/thread-growth investigation and protocol correctness must remain separate acceptance questions.

## Production safety

Repository reconciliation must not itself:

- stop, restart, reload, or signal production Trunk Recorder;
- alter `/etc/trunk-recorder/awin-all.json`;
- change the live tuner map;
- discard local-only commits before classification;
- run GitHub Actions or another metered workflow without explicit authorization.

## Exit criteria

Reconciliation is complete when:

1. one clearly identified GitHub branch/commit represents the accepted EastArkNet fork;
2. accepted September DMR work is no longer stranded only in SDR-host worktrees;
3. experimental CAP+ work remains isolated unless separately accepted;
4. `FORK_CHANGES.md` accurately describes the accepted fork and upstream relationship; and
5. production source provenance can be reconstructed from GitHub without depending on a dirty or local-only checkout.
