/* Rename functions from the command line or a file.
 *
 * Usage (headless, without -readOnly so the change is saved):
 *   analyzeHeadless <proj> <name> -process WINDREAM.EXE -noanalysis \
 *       -scriptPath <repo>\ghidra_scripts -postScript Rename.java \
 *       0043a306:MGM_SendMessage 0042493b:INPUT_PostEvents
 *   ... -postScript Rename.java @out\ghidra\match\renames-WINDREAM.EXE.tsv
 *
 * Each argument is <entry address in hex>:<new name>. Not "=": the .bat
 * launcher splits arguments on it. An argument @<file> reads tab-separated
 * lines <address> <name> [<comment>]; the comment is added to the function's
 * plate comment (tools/match_functions.py writes these). A comment starting
 * with [NAME] (tools/check_names.py) replaces the previous [NAME] paragraph
 * instead of adding another. A missing function
 * is created at the address; if that fails it is reported and skipped.
 * Run tools\re-checkpoint.ps1 afterwards to persist the names to re/symbols/.
 *
 * @category Dreams
 */

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;

public class Rename extends GhidraScript {

    private void rename(String address, String name, String comment) throws Exception {
        Function fn = getFunctionAt(toAddr(address));
        if (fn == null) {
            // Code reached only through a pointer is often left as a label.
            fn = createFunction(toAddr(address), null);
        }
        if (fn == null) {
            println("no function at " + address);
            return;
        }
        String old = fn.getName();
        if (!old.equals(name)) {
            fn.setName(name, SourceType.USER_DEFINED);
        }
        if (comment != null && comment.startsWith("[NAME]")) {
            // tools/check_names.py owns the [NAME] paragraph: replace it.
            String existing = fn.getComment();
            StringBuilder kept = new StringBuilder();
            if (existing != null) {
                for (String para : existing.split("\n\n")) {
                    if (!para.startsWith("[NAME]") && !para.isBlank()) {
                        kept.append(kept.length() > 0 ? "\n\n" : "").append(para);
                    }
                }
            }
            fn.setComment(comment + (kept.length() > 0 ? "\n\n" + kept : ""));
        }
        else if (comment != null && !comment.isEmpty()) {
            String existing = fn.getComment();
            if (existing == null || existing.isEmpty()) {
                fn.setComment(comment);
            }
            else if (!existing.contains(comment)) {
                fn.setComment(existing + "\n\n" + comment);
            }
        }
        println(fn.getEntryPoint() + "  " + old + " -> " + fn.getName());
    }

    @Override
    public void run() throws Exception {
        for (String arg : getScriptArgs()) {
            if (arg.startsWith("@")) {
                for (String line : Files.readAllLines(Path.of(arg.substring(1)),
                        StandardCharsets.UTF_8)) {
                    String[] f = line.split("\t", 3);
                    if (f.length >= 2 && !line.startsWith("#")) {
                        rename(f[0], f[1], f.length > 2 ? f[2] : null);
                    }
                }
                continue;
            }
            int sep = arg.indexOf(':');
            if (sep < 0) {
                println("skip (expected addr:name): " + arg);
                continue;
            }
            rename(arg.substring(0, sep), arg.substring(sep + 1), null);
        }
    }
}
