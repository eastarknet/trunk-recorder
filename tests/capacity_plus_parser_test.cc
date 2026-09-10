#include "systems/capacity_plus_parser.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>

namespace {
int failures = 0;

void expect(bool condition, const std::string &description) {
  if (!condition) {
    std::cerr << "FAIL: " << description << '\n';
    ++failures;
  }
}

double explicit_frequency(const std::map<int, double> &table, int lcn) {
  auto found = table.find(lcn);
  return found == table.end() ? 0 : found->second;
}
}

int main() {
  const std::map<int, double> sfcps = {
      {1, 158797500}, {2, 155175000}, {3, 154845000}};

  // Case A: single segment, LSN 1 and 3.
  const std::array<uint8_t, 8> case_a = {0xC1, 0xA0, 28, 123, 0, 0, 0, 0};
  CapacityPlusSiteStatus a = decode_capacity_plus_site_status(case_a.data(), case_a.size());
  expect(a.segment == 3 && a.rest_lsn == 1, "case A segment/rest LSN");
  expect(a.voice_assignments.size() == 2, "case A assignment count");
  expect(a.voice_assignments[0].talkgroup == 28 && a.voice_assignments[0].channel.lsn == 1 &&
             a.voice_assignments[0].channel.lcn == 1 && a.voice_assignments[0].channel.tdma_slot == 0 &&
             explicit_frequency(sfcps, a.voice_assignments[0].channel.lcn) == 158797500,
         "case A TG 28 route");
  expect(a.voice_assignments[1].talkgroup == 123 && a.voice_assignments[1].channel.lsn == 3 &&
             a.voice_assignments[1].channel.lcn == 2 && a.voice_assignments[1].channel.tdma_slot == 0 &&
             explicit_frequency(sfcps, a.voice_assignments[1].channel.lcn) == 155175000,
         "case A TG 123 route");

  // Case B: opposite TDMA slots, LSN 2 and 6.
  const std::array<uint8_t, 8> case_b = {0xC1, 0x44, 28, 220, 0, 0, 0, 0};
  CapacityPlusSiteStatus b = decode_capacity_plus_site_status(case_b.data(), case_b.size());
  expect(b.voice_assignments.size() == 2 && b.voice_assignments[0].channel.lcn == 1 &&
             b.voice_assignments[0].channel.tdma_slot == 1 &&
             b.voice_assignments[1].channel.lcn == 3 && b.voice_assignments[1].channel.tdma_slot == 1,
         "case B LSN 2/6 mapping");

  // Case C: continuation and last segments suppress independent voice grants.
  const std::array<uint8_t, 8> continuation = {0x01, 0x80, 28, 0, 0, 0, 0, 0};
  const std::array<uint8_t, 8> last = {0x41, 0x80, 28, 0, 0, 0, 0, 0};
  CapacityPlusSiteStatus c0 = decode_capacity_plus_site_status(continuation.data(), continuation.size());
  CapacityPlusSiteStatus c1 = decode_capacity_plus_site_status(last.data(), last.size());
  expect(c0.voice_assignments.empty() && c1.voice_assignments.empty() &&
             c0.rest_lsn == 1 && c1.rest_lsn == 1,
         "case C fragments suppress grants but retain rest LSN");

  // Case D: an explicit table lookup cannot invent an unmapped LCN frequency.
  CapacityPlusChannel unmapped;
  expect(capacity_plus_lsn_to_channel(7, unmapped) && unmapped.lcn == 4 &&
             explicit_frequency(sfcps, unmapped.lcn) == 0,
         "case D unmapped LCN is unresolved");

  // Case E: repeated rest frequency announcements do not request retuning.
  expect(!capacity_plus_rest_retune_needed(158797500, 158797500) &&
             capacity_plus_rest_retune_needed(155175000, 158797500) &&
             !capacity_plus_rest_retune_needed(155175000, 0),
         "case E repeated/unmapped rest suppression");

  // Case F: consume low destinations, then parse the high bitmap safely.
  const std::array<uint8_t, 8> case_f = {0xC1, 0x80, 1, 0x40, 220, 0, 0, 0};
  CapacityPlusSiteStatus f = decode_capacity_plus_site_status(case_f.data(), case_f.size());
  expect(f.voice_assignments.size() == 2 && f.voice_assignments[1].talkgroup == 220 &&
             f.voice_assignments[1].channel.lsn == 10 && f.voice_assignments[1].channel.lcn == 5 &&
             f.voice_assignments[1].channel.tdma_slot == 1,
         "case F high LSN bitmap and mapping");
  const std::array<uint8_t, 3> truncated = {0xC1, 0x80, 28};
  CapacityPlusSiteStatus bounded = decode_capacity_plus_site_status(truncated.data(), truncated.size());
  expect(bounded.voice_assignments.size() == 1, "case F bounded payload read");

  // OP25 emits SLCO followed by d0, d1, and d2. The Capacity Plus rest LSN
  // occupies the low five bits of d1, regardless of d0 or d1's upper bits.
  const std::array<uint8_t, 4> op25_slc = {0x0F, 0x07, 0xED, 0xA5};
  expect(capacity_plus_rest_lsn_from_op25_slc(op25_slc.data()) == 13,
         "OP25 CACH SLC uses d1 low five bits rather than d0");
  const std::array<uint8_t, 4> op25_slc_lsn16 = {0x0F, 0x03, 0xB0, 0x5A};
  expect(capacity_plus_rest_lsn_from_op25_slc(op25_slc_lsn16.data()) == 16,
         "OP25 CACH SLC preserves five-bit rest LSN 16");

  if (failures != 0) return EXIT_FAILURE;
  std::cout << "Capacity Plus parser tests passed\n";
  return EXIT_SUCCESS;
}
