#include "call.h"
#include "gr_blocks/transmission_sink.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
#include <unistd.h>

class TransmissionSinkLifecycleTest {
public:
  static bool deterministic(const gr::blocks::transmission_sink &sink) {
    return sink.d_sample_count == 0 && sink.d_slot == -1 &&
           sink.d_start_time == 0 && sink.d_stop_time == 0 &&
           sink.d_start_time_ms == 0 && sink.d_stop_time_ms == 0 &&
           sink.d_spike_count == 0 && sink.d_error_count == 0 &&
           sink.curr_src_id == -1 && sink.cached_src_id == -1 &&
           sink.d_current_call == nullptr && sink.d_current_call_num == 0 &&
           sink.d_current_call_freq == 0.0 &&
           sink.d_current_call_talkgroup == 0 &&
           sink.d_current_call_talkgroup_encoded == 0 &&
           sink.d_prior_transmission_length == 0.0 &&
           sink.state == AVAILABLE;
  }

  static bool attachment_reset(const gr::blocks::transmission_sink &sink,
                               const Call *call, int slot) {
    return sink.d_current_call == call && sink.d_slot == slot &&
           sink.d_sample_count == 0 && sink.d_start_time == 0 &&
           sink.d_stop_time == 0 && sink.d_start_time_ms == 0 &&
           sink.d_stop_time_ms == 0 && sink.d_spike_count == 0 &&
           sink.d_error_count == 0 && sink.cached_src_id == -1 &&
           sink.d_prior_transmission_length == 0.0 && sink.d_first_work &&
           !sink.d_termination_flag && sink.current_filename.empty() &&
           sink.state == IDLE;
  }

  static void age_last_write(gr::blocks::transmission_sink &sink,
                             std::chrono::milliseconds age) {
    sink.d_last_write_time = std::chrono::steady_clock::now() - age;
  }

  static void poison_stop_time(gr::blocks::transmission_sink &sink) {
    sink.d_stop_time = static_cast<time_t>(-107091000000000LL);
    sink.d_stop_time_ms = -107091000000000LL;
  }

  static int process_samples(gr::blocks::transmission_sink &sink,
                             int sample_count,
                             gr_vector_const_void_star &input,
                             gr_vector_void_star &output) {
    return sink.dowork(sample_count, input, output);
  }
};

namespace {
int failures = 0;

void expect(bool condition, const char *description) {
  if (!condition) {
    std::cerr << "FAIL: " << description << '\n';
    ++failures;
  }
}

double inactivity(const gr::blocks::transmission_sink &sink) {
  return std::chrono::duration<double>(std::chrono::steady_clock::now() -
                                      sink.get_last_write_time())
      .count();
}

class FakeCall final : public Call {
public:
  FakeCall(long call_num, double freq, std::string temp_dir)
      : call_num_(call_num), freq_(freq), temp_dir_(std::move(temp_dir)) {}

  long get_call_num() override { return call_num_; }
  double get_freq() override { return freq_; }
  std::string get_short_name() override { return "dmr-lifecycle-test"; }
  std::string get_capture_dir() override { return temp_dir_; }
  std::string get_temp_dir() override { return temp_dir_; }
  long get_talkgroup() override { return 1; }
  std::string get_talkgroup_display() override { return "1"; }
  std::string get_system_type() override { return "dmr"; }
  long get_current_source_id() override { return -1; }
  bool is_conventional() override { return false; }
  int get_tdma_slot() override { return 0; }

