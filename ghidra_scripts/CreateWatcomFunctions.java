/* Finds the Watcom C functions and switch tables that Ghidra's analysis misses,
 * and optionally creates them.
 *
 * Ghidra creates functions at the entry point, at direct call targets and at
 * MSVC/GCC prologue patterns. Watcom code defeats all three for code that is
 * only reached through a pointer (frame handlers, window procedures, sort
 * callbacks) and for switch tables, which Watcom 10.6 places in the code
 * segment just before the function and dispatches with
 * `jmp dword ptr cs:table[reg*4]`. Verified by compiling test cases with the
 * real 10.6 compiler (docs/re-setup.md).
 *
 * Signals, all read from the binary:
 *   1. stack-check prologues: `push imm; call __CHK` starts every function
 *      compiled with stack checking;
 *   2. switch tables: `[cmp reg,n; ja]` then `2E FF 24 xx table` gives the
 *      table address and its n+1 entries;
 *   3. relocations: every absolute address in the image is relocated. A
 *      relocated pointer into the code block is a case label when it sits in
 *      a switch table, otherwise a code pointer, i.e. a function entry.
 *
 * Usage (headless): CreateWatcomFunctions.java            report only
 *                   CreateWatcomFunctions.java apply      create functions,
 *                   define tables and add their computed-jump references,
 *                   then follow new call targets until nothing changes.
 * Combine `apply` with -readOnly to measure the effect without saving.
 * @category Dreams
 */
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.data.PointerDataType;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.reloc.Relocation;
import ghidra.program.model.symbol.*;
import java.util.*;

public class CreateWatcomFunctions extends GhidraScript {

    private Memory mem;
    private Listing listing;
    private MemoryBlock code;
    private byte[] bytes;
    private long base;

    /** A switch dispatch: the jmp instruction's address, its table and entries. */
    private static class Table {
        Address jmp, start;
        List<Address> targets = new ArrayList<>();
        boolean countFromCmp;
    }

    private int u8(long va) { return bytes[(int) (va - base)] & 0xff; }

    private long u32(long va) {
        int o = (int) (va - base);
        return (bytes[o] & 0xffL) | (bytes[o + 1] & 0xffL) << 8 | (bytes[o + 2] & 0xffL) << 16
                | (bytes[o + 3] & 0xffL) << 24;
    }

    private boolean inCode(long va) { return va >= base && va < base + bytes.length; }

    @Override
    public void run() throws Exception {
        boolean apply = Arrays.asList(getScriptArgs()).contains("apply");
        mem = currentProgram.getMemory();
        listing = currentProgram.getListing();
        code = mem.getBlock("AUTO");
        if (code == null) {
            for (MemoryBlock b : mem.getBlocks()) if (b.isExecute() && b.isInitialized()) { code = b; break; }
        }
        base = code.getStart().getOffset();
        bytes = new byte[(int) code.getSize()];
        code.getBytes(code.getStart(), bytes);

        List<Function> chk = getGlobalFunctions("__CHK");
        if (chk.isEmpty()) { printerr("no function named __CHK; run ApplyWatcomSigs.java first"); return; }
        long chkVa = chk.get(0).getEntryPoint().getOffset();

        println("BEFORE " + coverage());
        int round = 0;
        while (true) {
            round++;
            List<Table> tables = findTables();
            Set<Long> tableSites = new HashSet<>();
            Set<Long> tableStarts = new HashSet<>();
            for (Table t : tables) {
                tableStarts.add(t.start.getOffset());
                for (int i = 0; i < t.targets.size(); i++) tableSites.add(t.start.getOffset() + 4L * i);
            }

            TreeSet<Long> prologues = findPrologues(chkVa);
            Set<Long> relocSites = new HashSet<>();
            List<long[]> relocs = new ArrayList<>();
            for (Iterator<Relocation> it = currentProgram.getRelocationTable().getRelocations(); it.hasNext();) {
                Relocation r = it.next();
                try {
                    long site = r.getAddress().getOffset();
                    relocs.add(new long[] { site, getInt(r.getAddress()) & 0xffffffffL });
                    relocSites.add(site);
                } catch (Exception e) { /* unreadable site */ }
            }
            TreeSet<Long> pointers = new TreeSet<>();
            int caseSites = 0, unclassified = 0, tablesByBase = 0;
            for (long[] r : relocs) {
                long site = r[0], target = r[1];
                if (!inCode(target) || tableStarts.contains(target)) continue;
                if (tableSites.contains(site)) { caseSites++; continue; }
                if (relocSites.contains(target)) { tablesByBase++; continue; }   // points at a pointer: a table
                if (inCode(site) && listing.getInstructionContaining(toAddr(site)) == null) { unclassified++; continue; }
                pointers.add(target);
            }

            TreeSet<Long> fromPrologue = new TreeSet<>();
            for (long a : prologues) if (isNewEntry(a)) fromPrologue.add(a);
            TreeSet<Long> fromPointer = new TreeSet<>();
            int labels = 0;
            int invalid = 0;
            for (long a : pointers) {
                if (fromPrologue.contains(a) || !isNewEntry(a)) continue;
                if (fallsInto(a)) { labels++; continue; }
                if (!validSubroutine(a)) { invalid++; continue; }
                fromPointer.add(a);
            }
            if (invalid > 0) println("ROUND " + round + ": " + invalid + " code pointers rejected: not a valid subroutine");
            int tablesMissing = 0;
            for (Table t : tables) if (!tableDefined(t)) tablesMissing++;

            println(String.format("ROUND %d: tables %d (%d not yet defined, %d without a cmp bound, "
                    + "%d more addressed through a register) | prologues %d, new %d | code pointers %d, "
                    + "new %d, labels inside straight-line code %d | case-label sites %d | "
                    + "relocation sites in undisassembled code %d",
                    round, tables.size(), tablesMissing, tables.stream().filter(t -> !t.countFromCmp).count(),
                    tablesByBase, prologues.size(), fromPrologue.size(), pointers.size(), fromPointer.size(),
                    labels, caseSites, unclassified));

            if (!apply) {
                for (long a : fromPrologue) println("NEW " + toAddr(a) + " prologue");
                for (long a : fromPointer) println("NEW " + toAddr(a) + " pointer");
                break;
            }
            int changed = 0;
            for (Table t : tables) changed += defineTable(t);
            for (long a : fromPrologue) changed += makeFunction(toAddr(a)) ? 1 : 0;
            changed += followCalls();
            for (long a : fromPointer) {
                if (isNewEntry(a) && !fallsInto(a) && makeFunction(toAddr(a))) {
                    changed++;
                    if (Arrays.asList(getScriptArgs()).contains("verbose")) describe(toAddr(a));
                }
            }
            changed += followCalls();
            for (Table t : tables) {   // re-grow bodies that now reach their case labels
                Function f = getFunctionContaining(t.jmp);
                if (f != null) CreateFunctionCmd.fixupFunctionBody(currentProgram, f, monitor);
            }
            println("ROUND " + round + " changes " + changed + " | " + coverage());
            if (changed == 0 || round >= 10) break;
        }
        println("AFTER " + coverage());
    }

