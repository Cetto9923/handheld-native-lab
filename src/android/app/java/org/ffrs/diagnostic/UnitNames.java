package org.ffrs.diagnostic;

import java.util.Collections;
import java.util.HashMap;
import java.util.Map;

/**
 * The character names of the units, built into the app.
 *
 * The names come from the text table the game keeps in memory, so the cheat
 * can show the name the game uses instead of a number. A name file in the app
 * directory overrides this table, which is how a refreshed table is used
 * without rebuilding the app.
 */
public final class UnitNames {
    private static final Map<Integer, String> TABLE = new HashMap<>();

    static {
        TABLE.put(1001, "雷因");
        TABLE.put(1002, "拉斯韦尔");
        TABLE.put(1003, "菲娜");
        TABLE.put(1004, "莉德");
        TABLE.put(1005, "尼科尔");
        TABLE.put(1006, "魔人菲娜");
        TABLE.put(1007, "杰科");
        TABLE.put(1008, "樱");
    }

    private UnitNames() {}

    /** The built in names, by unit id. */
    public static Map<Integer, String> table() {
        return Collections.unmodifiableMap(TABLE);
    }

    /** The name of a unit, or null when it is not known. */
    public static String name(int unitId) {
        return TABLE.get(unitId);
    }
}