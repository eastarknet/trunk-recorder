#include "dmr_metadata_pending.h"

#include <cstdlib>
#include <iostream>
#include <string>

using gr::op25_repeater::dmr_metadata_pending;
using gr::op25_repeater::dmr_metadata_values;

namespace {

int failures = 0;

void expect(bool condition, const std::string &description)
{
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

bool metadata_eq(const dmr_metadata_values &v, int src, int dst, int cc)
{
    return v.src_id == src && v.dst_id == dst && v.cc == cc;
}

} // namespace

int main()
{
    // Case A: repeated metadata with zero output never emits anything and
    // remains bounded to one state tuple.
    dmr_metadata_pending pending;

    dmr_metadata_values out;

    for (int i = 0; i < 100000; ++i) {
        out = pending.update(0, 12345, 67890, 7, false, false);
        expect(out.empty(), "zero-output call must emit no metadata");
    }

    // First real output receives the latest pending metadata exactly once.
    out = pending.update(0, -1, -1, 0, false, true);
    expect(metadata_eq(out, 12345, 67890, 7),
           "first audio emits pending metadata");

    out = pending.update(0, -1, -1, 0, false, true);
    expect(out.empty(),
           "subsequent audio does not re-emit consumed metadata");

    // Case B: newer pending metadata replaces older metadata.
    pending.update(0, 100, 200, 1, false, false);
    pending.update(0, 101, 201, 2, false, false);

    out = pending.update(0, -1, -1, 0, false, true);
    expect(metadata_eq(out, 101, 201, 2),
           "latest pending metadata wins");

    // Case C: a terminator with no output invalidates pending metadata.
    pending.update(0, 111, 222, 3, false, false);
    out = pending.update(0, -1, -1, 0, true, false);

    expect(out.empty(),
           "terminator without audio emits no metadata");

    out = pending.update(0, -1, -1, 0, false, true);
    expect(out.empty(),
           "terminated metadata cannot bleed into later audio");

    // Case D: if output exists in the same scheduler cycle as termination,
    // current metadata still belongs to that real output.
    pending.update(0, 444, 555, 9, false, false);

    out = pending.update(0, -1, -1, 0, true, true);
    expect(metadata_eq(out, 444, 555, 9),
           "terminator with audio emits current metadata");

    out = pending.update(0, -1, -1, 0, false, true);
    expect(out.empty(),
           "terminator with audio leaves state clear");

    // Case E: TDMA slots remain independent.
    pending.update(0, 10, 20, 1, false, false);
    pending.update(1, 30, 40, 2, false, false);

    out = pending.update(0, -1, -1, 0, false, true);
    expect(metadata_eq(out, 10, 20, 1),
           "slot 0 emits slot 0 metadata");

    out = pending.update(1, -1, -1, 0, false, true);
    expect(metadata_eq(out, 30, 40, 2),
           "slot 1 preserves independent metadata");

    // Case F: individual fields preserve the original validity rules.
    pending.update(0, 0, -1, 0, false, false);
    out = pending.update(0, -1, -1, 0, false, true);
    expect(out.empty(),
           "zero/invalid metadata is ignored");

    pending.update(0, -1, 0, -1, false, false);
    out = pending.update(0, -1, -1, 0, false, true);
    expect(metadata_eq(out, -1, 0, 0),
           "destination zero retains original valid semantics");

    // Case G: invalid slot indexes fail closed.
    out = pending.update(2, 999, 999, 15, false, true);
    expect(out.empty(),
           "invalid slot emits nothing");

    if (failures != 0) {
        std::cerr << "DMR metadata pending tests failed: "
                  << failures << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "DMR metadata pending tests passed\n";
    return EXIT_SUCCESS;
}
