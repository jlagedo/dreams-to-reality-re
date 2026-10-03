/* Create a function at each address argument that no function contains yet
 * (code reached only through a pointer, which auto-analysis left as a label).
 * Usage: -postScript MakeFunction.java 0040d564 [...]
 * @category Guardian
 */
import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

public class MakeFunction extends GhidraScript {
    @Override
    protected void run() throws Exception {
        for (String arg : getScriptArgs()) {
            Address a = toAddr(arg.startsWith("0x") ? arg.substring(2) : arg);
            Function f = getFunctionContaining(a);
            if (f != null && f.getEntryPoint().equals(a)) {
                println(a + ": already " + f.getName());
                continue;
            }
            if (getInstructionAt(a) == null) {
                new DisassembleCommand(a, null, true).applyTo(currentProgram, monitor);
            }
            if (f != null) {
                println(a + ": inside " + f.getName() + " @ " + f.getEntryPoint() + "; left as is");
                continue;
            }
            CreateFunctionCmd cmd = new CreateFunctionCmd(a);
            boolean ok = cmd.applyTo(currentProgram, monitor);
            Function g = getFunctionAt(a);
            println(a + ": " + (ok && g != null ? "created, body " + g.getBody().getNumAddresses() + " bytes" : "failed: " + cmd.getStatusMsg()));
        }
    }
}
