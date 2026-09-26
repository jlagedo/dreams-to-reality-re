/* Name and type the Glide 2 import stubs in DREAMSFX.EXE.
 *
 * DREAMSFX.EXE links 3dfx's DOS import library (glide2x/h3/glide/src/
 * glimport.asm in the Glide source). It has three parallel tables:
 *   ENTRYNAMES   "_GRDRAWTRIANGLE@12\0" ...        the decorated names
 *   ENTRYTHUNKS  one 5-byte "call __loadme" per name, in the same order
 *   ENTRYTABLE   __dlltab: one pointer per name
 * The game calls the thunks directly; on first use __loadme loads
 * glide2x.ovl and patches the thunk. Naming each thunk and applying the
 * header prototype (__stdcall) types every Glide call site.
 *
 * Usage (headless, without -readOnly):
 *   analyzeHeadless ghidra dreams -process DREAMSFX.EXE -noanalysis \
 *       -scriptPath ghidra_scripts -postScript ApplyGlideImports.java <glide-src>
 *
 * <glide-src> is a checkout of the released Glide source (e.g. sezero/glide).
 * Headers come from glide2x/sst1 (Voodoo Graphics). They are parsed into a
 * temporary archive under out/ghidra and never checked in.
 *
 * @category Dreams
 */

import java.io.File;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Map;

import ghidra.app.cmd.function.ApplyFunctionSignatureCmd;
import ghidra.app.cmd.function.CreateFunctionCmd;
import ghidra.app.script.GhidraScript;
import ghidra.app.util.cparser.C.CParserUtils;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.block.CodeBlock;
import ghidra.program.model.block.IsolatedEntrySubModel;
import ghidra.program.model.data.DataType;
import ghidra.program.model.data.DataTypeConflictHandler;
import ghidra.program.model.data.DataTypeManager;
import ghidra.program.model.data.FileDataTypeManager;
import ghidra.program.model.data.FunctionDefinition;
import ghidra.program.model.data.VoidDataType;
import ghidra.program.model.listing.FlowOverride;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolType;

public class ApplyGlideImports extends GhidraScript {

    private String repoRoot() {
        String env = System.getenv("DREAMS_REPO");
        if (env != null && !env.isEmpty()) {
            return env;
        }
        return new File(sourceFile.getAbsolutePath()).getParentFile().getParent();
    }

    private String cString(Address at) throws Exception {
        StringBuilder sb = new StringBuilder();
        Memory mem = currentProgram.getMemory();
        for (int i = 0; i < 64; i++) {
            byte b = mem.getByte(at.add(i));
            if (b == 0) {
                return sb.toString();
            }
            sb.append((char) b);
        }
        return null;
    }

    private Address find(byte[] bytes) {
        Memory mem = currentProgram.getMemory();
        return mem.findBytes(mem.getMinAddress(), bytes, null, true, monitor);
    }

    private static byte[] le32(long v) {
        return new byte[] { (byte) v, (byte) (v >> 8), (byte) (v >> 16), (byte) (v >> 24) };
    }

    /* "_GRDRAWTRIANGLE@12" -> "GRDRAWTRIANGLE" */
    private static String key(String decorated) {
        String s = decorated.startsWith("_") ? decorated.substring(1) : decorated;
        int at = s.indexOf('@');
        return (at < 0 ? s : s.substring(0, at)).toUpperCase();
    }

    /* Idempotent rename: drop a stale label of the same name at the entry
     * (left by an earlier run) so re-running the script is safe. */
    private void rename(Function fn, String name) throws Exception {
        if (name.equals(fn.getName())) {
            return;
        }
        for (Symbol s : currentProgram.getSymbolTable().getSymbols(fn.getEntryPoint())) {
            if (name.equals(s.getName()) && s.getSymbolType() != SymbolType.FUNCTION) {
                s.delete();
            }
        }
        fn.setName(name, SourceType.USER_DEFINED);
    }

