#include "KeyboardStore.hpp"
#include "bridge/MacroCodec.hpp"
#include <InternalFileSystem.h>
#include <Adafruit_LittleFS.h>

using Adafruit_LittleFS_Namespace::File;
using Adafruit_LittleFS_Namespace::FILE_O_READ;
using Adafruit_LittleFS_Namespace::FILE_O_WRITE;
namespace {
constexpr const char* kDirectory = "/keyboard";
constexpr const char* kPending = "/keyboard/pending";
constexpr const char* kProfiles = "/keyboard/profiles";
constexpr const char* kSettings = "/keyboard/settings";
}
void KeyboardStore::path(unsigned slot, char* output, size_t size) {
    snprintf(output, size, "/keyboard/m%02u", slot);
}
int KeyboardStore::read(const char* filename, uint8_t* data, size_t capacity) {
    File file(filename, FILE_O_READ, InternalFS);
    if (!file || file.size() > capacity) return -1;
    const size_t size = file.size();
    const int count = file.read(data, size);
    return count == int(size) ? count : -1;
}
bool KeyboardStore::replace(const char* filename, const uint8_t* data, size_t length) {
    if (InternalFS.exists(kPending) && !InternalFS.remove(kPending)) return false;
    File file(kPending, FILE_O_WRITE, InternalFS);
    if (!file || file.write(data, length) != length) return false;
    file.flush(); file.close();
    uint8_t check[bridge::MacroCodec::kMaxSize];
    if (read(kPending, check, sizeof(check)) != int(length) || memcmp(check, data, length)) return false;
    // LittleFS rename is atomic, including replacement of an existing file.
    return InternalFS.rename(kPending, filename);
}
void KeyboardStore::begin(bridge::FactoryKeyboard& keyboard, bridge::HostProfiles& hosts) {
    // Bluefruit has already mounted InternalFS for its own bonding records.
    if (!InternalFS.exists(kDirectory) && !InternalFS.mkdir(kDirectory)) { ioError_ = true; return; }
    uint8_t data[bridge::MacroCodec::kMaxSize];
    if (InternalFS.exists(kSettings)) {
        bridge::FactorySettings settings;
        const int size = read(kSettings, data, sizeof(data));
        if (size < 0 || !bridge::MacroCodec::decodeSettings(data, size, settings)) corrupt_ = true;
        else keyboard.restoreSettings(settings);
    }
    if (InternalFS.exists(kProfiles)) {
        const int size = read(kProfiles, data, sizeof(data));
        if (size < 0 || !hosts.restore(data, size)) corrupt_ = true;
    }
    bridge::Macro macro;
    for (unsigned slot = 0; slot < bridge::FactoryKeyboard::kSlots; ++slot) {
        char filename[24]; path(slot, filename, sizeof(filename));
        if (!InternalFS.exists(filename)) continue;
        const int size = read(filename, data, sizeof(data));
        if (size < 0 || !bridge::MacroCodec::decode(slot, data, size, macro)) corrupt_ = true;
        else keyboard.restoreMacro(slot, macro);
    }
    if (xTaskCreate(worker, "storage", 1024, this, TASK_PRIO_LOW, &task_) != pdPASS) ioError_ = true;
}
void KeyboardStore::worker(void* context) {
    auto& self = *static_cast<KeyboardStore*>(context);
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (self.result_.load() != Result::Running) continue;
        char filename[24];
        if (self.pending_.profiles()) snprintf(filename, sizeof(filename), "%s", kProfiles);
        else if (self.pending_.settings()) snprintf(filename, sizeof(filename), "%s", kSettings);
        else path(self.pending_.slot(), filename, sizeof(filename));
        const bool directory = InternalFS.exists(kDirectory) || InternalFS.mkdir(kDirectory);
        const bool saved = directory && replace(filename, self.pending_.data(), self.pending_.size());
        self.result_.store(saved ? Result::Saved : Result::Failed);
    }
}
void KeyboardStore::poll(uint32_t now, bridge::FactoryKeyboard& keyboard, bridge::HostProfiles& hosts, bool idle) {
    const auto result = result_.load();
    if (result == Result::Running) return;
    if (result == Result::Saved || result == Result::Failed) {
        ioError_ = result == Result::Failed;
        if (!ioError_) pending_.accept(keyboard, hosts);
        result_.store(Result::Idle);
    }
    if (!task_ || !idle) return;
    if (now - lastAttempt_ < (ioError_ ? 5000u : 1000u)) return;
    if (!pending_.capture(keyboard, hosts)) return;
    lastAttempt_ = now;
    result_.store(Result::Running);
    xTaskNotifyGive(task_);
}
