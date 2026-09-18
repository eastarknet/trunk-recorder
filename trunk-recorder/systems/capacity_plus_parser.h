#ifndef CAPACITY_PLUS_PARSER_H
#define CAPACITY_PLUS_PARSER_H

#include <cstddef>
#include <vector>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

struct CapacityPlusChannel {
  int lsn;
  int lcn;
  int tdma_slot;
};

struct CapacityPlusVoiceAssignment {
  CapacityPlusChannel channel;
  uint8_t talkgroup;
};

struct CapacityPlusSiteStatus {
  uint8_t segment;
  int rest_lsn;
  std::vector<CapacityPlusVoiceAssignment> voice_assignments;
};

inline int capacity_plus_rest_lsn_from_op25_slc(const uint8_t *slc) {
  return slc[2] & 0x1f;
}

inline bool capacity_plus_lsn_to_channel(int lsn, CapacityPlusChannel &channel) {
  if (lsn < 1 || lsn > 16) return false;
  channel.lsn = lsn;
  channel.lcn = ((lsn - 1) / 2) + 1;
  channel.tdma_slot = (lsn - 1) % 2;
  return true;
}

inline bool capacity_plus_rest_retune_needed(double monitored_freq, double rest_freq) {
  return rest_freq != 0 && rest_freq != monitored_freq;
}

constexpr long long CAPACITY_PLUS_CONTROL_ACTIVITY_TIMEOUT_SECONDS = 60;

// Cross-frequency rest moves require repeated agreement before leaving a
// known-good Capacity Plus rest channel.
constexpr int CAPACITY_PLUS_REST_CONFIRMATIONS_REQUIRED = 2;
constexpr long long CAPACITY_PLUS_REST_CONFIRMATION_WINDOW_SECONDS = 3;

// Once a confirmed rest move is followed, fail back quickly if the target
// produces nothing but OP25 sync timeouts. If real DMR frames are present,
// allow a longer acquisition window for marginal RF before failing back.
constexpr long long CAPACITY_PLUS_PROBE_TIMEOUT_SECONDS = 5;
constexpr long long CAPACITY_PLUS_WEAK_PROBE_TIMEOUT_SECONDS = 15;

// Persistent multi-frequency signaling monitors are never retuned in
// response to rest-channel announcements. The legacy singleton retains its
// existing transition behavior.
inline bool capacity_plus_rest_signaling_retune_allowed(bool multi_frequency) {
  return !multi_frequency;
}

struct DmrSignalingSourceCoverage {
  double min_frequency;
  double max_frequency;
};

// `allowed_sources` must contain only sources admitted by the system's
// sources[] restriction. Returning -1 makes an uncovered frequency an
// explicit setup failure instead of borrowing an unlisted source.
inline int find_dmr_signaling_source(
    double frequency,
    const std::vector<DmrSignalingSourceCoverage> &allowed_sources) {
  for (size_t index = 0; index < allowed_sources.size(); ++index) {
    if (allowed_sources[index].min_frequency <= frequency &&
        allowed_sources[index].max_frequency >= frequency) {
      return static_cast<int>(index);
    }
  }
  return -1;
}

inline bool capacity_plus_rest_confirmation_window_open(
    long long now_seconds, long long first_seen_seconds) {
  if (first_seen_seconds <= 0) return false;
  if (now_seconds < first_seen_seconds) return false;
  return (now_seconds - first_seen_seconds) <=
         CAPACITY_PLUS_REST_CONFIRMATION_WINDOW_SECONDS;
}

inline long long capacity_plus_probe_timeout_seconds(bool saw_non_timeout_dmr) {
  return saw_non_timeout_dmr
             ? CAPACITY_PLUS_WEAK_PROBE_TIMEOUT_SECONDS
             : CAPACITY_PLUS_PROBE_TIMEOUT_SECONDS;
}

inline bool capacity_plus_probe_timed_out(
    long long now_seconds,
    long long probe_started_seconds,
    bool saw_non_timeout_dmr) {
  if (probe_started_seconds <= 0) return false;
  if (now_seconds < probe_started_seconds) return false;

  return (now_seconds - probe_started_seconds) >=
         capacity_plus_probe_timeout_seconds(saw_non_timeout_dmr);
}

inline bool capacity_plus_control_activity_lost(
    long long now_seconds, long long last_activity_seconds) {
  if (last_activity_seconds <= 0) return true;

  // A backwards wall-clock adjustment must not create an artificial outage.
  if (now_seconds < last_activity_seconds) return false;

  return (now_seconds - last_activity_seconds) >
         CAPACITY_PLUS_CONTROL_ACTIVITY_TIMEOUT_SECONDS;
}

inline CapacityPlusSiteStatus decode_capacity_plus_site_status(
    const uint8_t *payload, std::size_t payload_size) {
  CapacityPlusSiteStatus status = {0, 0, {}};
  if (!payload || payload_size == 0) return status;

  status.segment = (payload[0] >> 6) & 0x03;
  status.rest_lsn = payload[0] & 0x1f;

  // Continuation and last segments do not independently carry a complete
  // voice map. Their rest LSN remains usable by the caller.
  if ((status.segment != 2 && status.segment != 3) || payload_size < 2) {
    return status;
  }

  std::size_t cursor = 2;
  const uint8_t low_bitmap = payload[1];
  for (int bit = 0; bit < 8; ++bit) {
    if ((low_bitmap & (0x80 >> bit)) == 0) continue;
    if (cursor >= payload_size) return status;
    CapacityPlusChannel channel;
    const uint8_t talkgroup = payload[cursor++];
    if (talkgroup != 0 && capacity_plus_lsn_to_channel(bit + 1, channel)) {
      status.voice_assignments.push_back({channel, talkgroup});
    }
  }

  if (cursor >= payload_size) return status;
  const uint8_t high_bitmap = payload[cursor++];
  for (int bit = 0; bit < 8; ++bit) {
    if ((high_bitmap & (0x80 >> bit)) == 0) continue;
    if (cursor >= payload_size) return status;
    CapacityPlusChannel channel;
    const uint8_t talkgroup = payload[cursor++];
    if (talkgroup != 0 && capacity_plus_lsn_to_channel(bit + 9, channel)) {
      status.voice_assignments.push_back({channel, talkgroup});
    }
  }
  return status;
}

#endif
