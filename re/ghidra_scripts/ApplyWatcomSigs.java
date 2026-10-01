/* Name Watcom runtime functions from the library match CSV.
 *
 * src/dreams/watcom.py matches the Watcom runtime libraries against each game
 * binary and writes <DREAMS_WATCOM>\sigs\<program>.csv (docs/research/toolchain.md):
 *   file_offset,virtual_address,name,module,length,fixed_bytes,extent,runtime
 * "runtime" is the library release: 11.0 for the Windows builds, 10.6 for DOS
 * (a CSV without the column is 10.6). PE rows carry a virtual address; LE rows (DREAMS.EXE, DREAMSFX.EXE) only a
 * file offset. The LE loader keeps a raw copy of the file in an ".image"
 * overlay, and Memory.locateAddressesForFileOffset resolves into that copy,
 * not into the loaded objects. So the offset is mapped here through the LE
 * object and page tables (read from ".image"), and the bytes at the result are
 * compared with the file before anything is named.
 *
 * Usage (headless, without -readOnly):
 *   analyzeHeadless ghidra dreams -process DREAMSFX.EXE -noanalysis \
 *       -scriptPath re/ghidra_scripts -postScript ApplyWatcomSigs.java \
 *       <watcom>\sigs\dreamsfx.csv
 *
 * Rows matched over their full extent name the function at that address
 * (creating it if needed); "core"-only rows get a label and comment. Where one
 * body carries several public names ("fprintf_|fscanf_|sscanf_") the first is
 * used and all are listed in the comment. A current name that is any of the
 * listed aliases is kept (the registry picks sprintf_ out of
 * "fprintf_|sprintf_|..."). Other names we assigned are never overwritten;
 * conflicts are reported.
 *
 * Re-running with another release replaces the earlier "Watcom <version>
 * runtime:" comment paragraph. A name that an earlier run applied (listed in
 * that paragraph) at an address the new CSV no longer matches is removed,
 * together with the paragraph.
 *
 * @category Dreams
 */

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolType;

public class ApplyWatcomSigs extends GhidraScript {

    /* The paragraph a run writes: "Watcom 10.6 runtime: a_, b_ (module m, ...)." */
    private static final Pattern RUNTIME =
        Pattern.compile("Watcom [0-9.]+ runtime: ([^(\\n]*) \\(module [^\\n]*");

    private MemoryBlock image;
    private long pageSize, dataPages;
    private long[][] objects;   // {base, pageIndex, pageCount}

    private int u32(long off) throws Exception {
        return currentProgram.getMemory().getInt(image.getStart().add(off));
    }

    /* Read the LE object table from the loader's ".image" copy of the file. */
    private boolean loadLe() throws Exception {
        image = currentProgram.getMemory().getBlock(".image");
        if (image == null) {
            return false;
        }
        long le = u32(0x3c) & 0xffffffffL;
        pageSize = u32(le + 0x28) & 0xffffffffL;
        long table = le + (u32(le + 0x40) & 0xffffffffL);
        int count = u32(le + 0x44);
        dataPages = u32(le + 0x80) & 0xffffffffL;
        objects = new long[count][];
        for (int i = 0; i < count; i++) {
            long e = table + 24L * i;
            objects[i] = new long[] {
                u32(e + 4) & 0xffffffffL, u32(e + 12) & 0xffffffffL, u32(e + 16) & 0xffffffffL };
        }
        return true;
    }

    /* File offset -> loaded address, assuming DOS/4GW's sequential page map. */
    private Address leAddress(long off) throws Exception {
        if (off < dataPages) {
            return null;
        }
        long page = (off - dataPages) / pageSize + 1;
        for (long[] o : objects) {
            if (page >= o[1] && page < o[1] + o[2]) {
                Address a = toAddr(o[0] + (page - o[1]) * pageSize + (off - dataPages) % pageSize);
                byte[] want = new byte[16];
                byte[] got = new byte[16];
                image.getBytes(image.getStart().add(off), want);
                currentProgram.getMemory().getBytes(a, got);
                // Fixed-up bytes legitimately differ from the file; skip any
                // byte covered by a 32-bit relocation.
                var relocs = currentProgram.getRelocationTable();
                for (int i = 0; i < want.length; i++) {
                    boolean fixed = false;
                    for (int k = 0; k < 4 && !fixed; k++) {
                        fixed = relocs.hasRelocation(a.add(i).subtract(k));
                    }
                    if (!fixed && want[i] != got[i]) {
                        return null;
                    }
                }
                return a;
            }
        }
        return null;
    }

    /* Undo names an earlier version put on functions inside ".image". */
    private int cleanImage() throws Exception {
        if (image == null) {
            return 0;
        }
        int removed = 0;
        for (Function fn : currentProgram.getFunctionManager()
                .getFunctions(new ghidra.program.model.address.AddressSet(image.getStart(),
                    image.getEnd()), true)) {
            removeFunction(fn);
            removed++;
        }
        for (ghidra.program.model.symbol.Symbol s : currentProgram.getSymbolTable()
                .getAllSymbols(false)) {
            if (image.contains(s.getAddress()) && s.getSource() == SourceType.IMPORTED) {
                s.delete();
                removed++;
            }
        }
        currentProgram.getListing().clearComments(image.getStart(), image.getEnd());
        return removed;
    }

