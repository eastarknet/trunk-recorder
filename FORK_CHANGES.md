# Changes in this fork

This repository is a fork of
[Trunk Recorder](https://github.com/TrunkRecorder/trunk-recorder).

The `customizations` branch contains additional source-selection, recording,
and call-output behavior beyond the upstream version on which it is based.

## Upstream base

The initial fork changes are based on upstream commit:

~~~
3fe84145c0fea03356e64817113114718729a7f9
~~~

When incorporating these changes into a newer Trunk Recorder version,
review and port the individual features rather than assuming the complete
patch will apply without conflicts.

## Summary of changes

The fork adds the following behavior:

1. Per-system SDR source restrictions.
2. Source-aware control-channel setup and retuning.
3. Source-aware voice recorder selection with fallback.
4. A global `recordDenyTalkgroups` configuration option.
5. Rate-limited logging for denied talkgroups.
6. Optional duplicate output for patched talkgroups.
7. Separation of calls whose talkgroups are not present in the talkgroup file.
8. Building the existing `stat_socket` plugin.

Each change is described below.

---

## Per-system SDR source restrictions

A system can specify which entries from the top-level `sources` array it is
allowed to use.

Example:

~~~json
{
  "sources": [
    {
      "center": 770000000,
      "rate": 2400000
    },
    {
      "center": 773000000,
      "rate": 2400000
    }
  ],
  "systems": [
    {
      "shortName": "SITE_A",
      "type": "p25",
      "control_channels": [
        771000000
      ],
      "sources": [0]
    },
    {
      "shortName": "SITE_B",
      "type": "p25",
      "control_channels": [
        772500000
      ],
      "sources": [1]
    }
  ]
}
~~~

The values in a system's `sources` array are indexes into the top-level
`sources` array.

If the system does not specify `sources`, or its list is empty, the system
falls back to considering all configured sources.

The restriction is applied during:

- initial control-channel source selection;
- subsequent control-channel retuning;
- voice recorder assignment.

Invalid source indexes are rejected and logged.

### Implementation

Configured source indexes are stored on the `System` object through
`set_source_nums()` and `get_source_nums()`.

Relevant files:

~~~
trunk-recorder/config.cc
trunk-recorder/setup_systems.cc
trunk-recorder/monitor_systems.cc
trunk-recorder/systems/system.h
trunk-recorder/systems/system_impl.cc
trunk-recorder/systems/system_impl.h
~~~

---

## Voice recorder source selection and fallback

When more than one allowed source covers a voice frequency, candidate
sources are ordered by distance between the call frequency and each
source's center frequency.

The nearest-center source is attempted first.

If that source:

- does not have an available recorder; or
- returns a recorder that fails to start,

the next eligible covering source is attempted.

This avoids dropping a call solely because the first covering source cannot
provide a usable recorder.

If multiple candidates have the same center-frequency distance, the source
number is used as a deterministic tie breaker.

If sources cover the requested frequency but none can supply a recorder,
the call is placed into the `NO_RECORDER` monitoring state and an error is
logged.

---

## Global recordDenyTalkgroups

The fork supports a root-level `recordDenyTalkgroups` array.

Example:

~~~json
{
  "recordDenyTalkgroups": [
    1001,
    1002,
    1003
  ]
}
~~~

A grant for a talkgroup in this array is not recorded.

The list applies globally rather than being repeated separately for every
configured system.

Repeated control-channel grants for denied talkgroups can create excessive
log output. The fork rejects every matching grant while rate-limiting the
informational denial message for a given system/talkgroup combination to
once per minute.

Relevant files:

~~~
trunk-recorder/config.cc
trunk-recorder/global_structs.h
trunk-recorder/monitor_systems.cc
~~~

---

## Patched-talkgroup duplicate output

The fork adds the root-level Boolean option:

~~~json
{
  "patchGroupDuplicateOutput": true
}
~~~

The default is `false`.

When enabled and a completed call contains multiple patched talkgroups, the
fork can produce an additional call output for the other patched talkgroup
members.

For each duplicate, the fork:

- refreshes talkgroup metadata for the target talkgroup;
- calculates the output filename for that talkgroup;
- creates separate copies of the source transmission files;
- updates the copied transmission metadata;
- submits the duplicate through the normal call-output worker.

If the configured `filenameFormat` would cause a duplicate to use the same
filename as the original call, a talkgroup-based suffix is added as a
collision fallback.

If copying any source transmission fails, partially created files for that
duplicate are removed and the duplicate output is skipped.

### Storage consideration

Enabling this option intentionally creates additional recording output and
therefore increases disk usage and downstream processing volume.

Relevant files:

~~~
trunk-recorder/call_concluder/call_concluder.cc
trunk-recorder/config.cc
trunk-recorder/global_structs.h
~~~

---

## Unknown-talkgroup output separation

When a call's talkgroup is not present in the configured talkgroup file,
this fork places that call beneath an `unknown` subdirectory of the normal
capture directory.

Normal Trunk Recorder directory and filename construction then continues
beneath that location.

This makes calls with unknown talkgroups easy to distinguish from calls
whose talkgroup metadata is already defined.

Relevant file:

~~~
trunk-recorder/call_concluder/call_concluder.cc
~~~

This behavior is part of the fork's current implementation and is not
controlled by a separate configuration option.

---

## stat_socket plugin

The upstream source tree already contains the `plugins/stat_socket` plugin.

This fork enables that existing plugin in the top-level CMake build with:

~~~cmake
add_subdirectory(plugins/stat_socket)
~~~

No fork-specific implementation of `stat_socket` is introduced by this
change.

Relevant file:

~~~
CMakeLists.txt
~~~

---

## Configuration additions

| Location | Setting | Type | Default | Purpose |
| --- | --- | --- | --- | --- |
| Root | `recordDenyTalkgroups` | array of talkgroup IDs | empty | Prevent recording selected talkgroups globally |
| Root | `patchGroupDuplicateOutput` | Boolean | `false` | Produce additional outputs for patched talkgroups |
| System | `sources` | array of source indexes | all sources | Restrict a system to selected SDR sources |

---

## Porting these changes to another Trunk Recorder version

The initial implementation is kept together on the `customizations` branch
so the complete change set can be inspected against its upstream base.

To examine the complete difference:

~~~bash
git diff 3fe84145c0fea03356e64817113114718729a7f9..customizations
~~~

For a newer upstream version, the safest approach is generally to port the
features individually.

A useful dependency order is:

1. Add `source_nums` storage and accessors to `System`.
2. Parse each system's `sources` configuration.
3. Apply allowed-source filtering during system setup.
4. Apply allowed-source filtering during retuning.
5. Apply allowed-source filtering and fallback during voice recorder
   allocation.
6. Add the global deny-list configuration and grant check.
7. Add patch-group duplicate output if desired.
8. Add unknown-talkgroup output separation if desired.
9. Enable `stat_socket` if that plugin is required.

The source-selection changes span several code paths and should be ported
as one logical feature. Applying only the configuration parser without the
setup, retune, and recorder-allocation changes would not provide consistent
source isolation.

---

## Relationship to upstream

This fork is not an official Trunk Recorder release and is not maintained
by the upstream Trunk Recorder project.

For upstream installation instructions, general configuration, supported
radio systems, and development, refer to:

https://github.com/TrunkRecorder/trunk-recorder

The purpose of this branch is to preserve and document these additional
behaviors in a form that can be reviewed, reused, or selectively ported.
