#include "../trunk-recorder/dmr_timeout_policy.h"

#include <cassert>

int main() {
  // Exact defect: conventional DMR slot remains RECORDING after voice stops
  // because the final TLC/terminate event was missed.
  assert(should_force_conventional_dmr_timeout(
      "conventionalDMR", true, 0.30, 3.01, 3.0));

  // Still inside the timeout window.
  assert(!should_force_conventional_dmr_timeout(
      "conventionalDMR", true, 0.30, 2.99, 3.0));

  // Preserve strict existing "> timeout" semantics.
  assert(!should_force_conventional_dmr_timeout(
      "conventionalDMR", true, 0.30, 3.00, 3.0));

  // Normal TLC path has already made the sink non-recording.
  assert(!should_force_conventional_dmr_timeout(
      "conventionalDMR", false, 0.30, 30.0, 3.0));

  // No actual audio was ever recorded.
  assert(!should_force_conventional_dmr_timeout(
      "conventionalDMR", true, 0.0, 30.0, 3.0));

  // Do not alter conventional P25/analog behavior.
  assert(!should_force_conventional_dmr_timeout(
      "conventionalP25", true, 0.30, 30.0, 3.0));

  assert(!should_force_conventional_dmr_timeout(
      "conventional", true, 0.30, 30.0, 3.0));

  return 0;
}
