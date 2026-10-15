#include "dialogue_backlog.h"
#include "gbaimp/text_history.h"
#include "armv4t/runtime_arm.h"
#include "cutscene_dialogue_data.inl"
#include <algorithm>
#include <string>

namespace khcom {

namespace {

const char* get_speaker_name(uint8_t spk_id, const std::string& text = "") {
    (void)text;
    switch (spk_id) {
        case 0:  return "Sora";
        case 1:  return "Donald";
        case 2:  return "Goofy";
        case 3:  return "Marluxia";
        case 6:  return "Jiminy Cricket";
        case 20: return "Larxene";
        case 24: return "Donald";
        case 25: return "Goofy";
        case 26: return "Riku";
        case 27: return "Riku";
        case 31: return "Axel";
        case 32: return "Aerith";
        case 33: return "Leon";
        case 37: return "Yuffie";
        case 38: return "Vexen";
        case 39: return "Selphie";
        case 40: return "Tidus";
        case 41: return "Wakka";
        case 54: return "Larxene";
        case 58: return "Marluxia";
        case 60: return "Naminé";
        case 62: return "Tutorial";
        default: return "Dialogue";
    }
}

const DialogueLookupEntry* find_cutscene_dialogue(uint32_t ptr) {
    auto it = std::lower_bound(std::begin(kCutsceneDialogues), std::end(kCutsceneDialogues), ptr,
        [](const DialogueLookupEntry& e, uint32_t val) {
            return e.text_ptr < val;
        });
    if (it != std::end(kCutsceneDialogues) && it->text_ptr == ptr) {
        return &(*it);
    }
    return nullptr;
}



std::string decode_dialogue_string(uint32_t addr) {
    ScopedInternalBusRead guard;
    std::string result;
    result.reserve(128);
    for (size_t i = 0; i < 512; ++i) {
        uint16_t c = bus_read_u16(addr + i * 2);
        if (c == 0 || c == 0xFFFF) break;
        if (c >= 32 && c <= 126) {
            result.push_back(static_cast<char>(c));
        } else if (c == 0x0A) {
            result.push_back('\n');
        } else if (c == 0x85) {
            result += "...";
        } else if (c == 0x92) {
            result += "'";
        } else if (c == 0x93 || c == 0x94) {
            result += "\"";
        } else if (c == 0xE9) {
            result += "é";
        } else if (c >= 0xE000) {
            break;
        }
    }
    return result;
}

} // namespace

static thread_local bool s_internal_bus_read = false;

bool is_internal_bus_read() {
    return s_internal_bus_read;
}

void set_internal_bus_read(bool active) {
    s_internal_bus_read = active;
}

DialogueBacklog& DialogueBacklog::instance() {
    static DialogueBacklog s_instance;
    return s_instance;
}

DialogueBacklog::DialogueBacklog() = default;

void DialogueBacklog::on_bus_read_u16(uint32_t addr) {
    if (addr < 0x08FBD378u || addr > 0x08FFE504u) {
        return;
    }

    // Step 1: Detect start of a cutscene dialogue line
    if (const auto* match = find_cutscene_dialogue(addr)) {
        if (addr != last_pushed_text_ptr_) {
            pending_dialogue_ptr_ = addr;
            pending_speaker_id_ = match->speaker_id;
        }
        return;
    }

    // Step 2: Confirm active typewriter rendering when sequential character 1 or 2 is read
    if (pending_dialogue_ptr_ != 0 && (addr == pending_dialogue_ptr_ + 2 || addr == pending_dialogue_ptr_ + 4)) {
        uint32_t target_ptr = pending_dialogue_ptr_;
        uint8_t spk_id = pending_speaker_id_;
        pending_dialogue_ptr_ = 0;

        if (target_ptr != last_pushed_text_ptr_) {
            std::string dtext = decode_dialogue_string(target_ptr);
            if (!dtext.empty() && dtext.size() >= 3) {
                const char* spk_name = get_speaker_name(spk_id, dtext);
                gbaimp::TextHistory::instance().push_entry(spk_name, dtext);
                last_pushed_text_ptr_ = target_ptr;
            }
        }
    }
}

void DialogueBacklog::clear() {
    gbaimp::TextHistory::instance().clear();
    last_pushed_text_ptr_ = 0;
    pending_dialogue_ptr_ = 0;
    pending_speaker_id_ = 0;
}

// Read-only observer on every guest bus read: feeds the backlog (F2 history). It never overrides a value.
static int khcom_bus_read_hook(uint32_t pc, uint32_t addr, uint32_t width, uint32_t value, uint32_t* overridden) {
    (void)pc;
    (void)value;
    (void)overridden;

    if (is_internal_bus_read()) {
        return 0;
    }

    if (width == 1) {
        DialogueBacklog::instance().on_bus_read_u16(addr & ~1u);
    } else if (width == 2) {
        DialogueBacklog::instance().on_bus_read_u16(addr);
    } else if (width == 4) {
        DialogueBacklog::instance().on_bus_read_u16(addr);
        DialogueBacklog::instance().on_bus_read_u16(addr + 2);
    }
    return 0;
}

void khcom_install_backlog_hook() {
    g_runtime_bus_read_override = &khcom_bus_read_hook;
}

} // namespace khcom
