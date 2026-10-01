/* Apply function prototypes proven by matching decompilation.
 *
 * re/prototypes/<program>.tsv lists functions whose C, compiled with the
 * game's own compiler (Watcom 11.0 for the Windows builds), is byte-identical
 * to retail (docs/specs/000-the-recomp/spec.md, W3). Such a match fixes the
 * parameter count and registers, each parameter's width and signedness, and
 * the return type, so the prototype is applied as USER_DEFINED under
 * __watcall (__cdecl when variadic); ApplyWatcall.java leaves USER_DEFINED
 * signatures alone. Names are not touched: a match proves what the code does,
 * not what it is called.
 *
 * Columns: address, prototype, flags, evidence. The prototype's function
 * name is ignored. Prototypes with float or double parameters or returns are
 * skipped, as in ApplyWatcomHeaders.java (the __watcall model has no x87
 * return and no register pairs for doubles).
 *
 * Each typed function gets a [PROTO] paragraph in its plate comment, replaced
 * on every run, recording the flags and the evidence.
 *
 * Usage (headless; add -readOnly to preview):
 *   analyzeHeadless ghidra dreams -process WINDREAM.EXE -noanalysis \
 *       -scriptPath re/ghidra_scripts -postScript ApplyPrototypes.java \
 *       re\prototypes\WINDREAM.EXE.tsv
 *
 * @category Dreams
 */

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Paths;

import ghidra.app.cmd.function.ApplyFunctionSignatureCmd;
import ghidra.app.cmd.function.FunctionRenameOption;
import ghidra.app.script.GhidraScript;
import ghidra.app.services.DataTypeManagerService;
import ghidra.app.util.cparser.C.CParserUtils;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.DataType;
import ghidra.program.model.data.FunctionDefinitionDataType;
import ghidra.program.model.data.ParameterDefinition;
import ghidra.program.model.data.TypeDef;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;

public class ApplyPrototypes extends GhidraScript {

    private static final String TAG = "[PROTO]";

    private static boolean isFloat(DataType t) {
        while (t instanceof TypeDef td) {
            t = td.getBaseDataType();
        }
        String n = t.getName();
        return n.equals("float") || n.equals("double") || n.equals("longdouble");
    }

    private static String withoutTag(String comment) {
        if (comment == null || comment.isEmpty()) {
            return "";
        }
        StringBuilder kept = new StringBuilder();
        for (String para : comment.split("\n\n", -1)) {
            if (para.startsWith(TAG)) {
                continue;
            }
            if (kept.length() > 0) {
                kept.append("\n\n");
            }
            kept.append(para);
        }
        return kept.toString();
    }

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length == 0) {
            println("usage: ApplyPrototypes.java <re/prototypes/program.tsv>");
            return;
        }
        int applied = 0, floats = 0, failed = 0;
        for (String line : Files.readAllLines(Paths.get(args[0]), StandardCharsets.UTF_8)) {
            if (line.isBlank() || line.startsWith("#") || line.startsWith("address\t")) {
                continue;
            }
            String[] f = line.split("\t", -1);
            Address a = toAddr(f[0]);
            Function fn = getFunctionAt(a);
            if (fn == null) {
                println("FAILED " + f[0] + ": no function");
                failed++;
                continue;
            }
            FunctionDefinitionDataType def;
            try {
                def = CParserUtils.parseSignature((DataTypeManagerService) null, currentProgram,
                    f[1] + ";", false);
            }
            catch (Exception e) {
                println("FAILED " + f[0] + " " + fn.getName() + ": " + e.getMessage());
                failed++;
                continue;
            }
            if (def == null) {
                println("FAILED " + f[0] + " " + fn.getName() + ": prototype did not parse");
                failed++;
                continue;
            }
            boolean hasFloat = isFloat(def.getReturnType());
            for (ParameterDefinition p : def.getArguments()) {
                hasFloat |= isFloat(p.getDataType());
            }
            if (hasFloat) {
                floats++;
                continue;
            }
            // Watcom passes variadic arguments on the stack, caller-cleaned.
            String conv = def.hasVarArgs() ? "__cdecl" : "__watcall";
            def.setCallingConvention(conv);
            if (!new ApplyFunctionSignatureCmd(a, def, SourceType.USER_DEFINED, false,
                FunctionRenameOption.NO_CHANGE).applyTo(currentProgram, monitor)) {
                println("FAILED " + f[0] + " " + fn.getName() + ": signature not applied");
                failed++;
                continue;
            }
            fn.setCallingConvention(conv);
            String block = TAG + " Prototype proven by a byte-exact compile: Watcom 11.0 "
                + f[2] + ". " + f[3];
            String kept = withoutTag(getPlateComment(a));
            setPlateComment(a, kept.isEmpty() ? block : kept + "\n\n" + block);
            applied++;
        }
        println("prototypes: applied " + applied + ", skipped " + floats
            + " with float/double, " + failed + " failed");
    }
}
