#include "bridge/KeyboardDebounce.hpp"
#include "bridge/LightingDriver.hpp"

namespace {
volatile uint32_t& reg(uint32_t address) {
    return *reinterpret_cast<volatile uint32_t*>(address);
}

class Clock {
public:
    static void begin() {
        reg(0x40048238) &= ~3u; // IRC and IRC output powered.
        reg(0x40048070) = 0;   // MAINCLKSEL: internal 12 MHz RC.
        reg(0x40048074) = 0;
        reg(0x40048074) = 1;
        while (!(reg(0x40048074) & 1)) {}
        reg(0x40048078) = 1;   // AHB divider.
        // Reset defaults to three-cycle Flash reads (up to 50 MHz). At 12 MHz
        // one cycle is specified by UM10462; the extra waits break fast RX.
        reg(0x4003c010) &= ~3u;
        reg(0x40048080) |= (1u << 6) | (1u << 9) | (1u << 10) | (1u << 16);
        reg(0x40014004) = 2;   // CT32B0 reset.
        reg(0x4001400c) = 11;  // 12 MHz / 12 -> microsecond counter.
        reg(0x40014070) = 0;   // Timer mode, not capture counter mode.
        reg(0x40014004) = 1;
        reg(0x40018004) = 2;   // CT32B1: full 12 MHz precision for UART bits.
        reg(0x4001800c) = 0;
        reg(0x40018070) = 0;
        reg(0x40018004) = 1;
        reg(0xe000ed20) |= 0xff000000u; // SysTick must not delay the UART start ISR.
        reg(0xe000e014) = 11999; // SysTick wakes the main loop each millisecond.
        reg(0xe000e018) = 0;
        reg(0xe000e010) = 7;
    }
    static uint32_t now() { return reg(0x40014008); }
    static void delayUs(uint32_t duration) {
        const uint32_t start = now();
        while (now() - start < duration) {}
    }
};

class BitClock {
public:
    static constexpr uint32_t kBitTicks = (12000000 + bridge::kBaud / 2) / bridge::kBaud;
    static uint32_t now() { return reg(0x40018008); }
    static void wait(uint32_t start, uint32_t ticks) { while (now() - start < ticks) {} }
};

class Gpio {
public:
    static void configure(bridge::Pin pin, bool pullUp) {
        const bool analog = pin.port == 0 &&
            ((pin.bit >= 11 && pin.bit <= 16) || pin.bit == 22 || pin.bit == 23);
        const uint32_t function = pin.port == 0 && pin.bit >= 10 && pin.bit <= 15 ? 1 : 0;
        reg(0x40044000 + 4u * (pin.port * 24u + pin.bit)) =
            function | (pullUp ? 0x10u : 0u) | (analog ? 0x80u : 0u);
    }
    static void output(bridge::Pin pin, bool enabled) {
        auto& direction = reg(0x50002000 + pin.port * 4u);
        if (enabled) direction |= 1u << pin.bit;
        else direction &= ~(1u << pin.bit);
    }
    static void write(bridge::Pin pin, bool high) {
        reg((high ? 0x50002200 : 0x50002280) + pin.port * 4u) = 1u << pin.bit;
    }
    static bool read(bridge::Pin pin) {
        return (reg(0x50002100 + pin.port * 4u) & (1u << pin.bit)) != 0;
    }
};

class MatrixScanner {
public:
    void begin() {
        for (auto pin : bridge::kColumnPins) {
            Gpio::output(pin, false);
            Gpio::configure(pin, true);
        }
        for (auto pin : bridge::kRowPins) {
            Gpio::output(pin, false);
            Gpio::configure(pin, true);
            Gpio::write(pin, false);
        }
    }
    void scan(uint8_t* rows) {
        for (size_t row = 0; row < bridge::kRows; ++row) {
            const auto pin = bridge::kRowPins[row];
            Gpio::configure(pin, false);
            Gpio::output(pin, true); // Only selected row drives low; others are inputs.
            Clock::delayUs(10);
            uint8_t pressed = 0;
            for (size_t column = 0; column < bridge::kColumns; ++column)
                if (!Gpio::read(bridge::kColumnPins[column])) pressed |= 1u << column;
            rows[row] = pressed;
            Gpio::output(pin, false);
            Gpio::configure(pin, true);
            Clock::delayUs(2);
        }
    }
};

class SoftwareReceiver {
public:
    void begin() {
        Gpio::output(kRx, false);
        Gpio::configure(kRx, true);
        reg(0x40048080) |= 1u << 19; // PINT register clock.
        reg(0x40048178) = 10;       // PIN_INT0 = PIO0_10 (SWCLK).
        reg(0x4004c000) &= ~1u;     // Edge triggered.
        reg(0x4004c00c) = 1;        // No rising-edge interrupt.
        reg(0x4004c014) = 1;        // Falling-edge start bit.
        clearPending();
        reg(0xe000e100) = 1;
    }
    __attribute__((optimize("O2"))) void interrupt(uint32_t started) {
        clearPending();
        if (lineHigh()) return;
        // Receive the contiguous DMA burst in one bounded ISR. Returning and
        // re-entering for each byte at 12 MHz loses back-to-back start bits.
        // nRF sends a 12-byte command only after a complete matrix frame.
        for (unsigned count = 0; count < sizeof(data_); ++count) {
            BitClock::wait(started, BitClock::kBitTicks / 2);
            if (lineHigh()) break;
            uint8_t value = 0;
            for (unsigned bit = 0; bit < 8; ++bit) {
                BitClock::wait(started, BitClock::kBitTicks + BitClock::kBitTicks / 2 + bit * BitClock::kBitTicks);
                if (lineHigh()) value |= 1u << bit;
            }
            const uint8_t next = (head_ + 1) % sizeof(data_);
            const bool space = next != tail_;
            if (space) data_[head_] = value;
            BitClock::wait(started, 9 * BitClock::kBitTicks + BitClock::kBitTicks / 4);
            if (!lineHigh()) break;
            if (space) head_ = next;
            const uint32_t stopped = BitClock::now();
            while (lineHigh()) {
                if (BitClock::now() - stopped >= 2 * BitClock::kBitTicks) {
                    finish(); return;
                }
            }
            started = BitClock::now();
        }
        finish();
    }
    int read() {
        if (tail_ == head_) return -1;
        const uint8_t value = data_[tail_];
        tail_ = (tail_ + 1) % sizeof(data_);
        return value;
    }
    bool busy(uint32_t now) const { return head_ != tail_ || now - lastByte_ < 1000 || !Gpio::read(kRx); }
    static void clearPending() { reg(0x4004c024) = 1; reg(0xe000e280) = 1; }
private:
    void finish() { lastByte_ = Clock::now(); clearPending(); }
    // Avoid the generic GPIO call overhead inside the half-bit timing margin.
    static bool lineHigh() { return (reg(0x50002100) & (1u << 10)) != 0; }
    static constexpr bridge::Pin kRx = {0, 10};
    volatile uint8_t data_[32] = {};
    volatile uint8_t head_ = 0, tail_ = 0;
    volatile uint32_t lastByte_ = 0;
};
SoftwareReceiver receiver;

class LedBus {
public:
    void begin() {
        reg(0x40048004) |= 2; // Release I2C reset.
        reg(0x40048080) |= 1u << 5;
        reg(0x40044010) = (reg(0x40044010) & ~0x3fu) | 1; // PIO0_4 SCL.
        reg(0x40044014) = (reg(0x40044014) & ~0x3fu) | 1; // PIO0_5 SDA.
        reg(kBase + 0x18) = 0x6c;
        reg(kBase + 0x10) = 60; // 100 kHz at IRC 12 MHz.
        reg(kBase + 0x14) = 60;
        reg(kBase) = 0x40;
    }
    bool write(uint8_t address, uint8_t index, uint8_t value) {
        reg(kBase) = 0x20;
        bool ok = wait(0x08);
        if (ok) {
            reg(kBase + 0x08) = address << 1;
            reg(kBase + 0x18) = 0x28;
            ok = wait(0x18);
        }
        if (ok) ok = send(index);
        if (ok) ok = send(value);
        reg(kBase) = 0x10; // STOP, including NACK and timeout exits.
        reg(kBase + 0x18) = 0x28;
        const uint32_t started = Clock::now();
        while ((reg(kBase) & 0x10) && Clock::now() - started < 1000) {}
        if (reg(kBase) & 0x10) { begin(); return false; }
        return ok;
    }
private:
    bool send(uint8_t value) {
        reg(kBase + 0x08) = value;
        reg(kBase + 0x18) = 8;
        return wait(0x28);
    }
    bool wait(uint8_t status) {
        const uint32_t started = Clock::now();
        while (!(reg(kBase) & 8)) if (Clock::now() - started > 1000) return false;
        return (reg(kBase + 4) & 0xf8) == status;
    }
    static constexpr uint32_t kBase = 0x40000000;
};

class LockIndicators {
public:
    void begin() {
        for (auto pin : kPins) { Gpio::write(pin, true); Gpio::configure(pin, false); Gpio::output(pin, true); }
    }
    void set(uint8_t leds) {
        for (unsigned i = 0; i < 3; ++i) Gpio::write(kPins[i], !(leds & (1u << i)));
    }
private:
    // Factory handler 0x2f5a: Num, Caps, Scroll. Outputs are active low.
    static constexpr bridge::Pin kPins[] = {{1, 15}, {1, 14}, {0, 23}};
};

class SoftwareTransmitter {
public:
    void begin() {
        Gpio::write(kTx, true);
        Gpio::output(kTx, true);
        Gpio::configure(kTx, true); // SWDIO now becomes GPIO TX.
    }
    void send(const uint8_t* rows, uint8_t acknowledged, uint8_t flags) {
        uint8_t frame[bridge::kStatusFrameSize];
        bridge::encodeStatus(rows, sequence_++, acknowledged, flags, frame);
        // Polling timer deadlines avoid compiler-dependent delay loops. No ISR
        // may interrupt a bit; SysTick is only a wake source, not our timebase.
        __asm volatile("cpsid i" ::: "memory");
        for (uint8_t value : frame) {
            const uint32_t start = BitClock::now();
            reg(0x50002280) = 1u << 15;
            for (uint32_t bit = 0; bit < 8; ++bit) {
                BitClock::wait(start, (bit + 1) * BitClock::kBitTicks);
                reg((value >> bit) & 1 ? 0x50002200 : 0x50002280) = 1u << 15;
            }
            BitClock::wait(start, 9 * BitClock::kBitTicks);
            reg(0x50002200) = 1u << 15;
            BitClock::wait(start, 10 * BitClock::kBitTicks);
        }
        SoftwareReceiver::clearPending();
        __asm volatile("cpsie i" ::: "memory");
    }
private:
    static constexpr bridge::Pin kTx = {0, 15};
    uint8_t sequence_ = 0;
};

class PowerDown {
public:
    static bool released(const uint8_t* rows) {
        for (unsigned i = 0; i < bridge::kRows; ++i) if (rows[i]) return false;
        return true;
    }
    [[noreturn]] static void enter() {
        // The final ready frame has finished. TX stays high until a wake key.
        // Keep all other matrix pins pulled up to avoid floating input buffers.
        __asm volatile("cpsid i" ::: "memory");
        reg(0xe000e010) = 0;          // Stop SysTick and its pending wake.
        reg(0xe000ed04) = 1u << 25;
        reg(0xe000e180) = 0xffffffff;
        reg(0xe000e280) = 0xffffffff;
        Gpio::configure(bridge::kRowPins[16], false);
        Gpio::write(bridge::kRowPins[16], false);
        Gpio::output(bridge::kRowPins[16], true);
        // PINTSEL uses the IOCON numbering: port 1 starts at 24, NOT 32.
        reg(0x40048178) = 24 + 25;   // PIN_INT0 = PIO1_25, R16/C7.
        reg(0x4004c000) &= ~1u;      // Falling edge, UART receiver now stopped.
        reg(0x4004c00c) = 1;
        reg(0x4004c014) = 1;
        SoftwareReceiver::clearPending();
        reg(0x40048204) = 1;         // STARTERP0: only PIN_INT0 wakes us.
        reg(0x40048214) = 0;         // STARTERP1: no peripheral wake sources.
        reg(0xe000e100) = 1;
        reg(0x40014004) = 0;         // Stop CT32B0.
        reg(0x40018004) = 0;         // Stop CT32B1.
        reg(0x40048230) |= 0x7f;     // Reserved bits + BOD/WDT oscillator off.
        reg(0x40048234) = reg(0x40048238) & ~7u; // Restore IRC + flash on wake.
        reg(0x40038000) = (reg(0x40038000) & ~7u) | 2; // PMU Power-down.
        reg(0xe000ed10) |= 4;        // SLEEPDEEP.
        __asm volatile("dsb" ::: "memory");
        // PRIMASK prevents the old UART ISR from consuming the wake edge.
        // An enabled NVIC interrupt still wakes WFI while globally masked.
        if (Gpio::read(bridge::kColumnPins[7])) __asm volatile("wfi");
        reg(0xe000ed10) &= ~4u;
        Gpio::write({0, 15}, false); // Wake nRF through existing SWDIO / P0.08.
        // nRF's reset sequence cycles the rail. Hold LOW until that happens.
        reg(0xe000e180) = 0xffffffff;
        reg(0xe000e280) = 0xffffffff;
        for (;;) __asm volatile("dsb\nwfi");
    }
};
}