    /** A candidate entry is new if no function starts or runs there. */
    private boolean isNewEntry(long va) {
        Address a = toAddr(va);
        if (getFunctionAt(a) != null || getFunctionContaining(a) != null) return false;
        Data d = listing.getDefinedDataContaining(a);
        return d == null;
    }

    /** One line per pointer-created function: size, how it ends, its first instructions, who points at it. */
    private void describe(Address a) {
        Function f = getFunctionAt(a);
        if (f == null) return;
        StringBuilder first = new StringBuilder();
        Instruction ins = listing.getInstructionAt(a);
        for (int i = 0; i < 3 && ins != null; i++, ins = ins.getNext()) first.append(ins).append(" ; ");
        Instruction last = listing.getInstructionContaining(f.getBody().getMaxAddress());
        StringBuilder from = new StringBuilder();
        for (Reference r : getReferencesTo(a)) {
            Function g = getFunctionContaining(r.getFromAddress());
            from.append(r.getFromAddress()).append(g == null ? "" : "(" + g.getName() + ")").append(" ");
        }
        println(String.format("CREATED %s size=%d last=%s first=[%s] from=%s", a, f.getBody().getNumAddresses(),
                last == null ? "?" : last.getMnemonicString(), first, from));
    }

    /** Ghidra's own test, without touching the listing: decodes, flows to a return, no bad bytes. */
    private boolean validSubroutine(long va) {
        try {
            return new ghidra.app.util.PseudoDisassembler(currentProgram).isValidSubroutine(toAddr(va), true);
        } catch (Exception e) {
            return false;
        }
    }

    /** True when the instruction just before `va` is disassembled and falls through into it. */
    private boolean fallsInto(long va) {
        Instruction prev = listing.getInstructionContaining(toAddr(va - 1));
        if (prev == null) return false;
        Address fall = prev.getFallThrough();
        return fall != null && fall.getOffset() == va;
    }

    private TreeSet<Long> findPrologues(long chkVa) {
        TreeSet<Long> out = new TreeSet<>();
        for (int i = 0; i + 10 <= bytes.length; i++) {
            int op = bytes[i] & 0xff;
            long va = base + i;
            if (op == 0x68 && (bytes[i + 5] & 0xff) == 0xE8) {
                if (va + 10 + (int) u32(va + 6) == chkVa) out.add(va);
            } else if (op == 0x6A && (bytes[i + 2] & 0xff) == 0xE8) {
                if (va + 7 + (int) u32(va + 3) == chkVa) out.add(va);
            }
        }
        return out;
    }

