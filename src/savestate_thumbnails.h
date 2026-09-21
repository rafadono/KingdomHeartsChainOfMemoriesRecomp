#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace khcom {

struct SavestateMetadata {
    int slot = 0;
    bool exists = false;
    std::string timestamp;
    std::string image_path;
    int width = 0;
    int height = 0;
};

class SavestateThumbnailManager {
public:
    static SavestateThumbnailManager& instance();

    void capture_thumbnail(int slot, const uint32_t* rgba_pixels, int width, int height);
    SavestateMetadata get_slot_metadata(int slot);
    std::string get_thumbnail_path(int slot) const;

private:
    SavestateThumbnailManager();
    void ensure_save_directory();
};

} // namespace khcom
