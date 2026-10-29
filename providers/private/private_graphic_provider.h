#pragma once

#include "graphic/color_mode_processor.h"
#include "graphic/curved_book_processor.h"
#include "graphic/format_convert_processor.h"
#include "graphic/single_page_processor.h"
#include "graphic/thumbnail_processor.h"
#include "i_sdk_graphic_provider.h"

namespace editor {
namespace sdk {
namespace private_impl {

class PrivateGraphicProvider : public ISdkGraphicProvider {
public:
    std::string ProviderName() const override;
    SdkImageProcessResult Process(const SdkImageProcessRequest& request) override;
    SdkPageProcessResult ProcessPage(const SdkPageProcessRequest& request) override;
    SdkColorModeResult ApplyColorMode(const SdkColorModeRequest& request) override;
    SdkFormatConvertResult ConvertImageFormat(const SdkFormatConvertRequest& request) override;
    SdkThumbnailResult GenerateThumbnail(const SdkThumbnailRequest& request) override;

private:
    graphic::ColorModeProcessor color_mode_processor_;
    graphic::CurvedBookProcessor curved_book_processor_;
    graphic::FormatConvertProcessor format_convert_processor_;
    graphic::SinglePageProcessor single_page_processor_;
    graphic::ThumbnailProcessor thumbnail_processor_;
};

} // namespace private_impl
} // namespace sdk
} // namespace editor
