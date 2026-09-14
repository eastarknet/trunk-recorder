#ifndef INCLUDED_DMR_METADATA_PENDING_H
#define INCLUDED_DMR_METADATA_PENDING_H

#include <array>
#include <cstddef>

namespace gr {
namespace op25_repeater {

struct dmr_metadata_values
{
    int src_id;
    int dst_id;
    int cc;

    dmr_metadata_values()
        : src_id(-1),
          dst_id(-1),
          cc(0)
    {
    }

    bool empty() const
    {
        return src_id <= 0 && dst_id == -1 && cc == 0;
    }
};

class dmr_metadata_pending
{
public:
    dmr_metadata_pending()
    {
        clear(0);
        clear(1);
    }

    dmr_metadata_values update(std::size_t slot,
                               int src_id,
                               int dst_id,
                               int cc,
                               bool terminated,
                               bool has_output)
    {
        if (slot >= d_pending.size()) {
            return dmr_metadata_values();
        }

        dmr_metadata_values &pending = d_pending[slot];

        if (src_id != -1 && src_id != 0) {
            pending.src_id = src_id;
        }

        if (dst_id != -1) {
            pending.dst_id = dst_id;
        }

        if (cc != -1 && cc != 0) {
            pending.cc = cc;
        }

        if (!has_output) {
            if (terminated) {
                clear(slot);
            }
            return dmr_metadata_values();
        }

        dmr_metadata_values result = pending;
        clear(slot);
        return result;
    }

private:
    std::array<dmr_metadata_values, 2> d_pending;

    void clear(std::size_t slot)
    {
        if (slot < d_pending.size()) {
            d_pending[slot] = dmr_metadata_values();
        }
    }
};

} // namespace op25_repeater
} // namespace gr

#endif /* INCLUDED_DMR_METADATA_PENDING_H */
