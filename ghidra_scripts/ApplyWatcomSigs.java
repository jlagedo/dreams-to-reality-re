/* Name Watcom 10.6 runtime functions from the library match CSV.
 *
 * src/dreams/watcom.py matches the Watcom runtime libraries against each game
 * binary and writes <DREAMS_WATCOM>\sigs\<program>.csv (docs/toolchain.md):
 *   file_offset,virtual_address,name,module,length,fixed_bytes,extent
 * PE rows carry a virtual address; LE rows (DREAMS.EXE, DREAMSFX.EXE) only a
 * file offset. The LE loader keeps a raw copy of the file in an ".image"
 * overlay, and Memory.locateAddressesForFileOffset resolves into that copy,
 * not into the loaded objects. So the offset is mapped here through the LE
 * object and page tables (read from ".image"), and the bytes at the result are
 * compared with the file before anything is named.
 *
 * Usage (headless, without -readOnly):
 *   analyzeHeadless ghidra dreams -process DREAMSFX.EXE -noanalysis \
 *       -scriptPath ghidra_scripts -postScript ApplyWatcomSigs.java \
 *       <watcom>\sigs\dreamsfx.csv
 *
 * Rows matched over their full extent name the function at that address
 * (creating it if needed); "core"-only rows get a label and comment. Where one
 * body carries several public names ("fprintf_|fscanf_|sscanf_") the first is
 * used and all are listed in the comment. Names we assigned are never
 * overwritten; conflicts are reported.
 *
 * @category Dreams
 */

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.List;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.SourceType;

public class ApplyWatcomSigs extends GhidraScript {

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
        int named = 0, labelled = 0, kept = 0, unmapped = 0;
        for (String line : lines.subList(1, lines.size())) {
            String[] f = line.split(",", -1);
            if (f.length < 7) {
                continue;
            }
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
            String[] names = f[2].split("\\|");
            String comment = "Watcom 10.6 runtime: " + String.join(", ", names) + " (module "
                + f[3] + ", " + f[4] + " bytes, " + f[6] + " match; " + csv.getName() + ").";

            if (!f[6].equals("full")) {
                createLabel(addr, names[0], false, SourceType.IMPORTED);
                setPlateComment(addr, comment);
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
                println("could not create function at " + addr + " for " + names[0]);
                continue;
            }
            SourceType src = fn.getSymbol().getSource();
            if (src == SourceType.USER_DEFINED && !fn.getName().equals(names[0])) {
                println("kept " + fn.getName() + " at " + addr + " (library says " + f[2] + ")");
                kept++;
                continue;
            }
            fn.setName(names[0], SourceType.IMPORTED);
            if (fn.getComment() == null || !fn.getComment().contains("Watcom 10.6 runtime")) {
                fn.setComment(fn.getComment() == null ? comment : fn.getComment() + "\n\n" + comment);
            }
            named++;
        }
        println("named " + named + ", labelled " + labelled + ", kept " + kept
            + ", unmapped " + unmapped + " from " + csv);
    }
}
