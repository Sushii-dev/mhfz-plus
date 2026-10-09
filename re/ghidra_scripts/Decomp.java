// Headless: decompiles the functions containing the given addresses into a text file.
//   analyzeHeadless <proj dir> mhfo -process mhfo-hd.dll -noanalysis -readOnly -scriptPath re\ghidra_scripts
//                   -postScript Decomp.java <out.txt> <addr> [<addr> ...]
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.address.*;
import java.io.*;

public class Decomp extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        DecompInterface d = new DecompInterface();
        d.openProgram(currentProgram);
        try (PrintWriter w = new PrintWriter(new FileWriter(a[0]))) {
            for (int i = 1; i < a.length; i++) {
                Address at = toAddr(a[i]);
                Function f = getFunctionContaining(at);
                if (f == null) { w.println("// " + a[i] + ": no function"); continue; }
                DecompileResults r = d.decompileFunction(f, 120, monitor);
                w.println("// ===== " + a[i] + " in " + f.getName() + " @ " + f.getEntryPoint());
                w.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "// failed: " + r.getErrorMessage());
            }
        }
    }
}
