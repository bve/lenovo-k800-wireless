#include <assert.h>
#include <array>
#include <map>
#include <vector>
#include <stdio.h>
#include "LpcProgrammer.hpp"

void delay(unsigned long) {}

class Target : public DebugTarget {
public:
    bool attach() override { return connected; }
    uint32_t diagnostic() const override { return 0x0101a507; }
    void release(bool restart) override { released = restart; }
    bool waitHalted(uint32_t) override { return !timeout; }
    bool writeRegister(uint8_t reg, uint32_t value) override { registers[reg] = value; return true; }
    bool readMemory(uint32_t address, uint32_t& value) override {
        if (address < flash.size()) {
            value = bridge::read32(flash.data() + address);
            if (corruptRead && address == 0x1000) value ^= 1;
        } else if (address == 0x400483f4) value = part;
        else value = memory[address];
        return true;
    }
    bool writeMemory(uint32_t address, uint32_t value) override {
        memory[address] = value;
        if (address == 0xe000edf0 && value == 0xa05f0001) {
            assert(registers[15] == 0x1fff1ff0 && registers[14] == 0x10000001);
            assert(registers[17] == 0x10000fe0 && registers[20] == 1);
            assert(registers[0] == 0x10000040 && registers[1] == 0x10000060);
            const uint32_t command = memory[0x10000040], a = memory[0x10000044];
            const uint32_t b = memory[0x10000048], c = memory[0x1000004c];
            memory[0x10000060] = 0;
            if (command == failedCommand) { memory[0x10000060] = 11; return true; }
            if (command == 50) { preparedStart = a; preparedEnd = b; }
            else if (command == 52) {
                assert(a == 0 && b == 5 && c == 12000 && preparedStart == 0 && preparedEnd == 5);
                flash.fill(0xff); erased = true; preparedStart = 99;
            } else if (command == 53) {
                for (auto byte : flash) assert(byte == 0xff);
            } else if (command == 51) {
                assert(erased && a % 256 == 0 && b == 0x10000100 && c == 256);
                assert(preparedStart == a / 4096 && preparedEnd == a / 4096);
                assert(memory[0x10000050] == 12000);
                for (uint32_t i = 0; i < c; i += 4) bridge::write32(flash.data() + a + i, memory[b + i]);
                writes.push_back(a); preparedStart = 99;
            } else assert(false);
        }
        return true;
    }
    std::array<uint8_t, bridge::kLpcFlashSize> flash{};
    std::map<uint32_t, uint32_t> memory, registers;
    std::vector<uint32_t> writes;
    uint32_t part = 0x297a802b, preparedStart = 99, preparedEnd = 99, failedCommand = 0;
    bool erased = false, released = false, connected = true, timeout = false, corruptRead = false;
};

std::vector<uint8_t> image() {
    std::vector<uint8_t> data(0x884, 0xff); // Exercise partial final page.
    for (unsigned i = 0; i < 8; ++i) bridge::write32(data.data() + i * 4, 0);
    bridge::write32(data.data(), 0x10001000);
    bridge::write32(data.data() + 4, 0x301);
    bridge::write32(data.data() + 28, 0u - 0x10001000 - 0x301);
    return data;
}
void progress(uint32_t bytes, void* context) {
    auto& last = *static_cast<uint32_t*>(context);
    assert(bytes > last); last = bytes;
}
int main() {
    using bridge::UpdateError;
    auto data = image();
    assert(bridge::crc32(reinterpret_cast<const uint8_t*>("123456789"), 9) == 0xcbf43926);
    bridge::LpcImageBuffer buffer;
    assert(buffer.begin(0xffffffff, 0) == UpdateError::Size);
    assert(buffer.validate() == UpdateError::Incomplete);
    assert(buffer.begin(data.size(), bridge::crc32(data.data(), data.size())) == UpdateError::None);
    assert(buffer.append(1, data.data(), 4) == UpdateError::Offset);
    assert(buffer.append(0, data.data(), 0xffffffff) == UpdateError::Size);
    assert(buffer.append(0, data.data(), 16) == UpdateError::None);
    assert(buffer.validate() == UpdateError::Incomplete);
    assert(buffer.append(16, data.data() + 16, data.size() - 16) == UpdateError::None);
    assert(buffer.validate() == UpdateError::None);
    buffer.begin(data.size(), 123);
    buffer.append(0, data.data(), data.size());
    assert(buffer.validate() == UpdateError::Crc);
    for (uint32_t crp : {0x12345678u, 0x87654321u, 0x43218765u, 0x4e697370u}) {
        bridge::write32(data.data() + 0x2fc, crp);
        assert(bridge::validateLpcImage(data.data(), data.size()) == UpdateError::Protection);
    }
    data = image();
    uint32_t last = 0;
    Target target;
    LpcProgrammer programmer(target);
    assert(programmer.program(data.data(), data.size(), progress, &last) == UpdateError::None);
    assert(target.released && target.erased && last == data.size());
    assert(target.writes.front() == 256 && target.writes.back() == 0 && target.writes.size() == 9);
    for (uint32_t i = 0; i < target.flash.size(); ++i)
        assert(target.flash[i] == (i < data.size() ? data[i] : 0xff));

    Target wrong;
    wrong.part = 0x1234;
    LpcProgrammer wrongProgrammer(wrong);
    assert(wrongProgrammer.program(data.data(), data.size(), progress, &last) == UpdateError::Target);
    assert(!wrong.erased && wrong.released && wrongProgrammer.detail() == 0x1234);
    Target absent;
    absent.connected = false;
    LpcProgrammer absentProgrammer(absent);
    assert(absentProgrammer.program(data.data(), data.size(), progress, &last) == UpdateError::DebugPort);
    assert(!absent.erased && absent.released && absentProgrammer.detail() == 0x0101a507);
    Target failed;
    failed.failedCommand = 51;
    LpcProgrammer failedProgrammer(failed);
    assert(failedProgrammer.program(data.data(), data.size(), progress, &last) == UpdateError::Iap);
    assert(failed.erased && failed.released && failedProgrammer.detail() == 11);
    assert(bridge::read32(failed.flash.data()) == 0xffffffff);
    Target mismatch;
    mismatch.corruptRead = true; last = 0;
    LpcProgrammer mismatchProgrammer(mismatch);
    assert(mismatchProgrammer.program(data.data(), data.size(), progress, &last) == UpdateError::Verify);
    assert(mismatchProgrammer.detail() == 0x1000 && mismatch.released);
    Target timeout;
    timeout.timeout = true;
    LpcProgrammer timeoutProgrammer(timeout);
    assert(timeoutProgrammer.program(data.data(), data.size(), progress, &last) == UpdateError::Iap);
    assert(!timeout.erased && timeout.released);
    data[4] ^= 2;
    Target invalid;
    LpcProgrammer invalidProgrammer(invalid);
    assert(invalidProgrammer.program(data.data(), data.size(), progress, &last) == UpdateError::Vectors);
    assert(!invalid.erased && invalid.memory.empty());
    puts("PASS: image staging, protection, IAP ordering, verification and failure recovery");
}
