/* Rebuild PORT:* Function Tags, including owner-review state, and [PORT_MAP]
 * plate comments from the versioned opendreams/port-map.tsv. Run after importing
 * symbols into a fresh project.
 * Existing [NAME], [DOCS_SYNC], manual plate text and unrelated tags survive.
 *
 * Usage (Script Manager, or headless without -readOnly):
 *   ApplyPortMap.java
 *   ApplyPortMap.java @E:\\dev\\dreams\\opendreams\\port-map.tsv
 *
 * @category Dreams
 */

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.CodeUnit;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionTag;
import ghidra.program.model.listing.Listing;

public class ApplyPortMap extends GhidraScript {

    private static final String HEADER = "program\taddress\tchecked_name\tsource_block\t"
        + "cpp_file\tcpp_symbol\tstatus\tevidence\tadaptation\tcoverage\tremaining_work\treviewed";

    private static class Row {
        Function function;
        String cppFile;
        String cppSymbol;
        String status;
        String coverage;
        String remainingWork;
        String reviewed;
    }

    private String repoRoot() {
        String env = System.getenv("DREAMS_REPO");
        if (env != null && !env.isEmpty()) {
            return env;
        }
        return new File(sourceFile.getAbsolutePath()).getParentFile().getParent();
    }

    private Path mapPath() {
        String[] args = getScriptArgs();
        if (args.length > 0 && args[0].startsWith("@")) {
            return Path.of(args[0].substring(1));
        }
        return Path.of(repoRoot(), "opendreams", "port-map.tsv");
    }

    private String stripPortBlock(String comment) {
        if (comment == null || comment.isEmpty()) {
            return "";
        }
        StringBuilder kept = new StringBuilder();
        for (String para : comment.split("\n\n", -1)) {
            if (para.startsWith("[PORT_MAP]")) {
                continue;
            }
            if (kept.length() > 0) {
                kept.append("\n\n");
            }
            kept.append(para);
        }
        return kept.toString();
    }

    private List<Row> readRows(Path path) throws Exception {
        List<String> lines = Files.readAllLines(path, StandardCharsets.UTF_8);
        if (lines.isEmpty() || !HEADER.equals(lines.get(0))) {
            throw new IllegalArgumentException("unexpected port map header in " + path);
        }
        List<Row> rows = new ArrayList<>();
        Set<Address> seen = new HashSet<>();
        for (int i = 1; i < lines.size(); i++) {
            String line = lines.get(i);
            if (line.isBlank() || line.startsWith("#")) {
                continue;
            }
            String[] f = line.split("\t", -1);
            if (f.length != 12) {
                throw new IllegalArgumentException("port map line " + (i + 1) + " has "
                    + f.length + " columns, expected 12");
            }
            if (!f[0].equalsIgnoreCase(currentProgram.getName())) {
                continue;
            }
            Address address = toAddr(f[1]);
            Function function = getFunctionAt(address);
            if (function == null || !function.getName().equals(f[2])) {
                throw new IllegalArgumentException("port map line " + (i + 1)
                    + " does not match a checked function at " + f[1] + ": " + f[2]);
            }
            if (!seen.add(address)) {
                throw new IllegalArgumentException("duplicate port map address " + f[1]);
            }
            if (!Set.of("ported", "adapted", "replaced", "omitted").contains(f[6])) {
                throw new IllegalArgumentException("invalid status on port map line " + (i + 1));
            }
            if (!f[6].equals("omitted") && (f[4].isEmpty() || f[5].isEmpty())) {
                throw new IllegalArgumentException("missing C++ location on line " + (i + 1));
            }
            if (!Set.of("complete", "partial", "unverified", "none").contains(f[9]) ||
                (f[6].equals("omitted") != f[9].equals("none"))) {
                throw new IllegalArgumentException("invalid coverage on port map line " + (i + 1));
            }
            if ((f[9].equals("complete") && !f[10].equals("-")) ||
                (!f[9].equals("complete") && (f[10].isBlank() || f[10].equals("-")))) {
                throw new IllegalArgumentException("remaining_work disagrees with coverage on line "
                    + (i + 1));
            }
            if (!Set.of("yes", "no").contains(f[11])) {
                throw new IllegalArgumentException("reviewed must be yes or no on line " + (i + 1));
            }
            Row row = new Row();
            row.function = function;
            row.cppFile = f[4];
            row.cppSymbol = f[5];
            row.status = f[6];
            row.coverage = f[9];
            row.remainingWork = f[10];
            row.reviewed = f[11];
            rows.add(row);
        }
        return rows;
    }

    @Override
    public void run() throws Exception {
        // Validate the entire input before modifying any Ghidra annotation.
        Path path = mapPath();
        List<Row> rows = readRows(path);
        Listing listing = currentProgram.getListing();
        int cleared = 0;
        for (Function fn : currentProgram.getFunctionManager().getFunctions(true)) {
            if (monitor.isCancelled()) {
                throw new InterruptedException("port map update cancelled");
            }
            for (FunctionTag tag : new ArrayList<>(fn.getTags())) {
                if (tag.getName().startsWith("PORT:")) {
                    fn.removeTag(tag.getName());
                    cleared++;
                }
            }
            Address address = fn.getEntryPoint();
            String previous = listing.getComment(CodeUnit.PLATE_COMMENT, address);
            String kept = stripPortBlock(previous);
            if (previous != null && !previous.equals(kept)) {
                listing.setComment(address, CodeUnit.PLATE_COMMENT,
                    kept.isEmpty() ? null : kept);
                cleared++;
            }
        }
        for (Row row : rows) {
            row.function.addTag("PORT:" + row.status);
            row.function.addTag("PORT:" + row.coverage);
            if (!row.coverage.equals("complete")) row.function.addTag("PORT:needs-work");
            row.function.addTag(row.reviewed.equals("yes") ? "PORT:reviewed" : "PORT:review-pending");
            String block = "[PORT_MAP] " + row.status + " | " + row.coverage;
            if (!row.status.equals("omitted")) {
                block += "\n" + row.cppFile + " :: " + row.cppSymbol;
            }
            if (!row.remainingWork.equals("-")) block += "\nRemaining: " + row.remainingWork;
            block += "\nOwner reviewed: " + row.reviewed;
            Address address = row.function.getEntryPoint();
            String previous = listing.getComment(CodeUnit.PLATE_COMMENT, address);
            listing.setComment(address, CodeUnit.PLATE_COMMENT,
                previous == null || previous.isEmpty() ? block : previous + "\n\n" + block);
        }
        println("port map: cleared " + cleared + " old annotations; applied " + rows.size()
            + " rows to " + currentProgram.getName() + " from " + path);
    }
}
