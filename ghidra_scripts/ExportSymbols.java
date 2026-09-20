/* Export our analysis (function names, comments) to diffable TSV in re/symbols/.
 *
 * The Ghidra project is a build artefact: binary, unmergeable, and it embeds
 * copies of the game executables, so it must never be committed. The names and
 * notes we assign ARE the work, so they live in git as text.
 *
 * Addresses are stored as RVAs, not absolute, so a rebased project still
 * matches. The header records the binary's SHA-256 so ImportSymbols can warn
 * when symbols were exported from a different build.
 *
 * TSV rather than JSON: no dependency, and a one-line-per-symbol format gives
 * clean git diffs when someone renames a single function.
 *
 * Output: re/symbols/<program>.tsv
 *   #program / #sha256 / #imagebase header comments, then
 *   FUNC <rva> <name> <signature> <comment>
 *   CMT  <rva> <kind> <text>
 *
 * @category Dreams
 */

import java.io.File;
import java.io.PrintWriter;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressIterator;
import ghidra.program.model.listing.CodeUnit;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.symbol.SourceType;

public class ExportSymbols extends GhidraScript {

    private static final int[] KINDS = {
        CodeUnit.EOL_COMMENT, CodeUnit.PRE_COMMENT,
        CodeUnit.POST_COMMENT, CodeUnit.PLATE_COMMENT,
    };
    private static final String[] KIND_NAMES = { "eol", "pre", "post", "plate" };

    private String repoRoot() {
        String env = System.getenv("DREAMS_REPO");
        if (env != null && !env.isEmpty()) {
            return env;
        }
        return new File(sourceFile.getAbsolutePath()).getParentFile().getParent();
    }

    /** TSV is line- and tab-delimited, so neutralise both in free text. */
    private String clean(String s) {
        if (s == null) {
            return "";
        }
        return s.replace("\t", " ").replace("\r", " ").replace("\n", "\\n");
    }

    @Override
    public void run() throws Exception {
        long base = currentProgram.getImageBase().getOffset();
        Listing listing = currentProgram.getListing();

        File dir = new File(repoRoot(), "re" + File.separator + "symbols");
        if (!dir.isDirectory() && !dir.mkdirs()) {
            println("could not create " + dir);
            return;
        }
        File out = new File(dir, currentProgram.getName().toLowerCase() + ".tsv");

        int functions = 0;
        int comments = 0;
        try (PrintWriter w = new PrintWriter(out, "UTF-8")) {
            w.println("#program\t" + currentProgram.getName());
            w.println("#sha256\t" + currentProgram.getExecutableSHA256());
            w.println("#imagebase\t" + Long.toHexString(base));

            FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
            while (it.hasNext() && !monitor.isCancelled()) {
                Function fn = it.next();
                // Skip Ghidra's auto-generated FUN_xxxxxxxx names - they carry
                // no information and would churn the diff on every re-analysis.
                if (fn.getSymbol().getSource() == SourceType.DEFAULT) {
                    continue;
                }
                long rva = fn.getEntryPoint().getOffset() - base;
                w.println("FUNC\t" + Long.toHexString(rva)
                        + "\t" + clean(fn.getName())
                        + "\t" + clean(fn.getSignature().getPrototypeString())
                        + "\t" + clean(fn.getComment()));
                functions++;
            }

            for (int i = 0; i < KINDS.length; i++) {
                AddressIterator ai = listing.getCommentAddressIterator(
                        KINDS[i], currentProgram.getMemory(), true);
                while (ai.hasNext() && !monitor.isCancelled()) {
                    Address addr = ai.next();
                    String text = listing.getComment(KINDS[i], addr);
                    if (text == null || text.isEmpty()) {
                        continue;
                    }
                    w.println("CMT\t" + Long.toHexString(addr.getOffset() - base)
                            + "\t" + KIND_NAMES[i] + "\t" + clean(text));
                    comments++;
                }
            }
        }

        println("wrote " + out.getAbsolutePath());
        println("  " + functions + " named functions, " + comments + " comments");
        if (functions == 0) {
            println("  (nothing named yet - rename something first)");
        }
    }
}
