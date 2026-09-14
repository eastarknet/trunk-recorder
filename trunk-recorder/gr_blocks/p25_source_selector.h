#ifndef INCLUDED_P25_SOURCE_SELECTOR_H
#define INCLUDED_P25_SOURCE_SELECTOR_H

#include <gnuradio/block.h>
#include <gnuradio/blocks/api.h>

namespace gr {
namespace blocks {

class BLOCKS_API p25_source_selector : virtual public block {
public:
#if GNURADIO_VERSION < 0x030900
  typedef boost::shared_ptr<p25_source_selector> sptr;
#else
  typedef std::shared_ptr<p25_source_selector> sptr;
#endif

  static sptr make(size_t itemsize, unsigned int input_index);

  virtual void set_input_index(unsigned int input_index) = 0;
  virtual int input_index() const = 0;
};

} /* namespace blocks */
} /* namespace gr */

#endif /* INCLUDED_P25_SOURCE_SELECTOR_H */
