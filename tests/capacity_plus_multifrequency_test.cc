#include "systems/capacity_plus_parser.h"
#include "systems/dmr_parser.h"
#include "systems/system_impl.h"

#include <cstdlib>
#include <gnuradio/message.h>
#include <iostream>
#include <string>
#include <vector>

namespace {
int failures = 0;

void expect(bool condition, const char *description) {
  if (!condition) {
    std::cerr << "FAIL: " << description << '\n';
    ++failures;
  }
}

gr::message::sptr dmr_message(
    int type, int rxid, int slot,
    const std::vector<uint8_t> &payload,
    int cc = -1, int crc_status = -1) {
  const long packed_type =
      (1L << 16) | type;

  uint64_t packed_rxid =
      static_cast<uint64_t>(
          static_cast<uint32_t>(
              (rxid << 1) | slot));

  if (cc >= 0 && cc <= 15) {
    packed_rxid |= (1ULL << 36);
    packed_rxid |=
        (static_cast<uint64_t>(
             cc & 0x0f)
         << 32);
  }

  if (crc_status >= 0) {
    packed_rxid |= (1ULL << 37);

    if (crc_status != 0)
      packed_rxid |= (1ULL << 38);
  }

  return gr::message::make_from_string(
      std::string(
          reinterpret_cast<const char *>(
              payload.data()),
          payload.size()),
      packed_type,
      static_cast<long>(packed_rxid),
      0);
}
}  // namespace

int main() {
  System_impl system(7);
  system.set_system_type("dmr");
  system.add_control_channel(451000000);
  system.add_control_channel(452000000);
  system.add_control_channel(453000000);

  expect(!system.get_capacity_plus_multi_frequency(),
         "capacityPlusMultiFrequency defaults false");
  double frequency = 0;
  expect(system.resolve_dmr_monitor_frequency(99, frequency) &&
             frequency == 451000000,
         "legacy DMR attribution uses current control channel regardless of rxid");
  expect(capacity_plus_rest_signaling_retune_allowed(false),
         "legacy singleton rest retune behavior remains available");

  system.set_capacity_plus_multi_frequency(true);
  expect(system.get_capacity_plus_multi_frequency(),
         "capacityPlusMultiFrequency accepts true for DMR");
  Source *shared_source = reinterpret_cast<Source *>(0x1);
  Source *other_source = reinterpret_cast<Source *>(0x2);
  system.dmr_signaling_monitors.emplace_back(0, 451000000, shared_source);
  system.dmr_signaling_monitors.emplace_back(1, 452000000, shared_source);
  system.dmr_signaling_monitors.emplace_back(2, 453000000, other_source);

  for (int rxid = 0; rxid < 3; ++rxid) {
    frequency = 0;
    expect(system.resolve_dmr_monitor_frequency(rxid, frequency) &&
               frequency == 451000000 + rxid * 1000000,
           "monitor rxid resolves its distinct receive frequency");
  }
  expect(system.dmr_signaling_monitors[0].rxid == 0 &&
             system.dmr_signaling_monitors[1].rxid == 1 &&
             system.dmr_signaling_monitors[2].rxid == 2,
         "multi-frequency monitors use sequential per-system rxids");
  expect(system.dmr_signaling_monitors[0].source ==
             system.dmr_signaling_monitors[1].source,
         "two signaling frequencies may share one source");
  expect(system.dmr_signaling_monitors[2].source !=
             system.dmr_signaling_monitors[1].source,
         "another signaling frequency may use a different source");
  frequency = 123;
  expect(!system.resolve_dmr_monitor_frequency(-1, frequency) && frequency == 123 &&
             !system.resolve_dmr_monitor_frequency(3, frequency) && frequency == 123,
         "invalid monitor rxids fail without changing the output frequency");
  expect(!capacity_plus_rest_signaling_retune_allowed(true),
         "multi-frequency rest events cannot request signaling retunes");

  const std::vector<DmrSignalingSourceCoverage> allowed_coverage = {
      {450000000, 452500000}, {452750000, 454000000}};
  expect(find_dmr_signaling_source(451000000, allowed_coverage) == 0 &&
             find_dmr_signaling_source(452000000, allowed_coverage) == 0,
         "two monitor frequencies can select the same allowed source");
  expect(find_dmr_signaling_source(453000000, allowed_coverage) == 1,
         "a third monitor can select another allowed source");
  expect(find_dmr_signaling_source(455000000, allowed_coverage) == -1,
         "frequency outside allowed source coverage fails safely");
  const std::vector<DmrSignalingSourceCoverage> restricted_coverage = {
      {450000000, 452500000}};
  expect(find_dmr_signaling_source(453000000, restricted_coverage) == -1,
         "selection cannot borrow a covering source omitted by sources[]");

  DmrParser parser;
  const std::vector<uint8_t> vlc = {0x00, 0x00, 0x00, 0x00, 0x00,
                                    0x2a, 0x00, 0x00, 0x63};
  std::vector<TrunkMessage> messages =
      parser.parse_message(dmr_message(3, 1, 0, vlc), &system);
  expect(messages.size() == 1 && messages[0].freq == 452000000,
         "VLC frequency attribution follows the receiving monitor rxid");
  messages = parser.parse_message(dmr_message(4, 2, 1, vlc), &system);
  expect(messages.size() == 1 && messages[0].freq == 453000000,
         "TLC frequency attribution follows the receiving monitor rxid");
  messages = parser.parse_message(dmr_message(7, 0, 0, vlc), &system);
  expect(messages.size() == 1 && messages[0].freq == 451000000,
         "ELC frequency attribution follows the receiving monitor rxid");
  messages = parser.parse_message(dmr_message(3, 9, 0, vlc), &system);
  expect(messages.size() == 1 && messages[0].freq == 0,
         "unknown rxid does not receive a random parser frequency");

  const std::vector<uint8_t> tier3_aloha_csbk = {
      0x19, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00};

  messages = parser.parse_message(
      dmr_message(
          5, 1, 0,
          tier3_aloha_csbk, 9, 1),
      &system);

  expect(
      messages.size() == 1,
      "CRC-ok CSBK remains accepted");

  messages = parser.parse_message(
      dmr_message(
          5, 1, 0,
          tier3_aloha_csbk, 9, -1),
      &system);

  expect(
      messages.size() == 1,
      "CRC-unknown CSBK remains accepted for compatibility");

  messages = parser.parse_message(
      dmr_message(
          5, 1, 0,
          tier3_aloha_csbk, 9, 0),
      &system);

  expect(
      messages.empty(),
      "CRC-fail CSBK is rejected before decoding");

  system.mark_dmr_capplus_activity(1);
  expect(system.dmr_signaling_monitors[0].last_capplus_activity == 0 &&
             system.dmr_signaling_monitors[1].last_capplus_activity > 0 &&
             system.dmr_signaling_monitors[2].last_capplus_activity == 0,
         "valid Capacity Plus health is attributed only to its monitor");

  system.set_capacity_plus_multi_frequency(false);
  system.select_control_channel(453000000);
  messages = parser.parse_message(dmr_message(3, 99, 0, vlc), &system);
  expect(messages.size() == 1 && messages[0].freq == 453000000,
         "legacy VLC attribution continues to use current control channel");

  if (failures) return EXIT_FAILURE;
  std::cout << "Capacity Plus multi-frequency tests passed\n";
  return EXIT_SUCCESS;
}
