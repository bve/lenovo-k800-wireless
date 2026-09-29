// Export decompiler output for review; this is not recovered buildable source.
// @category Keyboard
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import java.io.PrintWriter;

public class ExportKeyboard extends GhidraScript {
    @Override
    public void run() throws Exception {
        DecompInterface decompiler = new DecompInterface();
        try (PrintWriter output = new PrintWriter(getScriptArgs()[0])) {
            decompiler.openProgram(currentProgram);
            var functions = currentProgram.getFunctionManager().getFunctions(true);
            while (functions.hasNext() && !monitor.isCancelled()) {
                var function = functions.next();
                var result = decompiler.decompileFunction(function, 30, monitor);
                output.println("\n/* Address: " + function.getEntryPoint() + " */");
                if (result.decompileCompleted()) {
                    output.println(result.getDecompiledFunction().getC());
                } else {
                    output.println("/* Decompilation failed: " + result.getErrorMessage() + " */");
                }
            }
        } finally {
            decompiler.dispose();
        }
    }
}
