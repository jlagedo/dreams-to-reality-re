/* Replace the [DOCS_SYNC] plate comments with a fresh set from the docs.
 *
 * Usage (headless, without -readOnly):
 *   analyzeHeadless ghidra dreams -process WINDREAM.EXE -noanalysis \
 *       -scriptPath ghidra_scripts -postScript ApplyDocComments.java \
 *       @out\ghidra\match\docsync-WINDREAM.EXE.tsv
 *
 * The file comes from tools/sync_doc_comments.py: <address> <text>, with \n
 * escapes. First every [DOCS_SYNC] block in the program is removed (the
 * [DOCS_SYNC] paragraph and the [docs/...] paragraphs that follow it), keeping
 * any other comment text such as [NAME] paragraphs; then each address gets its
 * new block appended to its plate comment.
 *
 * @category Dreams
 */

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressIterator;
import ghidra.program.model.listing.CodeUnit;
import ghidra.program.model.listing.Listing;

public class ApplyDocComments extends GhidraScript {

    private static String strip(String comment) {
        if (comment == null) {
            return "";
        }
        StringBuilder kept = new StringBuilder();
        for (String para : comment.split("\n\n")) {
            if (para.startsWith("[DOCS_SYNC]") || para.startsWith("[docs/") || para.isBlank()) {
                continue;
            }
            kept.append(kept.length() > 0 ? "\n\n" : "").append(para);
        }
        return kept.toString();
    }

    @Override
    public void run() throws Exception {
        Listing listing = currentProgram.getListing();
        List<Address> stale = new ArrayList<>();
        AddressIterator it = listing.getCommentAddressIterator(
            CodeUnit.PLATE_COMMENT, currentProgram.getMemory(), true);
        while (it.hasNext()) {
            Address a = it.next();
            String c = listing.getComment(CodeUnit.PLATE_COMMENT, a);
            if (c != null && c.contains("[DOCS_SYNC]")) {
                stale.add(a);
            }
        }
        for (Address a : stale) {
            String kept = strip(listing.getComment(CodeUnit.PLATE_COMMENT, a));
            listing.setComment(a, CodeUnit.PLATE_COMMENT, kept.isEmpty() ? null : kept);
        }
        int applied = 0;
        for (String arg : getScriptArgs()) {
            if (!arg.startsWith("@")) {
                continue;
            }
            for (String line : Files.readAllLines(Path.of(arg.substring(1)),
                    StandardCharsets.UTF_8)) {
                String[] f = line.split("\t", 2);
                if (f.length < 2 || line.startsWith("#")) {
                    continue;
                }
                Address a = toAddr(f[0]);
                String block = f[1].replace("\\n", "\n");
                String kept = strip(listing.getComment(CodeUnit.PLATE_COMMENT, a));
                listing.setComment(a, CodeUnit.PLATE_COMMENT,
                    kept.isEmpty() ? block : kept + "\n\n" + block);
                applied++;
            }
        }
        println("removed " + stale.size() + " old [DOCS_SYNC] blocks, applied " + applied);
    }
}
