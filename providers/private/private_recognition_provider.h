#pragma once

#include "i_sdk_recognition_provider.h"

namespace editor {
namespace sdk {
namespace private_impl {

class PrivateRecognitionProvider : public ISdkRecognitionProvider {
public:
    std::string ProviderName() const override;
    SdkBarcodeDetectResult DetectBarcode(const SdkBarcodeDetectRequest& request) override;
};

} // namespace private_impl
} // namespace sdk
} // namespace editor
