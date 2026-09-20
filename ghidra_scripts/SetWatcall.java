/* Apply the __watcall calling convention and decompile, to see the difference.
 *
 * Ghidra ships no Watcom compiler spec, so every function in a Watcom binary
 * decompiles with its arguments invisible — they surface as `extraout_ECX`,
 * `unaff_EBX` and friends because Ghidra assumes cdecl. This sets the convention
 * added by tools/watcall-cspec.patch and prints before/after.
 *
 * Usage (headless):
 *   analyzeHeadless <proj> dreams -process WINDREAM.EXE -noanalysis -readOnly \
 *       -scriptPath <repo>\ghidra_scripts -postScript SetWatcall.java 0045c278
 *
 * Pass ALL to apply it to every non-external, non-thunk function instead.
 *
 * @category Dreams
 */

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;

public class SetWatcall extends GhidraScript {

    private static final String CONV = "__watcall";
    private static final int TIMEOUT = 120;

    private DecompInterface decomp;

    private String decompile(Function fn) {
        DecompileResults res = decomp.decompileFunction(fn, TIMEOUT, monitor);
        if (res == null || !res.decompileCompleted()) {
            return "<decompilation failed: "
                    + (res == null ? "null" : res.getErrorMessage()) + ">";
        }
        return res.getDecompiledFunction().getC();
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length == 0) {
            println("usage: SetWatcall.java <addr|name> [...] | ALL");
            return;
        }

        boolean hasWatcall = false;
        for (String s : currentProgram.getFunctionManager()
                .getCallingConventionNames()) {
            println("available convention: " + s);
            if (CONV.equals(s)) {
                hasWatcall = true;
            }
        }
        if (!hasWatcall) {
            println("ERROR: " + CONV + " is not in this language's compiler spec.");
            println("Apply tools/watcall-cspec.patch and restart Ghidra.");
            return;
        }

        decomp = new DecompInterface();
        decomp.openProgram(currentProgram);

        if (args.length == 1 && "ALL".equalsIgnoreCase(args[0])) {
            int n = 0;
            for (Function fn : currentProgram.getFunctionManager().getFunctions(true)) {
                if (fn.isExternal() || fn.isThunk()) {
                    continue;
                }
                fn.setCallingConvention(CONV);
                fn.setSignatureSource(SourceType.DEFAULT);
                n++;
            }
            println("set " + CONV + " on " + n + " functions");
            return;
        }

        for (String token : args) {
            String hex = token.toLowerCase().startsWith("0x") ? token.substring(2) : token;
            Address addr = currentProgram.getAddressFactory()
                    .getDefaultAddressSpace().getAddress(Long.parseLong(hex, 16));
            Function fn = getFunctionAt(addr);
            if (fn == null) {
                fn = getFunctionContaining(addr);
            }
            if (fn == null) {
                println("no function at " + token);
                continue;
            }

            println("\n================ " + fn.getName() + " @ " + fn.getEntryPoint());
            println("---- BEFORE (" + fn.getCallingConventionName() + ")");
            println(decompile(fn));

            fn.setCallingConvention(CONV);
            // Drop the stored signature: it was inferred under the wrong
            // convention, and while it stands the decompiler cannot promote
            // EBX/ECX to parameters no matter what the prototype model says.
            fn.setSignatureSource(SourceType.DEFAULT);
            println("---- AFTER (" + fn.getCallingConventionName() + ")");
            println(decompile(fn));
        }
    }
}
