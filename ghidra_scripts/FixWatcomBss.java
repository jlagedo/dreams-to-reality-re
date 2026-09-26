/* Extend a truncated .bss to the next section (Watcom PE images).
 *
 * The Watcom linker writes VirtualSize = 0 for every section of WINDREAM.EXE
 * and GDIDREAM.EXE, and Ghidra then maps .bss short: 0x4c7000-0x59a1ff,
 * although the section runs to .reloc at 0x6b2000 and the game keeps globals
 * up to ~0x6726f8 (the window handle, DirectSound buffers, the frame
 * pointer). This maps the gap after each uninitialized block, up to the next
 * block, as uninitialized memory and joins it. It changes nothing when blocks
 * are already contiguous, so it is safe as a -preScript on every import.
 *
 * Usage (headless, without -readOnly):
 *   analyzeHeadless ghidra dreams -process WINDREAM.EXE -noanalysis \
 *       -scriptPath ghidra_scripts -postScript FixWatcomBss.java
 *
 * @category Dreams
 */

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;

public class FixWatcomBss extends GhidraScript {

    @Override
    public void run() throws Exception {
        Memory mem = currentProgram.getMemory();
        MemoryBlock[] blocks = mem.getBlocks();
        int fixed = 0;
        for (int i = 0; i + 1 < blocks.length; i++) {
            MemoryBlock b = blocks[i];
            if (b.isInitialized() || !b.getName().equalsIgnoreCase(".bss")) {
                continue;
            }
            Address gapStart = b.getEnd().add(1);
            Address next = blocks[i + 1].getStart();
            long gap = next.subtract(gapStart);
            if (gap <= 0) {
                continue;
            }
            MemoryBlock tail = mem.createUninitializedBlock(b.getName() + "_tail", gapStart,
                gap, false);
            tail.setRead(b.isRead());
            tail.setWrite(b.isWrite());
            MemoryBlock joined = mem.join(b, tail);
            println("extended " + joined.getName() + " to " + joined.getStart() + "-"
                + joined.getEnd() + " (+0x" + Long.toHexString(gap) + ")");
            fixed++;
        }
        if (fixed == 0) {
            println("no truncated .bss found");
        }
    }
}
