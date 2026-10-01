// Validate a map this library wrote by asking UNCIV ITSELF, headlessly.
//
//   javac -cp Unciv.jar -d out tools/unciv/ValidateWithUnciv.java
//   java  -cp out:Unciv.jar ValidateWithUnciv map.json [map.json...]
//
// WHY THIS AND NOT tools/validate_unciv.py. That script reads the ruleset JSON
// and checks names against it, which catches a terrain Unciv does not have. This
// runs the game's own `TileMap.setTransients` -- the step it performs before a
// map is usable -- so it catches whatever that catches, including things this
// project does not know to look for.
//
// It needs two things the Python one does not: the jar, and the jar's `jsons/`
// extracted into the working directory. Gdx's HeadlessFiles reads `internal`
// paths from the working directory rather than from the classpath, so without
// that the rulesets load with ZERO terrains and every map "fails" with
// "Terrain Ocean does not exist in ruleset!" -- which looks exactly like a
// broken map and is not one.
import com.badlogic.gdx.Gdx;
import com.badlogic.gdx.backends.headless.HeadlessFiles;
import com.unciv.logic.files.MapSaver;
import com.unciv.logic.map.TileMap;
import com.unciv.models.ruleset.Ruleset;
import com.unciv.models.ruleset.RulesetCache;

import java.lang.reflect.Method;
import java.nio.file.Files;
import java.nio.file.Paths;

public class ValidateWithUnciv {
    public static void main(String[] args) throws Exception {
        if (args.length == 0) {
            System.err.println("usage: ValidateWithUnciv <map.json>...");
            System.exit(2);
        }

        Gdx.files = new HeadlessFiles();
        RulesetCache.INSTANCE.loadRulesets(true, true);

        /* GameStarter reaches GUI.clearUndoCheckpoints, which wants the game
         * singleton. Console mode is the game's own headless flag. */
        com.unciv.UncivGame.Current = new com.unciv.UncivGame(true);
        com.unciv.UncivGame.Current.settings = new com.unciv.models.metadata.GameSettings();

        Ruleset ruleset = null;
        for (String key : RulesetCache.INSTANCE.keySet()) {
            Ruleset r = RulesetCache.INSTANCE.get(key);
            if (r != null && r.getTerrains().size() > 0) { ruleset = r; break; }
        }
        if (ruleset == null) {
            // Not a map failure: the assets are not where Gdx is looking.
            System.err.println("no ruleset has any terrains -- extract the jar's jsons/ "
                               + "into the working directory first");
            System.exit(2);
        }
        System.out.println("ruleset: " + ruleset.getName()
                           + " (" + ruleset.getTerrains().size() + " terrains)");

        int failed = 0;
        for (String path : args) {
            System.out.println("=== " + path + " ===");
            try {
                String text = new String(Files.readAllBytes(Paths.get(path)), "UTF-8");
                TileMap map = MapSaver.INSTANCE.mapFromSavedString(text);
                System.out.println("  loaded          " + map.getValues().size() + " tiles");

                map.setRuleset(ruleset);
                map.setTransients(ruleset, true);
                System.out.println("  setTransients   ok");

                /* AND AGAIN. The first call builds the tileMatrix; every call
                 * after it asserts that matrix is the right size, which is only
                 * true when the hex columns are centred on the origin:
                 *     check(tileMatrix.size in (-2 * leftX)..(3 - 2 * leftX))
                 * A map editor loads a map with ONE call, so a grid numbered
                 * 0..width-1 looks perfectly valid here -- and GameStarter,
                 * which calls it a second time through GameInfo.setTransients,
                 * throws "called on existing tileMatrix of different size".
                 * Every map this project wrote before 2026-10-01 was in that
                 * state: loadable, unplayable, and passing this file. */
                map.setTransients(ruleset, true);
                System.out.println("  setTransients   ok twice (the game does)");

                int unknown = 0;
                for (Object o : map.getValues()) {
                    com.unciv.logic.map.tile.Tile t = (com.unciv.logic.map.tile.Tile) o;
                    // getBaseTerrain is overloaded in the Kotlin; reflection picks
                    // the one that exists rather than making javac guess.
                    Method m = t.getClass().getMethod("getBaseTerrain");
                    String base = String.valueOf(m.invoke(t));
                    if (ruleset.getTerrains().get(base) == null) {
                        if (unknown++ < 3) System.out.println("  UNKNOWN base: " + base);
                    }
                    for (String f : t.getTerrainFeatures()) {
                        if (ruleset.getTerrains().get(f) == null) {
                            if (unknown++ < 3) System.out.println("  UNKNOWN feature: " + f);
                        }
                    }
                }
                if (unknown > 0) {
                    System.out.println("  FAILED          " + unknown + " unknown terrain(s)");
                    failed++;
                    continue;
                }
                System.out.println("  terrains        all known to the ruleset");

                /* And finally PLAY it, which is the question a map is for.
                 * Loading proves less than it looks: GameStarter runs
                 * setTransients again, assigns starting positions -- these maps
                 * declare none, and Unciv picks them itself -- and places each
                 * civ's opening units. A map can pass every check above and
                 * fail here. */
                com.unciv.models.metadata.GameSetupInfo info =
                    new com.unciv.models.metadata.GameSetupInfo();
                info.getMapParameters().setType(com.unciv.logic.map.MapGeneratedMainType.custom);
                info.getMapParameters().setName(new java.io.File(path).getName());
                info.setMapFile(new com.badlogic.gdx.files.FileHandle(new java.io.File(path)));
                com.unciv.logic.GameInfo game =
                    com.unciv.logic.GameStarter.Companion.startNewGame(info);
                int placed = 0;
                for (Object o : game.getCivilizations()) {
                    com.unciv.logic.civilization.Civilization c =
                        (com.unciv.logic.civilization.Civilization) o;
                    if (c.getUnits().getCivUnits().iterator().hasNext()) placed++;
                }
                if (placed == 0) {
                    System.out.println("  FAILED          game started but no civ got units");
                    failed++;
                    continue;
                }
                System.out.println("  startNewGame    ok, " + placed + " civ(s) placed"
                                   + " (map declares " + map.getStartingLocations().size()
                                   + " starting locations)");
            } catch (Exception e) {
                System.out.println("  FAILED          " + e);
                failed++;
            }
        }
        System.out.println(failed == 0 ? "all maps load AND start a game in Unciv"
                                       : failed + " map(s) failed");
        System.exit(failed == 0 ? 0 : 1);
    }
}
