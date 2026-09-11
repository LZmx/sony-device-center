// Regression tests for the V1/V2 generation boundary at the *service* layer.
//
// ProtocolV1Tests already proves that ProtocolV1 never emits opcode 0x22, which
// means POWER OFF on first-generation devices and BATTERY on second-generation
// ones. That test passes even when the wrong protocol object is chosen, because
// it starts from a ProtocolV1 instance. The bug these tests cover is one layer
// up: a V1 device being handed a V2 session in the first place.
#include <catch2/catch_test_macros.hpp>

#include "sony/core/DeviceService.h"
#include "sony/core/SonyDevice.h"
#include "sony/protocol/FrameCodec.h"
#include "sony/transport/FakeTransport.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

using namespace sony;
using namespace sony::core;
using namespace sony::protocol;
using namespace sony::transport;

namespace {

constexpr uint8_t kPowerOffOnV1 = 0x22;

// Records every opcode written to the wire so a test can assert on what was
// actually transmitted rather than on which code path was taken.
class OpcodeRecordingTransport : public FakeTransport {
public:
    size_t send(std::span<const std::byte> data) override {
        size_t res = FakeTransport::send(data);
        if (!data.empty()) {
            std::vector<uint8_t> bytes(data.size());
            std::transform(data.begin(), data.end(), bytes.begin(),
                           [](std::byte b) { return static_cast<uint8_t>(b); });
            try {
                auto frame = FrameCodec::decode(bytes);
                if (frame.type == DataType::DataMdr && !frame.payload.empty()) {
                    opcodes.push_back(frame.payload[0]);
                    _respond(frame);
                }
            } catch (...) {
                // Partial or malformed frames are not interesting here.
            }
        }
        return res;
    }

    [[nodiscard]] bool sent(uint8_t opcode) const {
        return std::find(opcodes.begin(), opcodes.end(), opcode) != opcodes.end();
    }

    std::vector<uint8_t> opcodes;

private:
    // Answer the connect-time refresh probes like a real V2 device so the
    // generation-resolution tests don't burn a timeout per unsupported GET.
    void _respond(const SonyFrame& frame) {
        queueIncoming(FrameCodec::encode(SonyFrame{
            .type = DataType::Ack,
            .sequence = frame.sequence,
            .payload = {}
        }));

        const uint8_t op = frame.payload[0];
        if (op == 0x00) {
            queueIncoming(FrameCodec::encode(SonyFrame{
                .type = DataType::DataMdr, .sequence = _respSeq++, .payload = {0x01, 0x00}
            }));
        } else if (op == 0x22) {
            queueIncoming(FrameCodec::encode(SonyFrame{
                .type = DataType::DataMdr, .sequence = _respSeq++, .payload = {0x23, 0x00, 85, 0x00}
            }));
        } else if (op == 0x66) {
            queueIncoming(FrameCodec::encode(SonyFrame{
                .type = DataType::DataMdr, .sequence = _respSeq++,
                .payload = {0x67, 0x17, 0x01, 0x01, 0x00, 0x00, 0x00}
            }));
        } else if (op == 0x56) {
            queueIncoming(FrameCodec::encode(SonyFrame{
                .type = DataType::DataMdr, .sequence = _respSeq++,
                .payload = {0x57, 0x00, 0x00, 0x06, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a, 0x0a}
            }));
        } else if (op == 0xe6) {
            queueIncoming(FrameCodec::encode(SonyFrame{
                .type = DataType::DataMdr, .sequence = _respSeq++, .payload = {0xe7, 0x01, 0x00}
            }));
        } else if (op == 0xf6) {
            const uint8_t sub = (frame.payload.size() >= 2) ? frame.payload[1] : 0x00;
            queueIncoming(FrameCodec::encode(SonyFrame{
                .type = DataType::DataMdr, .sequence = _respSeq++, .payload = {0xf7, sub, 0x00}
            }));
        } else if (op == 0x26) {
            queueIncoming(FrameCodec::encode(SonyFrame{
                .type = DataType::DataMdr, .sequence = _respSeq++, .payload = {0x27, 0x05, 0x00, 0x00}
            }));
        }
    }

    uint8_t _respSeq{0};
};

std::shared_ptr<OpcodeRecordingTransport> makeTransport() {
    return std::make_shared<OpcodeRecordingTransport>();
}

} // namespace

