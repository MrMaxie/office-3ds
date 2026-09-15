#pragma once

#include "office3ds/bridge/pairing.hpp"
#include "office3ds/platform/credential.hpp"

#include <string>

namespace office3ds::platform_3ds {

class BridgeClaimSource final : public platform::CredentialClaimSource {
public:
  BridgeClaimSource(bridge::PairingEndpoint endpoint, std::string pairing_material,
                    bool uses_qr_secret);
  ~BridgeClaimSource() override;

  [[nodiscard]] platform::CredentialClaimResult poll() override;
  void cancel() noexcept override;

private:
  bridge::PairingEndpoint endpoint_;
  std::string pairing_material_;
  bool uses_qr_secret_ = false;
  bool finished_ = false;
};

} // namespace office3ds::platform_3ds
