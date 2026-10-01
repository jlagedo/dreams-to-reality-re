/* Set __watcall across a Watcom-built program, sparing the runtime's hand-
 * written assembly helpers.
 *
 * Ghidra ships no Watcom compiler spec, so it falls back to cdecl/fastcall and
 * every argument passed in EAX/EDX/EBX/ECX vanishes into `extraout_*` and
 * `unaff_*`. re/tools/watcall-cspec.patch adds the prototype model; this applies it.
 *
 * Which functions get it is decided by the Watcom library match in
 * <watcom>\sigs\<program>.csv (see DREAMS_WATCOM and src/dreams/watcom.py). Watcom
 * decorates register-convention symbols with a TRAILING underscore -- memcpy_,
 * strlen_ -- so those take __watcall like Cryo's own code. The ones without
 * (__CHK, __STK, IF@DSIN, __FDD, __U8M ...; 42 in the Windows builds' 11.0
 * match) are assembly helpers with bespoke register contracts; they are left
 * alone, except that one still carrying __watcall from an earlier run with a
 * smaller match is reset to "unknown".
 *
 * Two more groups keep other conventions:
 *   - functions whose signature is IMPORTED or USER_DEFINED (the Watcom header
 *     prototypes from ApplyWatcomHeaders.java, hand typing) are kept as they are;
 *   - Win32 callbacks get __stdcall. A callback is a function whose address is
 *     loaded by an instruction followed, within CALLBACK_WINDOW instructions,
 *     by a call that does not land on one of the program's own functions: an
 *     import (RegisterClassA, SetConsoleCtrlHandler) or a COM method
 *     (IDirectDraw::EnumDisplayModes). Each one found is printed.
 *
 * Usage (headless, omit -readOnly to persist):
 *   analyzeHeadless <proj> dreams -process WINDREAM.EXE -noanalysis \
 *       -scriptPath <repo>\ghidra_scripts -postScript ApplyWatcall.java \
 *       <watcom>\sigs\windream.csv
 *
 * @category Dreams
 */

import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.SourceType;

public class ApplyWatcall extends GhidraScript {

    private static final String CONV = "__watcall";
    private static final String CALLBACK = "__stdcall";
    private static final int CALLBACK_WINDOW = 12;

    /** Functions whose address is handed to Windows (see the header). */
    private Set<Address> findCallbacks() {
        Set<Address> out = new HashSet<>();
        for (Function fn : currentProgram.getFunctionManager().getFunctions(true)) {
            for (Reference r : getReferencesTo(fn.getEntryPoint())) {
                if (r.getReferenceType().isCall() || r.getReferenceType().isJump()) {
                    continue;
                }
                Instruction ins = getInstructionAt(r.getFromAddress());
                for (int i = 0; ins != null && i < CALLBACK_WINDOW; i++) {
                    ins = ins.getNext();
                    if (ins == null || !ins.getFlowType().isCall()) {
                        continue;
                    }
                    boolean own = false;
                    for (Reference c : ins.getReferencesFrom()) {
                        Function t = getFunctionAt(c.getToAddress());
                        own |= t != null && !t.isExternal() && !t.isThunk();
                    }
                    if (!own) {
                        println("callback " + fn.getName() + " at " + fn.getEntryPoint()
                            + ": address taken at " + r.getFromAddress() + ", then " + ins);
                        out.add(fn.getEntryPoint());
                    }
                    break;
                }
            }
        }
        return out;
    }

    /** Entry addresses of runtime helpers whose convention must not be touched. */
    private Set<Long> readBespoke(String csv) throws Exception {
        Set<Long> skip = new HashSet<>();
        List<String> lines = Files.readAllLines(Paths.get(csv));
        for (String line : lines.subList(1, lines.size())) {
            String[] f = line.split(",");
            if (f.length < 3) {
                continue;
            }
            String name = f[2].split("[|]")[0];
            if (!name.endsWith("_")) {                 // no trailing _ => bespoke
                skip.add(Long.parseLong(f[1].replace("0x", ""), 16));
            }
        }
        return skip;
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length == 0) {
            println("usage: ApplyWatcall.java <watcom-sigs.csv>");
            return;
        }
        boolean available = false;
        for (String s : currentProgram.getFunctionManager().getCallingConventionNames()) {
            if (CONV.equals(s)) {
                available = true;
            }
        }
        if (!available) {
            println("ERROR: " + CONV + " missing. Apply re/tools/watcall-cspec.patch"
                    + " and restart Ghidra.");
            return;
        }

        Set<Long> bespoke = readBespoke(args[0]);
        println("runtime helpers to leave alone: " + bespoke.size());

        Set<Address> callbacks = findCallbacks();

        int applied = 0, skipped = 0, typed = 0, reset = 0, stdcall = 0;
        for (Function fn : currentProgram.getFunctionManager().getFunctions(true)) {
            if (fn.isExternal() || fn.isThunk()) {
                skipped++;
                continue;
            }
            if (bespoke.contains(fn.getEntryPoint().getOffset())) {
                if (CONV.equals(fn.getCallingConventionName())) {
                    fn.setCallingConvention(Function.UNKNOWN_CALLING_CONVENTION_STRING);
                    fn.setSignatureSource(SourceType.DEFAULT);
                    reset++;
                }
                skipped++;
                continue;
            }
            SourceType sig = fn.getSignatureSource();
            if (sig == SourceType.IMPORTED || sig == SourceType.USER_DEFINED) {
                typed++;
                continue;
            }
            // The stored signature was inferred under the wrong convention;
            // while it stands the decompiler cannot promote EBX/ECX to params.
            if (callbacks.contains(fn.getEntryPoint())) {
                fn.setCallingConvention(CALLBACK);
                stdcall++;
            }
            else {
                fn.setCallingConvention(CONV);
                applied++;
            }
            fn.setSignatureSource(SourceType.DEFAULT);
        }
        println("set " + CONV + " on " + applied + " functions and " + CALLBACK + " on "
            + stdcall + " callbacks; kept " + typed + " typed; skipped " + skipped
            + " (thunks, bespoke helpers; " + reset + " helpers reset from " + CONV + ")");
    }
}
