/* Re-apply names and comments exported by ExportSymbols.java.
 *
 * Lets anyone rebuild a fully annotated Ghidra project from their own copy of
 * the discs plus this repo, with no binary artefacts in version control.
 *
 * Reads re/symbols/<program>.tsv. Addresses are RVAs, so rebasing is fine.
 * Warns if the recorded SHA-256 does not match the loaded binary.
 *
 * @category Dreams
 */

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.InputStreamReader;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.CodeUnit;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.symbol.SourceType;

public class ImportSymbols extends GhidraScript {

    private String repoRoot() {
        String env = System.getenv("DREAMS_REPO");
        if (env != null && !env.isEmpty()) {
            return env;
        }
        return new File(sourceFile.getAbsolutePath()).getParentFile().getParent();
    }

    private int kindOf(String name) {
        switch (name) {
            case "eol":   return CodeUnit.EOL_COMMENT;
            case "pre":   return CodeUnit.PRE_COMMENT;
            case "post":  return CodeUnit.POST_COMMENT;
            case "plate": return CodeUnit.PLATE_COMMENT;
            default:      return -1;
        }
    }

    @Override
    public void run() throws Exception {
        File in = new File(repoRoot(), "re" + File.separator + "symbols"
                + File.separator + currentProgram.getName().toLowerCase() + ".tsv");
        if (!in.isFile()) {
            println("no symbol file at " + in.getAbsolutePath() + " - nothing to import");
            return;
        }

        Address base = currentProgram.getImageBase();
        Listing listing = currentProgram.getListing();
        int renamed = 0;
        int applied = 0;

        try (BufferedReader r = new BufferedReader(
                new InputStreamReader(new FileInputStream(in), "UTF-8"))) {
            String line;
            while ((line = r.readLine()) != null && !monitor.isCancelled()) {
                if (line.startsWith("#sha256\t")) {
                    String recorded = line.substring(8).trim();
                    String actual = currentProgram.getExecutableSHA256();
                    if (actual != null && !recorded.isEmpty() && !recorded.equals(actual)) {
                        println("WARNING: sha256 mismatch - these symbols came from a "
                                + "different binary. Applying anyway.");
                    }
                    continue;
                }
                if (line.startsWith("#") || line.isEmpty()) {
                    continue;
                }

                String[] f = line.split("\t", -1);
                if (f.length < 3) {
                    continue;
                }
                Address addr = base.add(Long.parseLong(f[1], 16));

                if ("FUNC".equals(f[0])) {
                    Function fn = getFunctionAt(addr);
                    if (fn == null) {
                        continue;
                    }
                    fn.setName(f[2], SourceType.USER_DEFINED);
                    if (f.length > 4 && !f[4].isEmpty()) {
                        fn.setComment(f[4].replace("\n", "\n"));
                    }
                    renamed++;
                } else if ("CMT".equals(f[0])) {
                    int kind = kindOf(f[2]);
                    if (kind < 0 || f.length < 4) {
                        continue;
                    }
                    listing.setComment(addr, kind, f[3].replace("\n", "\n"));
                    applied++;
                }
            }
        }
        println("applied " + renamed + " function names, " + applied + " comments");
    }
}
