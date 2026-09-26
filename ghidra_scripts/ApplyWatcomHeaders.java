/* Parse the Watcom 10.6 C headers and type the named runtime functions.
 *
 * Run after ApplyWatcomSigs.java has named the runtime (printf_, int386x_,
 * _getvideoconfig_, ...). The headers come from <DREAMS_WATCOM>\10.6-cd\H and
 * stay outside the repo; cleaned copies are written to out/ghidra/watcom-h:
 *   - #pragma aux / intrinsic / library lines are dropped (Watcom register
 *     specs such as "FP_SEG = __parm __caller [eax dx]" break Ghidra's parser);
 *   - pack(__push,1) / pack(__pop) become pack(push,1) / pack(pop);
 *   - __far, __near, __huge, __interrupt, __loadds, ... and __based(...) are
 *     removed, __segment becomes unsigned short (flat 32-bit model).
 *
 * Usage (headless, without -readOnly):
 *   analyzeHeadless ghidra dreams -process DREAMSFX.EXE -noanalysis \
 *       -scriptPath ghidra_scripts -postScript ApplyWatcomHeaders.java \
 *       <DREAMS_WATCOM>\10.6-cd\H
 *
 * LE programs parse as __DOS__ (with graph.h); PE programs as __NT__.
 * A runtime function "name_" (Watcom's register-convention decoration) gets
 * the prototype of "name": __watcall, or __cdecl if it is variadic (Watcom
 * passes those on the stack, caller pops). Functions with float or double
 * parameters or results are skipped: the 387 math library passes them in
 * ways neither model describes. Names are kept.
 *
 * @category Dreams
 */

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Map;

import ghidra.app.cmd.function.ApplyFunctionSignatureCmd;
import ghidra.app.cmd.function.FunctionRenameOption;
import ghidra.app.script.GhidraScript;
import ghidra.app.util.cparser.C.CParserUtils;
import ghidra.program.model.data.AbstractFloatDataType;
import ghidra.program.model.data.DataType;
import ghidra.program.model.data.DataTypeConflictHandler;
import ghidra.program.model.data.DataTypeManager;
import ghidra.program.model.data.FileDataTypeManager;
import ghidra.program.model.data.FunctionDefinition;
import ghidra.program.model.data.ParameterDefinition;
import ghidra.program.model.data.TypeDef;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionSignature;
import ghidra.program.model.symbol.SourceType;


public class ApplyWatcomHeaders extends GhidraScript {

    private static final String[] COMMON = {
        "stdio.h", "stdlib.h", "string.h", "malloc.h", "math.h", "time.h", "io.h", "fcntl.h",
        "dos.h", "i86.h", "conio.h", "direct.h", "process.h", "ctype.h", "setjmp.h", "signal.h",
    };

    static String clean(String src) {
        StringBuilder out = new StringBuilder();
        boolean cont = false;
        for (String line : src.split("\r?\n", -1)) {
            String t = line.trim();
            boolean drop = cont || t.matches("#\\s*pragma\\s+(aux|intrinsic|library|function"
                + "|disable_message|enable_message|warning|off|on|read_only_file|inline_depth"
                + "|inline_recursion|code_seg|data_seg|comment|message)\\b.*");
            cont = drop && t.endsWith("\\");
            if (drop) {
                out.append('\n');   // keep line numbers
                continue;
            }
            String l = line.replace("pack(__push,1)", "pack(push,1)")
                .replace("pack(__push,4)", "pack(push,4)").replace("pack(__pop)", "pack(pop)");
            l = l.replaceAll("\\b__based\\s*\\([^)]*\\)", "");
            l = l.replaceAll("\\b__segment\\b", "unsigned short");
            l = l.replaceAll("\\b__(far|near|huge|interrupt|loadds|saveregs|export|pascal"
                + "|fortran|syscall|self)\\b", "");
            out.append(l).append('\n');
        }
        return out.toString();
    }

