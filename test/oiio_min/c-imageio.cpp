#include <OpenImageIO/filesystem.h>
#include <OpenImageIO/imageio.h>

// FIXME: attributes in macros don't work
// #define CPPMM_IGNORE        __attribute__((annotate("cppmm:ignore")))
// #define CPPMM_MANUAL(fn)    __attribute__((annotate("cppmm:manual" # fn)))

namespace cppmm_bind {
namespace OIIO {

using ::OIIO::imagesize_t;
using ::OIIO::ProgressCallback;
using ::OIIO::stride_t;
using ::OIIO::string_view;

class ImageSpec {
    ImageSpec(::OIIO::TypeDesc format) noexcept
        __attribute__((annotate("cppmm:manual:new")));

    ~ImageSpec() __attribute__((annotate("cppmm:manual:delete")));

    ImageSpec(int xres, int yres, int nchans, ::OIIO::TypeDesc fmt) noexcept
        __attribute__((annotate("cppmm:manual:new_with_dimensions")));

    // TODO: assign name for copy ctor arg (gets lost)
    ImageSpec(const ::OIIO::ImageSpec& other)
        __attribute__((annotate("cppmm:rename:copy")));

    ::OIIO::ImageSpec& operator=(const ::OIIO::ImageSpec& other)
        __attribute__((annotate("cppmm:rename:assign")));

    ImageSpec(::OIIO::ImageSpec&& other)
        __attribute__((annotate("cppmm:ignore")));

    ::OIIO::ImageSpec& operator=(::OIIO::ImageSpec&& other)
        __attribute__((annotate("cppmm:ignore")));

    void set_format(::OIIO::TypeDesc fmt) noexcept;

    size_t channel_bytes() const noexcept;

    size_t channel_bytes(int chan, bool native) const noexcept
        __attribute__((annotate("cppmm:rename:channel_bytes_for")));

    imagesize_t scanline_bytes(bool native) const noexcept;

    static void auto_stride(stride_t& xstride, stride_t& ystride,
                            stride_t& zstride, stride_t channelsize,
                            int nchannels, int width, int height) noexcept;

    void attribute(string_view name, ::OIIO::TypeDesc type, const void* value);

    // TODO: enums
    enum SerialFormat {};
    enum SerialVerbose {};

    // TODO: returning strings
    std::string serialize(::OIIO::ImageSpec::SerialFormat format,
                          ::OIIO::ImageSpec::SerialVerbose verbose) const;
    // TODO: returning arrays with an out pointer
    void get_channelformats(std::vector<::OIIO::TypeDesc>& formats) const;

    void default_channel_names();
} __attribute__((annotate("cppmm:opaqueptr")));

class ImageInput {

    using unique_ptr = ::OIIO::ImageInput::unique_ptr;

    ImageInput() __attribute__((annotate("cppmm:ignore")));
    ImageInput(const ::OIIO::ImageInput& other)
        __attribute__((annotate("cppmm:rename:copy")));

    ::OIIO::ImageInput& operator=(const ::OIIO::ImageInput& other)
        __attribute__((annotate("cppmm:rename:assign")));

    const char* format_name(void) const;

    static unique_ptr open(const std::string& filename,
                           const ::OIIO::ImageSpec* config,
                           ::OIIO::Filesystem::IOProxy* ioproxy);

    static std::unique_ptr<::OIIO::ImageInput>
    create(::OIIO::string_view, bool, const ::OIIO::ImageSpec*,
           ::OIIO::Filesystem::IOProxy*, ::OIIO::string_view);

    bool read_image(int subimage, int miplevel, int chbegin, int chend,
                    ::OIIO::TypeDesc format, void* data, stride_t xstride,
                    stride_t ystride, stride_t zstride,
                    ProgressCallback progress_callback,
                    void* progress_callback_data);

    std::string geterror() const
        __attribute__((annotate("cppmm:manual:geterror")));
} __attribute__((annotate("cppmm:opaqueptr")));

bool getattribute(::OIIO::string_view, ::OIIO::TypeDesc, void*);
} // namespace OIIO
} // namespace cppmm_bind

namespace cppmm_manual {
// FIXME: doing this doesn't work great because it will cause errors as the
// types we're using having been generated yet...
/*
using OIIO_ImageInput = OIIO::ImageInput;
void OIIO_ImageInput_geterror(OIIO_ImageInput* self, char* errormsg, int len,
                          bool clear) {
const std::string errmsg = to_cpp(self)->geterror(clear);
OIIO::Strutil::safe_strcpy(errormsg, errmsg.c_str(), len);
}
*/
} // namespace cppmm_manual