/* Apply reviewed function-boundary fixes from re/boundaries/<program>.tsv.
 *
 * ReportBoundaries.java lists where control flow leaves a function body
 * through anything other than a return or a jump/call to a function entry.
 * Each defect, once examined, becomes a row here:
 *
 *   function  <addr>          disassemble at addr and create a function there
 *                             (entries Ghidra never reached: after a Watcom
 *                             switch table, pointer-only callbacks, helpers)
 *   body      <addr>          recompute the body of the function at addr from
 *                             its flow (code it jumps to but does not own)
 *   code      <addr> <len>    clear a reviewed misaligned instruction range,
 *                             then disassemble from the proven boundary;
 *                             refuses to erase any function entry in the range
 *   data      <addr> <len>    clear instructions over [addr, addr+len): data
 *                             that analysis decoded as code
 *   guid      <addr> <len>    the same, then type the range as GUIDs so that
 *                             relocated pointers to them (COM interface IDs)
 *                             are not taken for code pointers later
 *   table     <addr> <count>  a jump table of count code pointers that
 *                             CreateWatcomFunctions does not recognise (hand-
 *                             written dispatch such as jmp [eax+table]): type
 *                             the entries as pointers, disassemble the targets,
 *                             add computed-jump references from the one jump
 *                             that reads the table, and rebuild its function
 *   tail      <addr>          no change: a shared tail another function jumps
 *                             into (Watcom shares epilogues); recorded so the
 *                             report can be read against it
 *
 * Columns: address, action, length, note. Lines starting with # are comments.
 * `function` rows run first, then `data` and `guid`, then `code`, `table`, then
 * `body`, so a rebuilt body can stop at a newly created entry.
 *
 * New functions can hold Watcom switch dispatches whose tables nothing has
 * defined yet, so chain CreateWatcomFunctions.java apply afterwards: it
 * defines the tables, adds the case references and rebuilds those bodies.
 *
 * Usage (headless; add -readOnly to preview, and chain ReportBoundaries.java
 * in the same run to see the result):
 *   analyzeHeadless ghidra dreams -process WINDREAM.EXE -noanalysis \
 *       -scriptPath ghidra_scripts -postScript ApplyBoundaries.java \
 *       re\boundaries\WINDREAM.EXE.tsv \
 *       -postScript CreateWatcomFunctions.java apply
 *
 * @category Dreams
 */

import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.List;

import ghidra.app.cmd.disassemble.DisassembleCommand;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.script.GhidraScript;
import ghidra.app.util.datatype.microsoft.GuidDataType;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.data.PointerDataType;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.RefType;
import ghidra.program.model.symbol.ReferenceManager;
import ghidra.program.model.symbol.SourceType;

