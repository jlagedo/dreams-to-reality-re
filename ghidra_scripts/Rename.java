/* Rename functions from the command line.
 *
 * Usage (headless, without -readOnly so the change is saved):
 *   analyzeHeadless <proj> <name> -process WINDREAM.EXE -noanalysis \
 *       -scriptPath <repo>\ghidra_scripts -postScript Rename.java \
 *       0043a306:CTRL_Dispatcher 0042493b:Input_PostEvents
 *
 * Each argument is <entry address in hex>:<new name>. Not "=": the .bat
 * launcher splits arguments on it. A missing function is reported and
 * skipped. Run tools\re-checkpoint.ps1 afterwards to persist the names to
 * re/symbols/.
 *
 * @category Dreams
 */

import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.SourceType;

public class Rename extends GhidraScript {

    @Override
    public void run() throws Exception {
        for (String arg : getScriptArgs()) {
            int eq = arg.indexOf(':');
            if (eq < 0) {
                println("skip (expected addr:name): " + arg);
                continue;
            }
            Function fn = getFunctionAt(toAddr(arg.substring(0, eq)));
            if (fn == null) {
                println("no function at " + arg.substring(0, eq));
                continue;
            }
            String old = fn.getName();
            fn.setName(arg.substring(eq + 1), SourceType.USER_DEFINED);
            println(fn.getEntryPoint() + "  " + old + " -> " + fn.getName());
        }
    }
}
