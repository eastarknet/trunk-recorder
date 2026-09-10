#ifndef CAPACITY_PLUS_PARSER_H
#define CAPACITY_PLUS_PARSER_H

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
