/* Report function-boundary defects: control flow that leaves a function body
 * anywhere other than a return, a jump to a function entry or a call to one.
 *
 * Watcom shares epilogues between neighbouring functions and reaches them with
 * `je` as well as `jmp`, leaves code reached only through pointers without a
 * function, and ends functions in calls that never return (exit_, longjmp_).
 * Each of these shows up as a flow edge from one body to an address that is
 * not a function entry. Ghidra function bodies may overlap, so a shared tail
 * that sits inside both bodies is fine and is not reported.
 *
 * Kinds (one TSV row each; `from` is the instruction, `to` the target):
 *   falls_out  an instruction's fall-through leaves the body (the function
 *              ends without ret/jmp; a call there is to a non-returning or
 *              misjudged callee when `detail` is call)
 *   jump_in    a branch lands inside another function's body, off its entry,
 *              and outside the jumping function's own body
 *   jump_out   a branch lands in no function body at all
 *   call_in    a call lands inside a function body, off its entry
 *   call_out   a call lands on an instruction in no function
 *   orphan     a run of instructions in no function body (start, byte length,
 *              and how many references reach its start)
 *   undefined  a run of at least 8 undefined bytes in an executable block that
 *              is not all 00/90/CC filler (start, byte length, the function
 *              whose address span holds it); code nobody has disassembled, or
 *              data such as switch tables not yet defined
 *
 * With the reviewed fix list re/boundaries/<program>.tsv as a second
 * argument, a row whose target is recorded there as a `tail` is marked
 * `recorded`; the summary counts the rest, which still need a decision.
 *
 * Read-only. Usage (headless, from the repository root):
 *   analyzeHeadless ghidra dreams -process WINDREAM.EXE -noanalysis -readOnly \
 *       -scriptPath ghidra_scripts -postScript ReportBoundaries.java \
 *       out\ghidra\boundaries\WINDREAM.EXE.tsv re\boundaries\WINDREAM.EXE.tsv
 *
 * @category Dreams
 */

import java.io.File;
import java.io.PrintWriter;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.HashMap;
import java.util.HashSet;
import java.util.Map;
import java.util.Set;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.CodeUnit;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.FlowType;
import ghidra.program.model.symbol.Reference;

public class ReportBoundaries extends GhidraScript {

    private FunctionManager fm;
    private PrintWriter out;
    private final Map<String, Integer> counts = new HashMap<>();
    private final Set<String> tails = new HashSet<>();

    private String name(Function f) {
        return f == null ? "" : f.getName();
    }

    private void row(String kind, Address from, Function fromFn, Address to, String detail) {
        Function toFn = to == null ? null : fm.getFunctionContaining(to);
        boolean recorded = to != null && tails.contains(to.toString());
        out.println(kind + "\t" + from + "\t" + name(fromFn) + "\t" + (to == null ? "" : to)
            + "\t" + name(toFn) + "\t" + detail + "\t" + (recorded ? "recorded" : ""));
        counts.merge(recorded ? kind + " (recorded)" : kind, 1, Integer::sum);
    }

    private boolean inAnyBody(Address a) {
        return fm.getFunctionContaining(a) != null;
    }

    /** The function whose [entry, highest body address] span holds a, if any. */
    private Function spanning(Address a) {
        Function f = getFunctionBefore(a);
        if (f == null) {
            f = getFunctionAt(a);
        }
        return f != null && f.getBody().getMaxAddress().compareTo(a) >= 0 ? f : null;
    }

