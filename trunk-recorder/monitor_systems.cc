#include "monitor_systems.h"
#include <algorithm>
#include "recorders/p25_recorder.h"
#include "systems/capacity_plus_parser.h"
#include "systems/dmr_parser.h"
#include "dmr_timeout_policy.h"
#include <chrono>
#include <cmath>
#include <map>
#include <boost/log/sinks/text_file_backend.hpp>
#include <boost/log/core.hpp>

using namespace std;

// External reference to global log sink for SIGHUP rotation
extern boost::shared_ptr<boost::log::sinks::synchronous_sink<boost::log::sinks::text_file_backend>> global_log_sink;

volatile sig_atomic_t exit_flag = 0;
volatile sig_atomic_t rotate_log_flag = 0;
int exit_code = EXIT_SUCCESS;

void exit_interupt(int sig) { // can be called asynchronously
  exit_flag = 1;              // set flag
}

void rotate_log_signal(int sig) { // can be called asynchronously
  rotate_log_flag = 1;          // set flag
}

uint64_t time_since_epoch_millisec() {
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

bool start_recorder(Call *call, TrunkMessage message, Config &config, System *sys, std::vector<Source *> &sources) {
  Talkgroup *talkgroup = sys->find_talkgroup(call->get_talkgroup());

  bool source_found = false;
  bool recorder_found = false;
  bool override_record_unknown = false;

  Recorder *recorder;
  Recorder *debug_recorder;
  Recorder *sigmf_recorder;

  if (!talkgroup){
    BOOST_FOREACH (auto &TGID, sys->get_talkgroup_patch(call->get_talkgroup())) {  //for each talkgroup in the patch
      if (sys->find_talkgroup(TGID) != NULL){  //if the patched talkgroup is known
        override_record_unknown = true;
        std::string loghdr = log_header( call->get_short_name(), call->get_call_num(), call->get_talkgroup_display(), call->get_freq());
        BOOST_LOG_TRIVIAL(info) << loghdr << "\u001b[33mEnabling recording of TG not in Talkgroup File due to active supergroup patch\u001b[0m ";
      }
    }
  }

  if (!talkgroup && (sys->get_record_unknown() == false) && override_record_unknown == false) {
    call->set_state(MONITORING);
    call->set_monitoring_state(UNKNOWN_TG);
    if (sys->get_hideUnknown() == false) {
      std::string loghdr = log_header( call->get_short_name(), call->get_call_num(), call->get_talkgroup_display(), call->get_freq());
      BOOST_LOG_TRIVIAL(info) << loghdr << "\u001b[33mNot Recording: TG not in Talkgroup File\u001b[0m ";
    }
    return false;
  }

  if (talkgroup) {
    call->set_talkgroup_tag(talkgroup->alpha_tag);
  } else {
    call->set_talkgroup_tag("-");
  }

  if (call->get_encrypted() == true || (talkgroup && (talkgroup->mode.compare("E") == 0 || talkgroup->mode.compare("TE") == 0 || talkgroup->mode.compare("DE") == 0))) {
    if (talkgroup && (talkgroup->mode.compare("E") == 0 || talkgroup->mode.compare("TE") == 0 || talkgroup->mode.compare("DE") == 0)) {
      call->set_encrypted(true);
    }
    
    if (!sys->get_monitorEncrypted()) {
      call->set_state(MONITORING);
      call->set_monitoring_state(ENCRYPTED);
      if (sys->get_hideEncrypted() == false) {
        long unit_id = call->get_current_source_id();
        std::string tag = sys->find_unit_tag(unit_id);
        if (tag != "") {
          tag = " (\033[0;34m" + tag + "\033[0m)";
        }
        std::string loghdr = log_header( sys->get_short_name(), call->get_call_num(), call->get_talkgroup_display(), call->get_freq());
        BOOST_LOG_TRIVIAL(info) << loghdr << "\u001b[31mNot Recording: ENCRYPTED\u001b[0m - src: " << unit_id << tag;
      }
      return false;
    }
  }

  std::vector<Source *> candidate_sources;
  if (sys->get_source_nums().empty()) {
    candidate_sources = sources;
  } else {
    for (int source_num : sys->get_source_nums()) {
      if (source_num >= 0 && source_num < static_cast<int>(sources.size())) {
        candidate_sources.push_back(sources[source_num]);
      } else {
        BOOST_LOG_TRIVIAL(error) << "[" << sys->get_short_name()
                                 << "]\tConfigured source index out of range during recorder selection: "
                                 << source_num;
      }
    }
  }

  candidate_sources.erase(
      std::remove_if(candidate_sources.begin(), candidate_sources.end(), [call](Source *source) {
        return source->get_min_hz() > call->get_freq() || source->get_max_hz() < call->get_freq();
      }),
      candidate_sources.end());
  source_found = !candidate_sources.empty();

  std::sort(candidate_sources.begin(), candidate_sources.end(), [call](Source *a, Source *b) {
    const double a_distance = std::abs(a->get_center() - call->get_freq());
    const double b_distance = std::abs(b->get_center() - call->get_freq());
    return a_distance != b_distance ? a_distance < b_distance : a->get_num() < b->get_num();
  });

  int priority = talkgroup ? talkgroup->get_priority() : 0;
  if (talkgroup) {
    BOOST_FOREACH (auto &TGID, sys->get_talkgroup_patch(call->get_talkgroup())) {
      Talkgroup *patched_talkgroup = sys->find_talkgroup(TGID);
      if (patched_talkgroup && patched_talkgroup->get_priority() < priority) {
        priority = patched_talkgroup->get_priority();
        BOOST_LOG_TRIVIAL(info) << "Temporarily increased priority of talkgroup "
                                << call->get_talkgroup() << " to " << priority
                                << " due to active patch with talkgroup " << TGID;
      }
    }
  }

  for (Source *source : candidate_sources) {
    recorder = nullptr;
    call->set_is_analog(false);

    if (talkgroup) {
      if (talkgroup->mode.compare("A") == 0) {
        recorder = source->get_analog_recorder(talkgroup, priority, call);
        call->set_is_analog(true);
      } else if (sys->get_system_type() == "dmr") {
        recorder = source->get_dmr_recorder(talkgroup, priority, call);
      } else {
        recorder = source->get_digital_recorder(talkgroup, priority, call);
      }
    } else {
      std::string loghdr = log_header(call->get_short_name(), call->get_call_num(),
                                      call->get_talkgroup_display(), call->get_freq());
      BOOST_LOG_TRIVIAL(info) << loghdr << "TG not in Talkgroup File ";
      if ((config.default_mode == "analog") && (sys->get_system_type() == "smartnet")) {
        recorder = source->get_analog_recorder(call);
        call->set_is_analog(true);
      } else if (sys->get_system_type() == "dmr") {
        recorder = source->get_dmr_recorder(call);
      } else {
        recorder = source->get_digital_recorder(call);
      }
    }

    if (!recorder) continue;
    if (message.meta.length()) BOOST_LOG_TRIVIAL(trace) << message.meta;
    if (!recorder->start(call)) {
      call->set_state(MONITORING);
      continue;
    }

    call->set_recorder(recorder);
    call->set_state(RECORDING);
    plugman_setup_recorder(recorder);
    recorder_found = true;

    debug_recorder = source->get_debug_recorder();
    if (debug_recorder && debug_recorder->start(call)) {
      call->set_debug_recorder(debug_recorder);
      call->set_debug_recording(true);
      plugman_setup_recorder(debug_recorder);
    }

    sigmf_recorder = source->get_sigmf_recorder();
    if (sigmf_recorder && sigmf_recorder->start(call)) {
      call->set_sigmf_recorder(sigmf_recorder);
      call->set_sigmf_recording(true);
      plugman_setup_recorder(sigmf_recorder);
    }
    return true;
  }

  if (source_found && !recorder_found) {
    call->set_state(MONITORING);
    call->set_monitoring_state(NO_RECORDER);
    std::string loghdr = log_header(call->get_short_name(), call->get_call_num(),
                                    call->get_talkgroup_display(), call->get_freq());
    BOOST_LOG_TRIVIAL(error) << loghdr
        << "\u001b[36mNot Recording: no recorder available on any allowed source covering Freq\u001b[0m";
    return false;
  }

  if (!source_found) {
    call->set_state(MONITORING);
    call->set_monitoring_state(NO_SOURCE);
    std::string loghdr = log_header( call->get_short_name(), call->get_call_num(), call->get_talkgroup_display(), call->get_freq());
    BOOST_LOG_TRIVIAL(error) << loghdr << "\u001b[36mNot Recording: no allowed source covering Freq\u001b[0m";
    return false;
  }
  return false;
}

void print_status(std::vector<Source *> &sources, std::vector<System *> &systems, std::vector<Call *> &calls) {
  BOOST_LOG_TRIVIAL(info) << "Active Calls: " << calls.size();

  for (vector<Call *>::iterator it = calls.begin(); it != calls.end(); it++) {
    Call *call = *it;
    Recorder *recorder = call->get_recorder();
    std::string loghdr = log_header( call->get_short_name(), call->get_call_num(), call->get_talkgroup_display(), call->get_freq());
    if (call->get_state() == MONITORING) {
      BOOST_LOG_TRIVIAL(info) << loghdr << "Elapsed: " << std::setw(4) << call->elapsed() << " State: " << format_state(call->get_state(), call->get_monitoring_state());
    } else {
      if (call->is_conventional() ) {
        bool is_enabled = call->get_recorder()->is_enabled();
         BOOST_LOG_TRIVIAL(info) << loghdr << "Elapsed: " << std::setw(4) << call->elapsed() << " State: " << format_state(call->get_state()) << " Enabled: " << is_enabled;
      } else {
        BOOST_LOG_TRIVIAL(info) << loghdr << "Elapsed: " << std::setw(4) << call->elapsed() << " State: " << format_state(call->get_state());
      }
    }

    if (recorder) {
        BOOST_LOG_TRIVIAL(info) << "\t[ " << std::setw(2) << recorder->get_num() << " ] State: " << format_state(recorder->get_state());
    }
  }

  BOOST_LOG_TRIVIAL(info) << "Active Patches: ";
  for (std::vector<System *>::iterator it = systems.begin(); it != systems.end(); ++it) {
    System_impl *sys = (System_impl *)*it;

    sys->print_active_talkgroup_patches();
  }

  BOOST_LOG_TRIVIAL(info) << "Control Channel Decode Rates: ";
  for (std::vector<System *>::iterator it = systems.begin(); it != systems.end(); ++it) {
    System_impl *sys = (System_impl *)*it;

    if ((sys->get_system_type() != "conventional") && (sys->get_system_type() != "conventionalP25") && (sys->get_system_type() != "conventionalDMR") && (sys->get_system_type() != "conventionalSIGMF")) {
      BOOST_LOG_TRIVIAL(info) << "[" << sys->get_short_name() << "]\t" << format_freq(sys->get_current_control_channel()) << "\t" << sys->get_decode_rate() << " msg/sec";
      
      if ((sys->get_source()->get_autotune_source()) && (sys->get_system_type() == "p25")) {
        // If control channel source has autotune enabled, perform autotune adjustments and log to console
        autotune_control_channel(sys);
      }
    }
  }

  BOOST_LOG_TRIVIAL(info) << "Recorders: ";

  for (vector<Source *>::iterator it = sources.begin(); it != sources.end(); it++) {
    Source *source = *it;
    source->print_recorders();
  }
}

void manage_conventional_call(Call *call, Config &config) {
  Recorder *recorder = call->get_recorder();
  if (!recorder) return;

  // All lifecycle queries are slot-aware. For non-DMR recorders the slot
  // argument is ignored (the base Recorder defaults delegate to the slot-less
  // method). For DMR this routes to the right transmission_sink so two calls
  // sharing a recorder don't see each other's idle/length state.
  int slot = call->get_tdma_slot();
  const double current_length = call->get_current_length();
  const double seconds_since_last_write = recorder->since_last_write(slot);
  const State recorder_state = recorder->get_state(slot);

  // DMR normally transitions the slot to IDLE when a valid Terminator-with-LC
  // produces a terminate stream tag. RF loss or an undecodable final TLC can
  // leave the sink RECORDING forever, however. Use the sink's monotonic
  // last-write clock as a bounded fallback so an otherwise-complete
  // conventional DMR transmission cannot remain attached for hours.
  if (should_force_conventional_dmr_timeout(
          call->get_system_type(),
          recorder_state == RECORDING,
          current_length,
          seconds_since_last_write,
          config.call_timeout)) {
    BOOST_LOG_TRIVIAL(warning)
        << "[" << call->get_short_name() << "]\t"
        << call->get_call_num() << "C"
        << "\tConventional DMR missing-termination fallback"
        << "\tSlot: " << slot
        << "\tLength: " << current_length
        << "\tNo PCM for: " << seconds_since_last_write << "s"
        << "\tTimeout: " << config.call_timeout << "s";

    call->conclude_call();
    call->restart_call();
    plugman_setup_recorder(recorder);
    plugman_call_start(call);
    return;
  }

  if (current_length > 0) {
    BOOST_LOG_TRIVIAL(trace) << "[" << call->get_short_name() << "]\t\033[0;34m" << call->get_call_num() << "C\033[0m Call Length: " << call->get_current_length() << "s\t Idle: " << recorder->is_idle(slot) << "\t Squelched: " << recorder->is_squelched() << " Idle Count: " << call->get_idle_count();

    if (recorder->is_idle(slot)) {
      call->set_noise(recorder->get_pwr());
      call->increase_idle_count();
    } else {
      call->set_signal(recorder->get_pwr());
      if (call->get_idle_count() > 0) {
        call->reset_idle_count();
      }
    }

    if (call->get_idle_count() > config.call_timeout) {
      call->conclude_call();
      call->restart_call();
      plugman_setup_recorder(recorder);
      plugman_call_start(call);
    } else if ((current_length > call->get_system()->get_max_duration()) && (call->get_system()->get_max_duration() > 0)) {
      call->conclude_call();
      call->restart_call();
      plugman_setup_recorder(recorder);
      plugman_call_start(call);
    }
  } else if (!recorder->is_active(slot)) {
    // Conventional P25 and DMR recorders are started here (not in setup) because
    // the flowgraph has to be unlocked first. For DMR each slot's call hits
    // this branch independently and starts only its own slot.
    recorder->start(call);
    call->set_state(RECORDING);
    plugman_call_start(call);
    BOOST_LOG_TRIVIAL(trace) << "[" << call->get_short_name() << "]\t\033[0;34m" << call->get_call_num() << "C\033[0m Starting Conventional Recorder slot " << slot;
  }
}

void manage_calls(Config &config, std::vector<Call *> &calls) {
  bool ended_call = false;
  for (vector<Call *>::iterator it = calls.begin(); it != calls.end();) {
    Call *call = *it;
    State state = call->get_state();
    // Handle Conventional Calls
    if (call->is_conventional()) {
      manage_conventional_call(call, config);
      ++it;
      continue;
    }

    // Handle Trunked Calls

    if ((state == MONITORING) && (call->since_last_update() > config.call_timeout)) {
      ended_call = true;
      it = calls.erase(it);
      delete call;
      continue;
    }

    if (state == RECORDING) {
      Recorder *recorder = call->get_recorder();

      // Stop the call if:
      // - there hasn't been an UPDATE for it on the Control Channel in X seconds AND the recorder hasn't written anything in X seconds

      int slot = call->get_tdma_slot();
      if ((recorder->since_last_write(slot) > config.call_timeout) && (call->since_last_update() > config.call_timeout)) {
        std::string loghdr = log_header( call->get_short_name(), call->get_call_num(), call->get_talkgroup_display(), call->get_freq());
        BOOST_LOG_TRIVIAL(trace) << loghdr << "\u001b[36m Stopping Call because of Recorder \u001b[0m Rec last write: " << recorder->since_last_write(slot) << " State: " << format_state(recorder->get_state(slot));
        call->conclude_call();
        // The State of the Recorders has changed, so lets send an update
        ended_call = true;
        if (recorder != NULL) {
          plugman_setup_recorder(recorder);
        }
        it = calls.erase(it);
        delete call;
        continue;
      }
    } else if (call->since_last_update() > config.call_timeout) {
      Recorder *recorder = call->get_recorder();
      int slot = call->get_tdma_slot();
      std::string loghdr = log_header( call->get_short_name(), call->get_call_num(), call->get_talkgroup_display(), call->get_freq());
      BOOST_LOG_TRIVIAL(trace) << loghdr << "\u001b[36m  Call UPDATEs has been inactive for more than " << config.call_timeout << " Sec \u001b[0m Rec last write: " << recorder->since_last_write(slot) << " State: " << format_state(recorder->get_state(slot));
    }
    ++it;
  } // foreach loggers

  if (ended_call) {
    plugman_calls_active(calls);
  }
}

void current_system_status(TrunkMessage message, System *sys) {
  if (sys->update_status(message)) {
    plugman_setup_system(sys);
  }
}

void current_system_sysid(TrunkMessage message, System *sys) {
  if ((sys->get_system_type() == "p25") || (sys->get_system_type() == "conventionalP25")) {
    if (sys->update_sysid(message)) {
      plugman_setup_system(sys);
    }
  }
}

void unit_registration(System *sys, long source_id) {
  plugman_unit_registration(sys, source_id);
}

void unit_deregistration(System *sys, long source_id) {
  plugman_unit_deregistration(sys, source_id);
}

void unit_acknowledge_response(System *sys, long source_id) {
  plugman_unit_acknowledge_response(sys, source_id);
}

void unit_group_affiliation(System *sys, long source_id, long talkgroup_num) {
  plugman_unit_group_affiliation(sys, source_id, talkgroup_num);
}

void unit_data_grant(System *sys, long source_id) {
  plugman_unit_data_grant(sys, source_id);
}

void unit_answer_request(System *sys, long source_id, long talkgroup) {
  plugman_unit_answer_request(sys, source_id, talkgroup);
}

void unit_call_alert(System *sys, long source_id, long talkgroup) {
  plugman_unit_call_alert(sys, source_id, talkgroup);
}

void unit_location(System *sys, long source_id, long talkgroup_num) {
  plugman_unit_location(sys, source_id, talkgroup_num);
}




void handle_call_grant(TrunkMessage message, System *sys, bool grant_message, Config &config, std::vector<Source *> &sources, std::vector<Call *> &calls) {
  bool call_found = false;
  bool duplicate_grant = false;
  bool superseding_grant = false;
  bool recording_started [[maybe_unused]] = false;

  Call *original_call;

  /* Notes: it is possible for 2 Calls to exist for the same talkgroup on different freq. This happens when a Talkgroup starts on a freq
  that current recorder can't retune to. In this case, the current orig Talkgroup reocrder will keep going on the old freq, while a new
  recorder is start on a source that can cover that freq. This makes sure any of the remaining transmission that it is in the buffer
  of the original recorder gets flushed.
  UPDATED: however if we have 2 different talkgroups on the same freq we should do a stop_call on the original call since it is being used by another TG now. This will let the recorder keep
  going until it gets a termination flag.
  */

  // BOOST_LOG_TRIVIAL(info) << "TG: " << message.talkgroup << " sys num: " << message.sys_num << " freq: " << message.freq << " TDMA Slot" << message.tdma_slot << " TDMA: " << message.phase2_tdma;

  unsigned long message_preferredNAC = 0;
  unsigned long call_rfss_site = 0;
  unsigned long sys_rfss_site = 0;

  Talkgroup *message_talkgroup = sys->find_talkgroup(message.talkgroup);
  if (message_talkgroup) {
    message_preferredNAC = message_talkgroup->get_preferredNAC();
  }

  if (std::find(config.record_deny_talkgroups.begin(), config.record_deny_talkgroups.end(),
                message.talkgroup) != config.record_deny_talkgroups.end()) {
    static std::map<std::pair<int, unsigned long>, std::chrono::steady_clock::time_point> last_deny_log;
    const std::pair<int, unsigned long> deny_key(sys->get_sys_num(), message.talkgroup);
    const auto now = std::chrono::steady_clock::now();
    const auto previous = last_deny_log.find(deny_key);
    if (previous == last_deny_log.end() ||
        now - previous->second >= std::chrono::minutes(1)) {
      BOOST_LOG_TRIVIAL(info) << "[" << sys->get_short_name()
                              << "]\tSkipping denied recording talkgroup "
                              << message.talkgroup;
      last_deny_log[deny_key] = now;
    }
    return;
  }

  for (vector<Call *>::iterator it = calls.begin(); it != calls.end();) {
    Call *call = *it;

    /* This is for Multi-Site support */
    // Find candidate duplicate calls with the same talkgroup and different multisite-enabled systems
    if (call->get_talkgroup() == message.talkgroup) {
      if (call->get_sys_num() != message.sys_num) {
        if (call->get_system()->get_multiSite() && sys->get_multiSite()) {
          if (call->get_system()->get_wacn() == sys->get_wacn()) {
            // Default mode to match WACN and use RFSS/Site to identify duplicate calls
            sys_rfss_site = sys->get_sys_rfss() * 10000 + sys->get_sys_site_id();
            call_rfss_site = call->get_system()->get_sys_rfss() * 10000 + call->get_system()->get_sys_site_id();
            if ((sys_rfss_site != call_rfss_site) && (call->get_system()->get_multiSiteSystemName() == "")) {
              if (call->get_state() == RECORDING) {

                duplicate_grant = true;
                original_call = call;

                unsigned long call_preferredNAC = 0;
                Talkgroup *call_talkgroup = call->get_system()->find_talkgroup(message.talkgroup);
                if (call_talkgroup) {
                  call_preferredNAC = call_talkgroup->get_preferredNAC();
                }

                // Evaluate superseding grants by comparing call NAC or RFSS-Site against preferred NAC/site in talkgroup .csv
                if ((call_preferredNAC != call->get_system()->get_nac()) && (message_preferredNAC == sys->get_nac())) {
                  superseding_grant = true;
                } else if ((call_preferredNAC != call_rfss_site) && (message_preferredNAC == sys_rfss_site)) {
                  superseding_grant = true;
                }
              }
            }

            // Secondary mode to match multiSiteSystemName and use multiSiteSystemNumber.
            // If a multiSiteSystemName has been manually entered;
            // We already know that Call's system number does not match the message system number.
            // In this case, we check that the multiSiteSystemName is present, and that the Call and System multiSiteSystemNames are the same.
            else if ((call->get_system()->get_multiSiteSystemName() != "") && (call->get_system()->get_multiSiteSystemName() == sys->get_multiSiteSystemName())) {
              if (call->get_state() == RECORDING) {

                duplicate_grant = true;
                original_call = call;

                unsigned long call_preferredNAC = 0;
                Talkgroup *call_talkgroup = call->get_system()->find_talkgroup(message.talkgroup);
                if (call_talkgroup) {
                  call_preferredNAC = call_talkgroup->get_preferredNAC();
                }

                if ((call->get_system()->get_multiSiteSystemNumber() != 0) && (sys->get_multiSiteSystemNumber() != 0)) {
                  if ((call_preferredNAC != call->get_system()->get_multiSiteSystemNumber()) && (message_preferredNAC == sys->get_multiSiteSystemNumber())) {
                    superseding_grant = true;
                  }
                }
              }
            }
          }
        }
      }
    }

    if ((call->get_talkgroup() == message.talkgroup) && (call->get_sys_num() == message.sys_num) && (call->get_freq() == message.freq) && (call->get_tdma_slot() == message.tdma_slot) && (call->get_phase2_tdma() == message.phase2_tdma)) {
      call_found = true;
      bool source_updated = call->update(message);
      if (source_updated) {
        plugman_call_start(call);
      }
    }

    // There is an existing call on freq and slot that the new call will be started on. We should stop the older call. The older recorder will
    // keep writing to the file until it hits a termination flag, so no packets should be dropped.
    if ((call->get_state() == RECORDING) && (call->get_talkgroup() != message.talkgroup) && (call->get_sys_num() == message.sys_num) && (call->get_freq() == message.freq) && (call->get_tdma_slot() == message.tdma_slot) && (call->get_phase2_tdma() == message.phase2_tdma)) {
      Recorder *recorder = call->get_recorder();
      string recorder_state = "UNKNOWN";
      if (recorder != NULL) {
        recorder_state = format_state(recorder->get_state());
      }
      std::string loghdr = log_header( call->get_short_name(), call->get_call_num(), call->get_talkgroup_display(), call->get_freq());
      BOOST_LOG_TRIVIAL(trace) << loghdr << "\u001b[36mShould be Stopping RECORDING call, Recorder State: " << recorder_state << " RX overlapping TG message Freq, TG:" << message.talkgroup << "\u001b[0m";
    }

    it++;
  }

  if (!call_found) {
    Call *call = Call::make(message, sys, config);

    Talkgroup *talkgroup = sys->find_talkgroup(call->get_talkgroup());

    if (talkgroup) {
      call->set_talkgroup_tag(talkgroup->alpha_tag);
    } else {
      call->set_talkgroup_tag("-");
    }

    boost::format original_call_data;
    boost::format grant_call_data;

    if ((superseding_grant) || (duplicate_grant)) {
      if (original_call->get_system()->get_multiSiteSystemName() == "") {
        original_call_data = boost::format("\u001b[34m%sC\u001b[0m %X/%s-%s ") % original_call->get_call_num() % original_call->get_system()->get_wacn() % original_call->get_system()->get_sys_rfss() % original_call->get_system()->get_sys_site_id();
        grant_call_data = boost::format("\u001b[34m%sC\u001b[0m %X/%s-%s ") % call->get_call_num() % sys->get_wacn() % sys->get_sys_rfss() % +sys->get_sys_site_id();
      } else {
        original_call_data = boost::format("\u001b[34m%sC\u001b[0m %s/%s ") % original_call->get_call_num() % original_call->get_system()->get_multiSiteSystemName() % original_call->get_system()->get_multiSiteSystemNumber();
        grant_call_data = boost::format("\u001b[34m%sC\u001b[0m %s/%s ") % call->get_call_num() % sys->get_multiSiteSystemName() % sys->get_multiSiteSystemNumber();
      }
    }
    if (superseding_grant) {
      std::string loghdr = log_header( call->get_short_name(), call->get_call_num(), call->get_talkgroup_display(), call->get_freq());

      BOOST_LOG_TRIVIAL(info) << loghdr << "\u001b[36mSuperseding Grant\u001b[0m - Stopping original call: " << original_call_data << "- Superseding call: " << grant_call_data;
      // Attempt to start a new call on the preferred NAC.
      recording_started = start_recorder(call, message, config, sys, sources);

      if (recording_started) {
        // Clean up the original call.
        original_call->set_state(MONITORING);
        original_call->set_monitoring_state(SUPERSEDED);
        original_call->conclude_call();
      } else {

        BOOST_LOG_TRIVIAL(info) << loghdr << "\u001b[36mCould not start Superseding recorder.\u001b[0m Continuing original call: " << original_call->get_call_num() << "C";
      }
    } else if (duplicate_grant) {
      std::string loghdr = log_header( call->get_short_name(), call->get_call_num(), call->get_talkgroup_display(), call->get_freq());
      call->set_state(MONITORING);
      call->set_monitoring_state(DUPLICATE);
      BOOST_LOG_TRIVIAL(info) << loghdr << "\u001b[36mDuplicate Grant\u001b[0m - Not recording: " << grant_call_data << "- Original call: " << original_call_data;
    } else {
      recording_started = start_recorder(call, message, config, sys, sources);
      if (recording_started && !grant_message) {
        std::string loghdr = log_header( call->get_short_name(), call->get_call_num(), call->get_talkgroup_display(), call->get_freq());
        BOOST_LOG_TRIVIAL(info) << loghdr << "\u001b[36mThis was an UPDATE\u001b[0m";
      }
    }
    calls.push_back(call);
    plugman_call_start(call);
    plugman_calls_active(calls);
  }
}

void handle_call_update(TrunkMessage message, System *sys, std::vector<Call *> &calls) {
  bool call_found = false;

  /* Notes: it is possible for 2 Calls to exist for the same talkgroup on different freq. This happens when a Talkgroup starts on a freq
  that current recorder can't retune to. In this case, the current orig Talkgroup reocrder will keep going on the old freq, while a new
  recorder is start on a source that can cover that freq. This makes sure any of the remaining transmission that it is in the buffer
  of the original recorder gets flushed.
  UPDATED: however if we have 2 different talkgroups on the same freq we should do a stop_call on the original call since it is being used by another TG now. This will let the recorder keep
  going until it gets a termination flag.
  */

  for (vector<Call *>::iterator it = calls.begin(); it != calls.end(); ++it) {
    Call *call = *it;

    // BOOST_LOG_TRIVIAL(info) << "TG: " << call->get_talkgroup() << " | " << message.talkgroup << " sys num: " << call->get_sys_num() << " | " << message.sys_num << " freq: " << call->get_freq() << " | " << message.freq << " TDMA Slot" << call->get_tdma_slot() << " | " << message.tdma_slot << " TDMA: " << call->get_phase2_tdma() << " | " << message.phase2_tdma;
    if ((call->get_talkgroup() == message.talkgroup) && (call->get_sys_num() == message.sys_num) && (call->get_freq() == message.freq) && (call->get_tdma_slot() == message.tdma_slot) && (call->get_phase2_tdma() == message.phase2_tdma)) {
      call_found = true;

      if (message.encrypted) {
        call->set_encrypted(true);
      } else {
        Talkgroup *talkgroup = sys->find_talkgroup(message.talkgroup);
        if (talkgroup && (talkgroup->mode.compare("E") == 0 || talkgroup->mode.compare("TE") == 0 || talkgroup->mode.compare("DE") == 0)) {
          call->set_encrypted(true);
        }
      }

      bool source_updated = call->update(message);
      if (source_updated) {
        plugman_call_start(call);
      }
    }
  }

  if (!call_found) {
    // Note: some calls maybe removed before the UPDATEs stop on the trunking channel if there is some GAP in the updates.
    // BOOST_LOG_TRIVIAL(info) << "Call not found for UPDATE mesg - either we missed GRANT or removed Call too soon\tFreq: " << format_freq(message.freq) << "\tTG:" << message.talkgroup << "\tSource: " << message.source << "\tSys Num: " << message.sys_num << "\tTDMA Slot: " << message.tdma_slot << "\tTDMA: " << message.phase2_tdma;
  }
}

void handle_message(std::vector<TrunkMessage> messages, System *sys, Config &config, std::vector<Source *> &sources, std::vector<Call *> &calls, gr::top_block_sptr &tb) {
  for (std::vector<TrunkMessage>::iterator it = messages.begin(); it != messages.end(); it++) {
    TrunkMessage message = *it;

    switch (message.message_type) {
    case GRANT:
      handle_call_grant(message, sys, true, config, sources, calls);
      break;

    case UPDATE:
      if (config.new_call_from_update) {
        // Treat UPDATE as a GRANT and start a new call if we don't have one for this TG
        handle_call_grant(message, sys, false, config, sources, calls);
      } else {
        // Treat UPDATE as an UPDATE and only update existing calls
        handle_call_update(message, sys, calls);
      }
      break;

    case UU_V_GRANT:
      if (config.record_uu_v_calls) {
        handle_call_grant(message, sys, true, config, sources, calls);
      }
      break;

    case UU_V_UPDATE:
      if (config.record_uu_v_calls) {
        handle_call_update(message, sys, calls);
      }
      break;

    case CONTROL_CHANNEL:
      sys->add_control_channel(message.freq);
      break;

    case REGISTRATION:
      unit_registration(sys, message.source);
      break;

    case DEREGISTRATION:
      unit_deregistration(sys, message.source);
      break;

    case AFFILIATION:
      unit_group_affiliation(sys, message.source, message.talkgroup);
      break;

    case SYSID:
      current_system_sysid(message, sys);
      break;

    case STATUS:
      current_system_status(message, sys);
      break;

    case LOCATION:
      unit_location(sys, message.source, message.talkgroup);
      break;

    case ACKNOWLEDGE:
      unit_acknowledge_response(sys, message.source);
      break;

    case PATCH_ADD:
      sys->update_active_talkgroup_patches(message.patch_data);
      break;
    case PATCH_DELETE:
      sys->delete_talkgroup_patch(message.patch_data);
      break;

    case DATA_GRANT:
      unit_data_grant(sys, message.source);
      break;

    case UU_ANS_REQ:
      unit_answer_request(sys, message.source, message.talkgroup);
      break;

    case CALL_ALERT:
      unit_call_alert(sys, message.source, message.talkgroup);
      break;

    case INVALID_CC_MESSAGE:
    {
      //Do not count messages that aren't valid TSBK or MBTs.
      int msg_count = sys->get_message_count();
      if(msg_count > 1){
        sys->set_message_count(msg_count - 1);
      }
      break;
    }

    case TDULC:
      retune_system(sys,tb,sources);
      break;

    case CAPACITY_PLUS_REST_CHANNEL: {
      System_impl *dmr_system = (System_impl *)sys;
      if (!capacity_plus_rest_signaling_retune_allowed(
              dmr_system->get_capacity_plus_multi_frequency())) {
        // Every configured signaling frequency already has a persistent
        // decoder. The parser has updated logical rest state and attributed
        // valid activity to the receiver; an announcement is never a retune
        // request in this mode.
        break;
      }
      const double current_freq = sys->get_current_control_channel();
      const double target_freq = message.freq;
      const long long now_seconds =
          static_cast<long long>(time(NULL));

      // Valid CAP+ on a probe target proves acquisition immediately.
      if (dmr_system->dmr_capplus_probe_active &&
          dmr_system->dmr_capplus_probe_saw_valid) {
        BOOST_LOG_TRIVIAL(info)
            << "[" << sys->get_short_name()
            << "] Capacity Plus rest-channel probe acquired "
            << format_freq(current_freq);

        dmr_system->dmr_capplus_probe_active = false;
        dmr_system->dmr_capplus_probe_previous_freq = 0;
        dmr_system->dmr_capplus_probe_target_freq = 0;
        dmr_system->dmr_capplus_probe_started_at = 0;
        dmr_system->dmr_capplus_probe_saw_non_timeout = false;
        dmr_system->dmr_capplus_probe_saw_valid = false;
      }

      // A rest announcement for the RF channel already being monitored is
      // strong contradictory evidence against any pending cross-frequency
      // candidate. Require matching announcements to be consecutive.
      if (!capacity_plus_rest_retune_needed(current_freq, target_freq)) {
        dmr_system->dmr_capplus_pending_rest_freq = 0;
        dmr_system->dmr_capplus_pending_rest_first_seen = 0;
        dmr_system->dmr_capplus_pending_rest_count = 0;
        break;
      }

      const bool same_candidate =
          dmr_system->dmr_capplus_pending_rest_count > 0 &&
          dmr_system->dmr_capplus_pending_rest_freq == target_freq &&
          capacity_plus_rest_confirmation_window_open(
              now_seconds,
              dmr_system->dmr_capplus_pending_rest_first_seen);

      if (same_candidate) {
        dmr_system->dmr_capplus_pending_rest_count++;
      } else {
        dmr_system->dmr_capplus_pending_rest_freq = target_freq;
        dmr_system->dmr_capplus_pending_rest_first_seen = now_seconds;
        dmr_system->dmr_capplus_pending_rest_count = 1;
      }

      BOOST_LOG_TRIVIAL(debug)
          << "[" << sys->get_short_name()
          << "] Capacity Plus cross-frequency rest candidate "
          << format_freq(target_freq)
          << " confirmation "
          << dmr_system->dmr_capplus_pending_rest_count
          << "/"
          << CAPACITY_PLUS_REST_CONFIRMATIONS_REQUIRED;

      if (dmr_system->dmr_capplus_pending_rest_count <
          CAPACITY_PLUS_REST_CONFIRMATIONS_REQUIRED) {
        break;
      }

      // The current RF channel has just produced valid CAP+ signaling, so it
      // is the known-good fallback if the newly announced rest channel fails.
      const double previous_freq = current_freq;

      dmr_system->dmr_capplus_probe_active = true;
      dmr_system->dmr_capplus_probe_previous_freq = previous_freq;
      dmr_system->dmr_capplus_probe_target_freq = target_freq;
      dmr_system->dmr_capplus_probe_started_at = now_seconds;
      dmr_system->dmr_capplus_probe_saw_non_timeout = false;
      dmr_system->dmr_capplus_probe_saw_valid = false;

      dmr_system->dmr_capplus_pending_rest_freq = 0;
      dmr_system->dmr_capplus_pending_rest_first_seen = 0;
      dmr_system->dmr_capplus_pending_rest_count = 0;

      BOOST_LOG_TRIVIAL(info)
          << "[" << sys->get_short_name()
          << "] Capacity Plus confirmed rest move "
          << format_freq(previous_freq)
          << " -> "
          << format_freq(target_freq)
          << "; starting acquisition probe";

      if (!retune_system_to_frequency(sys, target_freq, tb, sources)) {
        BOOST_LOG_TRIVIAL(error)
            << "[" << sys->get_short_name()
            << "] Capacity Plus confirmed rest move could not retune to "
            << format_freq(target_freq)
            << "; retaining previous channel";

        dmr_system->dmr_capplus_probe_active = false;
        dmr_system->dmr_capplus_probe_previous_freq = 0;
        dmr_system->dmr_capplus_probe_target_freq = 0;
        dmr_system->dmr_capplus_probe_started_at = 0;
        dmr_system->dmr_capplus_probe_saw_non_timeout = false;
        dmr_system->dmr_capplus_probe_saw_valid = false;
      }
      break;
    }

    case UNKNOWN:
      break;
    }
  }
}

void retune_system(System *sys, gr::top_block_sptr &tb, std::vector<Source *> &sources) {
  System_impl *system = (System_impl *)sys;
  if (system->get_system_type() == "dmr" &&
      system->get_capacity_plus_multi_frequency()) {
    BOOST_LOG_TRIVIAL(debug) << "[" << system->get_short_name()
                             << "] Ignoring signaling retune request in "
                                "Capacity Plus multi-frequency mode";
    return;
  }
  double control_channel_freq = system->get_next_control_channel();
  retune_system_to_frequency(sys, control_channel_freq, tb, sources);
}

bool retune_system_to_frequency(System *sys, double control_channel_freq, gr::top_block_sptr &tb, std::vector<Source *> &sources) {
  System_impl *system = (System_impl *)sys;
  if (system->get_system_type() == "dmr" &&
      system->get_capacity_plus_multi_frequency()) {
    BOOST_LOG_TRIVIAL(debug) << "[" << system->get_short_name()
                             << "] Ignoring signaling retune to "
                             << format_freq(control_channel_freq)
                             << " in Capacity Plus multi-frequency mode";
    return false;
  }
  bool source_found = false;
  Source *current_source = system->get_source();

  std::vector<Source *> allowed_sources;
  if (system->get_source_nums().empty()) {
    allowed_sources = sources;
  } else {
    for (int source_num : system->get_source_nums()) {
      if (source_num >= 0 && source_num < static_cast<int>(sources.size())) {
        allowed_sources.push_back(sources[source_num]);
      } else {
        BOOST_LOG_TRIVIAL(error) << "[" << system->get_short_name()
                                 << "]\tConfigured source index out of range during retune: "
                                 << source_num;
      }
    }
  }

  const bool current_source_allowed =
      std::find(allowed_sources.begin(), allowed_sources.end(), current_source) != allowed_sources.end();

  BOOST_LOG_TRIVIAL(error) << "[" << system->get_short_name() << "] Retuning trunking decoder to: " << format_freq(control_channel_freq);

  if (current_source && current_source_allowed &&
      (current_source->get_min_hz() <= control_channel_freq) &&
      (current_source->get_max_hz() >= control_channel_freq)) {
    source_found = true;
    BOOST_LOG_TRIVIAL(info) << "\t - System Source " << current_source->get_num() << " - Min Freq: " << format_freq(current_source->get_min_hz()) << " Max Freq: " << format_freq(current_source->get_max_hz());
    // The source can cover the System's control channel, break out of the
    // For Loop
    if (system->get_system_type() == "smartnet") {
      system->smartnet_trunking->tune_freq(control_channel_freq);
      // Clear demod tracking state so reacquisition isn't biased by whatever
      // the loop converged to on the previous (possibly noise-only) channel.
      system->smartnet_trunking->reset();
    } else if (system->get_system_type() == "p25") {
      system->p25_trunking->tune_freq(control_channel_freq);
    } else if (system->get_system_type() == "dmr") {
      system->dmr_trunking->tune_freq(control_channel_freq);
    } else {
      BOOST_LOG_TRIVIAL(error) << "\t - Unknown system type for Retune";
    }
  } else {
    for (vector<Source *>::iterator src_it = allowed_sources.begin(); src_it != allowed_sources.end(); src_it++) {
      Source *source = *src_it;

      if ((source->get_min_hz() <= control_channel_freq) &&
          (source->get_max_hz() >= control_channel_freq)) {
        source_found = true;
        BOOST_LOG_TRIVIAL(info) << "\t - System Source " << source->get_num() << " - Min Freq: " << format_freq(source->get_min_hz()) << " Max Freq: " << format_freq(source->get_max_hz());

        if (system->get_system_type() == "smartnet") {
          system->set_source(source);
          // We must lock the flow graph in order to disconnect and reconnect blocks
          tb->lock();
          tb->disconnect(current_source->get_src_block(), 0, system->smartnet_trunking, 0);
          // Release the old hier_block2 before constructing the new one so its
          // sub-blocks (prefilter, fsk2_demod, framer) are torn down deterministically
          // instead of overlapping with the replacement.
          system->smartnet_trunking.reset();
          system->smartnet_trunking = smartnet_impl::make(control_channel_freq, source->get_center(), source->get_rate(), system->get_msg_queue(), system->get_sys_num());
          tb->connect(source->get_src_block(), 0, system->smartnet_trunking, 0);
          tb->unlock();
        } else if (system->get_system_type() == "p25") {
            if (system->p25_control_source_selector) {
              const auto selector_source =
                  std::find(
                      system->p25_control_source_selector_sources.begin(),
                      system->p25_control_source_selector_sources.end(),
                      source);

              if (selector_source ==
                  system->p25_control_source_selector_sources.end()) {
                BOOST_LOG_TRIVIAL(error)
                    << "\t - P25 selector has no input for Source "
                    << source->get_num();

                source_found = false;
                break;
              }

              if (!current_source ||
                  source->get_rate() !=
                      current_source->get_rate()) {
                BOOST_LOG_TRIVIAL(error)
                    << "\t - Refusing persistent P25 source "
                    << "switch across different sample rates";

                source_found = false;
                break;
              }

              const unsigned int selector_input =
                  static_cast<unsigned int>(
                      std::distance(
                          system->p25_control_source_selector_sources.begin(),
                          selector_source));

              system->p25_trunking->set_center(
                  source->get_center());

              system->p25_trunking->tune_freq(
                  control_channel_freq);

              system->p25_control_source_selector
                  ->set_input_index(selector_input);

              system->set_source(source);

              BOOST_LOG_TRIVIAL(info)
                  << "\t - Persistent P25 source switch "
                  << current_source->get_num()
                  << " -> "
                  << source->get_num()
                  << " on selector input "
                  << selector_input;
            } else {
              // Compatibility fallback for mixed-rate source sets.
              system->set_source(source);

              tb->lock();

              tb->disconnect(
                  current_source->get_src_block(),
                  0,
                  system->p25_trunking,
                  0);

              system->p25_trunking =
                  make_p25_trunking(
                      control_channel_freq,
                      source->get_center(),
                      source->get_rate(),
                      system->get_msg_queue(),
                      system->get_qpsk_mod(),
                      system->get_sys_num());

              tb->connect(
                  source->get_src_block(),
                  0,
                  system->p25_trunking,
                  0);

              tb->unlock();
            }
          } else if (system->get_system_type() == "dmr") {
          system->set_source(source);
          tb->lock();
          tb->disconnect(current_source->get_src_block(), 0, system->dmr_trunking, 0);
          system->dmr_trunking.reset();
          system->dmr_trunking = make_dmr_trunking(control_channel_freq, source->get_center(), source->get_rate(), system->get_msg_queue(), system->get_sys_num());
          tb->connect(source->get_src_block(), 0, system->dmr_trunking, 0);
          tb->unlock();
        } else {
          BOOST_LOG_TRIVIAL(error) << "\t - Unkown system type for Retune";
        }

        // break out of the For Loop
        break;
      }
    }
  }
  if (!source_found) {
    BOOST_LOG_TRIVIAL(error) << "\t - Unable to retune System control channel, freq not covered by any allowed source.";
  } else {
    system->select_control_channel(control_channel_freq);
    if ((system->get_source()->get_autotune_source()) && (system->get_system_type() == "p25")) {
      // If control channel source has autotune enabled, perform adjustments after retune completes
      // Don't store measurements since the control channel recorder just started
      autotune_control_channel(system, false);
    }
  }
  return source_found;
}

void check_message_count(float timeDiff, Config &config, gr::top_block_sptr &tb, std::vector<Source *> &sources, std::vector<System *> &systems) {
  plugman_setup_config(sources, systems);
  plugman_system_rates(systems, timeDiff);

  for (std::vector<System *>::iterator it = systems.begin(); it != systems.end(); ++it) {
    System_impl *sys = (System_impl *)*it;

    if ((sys->get_system_type() != "conventional") && (sys->get_system_type() != "conventionalP25") && (sys->get_system_type() != "conventionalDMR") && (sys->get_system_type() != "conventionalSIGMF")) {
      int msgs_decoded_per_second = std::floor(sys->message_count / timeDiff);
      sys->set_decode_rate(msgs_decoded_per_second);

      const long long capacity_plus_last_activity =
          sys->get_dmr_capplus_last_activity();
      const long long now_seconds =
          static_cast<long long>(time(NULL));

      bool probe_transition_handled = false;

      if (sys->get_system_type() == "dmr" &&
          sys->dmr_capplus_probe_active) {

        const long long probe_age =
            now_seconds >= sys->dmr_capplus_probe_started_at
                ? now_seconds - sys->dmr_capplus_probe_started_at
                : 0;

        if (sys->dmr_capplus_probe_saw_valid) {
          BOOST_LOG_TRIVIAL(info)
              << "[" << sys->get_short_name()
              << "] Capacity Plus rest-channel probe acquired "
              << format_freq(sys->get_current_control_channel())
              << " after " << probe_age << "s";

          sys->dmr_capplus_probe_active = false;
          sys->dmr_capplus_probe_previous_freq = 0;
          sys->dmr_capplus_probe_target_freq = 0;
          sys->dmr_capplus_probe_started_at = 0;
          sys->dmr_capplus_probe_saw_non_timeout = false;
          sys->dmr_capplus_probe_saw_valid = false;

        } else if (capacity_plus_probe_timed_out(
                       now_seconds,
                       sys->dmr_capplus_probe_started_at,
                       sys->dmr_capplus_probe_saw_non_timeout)) {

          const double failed_freq =
              sys->get_current_control_channel();
          const double fallback_freq =
              sys->dmr_capplus_probe_previous_freq;
          const bool saw_non_timeout =
              sys->dmr_capplus_probe_saw_non_timeout;

          BOOST_LOG_TRIVIAL(warning)
              << "[" << sys->get_short_name()
              << "] Capacity Plus rest-channel probe failed on "
              << format_freq(failed_freq)
              << " after " << probe_age << "s ("
              << (saw_non_timeout
                      ? "non-timeout DMR seen but no valid CAP+"
                      : "sync timeouts only")
              << "); returning to "
              << format_freq(fallback_freq);

          sys->dmr_capplus_probe_active = false;
          sys->dmr_capplus_probe_target_freq = 0;
          sys->dmr_capplus_probe_started_at = 0;
          sys->dmr_capplus_probe_saw_non_timeout = false;
          sys->dmr_capplus_probe_saw_valid = false;

          sys->dmr_capplus_pending_rest_freq = 0;
          sys->dmr_capplus_pending_rest_first_seen = 0;
          sys->dmr_capplus_pending_rest_count = 0;

          if (fallback_freq != 0 &&
              fallback_freq != failed_freq) {
            retune_system_to_frequency(
                sys, fallback_freq, tb, sources);
          }

          sys->dmr_capplus_probe_previous_freq = 0;
          probe_transition_handled = true;
        }
      }

      const bool capacity_plus_locked =
          (sys->get_system_type() == "dmr") &&
          (capacity_plus_last_activity > 0);

      // While actively proving a newly selected rest channel, the short
      // acquisition probe owns recovery. Once established, fall back to the
      // long stale-activity watchdog.
      const bool control_channel_lost =
          sys->get_capacity_plus_multi_frequency()
              ? false
              : ((!probe_transition_handled &&
                  !sys->dmr_capplus_probe_active)
                     ? (capacity_plus_locked
                            ? capacity_plus_control_activity_lost(
                                  now_seconds,
                                  capacity_plus_last_activity)
                            : (msgs_decoded_per_second < 2))
                     : false);

      if (control_channel_lost) {

        if (capacity_plus_locked) {
          const long long activity_age =
              now_seconds >= capacity_plus_last_activity
                  ? now_seconds - capacity_plus_last_activity
                  : 0;
          BOOST_LOG_TRIVIAL(warning)
              << "[" << sys->get_short_name()
              << "] Capacity Plus signaling stale for "
              << activity_age
              << "s; searching configured channels";
        }

        // if it loses track of the control channel, quit after a while
        if (config.control_retune_limit > 0) {
          sys->retune_attempts++;
          if (sys->retune_attempts > config.control_retune_limit) {
            BOOST_LOG_TRIVIAL(error) << "[" << sys->get_short_name() << "]\t"
                                     << "Control channel retune limit exceeded after " << sys->retune_attempts << " tries - Terminating trunk recorder";
            exit_flag = 1;
            exit_code = EXIT_FAILURE;
            return;
          }
        }
        if (sys->control_channel_count() > 1) {
          retune_system(sys, tb, sources);
        } else {
          BOOST_LOG_TRIVIAL(error) << "[" << sys->get_short_name() << "]\tThere is only one control channel defined";
        }

      } else {
        sys->retune_attempts = 0;
      }

      if (msgs_decoded_per_second < config.control_message_warn_rate) {
        BOOST_LOG_TRIVIAL(error) << "[" << sys->get_short_name() << "]\tfreq: " << format_freq(sys->get_current_control_channel()) << "\tControl Channel Message Decode Rate: " << msgs_decoded_per_second << "/sec, count:  " << sys->message_count;
      } else if (config.control_message_warn_rate == -1) {
        BOOST_LOG_TRIVIAL(info) << "[" << sys->get_short_name() << "]\tfreq: " << format_freq(sys->get_current_control_channel()) << "\tControl Channel Message Decode Rate: " << msgs_decoded_per_second << "/sec, count:  " << sys->message_count;
      }
    }
    sys->message_count = 0;
  }
}

void check_conventional_channel_detection(std::vector<Source *> &sources) {
  Source *source = NULL;
  for (vector<Source *>::iterator src_it = sources.begin(); src_it != sources.end(); src_it++) {
    source = *src_it;
    source->enable_detected_recorders();
  }
}

// This is to handle the messages that come off the Analog recorder.
void process_message_queues(std::vector<System *> &systems) {
  for (std::vector<System *>::iterator it = systems.begin(); it != systems.end(); ++it) {
    System_impl *sys = (System_impl *)*it;

    for (std::vector<analog_recorder_sptr>::iterator arit = sys->conventional_recorders.begin(); arit != sys->conventional_recorders.end(); ++arit) {
      analog_recorder_sptr ar = (analog_recorder_sptr)*arit;
      ar->process_message_queues();
    }
  }
}

// Process message queues for recorders associated with Calls
void process_recorder_message_queues(std::vector<Call *> &calls) {
  for (vector<Call *>::iterator it = calls.begin(); it != calls.end(); ++it) {
    Call *call = *it;
    if (call->get_state() == RECORDING) {
      Recorder *recorder = call->get_recorder();
      if (recorder && (recorder->get_type() == P25 || recorder->get_type() == P25C)) {
        p25_recorder *p25_rec = dynamic_cast<p25_recorder *>(recorder);
        // Verify recorder status as conventionals calls may be in a RECORDING:IDLE state
        if (p25_rec && (p25_rec->is_active())) {
          p25_rec->process_message_queues();
        }
      }
    }
  }
}

int monitor_messages(Config &config, gr::top_block_sptr &tb, std::vector<Source *> &sources, std::vector<System *> &systems, std::vector<Call *> &calls) {
  gr::message::sptr msg;

  time_t last_status_time = time(NULL);
  time_t last_decode_rate_check = time(NULL);
  time_t management_timestamp = time(NULL);
  uint64_t last_conventional_channel_detection_check = time_since_epoch_millisec();
  time_t current_time = time(NULL);
  uint64_t current_time_ms = time_since_epoch_millisec();
  std::vector<TrunkMessage> trunk_messages;
  SmartnetParser *smartnet_parser;
  P25Parser *p25_parser;
  DmrParser *dmr_parser;

  signal(SIGINT, exit_interupt);
  signal(SIGHUP, rotate_log_signal);

  smartnet_parser = new SmartnetParser(systems.front()); // this has to eventually be generic;
  p25_parser = new P25Parser();
  dmr_parser = new DmrParser();

  while (1) {

    if (exit_flag) { // my action when signal set it 1
      BOOST_LOG_TRIVIAL(info) << "Caught an Exit Signal...";
      for (vector<Call *>::iterator it = calls.begin(); it != calls.end();) {
        Call *call = *it;

        call->conclude_call();

        it = calls.erase(it);
        delete call;
      }

      BOOST_LOG_TRIVIAL(info) << "Cleaning up & Exiting...";
      Call_Concluder::shutdown_call_data_workers(std::chrono::seconds(10));
      return exit_code;
    }

    if (rotate_log_flag) { // SIGHUP received for log rotation
      rotate_log_flag = 0;  // reset flag
      if (global_log_sink) {
        BOOST_LOG_TRIVIAL(info) << "Received SIGHUP signal - rotating log file...";
        // Flush the sink
        global_log_sink->flush();
        // Rotate the log file by removing and re-adding the backend
        boost::log::core::get()->remove_sink(global_log_sink);
        boost::log::core::get()->add_sink(global_log_sink);
        BOOST_LOG_TRIVIAL(info) << "Log file rotation complete";
      }
    }

    process_message_queues(systems);
    process_recorder_message_queues(calls);

    plugman_poll_one();

    for (vector<System *>::iterator sys_it = systems.begin(); sys_it != systems.end(); sys_it++) {
      System_impl *system = (System_impl *)*sys_it;

      if ((system->get_system_type() == "p25") || (system->get_system_type() == "smartnet") || (system->get_system_type() == "dmr")) {
        msg.reset();
        msg = system->get_msg_queue()->delete_head_nowait();
        while (msg != 0) {
          system->set_message_count(system->get_message_count() + 1);

          if (system->get_system_type() == "smartnet") {
            trunk_messages = smartnet_parser->parse_message(msg, system);
            handle_message(trunk_messages, system, config, sources, calls, tb);
            plugman_trunk_message(trunk_messages, system);
          }

          if (system->get_system_type() == "p25") {
            trunk_messages = p25_parser->parse_message(msg, system);
            handle_message(trunk_messages, system, config, sources, calls, tb);
            plugman_trunk_message(trunk_messages, system);
          }

          if (system->get_system_type() == "dmr") {
            trunk_messages = dmr_parser->parse_message(msg, system);
            handle_message(trunk_messages, system, config, sources, calls, tb);
            plugman_trunk_message(trunk_messages, system);
          }

          if (msg->type() == -1) {
            BOOST_LOG_TRIVIAL(error) << "[" << system->get_short_name() << "]\t process_data_unit timeout";
          }

          msg.reset();
          msg = system->get_msg_queue()->delete_head_nowait();
        }
      }
    }
    current_time = time(NULL);
    current_time_ms = time_since_epoch_millisec();
    if ((current_time_ms - last_conventional_channel_detection_check) >= 100) {
      check_conventional_channel_detection(sources);
      last_conventional_channel_detection_check = current_time_ms;
    }

    if ((current_time - management_timestamp) >= 1.0) {
      manage_calls(config, calls);
      Call_Concluder::manage_call_data_workers();
      management_timestamp = current_time;
    }

    boost::this_thread::sleep(boost::posix_time::milliseconds(10));

    float decode_rate_check_time_diff = current_time - last_decode_rate_check;

    if (decode_rate_check_time_diff >= 3.0) {
      check_message_count(decode_rate_check_time_diff, config, tb, sources, systems);
      for (vector<Source *>::iterator src_it = sources.begin(); src_it != sources.end(); src_it++) {
        Source *source = *src_it;
        if (!source->got_samples()) {
          BOOST_LOG_TRIVIAL(error) << "Source " << source->get_num() << " has stopped receiving samples - Terminating trunk recorder";
          exit_code = EXIT_FAILURE;
          exit_flag = 1;
          break;
        }
      }
      last_decode_rate_check = current_time;
      for (vector<System *>::iterator sys_it = systems.begin(); sys_it != systems.end(); sys_it++) {
        System *system = *sys_it;
        if (system->get_system_type() == "p25") {
          system->clear_stale_talkgroup_patches();
        }
      }
    }

    float print_status_time_diff = current_time - last_status_time;

    if (print_status_time_diff > 200) {
      last_status_time = current_time;
      print_status(sources, systems, calls);
    }
  }
}
