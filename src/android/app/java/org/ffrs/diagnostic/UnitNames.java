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