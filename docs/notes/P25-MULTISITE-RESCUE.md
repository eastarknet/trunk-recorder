# P25 multisite dead-primary rescue

## Status

EastArkNet Version 1 is operationally validated in production as of 2026-09-21.

Implementation branch:

`feat/multisite-dead-primary-rescue-20260920`

The lifecycle fix that closed the orphaned provisional-candidate edge case was
validated from implementation baseline commit
`b677a7038adc63278c710d1dab1e93792a2525f9`.

This feature is intentionally narrow. Version 1 rescues an obviously dead P25
multisite winner; it does not rank working sites by audio quality.

## Problem

Normal multisite handling chooses one copy of a talkgroup call and suppresses
duplicate grants from other sites. That is normally desirable, but it can lose
a usable call when the first selected site receives the control-channel grant
while its voice recorder produces no audio.

Version 1 preserves first-site-wins behavior unless the selected primary has
produced absolutely no recorded audio and a duplicate grant arrives from
another P25 site.

## Version 1 policy

The rescue policy is deliberately conservative:

- P25 multisite calls only.
- Existing static preferred-NAC/site supersede behavior remains higher
  priority when configured.
- The primary must have produced zero audio.
- The primary grace threshold is 1000 ms.
- At most one provisional duplicate-site recorder is active for a primary at
  one time.
- The candidate timeout is 1500 ms.
- The primary remains active while the provisional candidate is tested.
- If the primary begins writing audio, the primary wins and the candidate is
  discarded.
- If the candidate begins writing audio while the primary is still silent, the
  candidate is promoted and the silent primary is retired as superseded.
- If neither recorder writes audio before the candidate timeout, the candidate
  is discarded and the primary remains the nominal winner.
- A later duplicate grant may start another rescue attempt after a previous
  candidate has already been resolved.
- No RSSI ranking, decoder-error ranking, spike ranking, continuous switching,
  best-of-N election, or audio stitching is performed in Version 1.

The management loop runs approximately once per second, so observed resolution
time can exceed the literal threshold depending on loop timing and when a
duplicate grant becomes available.

## Candidate lifecycle

Rescue attempts are tracked by original call number and provisional candidate
call number.

A provisional candidate is excluded from acting as the primary for another
duplicate-site rescue while its attempt is active.

Candidate cleanup reuses the existing `SUPERSEDED` call-conclusion path so a
discarded provisional recording is removed rather than emitted to output
plugins as an empty or losing call.

### Primary-disappeared lifecycle fix

Production observation exposed an edge case in the first Version 1 build:

1. A silent primary started a provisional candidate.
2. Normal call management concluded and deleted the primary before the rescue
   manager's next decision pass.
3. The rescue map entry was erased because the primary could no longer be
   found.
4. The still-recording candidate was left behind and lost its provisional
   identity.
5. A later duplicate grant could then treat that former candidate as a normal
   primary and start a nested rescue.

The corrected logic distinguishes a missing primary from a missing candidate.
If the primary no longer exists while its candidate is still recording, the
candidate is explicitly discarded before the rescue tracking entry is erased.

The corresponding log message is:

```text
[MULTISITE-RESCUE] Primary call <id> no longer exists; discarding provisional candidate.
```

## Logging

All Version 1 decision logs use the `[MULTISITE-RESCUE]` prefix.

Typical events include:

```text
Silent primary <site> call <id> has produced no audio after <ms>ms; starting provisional candidate <site> call <id>.
Candidate recorder started; primary remains active until one recorder proves audio.
Primary <site> call <id> began writing audio (...s); discarding candidate <site> call <id>.
Candidate <site> call <id> produced audio (...s); promoting it and retiring silent primary <site> call <id>.
Candidate <site> call <id> produced no audio within 1500ms; keeping silent primary <site> call <id>.
Primary call <id> no longer exists; discarding provisional candidate.
```

## Production validation

The corrected build entered service on EastArkNet at approximately
2026-09-20 15:33 America/Chicago.

Between that restart and the validation review on 2026-09-21, 14 rescue
attempts were observed:

- 8 candidates produced audio and were promoted.
- 5 candidates produced no audio and were abandoned.
- 1 primary disappeared while its candidate was pending; the corrected orphan
  cleanup path discarded the provisional candidate.

All three Version 1 resolution paths were therefore exercised in live traffic.

### Representative successful rescue

On talkgroup 20201:

- FCITY call 40 remained silent.
- SHELL call 47 was started provisionally.
- SHELL produced 0.18 seconds of audio.
- SHELL was promoted.
- FCITY was concluded as `SUPERSEDED` and its files were removed.
- SHELL subsequently concluded through normal call handling with a recorded
  transmission.

This demonstrated that Version 1 can recover audio from a duplicate site when
the original multisite winner records nothing.

### Representative orphan cleanup

On talkgroup 53758:

- SHELL call 3711 was the silent primary.
- WMEM call 3712 was started as its provisional candidate.
- The primary disappeared before the rescue election completed.
- The corrected code logged that primary 3711 no longer existed and discarded
  provisional candidate 3712.
- No nested rescue from 3712 followed.

This directly validated the lifecycle fix against the production failure mode
that motivated it.

## Configuration note

EastArkNet removed the `Preferred NAC` column from its AWIN talkgroup CSV
before this validation. That avoids forcing a static per-talkgroup site
preference in the deployment and is consistent with the planned next phase,
where call quality rather than a configured NAC should ultimately drive site
selection.

The core Trunk Recorder preferred-NAC/site logic still exists; Version 1 does
not remove it from the codebase.

## Next phase: quality-based multisite selection

Version 1 answers only a binary question: did a recorder produce any audio?

The planned next phase is early-call quality selection among working multisite
copies. Candidate signals to evaluate include recorder output continuity and
existing P25 decoder error/spike metrics.

Design constraints carried forward from Version 1:

- avoid rapid site flapping;
- avoid continuous quality chasing during a call;
- avoid stitching fragments from several sites unless explicitly designed and
  validated later;
- make the winner decision early, then remain stable unless the selected
  recorder completely fails;
- gather production measurements before choosing scoring weights or thresholds.

Version 2 should be treated as a separate policy layer, not as an expansion of
the Version 1 dead-primary test.