    private static boolean isFloat(DataType dt) {
        while (dt instanceof TypeDef) {
            dt = ((TypeDef) dt).getBaseDataType();
        }
        return dt instanceof AbstractFloatDataType;
    }

    private String repoRoot() {
        String env = System.getenv("DREAMS_REPO");
        return env != null && !env.isEmpty() ? env
            : new File(sourceFile.getAbsolutePath()).getParentFile().getParent();
    }

    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        if (a.length != 1) {
            throw new IllegalArgumentException("usage: ApplyWatcomHeaders.java <watcom H dir>");
        }
        boolean dos = currentProgram.getMemory().getBlock(".image") != null
            || !currentProgram.getExecutableFormat().contains("Portable");

        Path src = Path.of(a[0]);
        Path dst = Path.of(repoRoot(), "out", "ghidra", "watcom-h");
        Files.createDirectories(dst);
        try (var files = Files.list(src)) {
            for (Path p : (Iterable<Path>) files::iterator) {
                if (Files.isRegularFile(p)) {
                    Files.writeString(dst.resolve(p.getFileName().toString().toLowerCase()),
                        clean(Files.readString(p, StandardCharsets.ISO_8859_1)),
                        StandardCharsets.ISO_8859_1);
                }
            }
        }

        List<String> headers = new ArrayList<>();
        for (String h : COMMON) {
            headers.add(dst.resolve(h).toString());
        }
        if (dos) {
            headers.add(dst.resolve("graph.h").toString());
        }
        String[] defines = { "-D__386__=1", "-D__FLAT__=1", "-D__WATCOMC__=1060",
            "-D_M_IX86=500", "-D__X86__=1", dos ? "-D__DOS__=1" : "-D__NT__=1" };

        File archive = new File(repoRoot(), "out/ghidra/" + currentProgram.getName() + "-watcom.gdt");
        new File(archive.getAbsolutePath() + ".ulock").delete();
        if (archive.exists() && !archive.delete()) {
            throw new IllegalStateException("could not replace " + archive);
        }
        FileDataTypeManager parsed = CParserUtils.parseHeaderFiles(null,
            headers.toArray(new String[0]), new String[] { dst.toString() }, defines,
            archive.getAbsolutePath(), currentProgram.getLanguageID().getIdAsString(),
            currentProgram.getCompilerSpec().getCompilerSpecID().getIdAsString(), monitor);

        DataTypeManager target = currentProgram.getDataTypeManager();
        Map<String, FunctionDefinition> protos = new HashMap<>();
        int types = 0;
        try {
            Iterator<DataType> it = parsed.getAllDataTypes();
            while (it.hasNext()) {
                DataType added = target.addDataType(it.next(), DataTypeConflictHandler.REPLACE_HANDLER);
                types++;
                if (added instanceof FunctionDefinition) {
                    protos.put(added.getName(), (FunctionDefinition) added);
                }
            }
        }
        finally {
            parsed.close();
        }
        println("parsed " + headers.size() + " headers (" + (dos ? "DOS" : "NT") + "): "
            + types + " types, " + protos.size() + " prototypes");

        int applied = 0, variadic = 0, floats = 0;
        for (Function fn : currentProgram.getFunctionManager().getFunctions(true)) {
            String name = fn.getName();
            if (!name.endsWith("_") || name.length() < 2) {
                continue;
            }
            FunctionDefinition def = protos.get(name.substring(0, name.length() - 1));
            if (def == null) {
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
            new ApplyFunctionSignatureCmd(fn.getEntryPoint(), (FunctionSignature) def,
                SourceType.IMPORTED, false, FunctionRenameOption.NO_CHANGE)
                .applyTo(currentProgram, monitor);
            if (def.hasVarArgs()) {
                fn.setCallingConvention("__cdecl");
                variadic++;
            }
            else {
                fn.setCallingConvention("__watcall");
            }
            applied++;
        }
        println("typed " + applied + " runtime functions (" + variadic + " variadic as __cdecl); "
            + "skipped " + floats + " with float/double");
    }
}