    /** `[2E] FF 24 sib disp32` with sib = scale 4, no base: jmp [disp32 + reg*4]. */
    private List<Table> findTables() {
        List<Table> out = new ArrayList<>();
        for (int i = 1; i + 7 <= bytes.length; i++) {
            if ((bytes[i] & 0xff) != 0xFF || (bytes[i + 1] & 0xff) != 0x24) continue;
            int sib = bytes[i + 2] & 0xff;
            if ((sib & 0xC7) != 0x85) continue;           // scale 4, base = disp32
            int reg = (sib >> 3) & 7;
            if (reg == 4) continue;                        // no index
            long jmp = base + i - (((bytes[i - 1] & 0xff) == 0x2E) ? 1 : 0);
            long start = u32(base + i + 3);
            if (!inCode(start)) continue;
            Table t = new Table();
            t.jmp = toAddr(jmp);
            t.start = toAddr(start);
            long n = boundFromCmp(jmp, reg);
            t.countFromCmp = n >= 0;
            for (long k = 0; ; k++) {
                long site = start + 4 * k;
                if (!inCode(site + 3)) break;
                if (t.countFromCmp && k > n) break;
                long target = u32(site);
                if (!inCode(target)) break;
                if (!t.countFromCmp && (Math.abs(target - jmp) > 0x10000 || k > 1024)) break;
                t.targets.add(toAddr(target));
            }
            if (!t.targets.isEmpty()) out.add(t);
        }
        return out;
    }

    /** Looks back from the jmp for `cmp reg,imm; ja`. Returns the highest index, or -1. */
    private long boundFromCmp(long jmp, int reg) {
        long p;
        if (inCode(jmp - 2) && u8(jmp - 2) == 0x77) p = jmp - 2;
        else if (inCode(jmp - 6) && u8(jmp - 6) == 0x0F && u8(jmp - 5) == 0x87) p = jmp - 6;
        else return -1;
        if (inCode(p - 3) && u8(p - 3) == 0x83 && u8(p - 2) == (0xF8 | reg)) return u8(p - 1);
        if (inCode(p - 6) && u8(p - 6) == 0x81 && u8(p - 5) == (0xF8 | reg)) return u32(p - 4);
        if (reg == 0 && inCode(p - 5) && u8(p - 5) == 0x3D) return u32(p - 4);
        return -1;
    }

    private boolean tableDefined(Table t) {
        Data d = listing.getDataAt(t.start);
        return d != null && d.isDefined();
    }

    private int defineTable(Table t) {
        int changed = 0;
        ReferenceManager rm = currentProgram.getReferenceManager();
        for (int k = 0; k < t.targets.size(); k++) {
            Address site = t.start.add(4L * k);
            Address target = t.targets.get(k);
            try {
                Data d = listing.getDataAt(site);
                if (d == null || !d.isDefined()) {
                    if (listing.getInstructionContaining(site) == null) {
                        createData(site, PointerDataType.dataType);
                        changed++;
                    }
                }
            } catch (Exception e) { /* overlaps something already defined: leave it */ }
            if (listing.getInstructionAt(target) == null && disassemble(target)) changed++;
            boolean has = false;
            for (Reference r : rm.getReferencesFrom(t.jmp)) if (r.getToAddress().equals(target)) has = true;
            if (!has && listing.getInstructionAt(t.jmp) != null) {
                rm.addMemoryReference(t.jmp, target, RefType.COMPUTED_JUMP, SourceType.ANALYSIS, 0);
                changed++;
            }
        }
        return changed;
    }

    private boolean makeFunction(Address a) {
        if (listing.getInstructionAt(a) == null && !disassemble(a)) return false;
        return createFunction(a, null) != null;
    }

    /** Direct call targets that are not function entries yet. */
    private int followCalls() {
        int made = 0;
        TreeSet<Address> todo = new TreeSet<>();
        for (Instruction ins : listing.getInstructions(code.getStart(), true)) {
            if (!code.contains(ins.getAddress())) break;
            if (!ins.getFlowType().isCall()) continue;
            for (Address t : ins.getFlows()) {
                if (code.contains(t) && getFunctionAt(t) == null) todo.add(t);
            }
        }
        for (Address t : todo) if (getFunctionContaining(t) == null && makeFunction(t)) made++;
        return made;
    }

    private String coverage() {
        long inFn = 0, orphan = 0, undef = 0;
        int count = currentProgram.getFunctionManager().getFunctionCount();
        CodeUnitIterator it = listing.getCodeUnits(new AddressSet(code.getStart(), code.getEnd()), true);
        while (it.hasNext()) {
            CodeUnit cu = it.next();
            long len = cu.getLength();
            if (cu instanceof Instruction) {
                if (getFunctionContaining(cu.getAddress()) != null) inFn += len; else orphan += len;
            } else if (!((Data) cu).isDefined()) {
                undef += len;
            }
        }
        return String.format("functions %d | bytes: in functions %d, instructions outside functions %d, "
                + "undefined %d (code block %d)", count, inFn, orphan, undef, code.getSize());
    }
}
