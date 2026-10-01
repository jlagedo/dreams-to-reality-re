/* Locate Cryo container parsers by finding who reads the four-character tags.
 *
 * Containers first: every Cryo format opens with a 4CC, and the loader must
 * validate it. There are two ways a compiler emits that check, and we measured
 * which one Watcom used here:
 *
 *   1. Immediate compare  -  cmp dword ptr [x], 0x464E5344   ("DSNF" as an int)
 *   2. memcmp/strncmp     -  against a string literal in .data
 *
 * Scanning WINDREAM.EXE showed ZERO little-endian integer forms but exactly one
 * ASCII occurrence of each tag, so this build uses form 2. The literals are the
 * anchors: find references to them and you land in the parser.
 *
 *   DSNF @ 0xc261e    DANF @ 0xc1df1    DRDF @ 0xc1e48    UBIK @ 0xc3911
 *   F3DC @ 0x5620d    HNM4/HNM6/HNS6/UBB2/UBS2 cluster @ 0x7ab0-0x7ba3
 *
 * That last cluster - five video tags inside 0xc3 bytes - looks like a table
 * the code iterates over rather than five separate compares. Worth a look.
 *
 * Written in Java rather than Python deliberately: Ghidra compiles Java scripts
 * with no external dependency, whereas PyGhidra needs a matching jpype wheel
 * (Python <= 3.13). Keeps the repo reproducible for anyone who clones it.
 *
 * @category Dreams
 */

import java.util.LinkedHashMap;
import java.util.Map;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.Reference;

public class FindFormatParsers extends GhidraScript {

    private static final Map<String, String> MAGICS = new LinkedHashMap<>();
    static {
        MAGICS.put("DSNF", "scene loader (.DSN) - TOP PRIORITY, 157 MB packed");
        MAGICS.put("DANF", "animation loader (.DAN)");
        MAGICS.put("F3DC", "geometry loader (.3DC/.3DM)");
        MAGICS.put("PAK0", "model archive (.PAK)");
        MAGICS.put("UBIK", "image bundle (.BF)");
        MAGICS.put("DRDF", "dialogue bank (.DRD)");
        MAGICS.put("HNM4", "video, generation 4");
        MAGICS.put("HNS6", "video, generation 6");
        MAGICS.put("HNM6", "video, generation 6");
        MAGICS.put("UBB2", "UBIK presentation bundle");
        MAGICS.put("UBS2", "UBIK presentation bundle");
    }

    private static final String[] ANCHORS = {
        "Debordement dans DAN_Load3DC",
        "not enough memory in DAN_Load3DC",
        "HD.ID", "FULL.ID", "1CD.ID", "2CD.ID",
        ".DSN", ".DAN", ".3DC", ".3DM", ".PAK", ".BF",
        "Water surface", "DREAMS FSB",
    };

    private String where(Address addr) {
        Function fn = getFunctionContaining(addr);
        return fn == null ? "(no function)"
                : fn.getName() + " @ " + fn.getEntryPoint();
    }

    /** Report every reference to a literal, and the function each lives in. */
    private int reportRefs(Address at, String indent) {
        int n = 0;
        for (Reference ref : getReferencesTo(at)) {
            Address from = ref.getFromAddress();
            println(indent + "xref " + from + "  " + ref.getReferenceType()
                    + "  in " + where(from));
            n++;
        }
        if (n == 0) {
            println(indent + "(no references - literal may be reached via a table)");
        }
        return n;
    }

    /** All occurrences of a byte pattern across memory. */
    private java.util.List<Address> findAll(byte[] needle) throws Exception {
        java.util.List<Address> hits = new java.util.ArrayList<>();
        Memory mem = currentProgram.getMemory();
        Address at = mem.getMinAddress();
        while (at != null && !monitor.isCancelled()) {
            at = mem.findBytes(at, needle, null, true, monitor);
            if (at == null) {
                break;
            }
            hits.add(at);
            at = at.add(1);
        }
        return hits;
    }

    @Override
    public void run() throws Exception {
        println("======================================================");
        println("Cryo container parser hunt - " + currentProgram.getName());
        println("======================================================");

        println("");
        println("--- format tags as ASCII literals (memcmp style) ---");
        int totalRefs = 0;
        for (Map.Entry<String, String> e : MAGICS.entrySet()) {
            String tag = e.getKey();
            java.util.List<Address> hits = findAll(tag.getBytes("US-ASCII"));
            if (hits.isEmpty()) {
                continue;
            }
            println("");
            println("  " + tag + "   " + e.getValue());
            for (Address at : hits) {
                println("    literal at " + at);
                totalRefs += reportRefs(at, "      ");
            }
        }

        println("");
        println("--- format tags as little-endian immediates (cmp style) ---");
        boolean any = false;
        for (String tag : MAGICS.keySet()) {
            byte[] b = tag.getBytes("US-ASCII");
            byte[] le = { b[3], b[2], b[1], b[0] };
            for (Address at : findAll(le)) {
                println("  " + tag + " as 0x"
                        + Integer.toHexString(
                            ((b[3] & 0xFF) << 24) | ((b[2] & 0xFF) << 16)
                          | ((b[1] & 0xFF) << 8) | (b[0] & 0xFF))
                        + " at " + at + "  in " + where(at));
                any = true;
            }
        }
        if (!any) {
            println("  none - confirms this build compares tags as strings.");
        }

        println("");
        println("--- string anchors ---");
        for (String anchor : ANCHORS) {
            java.util.List<Address> hits = findAll(anchor.getBytes("US-ASCII"));
            if (hits.isEmpty()) {
                continue;
            }
            println("");
            println("  \"" + anchor + "\"");
            for (Address at : hits) {
                println("    at " + at);
                reportRefs(at, "      ");
            }
        }

        println("");
        println("Total xrefs to format tags: " + totalRefs);
        println("Rename what you identify, then run ExportSymbols.java.");
    }
}
