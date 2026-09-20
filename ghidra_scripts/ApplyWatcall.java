/* Set __watcall across a Watcom-built program, sparing the runtime's hand-
 * written assembly helpers.
 *
 * Ghidra ships no Watcom compiler spec, so it falls back to cdecl/fastcall and
 * every argument passed in EAX/EDX/EBX/ECX vanishes into `extraout_*` and
 * `unaff_*`. tools/watcall-cspec.patch adds the prototype model; this applies it.
 *
 * Which functions get it is decided by the Watcom library match in
 * E:\dev_game\watcom\sigs\<program>.csv (see src/dreams/watcom.py). Watcom
 * decorates register-convention symbols with a TRAILING underscore -- memcpy_,
 * strlen_ -- so those 88 take __watcall like Cryo's own code. The 31 without
 * one (__CHK, __STK, IF@DSIN, __FDD ...) are assembly helpers with bespoke
 * register contracts and are left untouched.
 *
 * Usage (headless, omit -readOnly to persist):
 *   analyzeHeadless <proj> dreams -process WINDREAM.EXE -noanalysis \
 *       -scriptPath <repo>\ghidra_scripts -postScript ApplyWatcall.java \
 *       E:\dev_game\watcom\sigs\windream.csv
 *
 * @category Dreams
 */

import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;

public class ApplyWatcall extends GhidraScript {

    private static final String CONV = "__watcall";

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
            println("ERROR: " + CONV + " missing. Apply tools/watcall-cspec.patch"
                    + " and restart Ghidra.");
            return;
        }

        Set<Long> bespoke = readBespoke(args[0]);
        println("runtime helpers to leave alone: " + bespoke.size());

        int applied = 0, skipped = 0;
        for (Function fn : currentProgram.getFunctionManager().getFunctions(true)) {
            if (fn.isExternal() || fn.isThunk()) {
                skipped++;
                continue;
            }
            if (bespoke.contains(fn.getEntryPoint().getOffset())) {
                skipped++;
                continue;
            }
            fn.setCallingConvention(CONV);
            // The stored signature was inferred under the wrong convention;
            // while it stands the decompiler cannot promote EBX/ECX to params.
            fn.setSignatureSource(SourceType.DEFAULT);
            applied++;
        }
        println("set " + CONV + " on " + applied + " functions, skipped " + skipped);
    }
}
