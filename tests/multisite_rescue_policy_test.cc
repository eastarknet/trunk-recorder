#include "../trunk-recorder/multisite_rescue_policy.h"

#include <cassert>

int main() {
  assert(!should_start_multisite_rescue(0.0, 0.99, 1000));
  assert(!should_start_multisite_rescue(0.0, 1.00, 999));
  assert(!should_start_multisite_rescue(0.01, 10.0, 10000));
  assert(should_start_multisite_rescue(0.0, 1.00, 1000));
  assert(should_start_multisite_rescue(0.0, 2.50, 2500));

  assert(evaluate_multisite_rescue(0.01, 0.50, 100) ==
         MultiSiteRescueDecision::KEEP_PRIMARY);
  assert(evaluate_multisite_rescue(0.01, 0.00, 2000) ==
         MultiSiteRescueDecision::KEEP_PRIMARY);
  assert(evaluate_multisite_rescue(0.00, 0.01, 100) ==
         MultiSiteRescueDecision::PROMOTE_CANDIDATE);
  assert(evaluate_multisite_rescue(0.00, 0.00, 1499) ==
         MultiSiteRescueDecision::PENDING);
  assert(evaluate_multisite_rescue(0.00, 0.00, 1500) ==
         MultiSiteRescueDecision::ABANDON_CANDIDATE);

  // If both begin writing before the management loop runs, prefer the
  // original recorder. Version 1 is a dead-primary rescue, not quality ranking.
  assert(evaluate_multisite_rescue(0.01, 0.01, 100) ==
         MultiSiteRescueDecision::KEEP_PRIMARY);

  return 0;
}
