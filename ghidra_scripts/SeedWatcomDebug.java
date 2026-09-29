/* Seed function entries from an authenticated Watcom debug-symbol export.
 * Does not rename functions or split existing bodies. Run ExportFunctionFeatures
 * next, generate the name registry, and check_names.py before Rename.java.
 * Usage: SeedWatcomDebug.java <symbols.tsv> <original-executable-sha256>
 * @category Dreams
 */
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.HashSet;
import java.util.Set;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

public class SeedWatcomDebug extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length != 2) throw new IllegalArgumentException("symbols.tsv sha256");
        if (!args[1].equalsIgnoreCase(currentProgram.getExecutableSHA256())) {
            throw new IllegalArgumentException("Executable SHA-256 does not match symbol source");
        }
        int created = 0, existing = 0, overlap = 0, failed = 0;
        Set<Address> seen = new HashSet<>();
        for (String line : Files.readAllLines(Path.of(args[0]), StandardCharsets.UTF_8)) {
            if (line.startsWith("address\t") || line.isBlank()) continue;
            String[] fields = line.split("\t", -1);
            if ((Integer.parseInt(fields[7]) & 4) == 0) continue;
            Address address = toAddr(fields[0]);
            if (!seen.add(address)) continue;
            if (getFunctionAt(address) != null) { existing++; continue; }
            Function owner = getFunctionContaining(address);
            if (owner != null) { overlap++; continue; }
            if (getInstructionAt(address) == null) disassemble(address);
            if (getInstructionAt(address) == null) { failed++; continue; }
            Function fn = createFunction(address, null);
            if (fn == null) failed++; else created++;
        }
        println("Debug entries: created=" + created + " existing=" + existing
            + " overlapping=" + overlap + " failed=" + failed);
    }
}