    private void undefinedRuns(MemoryBlock block) throws Exception {
        Address a = block.getStart();
        while (a != null && block.contains(a)) {
            Data d = getUndefinedDataAt(a);
            if (d == null) {
                CodeUnit cu = currentProgram.getListing().getCodeUnitAt(a);
                a = cu == null ? a.next() : cu.getMaxAddress().next();
                continue;
            }
            Address start = a;
            boolean filler = true;
            long n = 0;
            while (a != null && block.contains(a) && getUndefinedDataAt(a) != null) {
                int b = getByte(a) & 0xff;
                filler &= b == 0x00 || b == 0x90 || b == 0xcc;
                n++;
                a = a.next();
            }
            if (n >= 8 && !filler) {
                row("undefined", start, spanning(start), null, n + " bytes");
            }
        }
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length == 0) {
            println("usage: ReportBoundaries.java <out.tsv>");
            return;
        }
        fm = currentProgram.getFunctionManager();
        if (args.length > 1) {
            for (String line : Files.readAllLines(Paths.get(args[1]))) {
                String[] f = line.split("\t", -1);
                if (f.length > 1 && f[1].equals("tail")) {
                    tails.add(f[0].toLowerCase());
                }
            }
        }
        File file = new File(args[0]);
        file.getAbsoluteFile().getParentFile().mkdirs();
        out = new PrintWriter(file, "UTF-8");
        out.println("kind\tfrom\tfrom_function\tto\tto_function\tdetail\trecorded");

        for (Function fn : fm.getFunctions(true)) {
            if (fn.isExternal() || fn.isThunk()) {
                continue;
            }
            InstructionIterator it = currentProgram.getListing().getInstructions(fn.getBody(), true);
            while (it.hasNext()) {
                Instruction ins = it.next();
                FlowType flow = ins.getFlowType();
                Address fall = ins.getFallThrough();
                if (fall != null && !fn.getBody().contains(fall)) {
                    Function at = fm.getFunctionAt(fall);
                    if (at == null) {
                        row("falls_out", ins.getAddress(), fn, fall,
                            flow.isCall() ? "call" : ins.getMnemonicString().toLowerCase());
                    }
                }
                if (!flow.isJump() && !flow.isCall()) {
                    continue;
                }
                for (Reference r : ins.getReferencesFrom()) {
                    if (!r.getReferenceType().isFlow() || r.getReferenceType().isComputed()) {
                        continue;
                    }
                    Address to = r.getToAddress();
                    if (!to.isMemoryAddress() || fm.getFunctionAt(to) != null) {
                        continue;
                    }
                    Function owner = fm.getFunctionContaining(to);
                    if (flow.isCall()) {
                        if (owner != null) {
                            row("call_in", ins.getAddress(), fn, to, "");
                        }
                        else if (getInstructionAt(to) != null) {
                            row("call_out", ins.getAddress(), fn, to, "");
                        }
                    }
                    else if (!fn.getBody().contains(to)) {
                        row(owner != null ? "jump_in" : "jump_out", ins.getAddress(), fn, to,
                            ins.getMnemonicString().toLowerCase());
                    }
                }
            }
        }

        // Instructions that belong to no function, grouped into runs.
        for (MemoryBlock block : currentProgram.getMemory().getBlocks()) {
            if (!block.isExecute()) {
                continue;
            }
            InstructionIterator it = currentProgram.getListing().getInstructions(block.getStart(), true);
            Address start = null;
            Address next = null;
            long bytes = 0;
            while (it.hasNext()) {
                Instruction ins = it.next();
                if (!block.contains(ins.getAddress())) {
                    break;
                }
                boolean orphan = !inAnyBody(ins.getAddress());
                if (start != null && (!orphan || !ins.getAddress().equals(next))) {
                    row("orphan", start, null, null,
                        bytes + " bytes, " + getReferencesTo(start).length + " refs");
                    start = null;
                }
                if (orphan) {
                    if (start == null) {
                        start = ins.getAddress();
                        bytes = 0;
                    }
                    bytes += ins.getLength();
                    next = ins.getMaxAddress().next();
                }
            }
            if (start != null) {
                row("orphan", start, null, null,
                    bytes + " bytes, " + getReferencesTo(start).length + " refs");
            }
            undefinedRuns(block);
        }
        out.close();
        println("boundary report " + file + ": " + counts);
    }
}
