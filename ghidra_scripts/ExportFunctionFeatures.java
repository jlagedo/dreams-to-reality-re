/* Export per-function features for cross-build matching.
 *
 * The four game executables are one Watcom engine compiled several ways, so a
 * function in DREAMSFX.EXE usually has a twin in WINDREAM.EXE at a different
 * address, often with different register allocation. This dumps what survives
 * recompilation; tools/match_functions.py pairs the functions.
 *
 * Usage (headless, read-only):
 *   analyzeHeadless ghidra dreams -process WINDREAM.EXE -noanalysis -readOnly \
 *       -scriptPath ghidra_scripts -postScript ExportFunctionFeatures.java
 *
 * Output: out/ghidra/features/<program>.json, one object per function:
 *   entry, name, named (user-assigned), size, ins (instruction count),
 *   mnem   mnemonic sequence
 *   masked instruction text with values >= 0x10000 (addresses) masked
 *   consts scalar operands below 0x10000 (immediates, struct offsets)
 *   strings referenced string literals
 *   calls  direct callee entries in call-site order
 *   icalls indirect call count
 *
 * @category Dreams
 */

import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStreamWriter;
import java.io.Writer;
import java.util.ArrayList;
import java.util.List;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import com.google.gson.Gson;
import com.google.gson.GsonBuilder;
import com.google.gson.JsonArray;
import com.google.gson.JsonObject;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.InstructionIterator;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.scalar.Scalar;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.SourceType;

public class ExportFunctionFeatures extends GhidraScript {

    private static final Pattern HEX = Pattern.compile("0x[0-9a-fA-F]+");
    private static final long ADDRESS_FLOOR = 0x10000L;

    private String repoRoot() {
        String env = System.getenv("DREAMS_REPO");
        if (env != null && !env.isEmpty()) {
            return env;
        }
        return new File(sourceFile.getAbsolutePath()).getParentFile().getParent();
    }

    private static String mask(String text) {
        Matcher m = HEX.matcher(text);
        StringBuffer sb = new StringBuffer();
        while (m.find()) {
            long v = Long.parseUnsignedLong(m.group().substring(2), 16);
            m.appendReplacement(sb, v >= ADDRESS_FLOOR ? "A" : m.group());
        }
        m.appendTail(sb);
        return sb.toString();
    }

    /* A printable C string at addr, or null. */
    private String stringAt(Address addr) {
        Data d = getDataAt(addr);
        if (d != null && d.hasStringValue()) {
            Object v = d.getValue();
            return v == null ? null : v.toString();
        }
        Memory mem = currentProgram.getMemory();
        StringBuilder sb = new StringBuilder();
        try {
            for (int i = 0; i < 200; i++) {
                int b = mem.getByte(addr.add(i)) & 0xff;
                if (b == 0) {
                    break;
                }
                if (b < 0x20 || b > 0x7e) {
                    return null;
                }
                sb.append((char) b);
            }
        }
        catch (Exception e) {
            return null;
        }
        return sb.length() >= 4 ? sb.toString() : null;
    }

    @Override
    public void run() throws Exception {
        JsonArray out = new JsonArray();
        int count = 0;
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            if (monitor.isCancelled()) {
                break;
            }
            if (f.isExternal() || f.isThunk()) {
                continue;
            }
            JsonObject o = new JsonObject();
            o.addProperty("entry", f.getEntryPoint().toString());
            o.addProperty("name", f.getName());
            o.addProperty("named", f.getSymbol().getSource() == SourceType.USER_DEFINED
                || f.getSymbol().getSource() == SourceType.IMPORTED);
            o.addProperty("size", f.getBody().getNumAddresses());

            JsonArray mnem = new JsonArray();
            JsonArray masked = new JsonArray();
            JsonArray consts = new JsonArray();
            JsonArray strings = new JsonArray();
            JsonArray calls = new JsonArray();
            List<String> seenStrings = new ArrayList<>();
            int icalls = 0;
            int ins = 0;

            InstructionIterator it = currentProgram.getListing().getInstructions(f.getBody(), true);
            while (it.hasNext()) {
                Instruction i = it.next();
                ins++;
                mnem.add(i.getMnemonicString());
                masked.add(mask(i.toString()));
                for (int op = 0; op < i.getNumOperands(); op++) {
                    for (Object obj : i.getOpObjects(op)) {
                        if (obj instanceof Scalar) {
                            long v = ((Scalar) obj).getUnsignedValue();
                            if (v < ADDRESS_FLOOR) {
                                consts.add(v);
                            }
                        }
                    }
                }
                boolean isCall = i.getFlowType().isCall();
                boolean direct = false;
                for (Reference r : i.getReferencesFrom()) {
                    if (isCall && r.getReferenceType().isCall()) {
                        Function callee = getFunctionAt(r.getToAddress());
                        if (callee != null) {
                            calls.add(callee.getEntryPoint().toString());
                            direct = true;
                        }
                    }
                    else if (r.getReferenceType().isData() || r.getReferenceType().isRead()) {
                        String s = stringAt(r.getToAddress());
                        if (s != null && !seenStrings.contains(s)) {
                            seenStrings.add(s);
                            strings.add(s);
                        }
                    }
                }
                if (isCall && !direct) {
                    icalls++;
                }
            }
            o.addProperty("ins", ins);
            o.add("mnem", mnem);
            o.add("masked", masked);
            o.add("consts", consts);
            o.add("strings", strings);
            o.add("calls", calls);
            o.addProperty("icalls", icalls);
            out.add(o);
            count++;
        }

        File dir = new File(repoRoot(), "out/ghidra/features");
        if (!dir.isDirectory() && !dir.mkdirs()) {
            throw new IllegalStateException("could not create " + dir);
        }
        File file = new File(dir, currentProgram.getName() + ".json");
        Gson gson = new GsonBuilder().create();
        try (Writer w = new OutputStreamWriter(new FileOutputStream(file), "UTF-8")) {
            gson.toJson(out, w);
        }
        println("exported " + count + " functions to " + file);
    }
}
