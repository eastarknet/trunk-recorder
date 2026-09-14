#include "p25_source_selector_impl.h"

#include <algorithm>
#include <gnuradio/io_signature.h>
#include <stdexcept>

namespace gr {
namespace blocks {

p25_source_selector::sptr
p25_source_selector::make(size_t itemsize, unsigned int input_index) {
  return gnuradio::get_initial_sptr(
      new p25_source_selector_impl(itemsize, input_index));
}

p25_source_selector_impl::p25_source_selector_impl(
    size_t itemsize,
    unsigned int input_index)
    : block("p25_source_selector",
            io_signature::make(1, -1, itemsize),
            io_signature::make(1, 1, itemsize)),
      d_itemsize(itemsize),
      d_input_index(input_index),
      d_num_inputs(0) {
  set_tag_propagation_policy(TPP_DONT);
}

p25_source_selector_impl::~p25_source_selector_impl() {}

void p25_source_selector_impl::set_input_index(
    unsigned int input_index) {
  gr::thread::scoped_lock lock(d_mutex);

  if (input_index >= d_num_inputs) {
    throw std::out_of_range(
        "P25 source selector input index out of range");
  }

  d_input_index = input_index;
}

int p25_source_selector_impl::input_index() const {
  gr::thread::scoped_lock lock(d_mutex);
  return static_cast<int>(d_input_index);
}

void p25_source_selector_impl::forecast(
    int noutput_items,
    gr_vector_int &ninput_items_required) {
  gr::thread::scoped_lock lock(d_mutex);

  for (size_t i = 0;
       i < ninput_items_required.size();
       ++i) {
    ninput_items_required[i] = 0;
  }

  if (d_input_index < ninput_items_required.size()) {
    ninput_items_required[d_input_index] =
        noutput_items;
  }
}

bool p25_source_selector_impl::check_topology(
    int ninputs,
    int noutputs) {
  gr::thread::scoped_lock lock(d_mutex);

  if (ninputs < 1 ||
      noutputs != 1 ||
      d_input_index >= static_cast<unsigned int>(ninputs)) {
    return false;
  }

  d_num_inputs = static_cast<unsigned int>(ninputs);
  return true;
}

int p25_source_selector_impl::general_work(
    int noutput_items,
    gr_vector_int &ninput_items,
    gr_vector_const_void_star &input_items,
    gr_vector_void_star &output_items) {
  const uint8_t **in =
      reinterpret_cast<const uint8_t **>(&input_items[0]);

  uint8_t *out =
      reinterpret_cast<uint8_t *>(output_items[0]);

  gr::thread::scoped_lock lock(d_mutex);

  if (d_input_index >= ninput_items.size()) {
    return 0;
  }

  const int selected_items =
      std::min(
          noutput_items,
          ninput_items[d_input_index]);

  if (selected_items > 0) {
    std::copy(
        in[d_input_index],
        in[d_input_index] +
            selected_items * d_itemsize,
        out);
  }

  // The active source advances only by samples actually copied.
  // Inactive inputs are drained so switching never exposes stale IQ.
  for (size_t i = 0; i < ninput_items.size(); ++i) {
    if (i == d_input_index) {
      if (selected_items > 0) {
        consume(i, selected_items);
      }
    } else if (ninput_items[i] > 0) {
      consume(i, ninput_items[i]);
    }
  }

  return selected_items;
}

} /* namespace blocks */
} /* namespace gr */