    private Map<String, FunctionDefinition> parseHeaders(File glide) throws Exception {
        File sst1 = new File(glide, "glide2x/sst1");
        File header = new File(sst1, "glide/src/glide.h");
        if (!header.isFile()) {
            throw new IllegalArgumentException("no Glide 2 header at " + header);
        }
        String[] includes = {
            new File(sst1, "glide/src").getAbsolutePath(),
            new File(sst1, "init").getAbsolutePath(),
            new File(glide, "swlibs/fxmisc").getAbsolutePath(),
        };
        // Watcom on DOS: FX_CALL expands to __stdcall.
        String[] args = { "-D__WATCOMC__=1", "-D__DOS__=1", "-D__MSDOS__=1" };

        File tempDir = new File(repoRoot(), "out/ghidra");
        if (!tempDir.isDirectory() && !tempDir.mkdirs()) {
            throw new IllegalStateException("could not create " + tempDir);
        }
        File archive = new File(tempDir, "glide2x.gdt");
        new File(archive.getAbsolutePath() + ".ulock").delete();
        if (archive.exists() && !archive.delete()) {
            throw new IllegalStateException("could not replace " + archive);
        }

        FileDataTypeManager parsed = CParserUtils.parseHeaderFiles(null,
            new String[] { header.getAbsolutePath() }, includes, args,
            archive.getAbsolutePath(), currentProgram.getLanguageID().getIdAsString(),
            currentProgram.getCompilerSpec().getCompilerSpecID().getIdAsString(), monitor);

        DataTypeManager target = currentProgram.getDataTypeManager();
        Map<String, FunctionDefinition> byKey = new HashMap<>();
        int count = 0;
        try {
            Iterator<DataType> it = parsed.getAllDataTypes();
            while (it.hasNext() && !monitor.isCancelled()) {
                DataType added = target.addDataType(it.next(),
                    DataTypeConflictHandler.REPLACE_HANDLER);
                count++;
                if (added instanceof FunctionDefinition) {
                    byKey.put(added.getName().toUpperCase(), (FunctionDefinition) added);
                }
            }
        }
        finally {
            parsed.close();
        }
        println("imported " + count + " Glide data types, " + byKey.size() + " prototypes");
        return byKey;
    }

    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        if (a.length < 1) {
            throw new IllegalArgumentException("usage: ApplyGlideImports.java <glide-src>");
        }
        Map<String, FunctionDefinition> protos = parseHeaders(new File(a[0]));

        // ENTRYNAMES starts with the first EntryPoint in glimport.asm.
        Address firstName = find("_GRAADRAWLINE@8\0".getBytes("US-ASCII"));
        if (firstName == null) {
            throw new IllegalStateException("Glide name table not found");
        }
        Address table = find(le32(firstName.getOffset()));
        if (table == null) {
            throw new IllegalStateException("__dlltab not found");
        }

        List<String> names = new ArrayList<>();
        Memory mem = currentProgram.getMemory();
        // ENTRYTABLE is the last thing in the code object; stop at its end.
        for (Address p = table; mem.getBlock(p) != null
                && mem.getBlock(p).contains(p.add(3)); p = p.add(4)) {
            Address ptr = toAddr(mem.getInt(p) & 0xffffffffL);
            if (!mem.contains(ptr)) {
                break;
            }
            String s = cString(ptr);
            // Not just _GR*/_GU*: the list ends with _CONVERTANDDOWNLOADRLE@64.
            if (s == null || !s.matches("_[A-Z0-9_]+@\\d+")) {
                break;
            }
            names.add(s);
        }
        int n = names.size();

        // ENTRYTHUNKS ends where the dword-aligned ENTRYTABLE begins.
        Address first = null;
        Address loadme = null;
        for (int pad = 0; pad < 4 && first == null; pad++) {
            Address cand = table.subtract(5L * n + pad);
            Address target = null;
            boolean ok = true;
            for (int i = 0; i < n && ok; i++) {
                Address t = cand.add(5L * i);
                if ((mem.getByte(t) & 0xff) != 0xe8) {
                    ok = false;
                    break;
                }
                Address dest = t.add(5 + mem.getInt(t.add(1)));
                if (target == null) {
                    target = dest;
                }
                ok = dest.equals(target);
            }
            if (ok) {
                first = cand;
                loadme = target;
            }
        }
        if (first == null) {
            throw new IllegalStateException("thunk run not found before " + table);
        }

        createLabel(table, "__dlltab", true, SourceType.USER_DEFINED);
        createLabel(first, "__dllfirst", true, SourceType.USER_DEFINED);
        Function lm = getFunctionAt(loadme);
        if (lm == null) {
            disassemble(loadme);
            lm = createFunction(loadme, "__loadme");
        }
        if (lm != null) {
            lm.setName("__loadme", SourceType.USER_DEFINED);
            // Clear any prototype an earlier run leaked through the thunks.
            lm.replaceParameters(Function.FunctionUpdateType.DYNAMIC_STORAGE_ALL_PARAMS,
                true, SourceType.DEFAULT);
            lm.setReturnType(VoidDataType.dataType, SourceType.DEFAULT);
        }
        Address dllName = find("glide2x\0".getBytes("US-ASCII"));
        if (dllName != null) {
            createLabel(dllName, "__dllname", true, SourceType.USER_DEFINED);
        }
        println(n + " thunks at " + first + ", __dlltab " + table + ", __loadme " + loadme);

