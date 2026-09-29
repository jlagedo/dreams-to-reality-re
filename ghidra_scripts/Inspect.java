/* Read-only binary evidence: refs:<address|symbol>, code:<address>, data:<address>,
 * bytes:<start>-<end> (inclusive, at most 1 MiB),
 * annotations:<function-address> (tags and plate comment),
 * field:<hex-offset>. Usage: -postScript Inspect.java refs:timeGetTime refs:005e5388
 * @category Dreams
 */
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.CodeUnit;
import ghidra.program.model.listing.FunctionTag;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.Symbol;

public class Inspect extends GhidraScript {
    private Address address(String value) {
        return toAddr(Long.parseLong(value.replace("0x", ""), 16));
    }

    private void refs(Address addr) {
        println("REFERENCES " + addr);
        for (Reference ref : getReferencesTo(addr)) {
            Function fn = getFunctionContaining(ref.getFromAddress());
            println(ref.getFromAddress() + " " + ref.getReferenceType() + " "
                + (fn == null ? "data" : fn.getName()) + " "
                + getInstructionAt(ref.getFromAddress()));
        }
    }

    public void run() throws Exception {
        for (String arg : getScriptArgs()) {
            String[] parts = arg.split(":", 2);
            String command = parts[0], value = parts[1];
            println("REQUEST " + arg);
            if (command.equals("refs")) {
                try { refs(address(value)); }
                catch (NumberFormatException ex) {
                    for (Symbol symbol : currentProgram.getSymbolTable().getAllSymbols(true)) {
                        if (symbol.getName().equals(value)) refs(symbol.getAddress());
                    }
                }
            } else if (command.equals("calls")) {
                for (Reference ref : getReferencesTo(address(value))) {
                    Instruction ins = getInstructionAt(ref.getFromAddress());
                    if (ins == null) continue;
                    println("CALLSITE " + ref.getFromAddress());
                    for (int n = 0; n < 10 && ins.getPrevious() != null; n++) ins = ins.getPrevious();
                    while (ins != null && ins.getAddress().compareTo(ref.getFromAddress()) <= 0) {
                        println(ins.getAddress() + " " + ins);
                        ins = ins.getNext();
                    }
                }
            } else if (command.equals("annotations")) {
                Function fn = getFunctionAt(address(value));
                if (fn == null) throw new IllegalArgumentException("No function at " + value);
                println("FUNCTION " + fn.getName() + " " + fn.getEntryPoint());
                for (FunctionTag tag : fn.getTags()) println("TAG " + tag.getName());
                println("PLATE " + currentProgram.getListing().getComment(
                    CodeUnit.PLATE_COMMENT, fn.getEntryPoint()));
            } else if (command.equals("code")) {
                Function fn = getFunctionContaining(address(value));
                if (fn == null) { println("No function at " + value); continue; }
                for (Instruction ins : currentProgram.getListing().getInstructions(fn.getBody(), true)) {
                    println(ins.getAddress() + " " + ins);
                }
            } else if (command.equals("range")) {
                String[] bounds = value.split("-", 2);
                Address end = address(bounds[1]);
                for (Instruction ins : currentProgram.getListing().getInstructions(address(bounds[0]), true)) {
                    if (ins.getAddress().compareTo(end) > 0) break;
                    println(ins.getAddress() + " " + ins);
                }
            } else if (command.equals("bytes")) {
                String[] bounds = value.split("-", 2);
                Address begin = address(bounds[0]), end = address(bounds[1]);
                long count = end.subtract(begin) + 1;
                if (count < 1 || count > 1048576) throw new IllegalArgumentException("Byte range limit");
                for (long offset = 0; offset < count; offset += 32) {
                    byte[] bytes = getBytes(begin.add(offset), (int)Math.min(32, count-offset));
                    StringBuilder hex = new StringBuilder();
                    for (byte b : bytes) hex.append(String.format("%02x", b & 255));
                    println("BYTES " + begin.add(offset) + " " + hex);
                }
            } else if (command.equals("data")) {
                Address addr = address(value);
                if (currentProgram.getMemory().getBlock(addr) == null
                    || !currentProgram.getMemory().getBlock(addr).isInitialized()) {
                    println(addr + " has no initialized on-disk bytes");
                    continue;
                }
                int word = getInt(addr);
                long wide = getLong(addr);
                println(addr + " u32=" + Integer.toUnsignedString(word)
                    + " f32=" + Float.intBitsToFloat(word) + " f64=" + Double.longBitsToDouble(wide));
            } else if (command.equals("field")) {
                String needle = "+ 0x" + value.replace("0x", "").toLowerCase() + "]";
                for (Instruction ins : currentProgram.getListing().getInstructions(true)) {
                    if (ins.toString().toLowerCase().contains(needle)) {
                        Function fn = getFunctionContaining(ins.getAddress());
                        println(ins.getAddress() + " " + (fn == null ? "?" : fn.getName()) + " " + ins);
                    }
                }
            } else throw new IllegalArgumentException("Unknown command " + command);
        }
    }
}
