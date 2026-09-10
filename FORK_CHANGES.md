# EastArkNet Trunk Recorder changes

This branch semantically ports EastArkNet behavior onto upstream Trunk Recorder
commit `7b27f8be0a757453d63f1b6ee631a994e03e534d`, which includes upstream trunked
DMR support.

## Retained features

- A trunked system may use `"sources": [0, 1, ...]` to restrict initial
  control-channel setup, control-channel retuning, and voice calls to those
  root source indexes. An omitted or empty list retains upstream all-source
  behavior. Invalid indexes are rejected. These restrictions apply to P25,
  SmartNet, and trunked DMR systems.
- Voice recorder candidates must cover the call frequency and are tried in
  nearest-center-frequency order, with source number breaking ties. Allocation
  or start failure falls through to the next candidate. Trunked DMR candidates
  use `get_dmr_recorder()` for known and unknown talkgroups; they are never
  routed through the P25/digital recorder path.
- Root `recordDenyTalkgroups` lists talkgroup IDs whose grants are rejected for
  every protocol. Informational rejection logs are throttled per system and
  talkgroup to approximately once per minute.
- Root `patchGroupDuplicateOutput` (default `false`) duplicates completed patch
  calls for other patch members through normal output processing. Each copy has
  refreshed talkgroup metadata, collision-safe output names, separate source
  transmission files, and partial-copy cleanup on failure.
- Calls absent from a system talkgroup file are written beneath
  `<captureDir>/unknown/`, followed by normal directory and filename layout.
- The existing upstream `plugins/stat_socket` directory is enabled by the
  top-level CMake build so its ABI matches the Trunk Recorder binary.

## DMR preservation

The source-selection changes wrap, but do not replace, upstream DMR setup,
retuning, recorder allocation, message handling, LCN mapping, or parsing. The
upstream slot-aware DMR recorder lifecycle and dual-slot conventional DMR
behavior are intentionally preserved.

## Capacity Plus completion

- Capacity Plus Site Status (`CSBKO 0x3E`, `FID 0x10`) first and single
  segments now decode their ordered 8-bit voice talkgroup assignments into
  normal routed grants. Continuation and last segments do not independently
  emit grants.
- Capacity Plus logical slot numbers are validated and converted to LCN plus
  zero-based TDMA slot. Voice and rest-channel routing require an explicit LCN
  mapping; Capacity Plus never claims an arbitrary frequency from `channels`.
- Site Status, Neighbor Report, and CACH rest-LSN announcements update rest state and
  move the DMR trunking decoder directly to the mapped RF frequency when it
  changes. The shared targeted-retune path retains per-system source
  restrictions, including when movement requires rebuilding the DMR flowgraph
  on another allowed source.
- The explicit `lcnTable` maps Capacity Plus LCNs to RF frequencies for grants
  and rest movement, but does not populate the trunked system
  `control_channels` search list. For a cold start, configure every possible
  rest RF frequency in `control_channels` so Trunk Recorder can find the active
  rest channel before decoding a rest-movement event. For EastArkNet SFCPS,
  those frequencies will be `158797500`, `155175000`, and `154845000`.

This protocol implementation has deterministic parser validation but has not
yet been validated against the production SFCPS system.

## Upgrade warning

Future upstream upgrades require another semantic feature port. Do not blindly
apply or cherry-pick the old EastArkNet patch: source selection and call output
overlap actively developed upstream protocol and recorder code.
