module;
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
export module GPP.Graphics:Assets.Image;

export
{
    using ::stbi_uc;
    using ::stbi_us;

    using ::STBI_default;
    using ::STBI_grey;
    using ::STBI_grey_alpha;
    using ::STBI_rgb;
    using ::STBI_rgb_alpha;

    using ::stbi_load;
    using ::stbi_load_16;
    using ::stbi_loadf;
    using ::stbi_load_from_memory;
    using ::stbi_load_16_from_memory;
    using ::stbi_loadf_from_memory;
    using ::stbi_is_hdr;
    using ::stbi_image_free;
    using ::stbi_failure_reason;
    using ::stbi_info;
    using ::stbi_set_flip_vertically_on_load;

    using ::stbi_write_png;
    using ::stbi_write_jpg;
}