        // __loadme ends by jumping into glide2x.ovl, never back to the thunk, so
        // auto-analysis marks it and every thunk no-return and truncates each
        // caller after its first Glide call. Undo that, then re-analyse.
        if (lm != null) {
            lm.setNoReturn(false);
        }
        Address end = first.add(5L * n);
        for (int i = 0; i < n; i++) {
            Function fn = getFunctionAt(first.add(5L * i));
            if (fn != null) {
                fn.setNoReturn(false);
            }
        }
        int reopened = 0;
        for (int pass = 0; pass < 8; pass++) {
            int before = reopened;
            for (int i = 0; i < n; i++) {
                for (Reference r : getReferencesTo(first.add(5L * i))) {
                    Instruction call = getInstructionAt(r.getFromAddress());
                    if (call == null || !r.getReferenceType().isCall()) {
                        continue;
                    }
                    if (call.getFlowOverride() == FlowOverride.CALL_RETURN) {
                        call.setFlowOverride(FlowOverride.NONE);
                    }
                    Address next = call.getMaxAddress().add(1);
                    if (getInstructionAt(next) == null && disassemble(next)) {
                        reopened++;
                    }
                }
            }
            analyzeChanges(currentProgram);
            if (reopened == before) {
                break;
            }
        }
        println("re-disassembled after " + reopened + " Glide call sites");

        // The callers' bodies were cut at their first Glide call; regrow them.
        int regrown = 0;
        for (int pass = 0; pass < 8; pass++) {
            int orphans = 0;
            for (int i = 0; i < n; i++) {
                for (Reference r : getReferencesTo(first.add(5L * i))) {
                    Address from = r.getFromAddress();
                    if (!r.getReferenceType().isCall() || getFunctionContaining(from) != null) {
                        continue;
                    }
                    orphans++;
                    Function owner = getFunctionBefore(from);
                    if (owner != null && CreateFunctionCmd.fixupFunctionBody(
                            currentProgram, owner, monitor)
                            && owner.getBody().contains(from)) {
                        regrown++;
                        continue;
                    }
                    // Not reachable from the previous function: a routine
                    // only reached through a pointer. Create it at its entry.
                    CodeBlock sub = new IsolatedEntrySubModel(currentProgram)
                        .getFirstCodeBlockContaining(from, monitor);
                    if (sub != null && getFunctionAt(sub.getFirstStartAddress()) == null
                            && createFunction(sub.getFirstStartAddress(), null) != null) {
                        regrown++;
                    }
                }
            }
            if (orphans == 0) {
                break;
            }
        }
        println("regrew or created " + regrown + " caller functions");

        List<String> used = new ArrayList<>();
        int typed = 0;
        for (int i = 0; i < n; i++) {
            Address t = first.add(5L * i);
            String decorated = names.get(i);
            FunctionDefinition def = protos.get(key(decorated));

            if (getInstructionAt(t) == null) {
                disassemble(t);
            }
            Function fn = getFunctionAt(t);
            if (fn == null) {
                fn = createFunction(t, null);
            }
            if (fn == null) {
                println("could not create function at " + t + " for " + decorated);
                continue;
            }
            // Auto-analysis makes each "call __loadme" a thunk of __loadme, and a
            // thunk shares its target's signature: every prototype would land on
            // __loadme. Unlink first.
            if (fn.isThunk()) {
                fn.setThunkedFunction(null);
            }
            fn.setBody(new AddressSet(t, t.add(4)));
            if (def != null) {
                rename(fn, def.getName());
                new ApplyFunctionSignatureCmd(t, def, SourceType.USER_DEFINED)
                    .applyTo(currentProgram, monitor);
                typed++;
            }
            else {
                rename(fn, decorated.substring(1).replace('@', '_'));
                println("no prototype for " + decorated);
            }
            fn.setCallingConvention("__stdcall");
            // __stdcall callee pops exactly the @N argument bytes.
            fn.setStackPurgeSize(Integer.parseInt(decorated.substring(decorated.indexOf('@') + 1)));
            fn.setComment("Glide 2 import thunk for " + decorated
                + " (glimport.asm); patched to glide2x.ovl by __loadme on first call.");

            int calls = 0;
            for (Reference r : getReferencesTo(t)) {
                Address from = r.getFromAddress();
                if (r.getReferenceType().isCall()
                        && (from.compareTo(first) < 0 || from.compareTo(end) >= 0)) {
                    calls++;
                }
            }
            if (calls > 0) {
                used.add(String.format("%4d  %s", calls, fn.getName()));
            }
        }

        println("typed " + typed + "/" + n + " thunks; " + used.size() + " are called:");
        used.sort(null);
        for (String u : used) {
            println(u);
        }
    }
}
