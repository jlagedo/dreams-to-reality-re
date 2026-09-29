/* Verify imported structure sizes and named member offsets against JSON evidence.
 * Usage: CheckTypeLayouts.java re/reviews/wip-type-layouts.json
 * @category Dreams
 */
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Iterator;
import com.google.gson.JsonElement;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.data.DataType;
import ghidra.program.model.data.DataTypeComponent;
import ghidra.program.model.data.Structure;

public class CheckTypeLayouts extends GhidraScript {
    @Override
    public void run() throws Exception {
        int structures = 0, members = 0;
        for (JsonElement element : JsonParser.parseString(
                Files.readString(Path.of(getScriptArgs()[0]))).getAsJsonArray()) {
            JsonObject expected = element.getAsJsonObject();
            String name = expected.get("name").getAsString();
            Structure found = null;
            Iterator<DataType> it = currentProgram.getDataTypeManager().getAllDataTypes();
            while (it.hasNext()) {
                DataType type = it.next();
                if (type instanceof Structure && type.getName().equals(name)) found = (Structure)type;
            }
            if (found == null || found.getLength() != expected.get("size").getAsInt()) {
                throw new IllegalStateException("Missing or wrong-sized structure " + name);
            }
            for (JsonElement item : expected.getAsJsonArray("fields")) {
                JsonObject field = item.getAsJsonObject();
                DataTypeComponent component = found.getComponentAt(field.get("offset").getAsInt());
                if (component == null
                        || component.getOffset() != field.get("offset").getAsInt()
                        || component.getLength() != field.get("size").getAsInt()
                        || !field.get("name").getAsString().equals(component.getFieldName())) {
                    throw new IllegalStateException("Member differs: " + name + "."
                        + field.get("name").getAsString());
                }
                members++;
            }
            structures++;
        }
        println("Layout checks passed: " + structures + " structures, " + members + " members");
    }
}
