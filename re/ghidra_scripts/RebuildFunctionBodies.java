/* Recompute selected existing function bodies from the current instruction flow.
 *
 * Use after import typing exposes code beyond a previously truncated call:
 *   analyzeHeadless ghidra dreams -process DREAMSFX.EXE -noanalysis
 *     -scriptPath re/ghidra_scripts -postScript RebuildFunctionBodies.java 000674c0
 * Add -readOnly to preview. This neither creates nor renames functions, and
 * does not remove fragment functions; use MergeFragments for reviewed splits.
 * @category Dreams
 */
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;

public class RebuildFunctionBodies extends GhidraScript {
    @Override
    public void run() throws Exception {
        for (String token : getScriptArgs()) {
            Function fn = getFunctionAt(toAddr(Long.parseLong(token.replace("0x", ""), 16)));
            if (fn == null) {
                throw new IllegalArgumentException("No function at " + token);
            }
            long before = fn.getBody().getNumAddresses();
            CreateFunctionCmd.fixupFunctionBody(currentProgram, fn, monitor);
            println(fn.getEntryPoint() + " body " + before + " -> "
                + fn.getBody().getNumAddresses() + " bytes");
        }
    }
}
