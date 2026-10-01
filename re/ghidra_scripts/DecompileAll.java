/* Decompile every function in the program to one C file plus an index.
 *
 * Usage (headless, read-only):
 *   analyzeHeadless <proj> <name> -process WINDREAM.EXE -noanalysis -readOnly \
 *       -scriptPath <repo>\ghidra_scripts -postScript DecompileAll.java <out-dir>
 *
 * Writes <out-dir>/<program>.c (functions in address order) and
 * <out-dir>/<program>.tsv, one row per function:
 *
 *   entry name bytes convention signature status lines unaff extraout in warnings
 *
 * unaff, extraout and in count the distinct unaff_*, extraout_* and in_*
 * variables in the output: registers the decompiler could not tie to a
 * parameter, a call result or a local. warnings counts the decompiler's
 * WARNING comments. The C is game-derived; keep <out-dir> under out/.
 *
 * @category Dreams
 */

import java.io.File;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.decompiler.parallel.DecompileConfigurer;
import ghidra.app.decompiler.parallel.DecompilerCallback;
import ghidra.app.decompiler.parallel.ParallelDecompiler;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Program;
import ghidra.util.task.TaskMonitor;

public class DecompileAll extends GhidraScript {

    private static final int TIMEOUT_SECONDS = 180;
    private static final Pattern UNAFF = Pattern.compile("\\bunaff_\\w+");
    private static final Pattern EXTRAOUT = Pattern.compile("\\bextraout_\\w+");
    private static final Pattern IN = Pattern.compile("\\bin_\\w+");
    private static final Pattern WARNING = Pattern.compile("WARNING:");

    private static final class Row {
        final Function fn;
        final String c;
        final String status;

        Row(Function fn, String c, String status) {
            this.fn = fn;
            this.c = c;
            this.status = status;
        }
    }

    private static int distinct(Pattern p, String text) {
        Set<String> seen = new HashSet<>();
        Matcher m = p.matcher(text);
        while (m.find()) {
            seen.add(m.group());
        }
        return seen.size();
    }

    private static int count(Pattern p, String text) {
        int n = 0;
        Matcher m = p.matcher(text);
        while (m.find()) {
            n++;
        }
        return n;
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 1) {
            println("usage: DecompileAll.java <out-dir>");
            return;
        }
        File dir = new File(args[0]);
        dir.mkdirs();
        Program program = currentProgram;

        List<Function> functions = new ArrayList<>();
        for (Function fn : program.getFunctionManager().getFunctions(true)) {
            if (!fn.isExternal()) {
                functions.add(fn);
            }
        }

        DecompileConfigurer configurer = new DecompileConfigurer() {
            @Override
            public void configure(DecompInterface ifc) {
                DecompileOptions opts = new DecompileOptions();
                opts.grabFromProgram(program);
                ifc.setOptions(opts);
                ifc.toggleCCode(true);
                ifc.toggleSyntaxTree(true);
                ifc.setSimplificationStyle("decompile");
            }
        };
        DecompilerCallback<Row> callback = new DecompilerCallback<>(program, configurer) {
            @Override
            public Row process(DecompileResults res, TaskMonitor m) throws Exception {
                Function fn = res.getFunction();
                if (!res.decompileCompleted()) {
                    String why = res.getErrorMessage();
                    return new Row(fn, "/* decompilation failed: "
                            + (why == null ? "" : why.trim()) + " */\n", "failed");
                }
                return new Row(fn, res.getDecompiledFunction().getC(), "ok");
            }
        };
        callback.setTimeout(TIMEOUT_SECONDS);

        List<Row> rows;
        try {
            rows = ParallelDecompiler.decompileFunctions(callback, functions, monitor);
        } finally {
            callback.dispose();
        }
        rows.removeIf(r -> r == null);
        rows.sort((a, b) -> a.fn.getEntryPoint().compareTo(b.fn.getEntryPoint()));

        String base = program.getName();
        int failed = 0;
        int unaff = 0;
        int extraout = 0;
        int in = 0;
        try (PrintWriter c = new PrintWriter(new File(dir, base + ".c"), StandardCharsets.UTF_8);
                PrintWriter t = new PrintWriter(new File(dir, base + ".tsv"), StandardCharsets.UTF_8)) {
            c.println("/* " + base + ": " + rows.size() + " functions, decompiled by Ghidra. */");
            t.println("entry\tname\tbytes\tconvention\tsignature\tstatus\tlines\tunaff\textraout\tin\twarnings");
            for (Row r : rows) {
                Function fn = r.fn;
                int nu = distinct(UNAFF, r.c);
                int ne = distinct(EXTRAOUT, r.c);
                int ni = distinct(IN, r.c);
                unaff += nu > 0 ? 1 : 0;
                extraout += ne > 0 ? 1 : 0;
                in += ni > 0 ? 1 : 0;
                failed += r.status.equals("ok") ? 0 : 1;
                c.println();
                c.println("/* ==== " + fn.getEntryPoint() + " " + fn.getName()
                        + (fn.isThunk() ? " (thunk)" : "") + " ==== */");
                c.print(r.c);
                t.println(fn.getEntryPoint() + "\t" + fn.getName()
                        + "\t" + fn.getBody().getNumAddresses()
                        + "\t" + fn.getCallingConventionName()
                        + "\t" + fn.getSignatureSource()
                        + "\t" + r.status
                        + "\t" + r.c.split("\n", -1).length
                        + "\t" + nu + "\t" + ne + "\t" + ni
                        + "\t" + count(WARNING, r.c));
            }
        }
        println("DecompileAll " + base + ": " + rows.size() + " functions, " + failed
                + " failed; functions with unaff_ " + unaff + ", extraout_ " + extraout
                + ", in_ " + in + " -> " + dir);
    }
}