public class ApplyBoundaries extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length == 0) {
            println("usage: ApplyBoundaries.java <re/boundaries/program.tsv>");
            return;
        }
        List<String[]> rows = new ArrayList<>();
        for (String line : Files.readAllLines(Paths.get(args[0]))) {
            if (line.isBlank() || line.startsWith("#") || line.startsWith("address\t")) {
                continue;
            }
            rows.add(line.split("\t", -1));
        }
        int created = 0, rebuilt = 0, cleared = 0, tables = 0, tails = 0, failed = 0;

        for (String[] r : rows) {
            if (!r[1].equals("function")) {
                continue;
            }
            Address a = toAddr(r[0]);
            if (getFunctionAt(a) != null) {
                continue;
            }
            if (getInstructionAt(a) == null) {
                DisassembleCommand dis = new DisassembleCommand(a, null, true);
                dis.applyTo(currentProgram, monitor);
            }
            CreateFunctionCmd cmd = new CreateFunctionCmd(a);
            if (cmd.applyTo(currentProgram, monitor)) {
                created++;
                println("function " + a + " (" + getFunctionAt(a).getBody().getNumAddresses()
                    + " bytes)");
            }
            else {
                failed++;
                println("FAILED function " + a + ": " + cmd.getStatusMsg());
            }
        }
        for (String[] r : rows) {
            boolean guid = r[1].equals("guid");
            if (!guid && !r[1].equals("data")) {
                continue;
            }
            Address a = toAddr(r[0]);
            long len = Long.parseLong(r[2]);
            Address end = a.add(len - 1);
            for (Function f : currentProgram.getFunctionManager()
                    .getFunctions(new AddressSet(a, end), true)) {
                println("removed function " + f.getName() + " inside data");
                removeFunction(f);
            }
            currentProgram.getListing().clearCodeUnits(a, end, false);
            if (guid) {
                for (long off = 0; off + 16 <= len; off += 16) {
                    createData(a.add(off), new GuidDataType());
                }
            }
            cleared++;
            println((guid ? "guid " : "data ") + a + "-" + end + " cleared");
        }
        for (String[] r : rows) {
            if (!r[1].equals("code")) continue;
            Address a = toAddr(r[0]);
            long length = Long.parseLong(r[2]);
            if (length < 1) throw new IllegalArgumentException("Empty code repair");
            Address end = a.add(length - 1);
            for (Function f : currentProgram.getFunctionManager()
                    .getFunctions(new AddressSet(a, end), true)) {
                throw new IllegalArgumentException("Code repair would erase entry " + f.getEntryPoint());
            }
            currentProgram.getListing().clearCodeUnits(a, end, false);
            if (!new DisassembleCommand(a, null, true).applyTo(currentProgram, monitor)) {
                throw new IllegalStateException("Failed to disassemble reviewed code at " + a);
            }
            println("code " + a + "-" + end + " realigned");
        }
        for (String[] r : rows) {
            if (!r[1].equals("table")) {
                continue;
            }
            if (applyTable(toAddr(r[0]), Integer.parseInt(r[2]))) {
                tables++;
            }
            else {
                failed++;
            }
        }
        for (String[] r : rows) {
            if (r[1].equals("tail")) {
                tails++;
            }
            if (!r[1].equals("body")) {
                continue;
            }
            Function fn = getFunctionAt(toAddr(r[0]));
            if (fn == null) {
                failed++;
                println("FAILED body " + r[0] + ": no function");
                continue;
            }
            long before = fn.getBody().getNumAddresses();
            CreateFunctionCmd.fixupFunctionBody(currentProgram, fn, monitor);
            rebuilt++;
            println("body " + fn.getName() + " " + before + " -> "
                + fn.getBody().getNumAddresses() + " bytes");
        }
        println("boundaries: " + created + " functions created, " + rebuilt + " bodies rebuilt, "
            + cleared + " data ranges cleared, " + tables + " jump tables, " + tails
            + " shared tails recorded, " + failed + " failed");
    }

    /** See `table` in the header. */
    private boolean applyTable(Address table, int count) throws Exception {
        // Ghidra keeps no reference for a [reg+disp] operand, so match the
        // displacement of every computed jump against the table address.
        Instruction jmp = null;
        for (Instruction ins : currentProgram.getListing().getInstructions(true)) {
            if (!ins.getFlowType().isJump() || !ins.getFlowType().isComputed()) {
                continue;
            }
            for (Object o : ins.getOpObjects(0)) {
                if (o instanceof Scalar s && s.getUnsignedValue() == table.getOffset()) {
                    jmp = ins;
                }
            }
        }
        if (jmp == null) {
            println("FAILED table " + table + ": no computed jump reads it");
            return false;
        }
        ReferenceManager rm = currentProgram.getReferenceManager();
        for (int i = 0; i < count; i++) {
            Address slot = table.add(4L * i);
            Address target = toAddr(getInt(slot) & 0xffffffffL);
            currentProgram.getListing().clearCodeUnits(slot, slot.add(3), false);
            createData(slot, PointerDataType.dataType);
            if (getInstructionAt(target) == null) {
                new DisassembleCommand(target, null, true).applyTo(currentProgram, monitor);
            }
            rm.addMemoryReference(jmp.getAddress(), target, RefType.COMPUTED_JUMP,
                SourceType.USER_DEFINED, 0);
        }
        Function fn = getFunctionContaining(jmp.getAddress());
        if (fn != null) {
            CreateFunctionCmd.fixupFunctionBody(currentProgram, fn, monitor);
        }
        println("table " + table + ": " + count + " targets from " + jmp.getAddress()
            + (fn == null ? "" : " in " + fn.getName() + ", body now "
                + fn.getBody().getNumAddresses() + " bytes"));
        return true;
    }
}
