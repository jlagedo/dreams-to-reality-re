/* Decompile one or more functions to C and print them.
 *
 * Usage (headless):
 *   analyzeHeadless <proj> <name> -process WINDREAM.EXE -noanalysis \
 *       -scriptPath <repo>\ghidra_scripts -postScript Decompile.java 004175bc
 *
 * Arguments are function entry addresses in hex (with or without 0x) or
 * function names. Pass several to dump a whole call chain in one run.
 *
 * Also prints callers and callees, because the surrounding call graph is
 * usually what tells you whether you are looking at the parser itself or at a
 * wrapper around it.
 *
 * @category Dreams
 */

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;

public class Decompile extends GhidraScript {

    private static final int TIMEOUT_SECONDS = 120;

    private Function resolve(String token) {
        // Try as an address first, then fall back to a name lookup.
        String hex = token.toLowerCase().startsWith("0x") ? token.substring(2) : token;
        try {
            Address addr = currentProgram.getAddressFactory()
                    .getDefaultAddressSpace().getAddress(Long.parseLong(hex, 16));
            Function fn = getFunctionAt(addr);
            if (fn == null) {
                fn = getFunctionContaining(addr);
            }
            if (fn != null) {
                return fn;
            }
        } catch (NumberFormatException | NullPointerException ignored) {
            // not an address, try the name table
        }
        for (Function fn : currentProgram.getFunctionManager().getFunctions(true)) {
            if (fn.getName().equals(token)) {
                return fn;
            }
        }
        return null;
    }

    private void printCallGraph(Function fn) {
        StringBuilder callers = new StringBuilder();
        for (Reference ref : getReferencesTo(fn.getEntryPoint())) {
            Function from = getFunctionContaining(ref.getFromAddress());
            if (from != null && callers.indexOf(from.getName()) < 0) {
                callers.append(from.getName()).append("@").append(from.getEntryPoint()).append("  ");
            }
        }
        println("// callers: " + (callers.length() == 0 ? "(none found)" : callers.toString()));

        StringBuilder callees = new StringBuilder();
        for (Function c : fn.getCalledFunctions(monitor)) {
            callees.append(c.getName()).append("@").append(c.getEntryPoint()).append("  ");
        }
        println("// callees: " + (callees.length() == 0 ? "(none)" : callees.toString()));
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length == 0) {
            println("usage: Decompile.java <addr-or-name> [more...]");
            return;
        }

        DecompInterface ifc = new DecompInterface();
        DecompileOptions opts = new DecompileOptions();
        ifc.setOptions(opts);
        ifc.toggleCCode(true);
        ifc.toggleSyntaxTree(true);
        ifc.setSimplificationStyle("decompile");
        if (!ifc.openProgram(currentProgram)) {
            println("decompiler failed to open program: " + ifc.getLastMessage());
            return;
        }

        try {
            for (String token : args) {
                Function fn = resolve(token);
                println("");
                println("/* ================================================== */");
                if (fn == null) {
                    println("/* could not resolve " + token + " */");
                    continue;
                }
                println("/* " + fn.getName() + " @ " + fn.getEntryPoint()
                        + "   body " + fn.getBody().getNumAddresses() + " bytes */");
                println("/* ================================================== */");
                printCallGraph(fn);

                DecompileResults res = ifc.decompileFunction(fn, TIMEOUT_SECONDS, monitor);
                if (res == null || !res.decompileCompleted()) {
                    println("/* decompilation failed: "
                            + (res == null ? "null" : res.getErrorMessage()) + " */");
                    continue;
                }
                println(res.getDecompiledFunction().getC());
            }
        } finally {
            ifc.dispose();
        }
    }
}
