/* Export exact Ghidra function ranges and hashes of loaded body bytes.
 * Read-only validation for cross-build name transfer; outputs out/ghidra/bodies.
 * @category Dreams
 */
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.HexFormat;
import java.util.List;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.AddressRange;
import ghidra.program.model.address.AddressRangeIterator;
import ghidra.program.model.listing.Function;

public class ExportBodyHashes extends GhidraScript {
    @Override
    public void run() throws Exception {
        Path root = Path.of(System.getenv("DREAMS_REPO"));
        Path dir = root.resolve("out/ghidra/bodies");
        Files.createDirectories(dir);
        List<String> rows = new ArrayList<>();
        rows.add("#sha256\t" + currentProgram.getExecutableSHA256());
        rows.add("address\tsize\tranges\tsha256");
        for (Function fn : currentProgram.getFunctionManager().getFunctions(true)) {
            if (fn.isExternal() || fn.isThunk()) continue;
            MessageDigest digest = MessageDigest.getInstance("SHA-256");
            List<String> ranges = new ArrayList<>();
            AddressRangeIterator iterator = fn.getBody().getAddressRanges();
            while (iterator.hasNext()) {
                AddressRange range = iterator.next();
                byte[] bytes = new byte[Math.toIntExact(range.getLength())];
                if (currentProgram.getMemory().getBytes(range.getMinAddress(), bytes) != bytes.length) {
                    throw new IllegalStateException("Incomplete body read: " + fn.getEntryPoint());
                }
                digest.update(bytes);
                ranges.add(range.getMinAddress() + "-" + range.getMaxAddress());
            }
            rows.add(fn.getEntryPoint() + "\t" + fn.getBody().getNumAddresses() + "\t"
                + String.join(",", ranges) + "\t" + HexFormat.of().formatHex(digest.digest()));
        }
        Path output = dir.resolve(currentProgram.getName() + ".tsv");
        Files.write(output, rows);
        println("Body hashes: " + (rows.size() - 2) + " functions -> " + output);
    }
}