    /* Names listed in the runtime paragraph of the plate comment at addr. */
    private Set<String> earlierNames(Address addr) {
        String c = getPlateComment(addr);
        Matcher m = c == null ? null : RUNTIME.matcher(c);
        return m != null && m.find() ? new HashSet<>(Arrays.asList(m.group(1).split(", ")))
            : Set.of();
    }

    /* Replace the runtime paragraph at addr, or append one if there is none. */
    private void putComment(Address addr, String comment) {
        String c = getPlateComment(addr);
        if (c == null || c.isEmpty()) {
            c = comment;
        }
        else {
            Matcher m = RUNTIME.matcher(c);
            c = m.find() ? c.substring(0, m.start()) + comment + c.substring(m.end())
                : c + "\n\n" + comment;
        }
        setPlateComment(addr, c);
    }

    /* Drop what an earlier run put at an address the CSV no longer matches. */
    private int removeStale(Address addr) throws Exception {
        Set<String> names = earlierNames(addr);
        if (names.isEmpty()) {
            return 0;
        }
        Function fn = getFunctionAt(addr);
        if (fn != null && names.contains(fn.getName())) {
            println("removed stale " + fn.getName() + " at " + addr);
            fn.setName(null, SourceType.DEFAULT);
        }
        for (Symbol s : currentProgram.getSymbolTable().getSymbols(addr)) {
            if (s.getSymbolType() == SymbolType.LABEL && names.contains(s.getName())) {
                println("removed stale label " + s.getName() + " at " + addr);
                s.delete();
            }
        }
        String c = getPlateComment(addr);
        Matcher m = RUNTIME.matcher(c);
        m.find();
        String rest = (c.substring(0, m.start()) + c.substring(m.end())).strip();
        setPlateComment(addr, rest.isEmpty() ? null : rest);
        return 1;
    }

    @Override
    public void run() throws Exception {
        boolean le = loadLe();
        if (le) {
            println("LE image: page size 0x" + Long.toHexString(pageSize) + ", "
                + objects.length + " objects; removed " + cleanImage()
                + " stale symbols from .image");
        }
        String[] args = getScriptArgs();
        if (args.length != 1) {
            throw new IllegalArgumentException("usage: ApplyWatcomSigs.java <sigs.csv>");
        }
        File csv = new File(args[0]);
        List<String> lines = Files.readAllLines(csv.toPath(), StandardCharsets.UTF_8);
        int named = 0, labelled = 0, kept = 0, unmapped = 0, stale = 0;
        Set<Address> matched = new HashSet<>();
        for (String line : lines.subList(1, lines.size())) {
            String[] f = line.split(",", -1);
            if (f.length < 7) {
                continue;
            }
            String version = f.length > 7 && !f[7].isEmpty() ? f[7] : "10.6";
            Address addr;
            if (!f[1].isEmpty()) {
                addr = toAddr(Long.decode(f[1]));
            }
            else {
                addr = le ? leAddress(Long.decode(f[0])) : null;
                if (addr == null) {
                    println("no verified address for file offset " + f[0] + " (" + f[2] + ")");
                    unmapped++;
                    continue;
                }
            }
            matched.add(addr);
            List<String> names = Arrays.asList(f[2].split("\\|"));
            String comment = "Watcom " + version + " runtime: " + String.join(", ", names)
                + " (module " + f[3] + ", " + f[4] + " bytes, " + f[6] + " match; "
                + csv.getName() + ").";
            // Names an earlier run put here that this release does not list.
            Set<String> earlier = new HashSet<>(earlierNames(addr));
            earlier.removeAll(names);
            for (Symbol s : currentProgram.getSymbolTable().getSymbols(addr)) {
                if (s.getSymbolType() == SymbolType.LABEL && earlier.contains(s.getName())) {
                    s.delete();
                }
            }

            if (!f[6].equals("full")) {
                Function at = getFunctionAt(addr);
                if (at != null && earlier.contains(at.getName())) {
                    at.setName(null, SourceType.DEFAULT);
                }
                if (at == null || !names.contains(at.getName())) {
                    createLabel(addr, names.get(0), false, SourceType.IMPORTED);
                }
                putComment(addr, comment);
                labelled++;
                continue;
            }
            Function fn = getFunctionAt(addr);
            if (fn == null) {
                if (getInstructionAt(addr) == null) {
                    disassemble(addr);
                }
                fn = createFunction(addr, null);
            }
            if (fn == null) {
                println("could not create function at " + addr + " for " + names.get(0));
                continue;
            }
            SourceType src = fn.getSymbol().getSource();
            boolean alias = names.contains(fn.getName());
            if (!alias && src == SourceType.USER_DEFINED && !earlier.contains(fn.getName())) {
                println("kept " + fn.getName() + " at " + addr + " (library says " + f[2] + ")");
                kept++;
                continue;
            }
            if (!alias) {
                fn.setName(names.get(0), SourceType.IMPORTED);
            }
            putComment(addr, comment);
            named++;
        }

        Set<Address> commented = new HashSet<>();
        for (Function fn : currentProgram.getFunctionManager().getFunctions(true)) {
            commented.add(fn.getEntryPoint());
        }
        for (Symbol s : currentProgram.getSymbolTable().getAllSymbols(false)) {
            commented.add(s.getAddress());
        }
        for (Address a : commented) {
            if (a.isMemoryAddress() && !matched.contains(a)) {
                stale += removeStale(a);
            }
        }
        println("named " + named + ", labelled " + labelled + ", kept " + kept
            + ", unmapped " + unmapped + ", stale removed " + stale + " from " + csv);
    }
}
