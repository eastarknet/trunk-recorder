#include "dmr_slot.h"
#include "golay2087.h"
#include "hamming.h"

#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

class DmrEmbeddedLcTest {
public:
    static byte_vector encode_embedded_lc(
        const std::array<uint8_t, 9>& payload)
    {
        std::array<bool, 128> data{};

        static const int payload_positions[72] = {
             0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10,
            16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26,
            32, 33, 34, 35, 36, 37, 38, 39, 40, 41,
            48, 49, 50, 51, 52, 53, 54, 55, 56, 57,
            64, 65, 66, 67, 68, 69, 70, 71, 72, 73,
            80, 81, 82, 83, 84, 85, 86, 87, 88, 89,
            96, 97, 98, 99,100,101,102,103,104,105
        };
        static const int crc_positions[5] = {42, 58, 74, 90, 106};

        for (int bit = 0; bit < 72; ++bit) {
            data[payload_positions[bit]] =
                ((payload[bit / 8] >> (7 - (bit % 8))) & 0x1) != 0;
        }

        unsigned int crc = 0;
        for (uint8_t value : payload)
            crc += value;
        crc %= 31;

        for (int bit = 0; bit < 5; ++bit)
            data[crc_positions[bit]] =
                ((crc >> (4 - bit)) & 0x1) != 0;

        for (int row = 0; row < 7; ++row)
            CHamming::encode16114(data.data() + (row * 16));

        for (int col = 0; col < 16; ++col) {
            bool parity = false;
            for (int row = 0; row < 7; ++row)
                parity ^= data[(row * 16) + col];
            data[112 + col] = parity;
        }

        return interleave(data);
    }

    static byte_vector with_hamming_failure(const byte_vector& embedded)
    {
        auto data = deinterleave(embedded);
        data[0] = !data[0];
        data[1] = !data[1];
        return interleave(data);
    }

    static byte_vector with_parity_failure(const byte_vector& embedded)
    {
        auto data = deinterleave(embedded);
        data[112] = !data[112];
        return interleave(data);
    }

    static bool decode_direct(dmr_slot& slot, const byte_vector& embedded)
    {
        slot.d_emb = embedded;
        return slot.decode_embedded_lc();
    }

    static bool lc_matches(
        const dmr_slot& slot,
        const std::array<uint8_t, 9>& payload)
    {
        return slot.d_lc_valid &&
               slot.d_lc.size() == payload.size() &&
               std::equal(
                   slot.d_lc.begin(),
                   slot.d_lc.end(),
                   payload.begin());
    }

    static bool decode_end_lc(
        dmr_slot& slot,
        const byte_vector& embedded,
        uint8_t color_code)
    {
        std::memset(slot.d_slot, 0, sizeof(slot.d_slot));
        slot.d_emb.assign(
            embedded.begin(),
            embedded.begin() + 96);
        slot.d_cc = color_code;

        bit_vector emb_sig(16, false);
        emb_sig[0] = ((color_code >> 3) & 0x1) != 0;
        emb_sig[1] = ((color_code >> 2) & 0x1) != 0;
        emb_sig[2] = ((color_code >> 1) & 0x1) != 0;
        emb_sig[3] = (color_code & 0x1) != 0;
        emb_sig[4] = false; // PI
        emb_sig[5] = true;  // LCSS=2 (End LC)
        emb_sig[6] = false;
        CQR1676::encode(emb_sig);

        for (int i = 0; i < 8; ++i)
            slot.d_slot[SYNC_EMB + i] = emb_sig[i];
        for (int i = 0; i < 32; ++i)
            slot.d_slot[SYNC_EMB + 8 + i] = embedded[96 + i];
        for (int i = 0; i < 8; ++i)
            slot.d_slot[SLOT_R - 8 + i] = emb_sig[8 + i];

        return slot.decode_emb();
    }

private:
    static std::array<bool, 128> deinterleave(
        const byte_vector& embedded)
    {
        std::array<bool, 128> data{};
        unsigned int b = 0;

        for (unsigned int a = 0; a < 128; ++a) {
            data[b] = embedded[a] != 0;
            b += 16;
            if (b > 127)
                b -= 127;
        }

        return data;
    }

    static byte_vector interleave(
        const std::array<bool, 128>& data)
    {
        byte_vector embedded(128, 0);
        unsigned int b = 0;

        for (unsigned int a = 0; a < 128; ++a) {
            embedded[a] = data[b] ? 1 : 0;
            b += 16;
            if (b > 127)
                b -= 127;
        }

        return embedded;
    }
};

namespace {

int failures = 0;

void expect(bool condition, const char* description)
{
    if (!condition) {
        std::cerr << "FAIL: " << description << '\n';
        ++failures;
    }
}

dmr_slot make_slot(log_ts& logger)
{
    return dmr_slot(
        0,
        logger,
        0,
        -1,
        gr::msg_queue::sptr());
}

} // namespace

int main()
{
    log_ts logger;

    const std::array<uint8_t, 9> payload = {
        0x00, 0x00, 0x00,
        0x12, 0x34, 0x56,
        0x65, 0x43, 0x21
    };

    const byte_vector valid =
        DmrEmbeddedLcTest::encode_embedded_lc(payload);

    {
        dmr_slot slot = make_slot(logger);
        expect(
            DmrEmbeddedLcTest::decode_direct(slot, valid),
            "valid embedded LC passes Hamming and parity");
        expect(
            DmrEmbeddedLcTest::lc_matches(slot, payload),
            "valid embedded LC preserves the 72-bit payload");
    }

    {
        dmr_slot slot = make_slot(logger);
        const byte_vector corrupt =
            DmrEmbeddedLcTest::with_hamming_failure(valid);
        expect(
            !DmrEmbeddedLcTest::decode_direct(slot, corrupt),
            "uncorrectable Hamming error is rejected");
    }

    {
        dmr_slot slot = make_slot(logger);
        const byte_vector corrupt =
            DmrEmbeddedLcTest::with_parity_failure(valid);
        expect(
            !DmrEmbeddedLcTest::decode_direct(slot, corrupt),
            "parity-row error is rejected");
    }

    {
        dmr_slot slot = make_slot(logger);
        expect(
            DmrEmbeddedLcTest::decode_end_lc(slot, valid, 7),
            "valid End LC signaling is accepted");

        const std::pair<bool, long> first =
            slot.get_terminated();
        const std::pair<bool, long> second =
            slot.get_terminated();

        expect(
            first.first,
            "valid End LC produces a termination event");
        expect(
            !second.first,
            "termination event is consumed exactly once");
    }

    if (failures != 0) {
        std::cerr
            << "DMR embedded LC tests failed: "
            << failures << '\n';
        return EXIT_FAILURE;
    }

    std::cout << "DMR embedded LC tests passed\n";
    return EXIT_SUCCESS;
}
