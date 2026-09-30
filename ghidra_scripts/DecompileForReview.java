/* Decompile selected functions without plate/inline comments or candidate names.
 * Intended for a review performed before revealing cross-build name proposals.
 * Usage: DecompileForReview.java <output-file> <hex-address> ...
 * @category Dreams
 */
import java.nio.file.Files;
import java.nio.file.Path;
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;

public class DecompileForReview extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        DecompInterface decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);
        StringBuilder result = new StringBuilder();
        try {
            for (int i = 1; i < args.length; i++) {
                Function fn = getFunctionAt(toAddr(args[i]));
                if (fn == null) throw new IllegalArgumentException("Missing function " + args[i]);
                DecompileResults decoded = decompiler.decompileFunction(fn, 60, monitor);
                if (!decoded.decompileCompleted()) throw new IllegalStateException(decoded.getErrorMessage());
                String code = decoded.getDecompiledFunction().getC();
                code = code.replaceAll("(?s)/\\*.*?\\*/", "").replaceAll("(?m)//.*$", "");
                result.append("\nREVIEW ").append(args[i]).append("\n").append(code).append("\n");
            }
        } finally {
            decompiler.dispose();
        }
        Files.writeString(Path.of(args[0]), result.toString());
        println("Wrote " + (args.length - 1) + " comment-stripped functions");
    }
}
