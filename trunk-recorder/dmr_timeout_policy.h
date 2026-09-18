#ifndef DMR_TIMEOUT_POLICY_H
#define DMR_TIMEOUT_POLICY_H

#include <string>

inline bool should_force_conventional_dmr_timeout(
    const std::string &system_type,
    bool recorder_is_recording,
    double current_length,
    double seconds_since_last_write,
    double call_timeout) {
  return system_type == "conventionalDMR" &&
         recorder_is_recording &&
         current_length > 0.0 &&
         seconds_since_last_write > call_timeout;
}

#endif
