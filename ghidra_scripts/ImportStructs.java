/* Import the checked-in C layouts from re/structs into the current program.
 *
 * C headers are the source of truth; the temporary .gdt is generated under
 * out/ghidra and is never checked in. DREAMS.DAT records are external decoded
 * buffers, so their types are added to the Data Type Manager but not applied
 * to unrelated executable addresses. The observed player pointer is typed
 * when its BSS address is present in the loaded program.
 *
 * @category Dreams
 */

import java.io.File;
import java.util.Iterator;

import ghidra.app.script.GhidraScript;
import ghidra.app.util.cparser.C.CParserUtils;
import ghidra.program.model.data.DataType;
import ghidra.program.model.data.DataTypeConflictHandler;
import ghidra.program.model.data.DataTypeManager;
import ghidra.program.model.data.FileDataTypeManager;
import ghidra.program.model.data.PointerDataType;
import ghidra.program.model.data.Structure;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SourceType;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.MemoryBlock;

public class ImportStructs extends GhidraScript {

    private String repoRoot() {
        String env = System.getenv("DREAMS_REPO");
        if (env != null && !env.isEmpty()) {
            return env;
        }
        return new File(sourceFile.getAbsolutePath()).getParentFile().getParent();
    }

    private DataType findType(DataTypeManager manager, String name) {
        Iterator<DataType> it = manager.getAllDataTypes();
        while (it.hasNext()) {
            DataType dt = it.next();
            if (name.equals(dt.getName())) {
                return dt;
            }
        }
        return null;
    }

    @Override
    public void run() throws Exception {
        String programName = currentProgram.getName();
        if (!programName.equalsIgnoreCase("WINDREAM.EXE")
                && !programName.equalsIgnoreCase("GDIDREAM.EXE")) {
            println("skipping game layouts for " + programName);
            return;
        }

        File root = new File(repoRoot());
        File header = new File(root, "re/structs/windream.h");
        if (!header.isFile()) {
            println("no struct header at " + header.getAbsolutePath());
            return;
        }

        File tempDir = new File(root, "out/ghidra");
        if (!tempDir.isDirectory() && !tempDir.mkdirs()) {
            throw new IllegalStateException("could not create " + tempDir);
        }
        File archive = new File(tempDir, "windream-structs.gdt");
        File lock = new File(archive.getAbsolutePath() + ".ulock");
        if (archive.exists() && !archive.delete()) {
            throw new IllegalStateException("could not replace " + archive);
        }
        if (lock.exists() && !lock.delete()) {
            throw new IllegalStateException("could not remove " + lock);
        }

        FileDataTypeManager parsed = CParserUtils.parseHeaderFiles(
            null,
            new String[] { header.getAbsolutePath() },
            new String[0],
            new String[0],
            archive.getAbsolutePath(),
            currentProgram.getLanguageID().getIdAsString(),
            "windows",
            monitor);

        DataTypeManager target = currentProgram.getDataTypeManager();
        int imported = 0;
        int structures = 0;
        try {
            Iterator<DataType> it = parsed.getAllDataTypes();
            while (it.hasNext() && !monitor.isCancelled()) {
                DataType sourceType = it.next();
                target.addDataType(sourceType, DataTypeConflictHandler.REPLACE_HANDLER);
                imported++;
                if (sourceType instanceof Structure) {
                    structures++;
                    println("imported " + sourceType.getPathName()
                        + " size=0x" + Integer.toHexString(sourceType.getLength()));
                }
            }
        }
        finally {
            parsed.close();
        }

        println("imported " + structures + " structures and " + imported
            + " total C data types from " + header.getAbsolutePath());

        DataType actor = findType(target, "RuntimeActorObserved");
        if (actor == null) {
            println("RuntimeActorObserved not found; player pointer left unchanged");
            return;
        }

        Address playerPointer = toAddr(0x004fbb48L);
        MemoryBlock block = currentProgram.getMemory().getBlock(playerPointer);
        if (block == null) {
            println("player actor pointer 004fbb48 is outside loaded memory; type saved in Data Type Manager only");
            return;
        }

        Listing listing = currentProgram.getListing();
        Data existing = listing.getDataAt(playerPointer);
        PointerDataType pointerType = new PointerDataType(actor,
            currentProgram.getDefaultPointerSize(), target);
        if (existing == null) {
            listing.createData(playerPointer, pointerType);
            Symbol symbol = currentProgram.getSymbolTable().getPrimarySymbol(playerPointer);
            if (symbol != null && symbol.getName().startsWith("DAT_")) {
                symbol.setName("g_PlayerActor", SourceType.USER_DEFINED);
            }
            println("typed 004fbb48 as pointer to RuntimeActorObserved");
        }
        else if (existing.getDataType().isEquivalent(pointerType)) {
            println("004fbb48 already has the observed actor pointer type");
        }
        else if (existing.getLength() == pointerType.getLength()
                && existing.getDataType().getName().startsWith("undefined")) {
            listing.clearCodeUnits(playerPointer,
                playerPointer.add(pointerType.getLength() - 1), false);
            listing.createData(playerPointer, pointerType);
            println("retyped undefined data at 004fbb48 as pointer to RuntimeActorObserved");
        }
        else {
            println("004fbb48 already contains " + existing.getDataType().getPathName()
                + "; preserved existing data and left the actor pointer untyped");
        }
    }
}