TEST_CASE("Connecting a V1 device by name never sends the power-off opcode",
          "[core][protocol][v1][regression]") {
    auto transport = makeTransport();
    DeviceService service(transport, nullptr);

    service.connect(DeviceAddress("11:22:33:44:55:66"), "WH-1000XM3");

    REQUIRE(service.activeDevice() != nullptr);
    CHECK(service.activeDevice()->protocolVersion() == SonyProtocolVersion::V1);
    CHECK_FALSE(transport->sent(kPowerOffOnV1));
}

TEST_CASE("Connecting without a device name never sends the power-off opcode",
          "[core][protocol][v1][regression]") {
    // sonyd -d <address> used to label any address "WH-1000XM5", which selected
    // the V2 command set and switched legacy headphones off on connect. With no
    // name the profile must fall back to V1, not to V2.
    auto transport = makeTransport();
    DeviceService service(transport, nullptr);

    service.connect(DeviceAddress("11:22:33:44:55:66"));

    REQUIRE(service.activeDevice() != nullptr);
    CHECK(service.activeDevice()->protocolVersion() == SonyProtocolVersion::V1);
    CHECK_FALSE(transport->sent(kPowerOffOnV1));
}

TEST_CASE("An unrecognised device name is treated as V1",
          "[core][protocol][v1][regression]") {
    auto transport = makeTransport();
    DeviceService service(transport, nullptr);

    service.connect(DeviceAddress("11:22:33:44:55:66"), "Some Unknown Headset");

    REQUIRE(service.activeDevice() != nullptr);
    CHECK(service.activeDevice()->protocolVersion() == SonyProtocolVersion::V1);
    CHECK_FALSE(transport->sent(kPowerOffOnV1));
}

TEST_CASE("A V2 generation does not carry over to a later V1 connection",
          "[core][protocol][v1][regression]") {
    // The device object is reused across connects. Resolving the generation only
    // when a name was supplied left the previous device's V2 version in place.
    auto transport = makeTransport();
    DeviceService service(transport, nullptr);

    service.connect(DeviceAddress("11:22:33:44:55:66"), "WH-1000XM5");
    REQUIRE(service.activeDevice()->protocolVersion() == SonyProtocolVersion::V2);

    transport->opcodes.clear();
    service.connect(DeviceAddress("AA:BB:CC:DD:EE:FF"), "MDR-1000X");

    CHECK(service.activeDevice()->protocolVersion() == SonyProtocolVersion::V1);
    CHECK_FALSE(transport->sent(kPowerOffOnV1));
}

TEST_CASE("A known V2 device still selects the V2 command set",
          "[core][protocol][v2]") {
    // The safe default must not silently downgrade devices that do support V2.
    auto transport = makeTransport();
    DeviceService service(transport, nullptr);

    service.connect(DeviceAddress("11:22:33:44:55:66"), "WH-1000XM5");

    REQUIRE(service.activeDevice() != nullptr);
    CHECK(service.activeDevice()->protocolVersion() == SonyProtocolVersion::V2);
}

TEST_CASE("A transport reporting V2 keeps an unrecognised name on the V2 command set",
          "[core][protocol][v2][regression]") {
    // macOS selects the generation from the SDP service record while
    // connecting (v2 UUID matched -> V2). An unrecognised Bluetooth name must
    // not silently downgrade that to V1, which drives a v2 link with v1
    // command layouts — the device never replies, so nothing works.
    auto transport = makeTransport();
    transport->setReportedGeneration(SdpGeneration::V2);
    DeviceService service(transport, nullptr);

    service.connect(DeviceAddress("11:22:33:44:55:66"), "Some Unknown Headset");

    REQUIRE(service.activeDevice() != nullptr);
    CHECK(service.activeDevice()->protocolVersion() == SonyProtocolVersion::V2);
}

TEST_CASE("A transport reporting V1 wins over a name that would resolve to V2",
          "[core][protocol][v1][regression]") {
    // A device that answered the v1 service record speaks the v1 command set
    // even if its name looks like a V2 model. Trusting SDP keeps opcode 0x22
    // (POWER OFF on v1) off the wire.
    auto transport = makeTransport();
    transport->setReportedGeneration(SdpGeneration::V1);
    DeviceService service(transport, nullptr);

    service.connect(DeviceAddress("11:22:33:44:55:66"), "WH-1000XM5");

    REQUIRE(service.activeDevice() != nullptr);
    CHECK(service.activeDevice()->protocolVersion() == SonyProtocolVersion::V1);
    CHECK_FALSE(transport->sent(kPowerOffOnV1));
}
