#ifndef MULTISITE_RESCUE_POLICY_H
#define MULTISITE_RESCUE_POLICY_H

#include <cstdint>

constexpr std::int64_t MULTISITE_RESCUE_PRIMARY_GRACE_MS = 1000;
constexpr std::int64_t MULTISITE_RESCUE_CANDIDATE_TIMEOUT_MS = 1500;

enum class MultiSiteRescueDecision {
  PENDING,
  KEEP_PRIMARY,
  PROMOTE_CANDIDATE,
  ABANDON_CANDIDATE
};

inline bool should_start_multisite_rescue(double primary_audio_length,
                                          double primary_silent_seconds,
                                          std::int64_t primary_age_ms) {
  return primary_audio_length <= 0.0 &&
         primary_age_ms >= MULTISITE_RESCUE_PRIMARY_GRACE_MS &&
         primary_silent_seconds >=
             (static_cast<double>(MULTISITE_RESCUE_PRIMARY_GRACE_MS) / 1000.0);
}

inline MultiSiteRescueDecision evaluate_multisite_rescue(
    double primary_audio_length,
    double candidate_audio_length,
    std::int64_t candidate_age_ms) {
  // Preserve current first-site behavior whenever the original begins
  // producing audio, even if the rescue candidate also starts writing.
  if (primary_audio_length > 0.0) {
    return MultiSiteRescueDecision::KEEP_PRIMARY;
  }

  if (candidate_audio_length > 0.0) {
    return MultiSiteRescueDecision::PROMOTE_CANDIDATE;
  }

  if (candidate_age_ms >= MULTISITE_RESCUE_CANDIDATE_TIMEOUT_MS) {
    return MultiSiteRescueDecision::ABANDON_CANDIDATE;
  }

  return MultiSiteRescueDecision::PENDING;
}

#endif
