/* Merge functions that Ghidra split off the middle of another function.
 *
 * Auto-analysis sometimes makes a function at a jump target inside another
 * function. The piece has no callers and uses the parent's frame (unaff_EBP),
 * and the parent's body already covers it. This removes the pieces and
 * recomputes the parent's body.
 *
 * Usage (headless, without -readOnly):
 *   analyzeHeadless ghidra dreams -process WINDREAM.EXE -noanalysis \
 *       -scriptPath ghidra_scripts -postScript MergeFragments.java \
 *       0041f42e:0041f685 0041f42e:0041f699
 *
 * Each argument is <parent entry>:<fragment entry> (the .bat launcher splits
 * arguments on commas, so give one fragment per argument).
 * A fragment that has callers is left alone and reported.
 *
 * @category Dreams
 */

import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;

public class MergeFragments extends GhidraScript {

    @Override
    public void run() throws Exception {
        for (String arg : getScriptArgs()) {
            String[] parts = arg.split(":");
            Function parent = getFunctionAt(toAddr(parts[0]));
            if (parent == null || parts.length != 2) {
                println("skip (no parent function or bad argument): " + arg);
                continue;
            }
            for (String frag : parts[1].split(",")) {
                Function f = getFunctionAt(toAddr(frag));
                if (f == null) {
                    println("no function at " + frag);
                    continue;
                }
                boolean called = false;
                for (Reference r : getReferencesTo(f.getEntryPoint())) {
                    if (r.getReferenceType().isCall()) {
                        called = true;
                    }
                }
                if (called) {
                    println("kept " + f.getName() + ": it has callers");
                    continue;
                }
                removeFunction(f);
                println("removed fragment " + frag);
            }
            CreateFunctionCmd.fixupFunctionBody(currentProgram, parent, monitor);
            println(parent.getName() + " body now " + parent.getBody().getNumAddresses()
                + " bytes");
        }
    }
}
