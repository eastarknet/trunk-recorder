#ifndef INCLUDED_P25_SOURCE_SELECTOR_IMPL_H
#define INCLUDED_P25_SOURCE_SELECTOR_IMPL_H

#include "p25_source_selector.h"
#include <gnuradio/thread/thread.h>

namespace gr {
namespace blocks {

class p25_source_selector_impl : public p25_source_selector {
private:
  size_t d_itemsize;
  unsigned int d_input_index;
  unsigned int d_num_inputs;
  mutable gr::thread::mutex d_mutex;

public:
  p25_source_selector_impl(size_t itemsize, unsigned int input_index);
  ~p25_source_selector_impl();

  void forecast(int noutput_items,
                gr_vector_int &ninput_items_required) override;

  bool check_topology(int ninputs, int noutputs) override;

  void set_input_index(unsigned int input_index) override;
  int input_index() const override;

  int general_work(int noutput_items,
                   gr_vector_int &ninput_items,
                   gr_vector_const_void_star &input_items,
                   gr_vector_void_star &output_items) override;
};

} /* namespace blocks */
} /* namespace gr */

#endif /* INCLUDED_P25_SOURCE_SELECTOR_IMPL_H */
