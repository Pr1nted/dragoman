// Does this library put its hexes exactly where Unciv's own rectangular
// TileMap constructor would? That constructor is the definition of a valid
// grid -- TileMap.setTransients asserts a property only it satisfies -- so
// comparing against it is stronger than re-deriving the arithmetic.
//
//   javac -cp Unciv.jar -d out tools/unciv/CompareGrid.java
//   java  -cp out:Unciv.jar CompareGrid map.json
import com.badlogic.gdx.Gdx;
import com.badlogic.gdx.backends.headless.HeadlessFiles;
import com.unciv.logic.map.TileMap;
import com.unciv.models.ruleset.Ruleset;
import com.unciv.models.ruleset.RulesetCache;

import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.HashSet;
import java.util.Set;

public class CompareGrid {
    public static void main(String[] args) throws Exception {
        Gdx.files = new HeadlessFiles();
        RulesetCache.INSTANCE.loadRulesets(true, true);
        Ruleset ruleset = null;
        for (String k : RulesetCache.INSTANCE.keySet()) {
            Ruleset r = RulesetCache.INSTANCE.get(k);
            if (r != null && r.getTerrains().size() > 0) { ruleset = r; break; }
        }
        if (ruleset == null) { System.err.println("no ruleset"); System.exit(2); }

        String text = new String(Files.readAllBytes(Paths.get(args[0])), "UTF-8");
        TileMap mine = com.unciv.logic.files.MapSaver.INSTANCE.mapFromSavedString(text);
        int w = mine.getMapParameters().getMapSize().getWidth();
        int h = mine.getMapParameters().getMapSize().getHeight();
        System.out.println("declared " + w + "x" + h + ", " + mine.getValues().size() + " tiles");

        TileMap theirs = new TileMap(w, h, ruleset, true);
        Set<String> a = new HashSet<>(), b = new HashSet<>();
        for (Object o : mine.getValues())
            a.add(((com.unciv.logic.map.tile.Tile) o).getPosition().toString());
        for (Object o : theirs.getValues())
            b.add(((com.unciv.logic.map.tile.Tile) o).getPosition().toString());

        Set<String> onlyMine = new HashSet<>(a); onlyMine.removeAll(b);
        Set<String> onlyTheirs = new HashSet<>(b); onlyTheirs.removeAll(a);
        System.out.println("mine   " + a.size() + " positions");
        System.out.println("unciv  " + b.size() + " positions");
        System.out.println("only in mine:  " + onlyMine.size());
        System.out.println("only in unciv: " + onlyTheirs.size());
        boolean ok = onlyMine.isEmpty() && onlyTheirs.isEmpty();
        System.out.println(ok ? "IDENTICAL GRID" : "GRIDS DIFFER");
        System.exit(ok ? 0 : 1);
    }
}
