/* Parse C headers into the program and type named globals.
 *
 * Usage (headless, without -readOnly):
 *   analyzeHeadless ghidra dreams -process WINDREAM.EXE -noanalysis \
 *       -scriptPath ghidra_scripts -postScript ApplyTypes.java \
 *       re/structs/directx.h re/structs/windream-globals.tsv
 *
 * Arguments are repo-relative or absolute paths. Every .h is parsed (together,
 * in order) into a temporary archive under out/ghidra and its types are added
 * to the program's Data Type Manager. Every .tsv lists globals, one per line:
 *   <address> <type> <name> [<comment>]
 * where <type> is a type name, optionally followed by "*" or "[n]". The data
 * is created at the address (undefined bytes are cleared; existing typed data
 * is left alone and reported), labelled, and the comment set as EOL comment.
 * Labels are not exported by ExportSymbols.java, so the .tsv is the record:
 * re-run this script on a fresh project.
 *
 * @category Dreams
 */

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.Iterator;
import java.util.List;

import ghidra.app.script.GhidraScript;
import ghidra.app.util.cparser.C.CParserUtils;
import ghidra.program.model.address.Address;
import ghidra.program.model.data.ArrayDataType;
import ghidra.program.model.data.DataType;
import ghidra.program.model.data.DataTypeConflictHandler;
import ghidra.program.model.data.DataTypeManager;
import ghidra.program.model.data.FileDataTypeManager;
import ghidra.program.model.data.PointerDataType;
import ghidra.program.model.listing.CodeUnit;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.symbol.SourceType;

public class ApplyTypes extends GhidraScript {

    private File root;

    private File resolve(String path) {
        File f = new File(path);
        return f.isAbsolute() ? f : new File(root, path);
    }

    private DataType find(DataTypeManager dtm, String name, List<String> preferred) {
        DataType fallback = null;
        Iterator<DataType> it = dtm.getAllDataTypes();
        while (it.hasNext()) {
            DataType dt = it.next();
            if (!name.equals(dt.getName())) {
                continue;
            }
            for (String category : preferred) {
                if (dt.getCategoryPath().getPath().startsWith("/" + category)) {
                    return dt;
                }
            }
            fallback = dt;
        }
        return fallback;
    }

    private DataType parseType(DataTypeManager dtm, String spec, List<String> preferred) {
        String s = spec.trim();
        int count = -1;
        if (s.endsWith("]")) {
            count = Integer.parseInt(s.substring(s.indexOf('[') + 1, s.length() - 1));
            s = s.substring(0, s.indexOf('[')).trim();
        }
        int pointers = 0;
        while (s.endsWith("*")) {
            pointers++;
            s = s.substring(0, s.length() - 1).trim();
        }
        DataType dt = find(dtm, s, preferred);
        if (dt == null) {
            return null;
        }
        for (int i = 0; i < pointers; i++) {
            dt = new PointerDataType(dt, currentProgram.getDefaultPointerSize(), dtm);
        }
        if (count > 0) {
            dt = new ArrayDataType(dt, count, dt.getLength(), dtm);
        }
        return dt;
    }

    @Override
    public void run() throws Exception {
        String env = System.getenv("DREAMS_REPO");
        root = env != null && !env.isEmpty() ? new File(env)
            : new File(sourceFile.getAbsolutePath()).getParentFile().getParentFile();

        List<String> headers = new ArrayList<>();
        List<File> tables = new ArrayList<>();
        List<String> categories = new ArrayList<>();
        for (String arg : getScriptArgs()) {
            File f = resolve(arg);
            if (arg.endsWith(".h")) {
                headers.add(f.getAbsolutePath());
                categories.add(f.getName());
            }
            else {
                tables.add(f);
            }
        }

        DataTypeManager target = currentProgram.getDataTypeManager();
        if (!headers.isEmpty()) {
            File dir = new File(root, "out/ghidra");
            dir.mkdirs();
            File archive = new File(dir, currentProgram.getName() + "-types.gdt");
            new File(archive.getAbsolutePath() + ".ulock").delete();
            if (archive.exists() && !archive.delete()) {
                throw new IllegalStateException("could not replace " + archive);
            }
            FileDataTypeManager parsed = CParserUtils.parseHeaderFiles(null,
                headers.toArray(new String[0]), new String[0], new String[0],
                archive.getAbsolutePath(), currentProgram.getLanguageID().getIdAsString(),
                currentProgram.getCompilerSpec().getCompilerSpecID().getIdAsString(), monitor);
            int n = 0;
            try {
                Iterator<DataType> it = parsed.getAllDataTypes();
                while (it.hasNext()) {
                    target.addDataType(it.next(), DataTypeConflictHandler.REPLACE_HANDLER);
                    n++;
                }
            }
            finally {
                parsed.close();
            }
            println("imported " + n + " data types from " + headers);
        }

        Listing listing = currentProgram.getListing();
        for (File table : tables) {
            for (String line : Files.readAllLines(table.toPath(), StandardCharsets.UTF_8)) {
                if (line.isBlank() || line.startsWith("#")) {
                    continue;
                }
                String[] f = line.split("\t", 4);
                Address addr = toAddr(f[0]);
                DataType dt = parseType(target, f[1], categories);
                if (dt == null) {
                    println("unknown type " + f[1] + " for " + f[0]);
                    continue;
                }
                Data existing = listing.getDataAt(addr);
                boolean undefined = existing == null
                    || existing.getDataType().getName().startsWith("undefined")
                    || existing.getDataType().isEquivalent(dt);
                if (!undefined) {
                    println("kept " + existing.getDataType().getName() + " at " + addr
                        + " (wanted " + f[1] + ")");
                }
                else {
                    listing.clearCodeUnits(addr, addr.add(dt.getLength() - 1), false);
                    listing.createData(addr, dt);
                }
                createLabel(addr, f[2], true, SourceType.USER_DEFINED);
                if (f.length > 3 && !f[3].isEmpty()) {
                    listing.setComment(addr, CodeUnit.EOL_COMMENT, f[3]);
                }
                println(addr + "  " + f[1] + "  " + f[2]);
            }
        }
    }
}
