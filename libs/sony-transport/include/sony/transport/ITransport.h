#pragma once

#include "DeviceAddress.h"
#include <cstddef>
#include <optional>
#include <span>

namespace sony::transport {

// Wire-protocol generation resolved from platform service discovery (which
// Sony SDP service record the RFCOMM link was opened against). Kept separate
// from sony::protocol::SonyProtocolVersion / ProtocolGeneration so the
// transport layer does not depend on the protocol layer.
enum class SdpGeneration {
    V1,
    V2
};

class ITransport {
public:
    virtual ~ITransport() = default;

    virtual void connect(const DeviceAddress& address) = 0;
    virtual void disconnect() noexcept = 0;
    [[nodiscard]] virtual bool isConnected() const noexcept = 0;
    virtual size_t send(std::span<const std::byte> data) = 0;
    virtual size_t receive(std::span<std::byte> buffer) = 0;

    // Generation reported by the underlying platform connector, resolved from
    // the SDP service record during connect(). nullopt when the transport has
    // no platform evidence (e.g. FakeTransport).
    [[nodiscard]] virtual std::optional<SdpGeneration> sdpGeneration() const noexcept {
        return std::nullopt;
    }
};

} // namespace sony::transport