  void restart_call() override {}
  void stop_call() override {}
  void conclude_call() override {}
  void set_sigmf_recorder(Recorder *) override {}
  Recorder *get_sigmf_recorder() override { return nullptr; }
  void set_debug_recorder(Recorder *) override {}
  Recorder *get_debug_recorder() override { return nullptr; }
  void set_recorder(Recorder *) override {}
  Recorder *get_recorder() override { return nullptr; }
  int get_sys_num() override { return 0; }
  void set_freq(double value) override { freq_ = value; }
  bool update(TrunkMessage) override { return false; }
  int get_idle_count() override { return 0; }
  void increase_idle_count() override {}
  void reset_idle_count() override {}
  int since_last_update() override { return 0; }
  double since_last_voice_update() override { return 0; }
  long elapsed() override { return 0; }
  double get_current_length() override { return 0; }
  long get_stop_time() override { return 0; }
  void set_debug_recording(bool) override {}
  bool get_debug_recording() override { return false; }
  void set_sigmf_recording(bool) override {}
  bool get_sigmf_recording() override { return false; }
  void set_state(State) override {}
  State get_state() override { return ACTIVE; }
  void set_monitoring_state(MonitoringState) override {}
  MonitoringState get_monitoring_state() override { return UNSPECIFIED; }
  void set_phase2_tdma(bool) override {}
  bool get_phase2_tdma() override { return true; }
  void set_tdma_slot(int) override {}
  bool get_is_analog() override { return false; }
  void set_is_analog(bool) override {}
  const char *get_xor_mask() override { return nullptr; }
  time_t get_start_time() override { return 0; }
  std::int64_t get_start_time_ms() override { return 0; }
  void set_encrypted(bool) override {}
  bool get_encrypted() override { return false; }
  void set_emergency(bool) override {}
  bool get_emergency() override { return false; }
  int get_priority() override { return 0; }
  bool get_mode() override { return false; }
  bool get_duplex() override { return false; }
  double get_signal() override { return 0; }
  double get_noise() override { return 0; }
  int get_freq_error() override { return 0; }
  void set_signal(double) override {}
  void set_noise(double) override {}
  void set_talkgroup_tag(std::string) override {}
  void clear_transmission_list() override {}
  boost::property_tree::ptree get_stats() override { return {}; }
  std::string get_talkgroup_tag() override { return {}; }
  double get_final_length() override { return 0; }
  bool get_conversation_mode() override { return false; }
  System *get_system() override { return nullptr; }
  std::vector<Transmission> get_transmissions() override { return {}; }

private:
  long call_num_;
  double freq_;
  std::string temp_dir_;
};
} // namespace

int main() {
  char temp_template[] = "/tmp/tr-dmr-lifecycle-XXXXXX";
  char *temp_dir = mkdtemp(temp_template);
  expect(temp_dir != nullptr, "temporary directory created");
  if (!temp_dir) return EXIT_FAILURE;

  auto sink = gr::blocks::transmission_sink::make(1, 8000, 16);
  expect(TransmissionSinkLifecycleTest::deterministic(*sink),
         "primitive lifecycle state is deterministic after construction");
  expect(std::isfinite(inactivity(*sink)) && inactivity(*sink) >= 0.0 &&
             inactivity(*sink) < 1.0,
         "new sink has sane finite steady-clock inactivity");

  gr_vector_const_void_star empty_input;
  gr_vector_void_star no_output;
  expect(sink->work(480, empty_input, no_output) == 480,
         "unattached permanently-connected slot drops a full buffer safely");

  FakeCall first_call(16, 155175000.0, temp_dir);
  sink->start_recording(&first_call, 0);
  expect(TransmissionSinkLifecycleTest::attachment_reset(*sink, &first_call, 0),
         "new attachment resets lifecycle state");
  expect(inactivity(*sink) >= 0.0 && inactivity(*sink) < 1.0,
         "attached unterminated sink has sane inactivity");

  int16_t samples[8] = {};
  gr_vector_const_void_star input(1, samples);
  expect(TransmissionSinkLifecycleTest::process_samples(
             *sink, 8, input, no_output) == 8,
         "attached sink processes audio samples");
  expect(inactivity(*sink) >= 0.0 && inactivity(*sink) < 1.0,
         "sample processing resets steady-clock inactivity");

  TransmissionSinkLifecycleTest::poison_stop_time(*sink);
  TransmissionSinkLifecycleTest::age_last_write(*sink,
                                                std::chrono::milliseconds(1500));
  const double silent_for = inactivity(*sink);
  expect(std::isfinite(silent_for) && silent_for > 1.0 && silent_for < 2.0,
         "inactivity grows beyond representative timeout after samples stop");
  expect(sink->get_stop_time() < 0 && silent_for > 0.0,
         "unset or invalid canonical stop metadata cannot corrupt inactivity");

  const std::string first_filename = sink->get_filename();
  sink->stop_recording();
  FakeCall second_call(17, 155175000.0, temp_dir);
  sink->start_recording(&second_call, 1);
  expect(TransmissionSinkLifecycleTest::attachment_reset(*sink, &second_call, 1),
         "reused sink atomically attaches the requested alternate slot");
  sink->stop_recording();
  expect(sink->work(480, empty_input, no_output) == 480,
         "reused unattached sink continues to drop samples safely");

  if (!first_filename.empty()) std::remove(first_filename.c_str());
  const std::string child_dir = std::string(temp_dir) + "/dmr-lifecycle-test";
  rmdir(child_dir.c_str());
  rmdir(temp_dir);

  if (failures) {
    std::cerr << failures << " DMR lifecycle assertion(s) failed\n";
    return EXIT_FAILURE;
  }
  std::cout << "DMR lifecycle tests passed\n";
  return EXIT_SUCCESS;
}