extern "C" void SysTick_Handler() {}
extern "C" __attribute__((optimize("O2"))) void uartReceive(uint32_t started) { receiver.interrupt(started); }

extern "C" int main() {
    Clock::begin();
    __asm volatile("cpsie i" ::: "memory");
    // Recovery window after every reset: SWD remains available for three seconds.
    const uint32_t started = Clock::now();
    while (Clock::now() - started < 3000000) __asm volatile("wfi");
    MatrixScanner scanner;
    SoftwareTransmitter transmitter;
    bridge::Debouncer<bridge::KeyboardDebounceTiming> debounce;
    LedBus bus;
    bridge::LightingDriver<LedBus> leds(bus);
    LockIndicators locks;
    bridge::ControlParser commands;
    bridge::LightingState lighting{0, 0, 5, 5, 0, false, false};
    scanner.begin();
    transmitter.begin();
    receiver.begin();
    bus.begin();
    locks.begin();
    uint32_t lastScan = Clock::now();
    uint32_t lastSend = lastScan - 100000;
    uint32_t lastLeds = lastScan - 10000;
    uint8_t acknowledged = 0, ledStatus = 0;
    bool hasCommand = false, changed = true, lightsApplied = false;
    for (;;) {
        const uint32_t now = Clock::now();
        for (int byte = receiver.read(); byte >= 0; byte = receiver.read()) {
            if (commands.push(byte, Clock::now(), lighting, acknowledged)) {
                hasCommand = true;
                lightsApplied = false;
                locks.set(lighting.visibleHostLeds());
            }
        }
        if (now - lastLeds >= 10000) {
            ledStatus = leds.apply(lighting, now / 1000);
            lightsApplied = ledStatus == 3;
            lastLeds = Clock::now();
        }
        if (now - lastScan >= 1000) {
            lastScan = now;
            uint8_t raw[bridge::kRows];
            scanner.scan(raw);
            changed |= debounce.update(raw, Clock::now());
        }
        // Leave 4 ms for a 1 ms nRF poll, a 1.04 ms reply and the RX guard.
        // Scan between transfers; a reverse command briefly occupies the RX ISR.
        const uint32_t sendNow = Clock::now();
        if ((changed || sendNow - lastSend >= 100000) && sendNow - lastSend >= 4000 && !receiver.busy(sendNow)) {
            // ACK sleep only after lights are off and every key is released.
            // A failed LED bus or a held key leaves the nRF awake to recover.
            const bool sleepReady = lighting.sleep && ledStatus == 3 &&
                PowerDown::released(debounce.rows()) && lightsApplied;
            transmitter.send(debounce.rows(), acknowledged, uint8_t(hasCommand) | (ledStatus << 1) |
                             bridge::kSleepSupported | (sleepReady ? bridge::kSleepReady : 0));
            if (sleepReady) PowerDown::enter();
            lastSend = Clock::now();
            changed = false;
        }
        __asm volatile("wfi");
    }
}
