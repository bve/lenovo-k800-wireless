// Prepare both vector tables in the LPC11U13 keyboard dump for analysis.
// @category Keyboard
import ghidra.app.script.GhidraScript;
import ghidra.program.model.lang.Register;
import java.math.BigInteger;

public class PrepareKeyboard extends GhidraScript {
    @Override
    public void run() throws Exception {
        var memory = currentProgram.getMemory();
        memory.createUninitializedBlock("SRAM", toAddr(0x10000000L), 0x1000, false);
        memory.createUninitializedBlock("USB_SRAM", toAddr(0x20004000L), 0x800, false);
        memory.createUninitializedBlock("APB", toAddr(0x40000000L), 0x80000, false);
        memory.createUninitializedBlock("GPIO", toAddr(0x50000000L), 0x3000, false);
        memory.createUninitializedBlock("Cortex", toAddr(0xe000e000L), 0x1000, false);
        Register thumb = currentProgram.getRegister("TMode");
        currentProgram.getProgramContext().setValue(thumb, toAddr(0), toAddr(0x5fff), BigInteger.ONE);
        for (long base : new long[]{0, 0x2000}) {
            for (int index = 1; index < 48; index++) {
                long target = Integer.toUnsignedLong(getInt(toAddr(base + 4 * index)));
                if ((target & 1) != 0 && target < 0x6000) {
                    var address = toAddr(target & ~1L);
                    disassemble(address);
                    if (getFunctionAt(address) == null) createFunction(address, null);
                }
            }
        }
    }
}
