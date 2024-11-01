#pragma leco tool
#include "../tora/sqlite3.h"
#include <stdlib.h>

import jute;
import print;
import silog;
import tora;

static const auto home_dir = jute::view::unsafe(getenv("HOME"));

static auto v(const unsigned char * n) {
  if (!n) return jute::view {};
  return jute::view::unsafe(reinterpret_cast<const char *>(n));
}

jute::heap prop(tora::db & db, jute::view name) {
  auto stmt = db.prepare(R"(
    SELECT value FROM eff_prop WHERE key = ?
  )");
  stmt.bind(1, name);
  if (!stmt.step()) return {};
  return v(stmt.column_text(0));
}

jute::heap apply_props(tora::db & db, jute::view in) {
  jute::heap str { in };
  for (unsigned i = 0; i < str.size(); i++) {
    if ((*str)[i] != '$') continue;
    if ((*str)[i + 1] != '{') continue;

    unsigned j {};
    for (j = i; j < str.size() && (*str)[j] != '}'; j++) {
    }

    if (j == str.size()) return str;

    jute::view before { str.begin(), i };
    jute::view name { str.begin() + i + 2, j - i - 2 };
    jute::view after { str.begin() + j + 1, str.size() - j - 1 };

    str = before + *prop(db, name) + after;
    i--;
  }
  return str;
}

void prop_fn(sqlite3_context * ctx, int argc, sqlite3_value ** argv) {
  silog::assert(argc == 1, "expecting only a single argument");
  auto val = v(sqlite3_value_text(argv[0]));
  for (unsigned i = 0; i < val.size(); i++) {
    if (val[i] != '$') continue;
    if (val[i + 1] != '{') continue;

    for (unsigned j = i + 2; j < val.size(); j++) {
      if (val[j] != '}') continue;

      return sqlite3_result_text(ctx, val.begin() + i + 2, j - i - 2, SQLITE_TRANSIENT);
    }
  }
  return sqlite3_result_null(ctx);
}

int main(int argc, char ** argv) {
  if (argc < 3) {
    silog::log(silog::error, "requires group/artefact/version");
    return 1;
  }

  auto file = (home_dir + "/.m2/meeql").cstr();
  tora::db db { file.begin() };

  sqlite3_create_function(db.handle(), "propinator", 1, SQLITE_UTF8, nullptr, prop_fn, nullptr, nullptr);

  auto stmt = db.prepare(R"(
    CREATE TEMP TABLE pom_chain AS
    WITH RECURSIVE
      pom_chain(id, root, depth) AS (
        SELECT id, id, 0
        FROM pom
        WHERE pom.group_id = ?
          AND pom.artefact_id = ?
          AND pom.version = ?
        UNION ALL
        SELECT pom.parent, pom_chain.root, pom_chain.depth + 1
        FROM pom
        JOIN pom_chain ON pom_chain.id = pom.id
      )
    SELECT * FROM pom_chain
  )");
  stmt.bind(1, jute::view::unsafe(argv[1]));
  stmt.bind(2, jute::view::unsafe(argv[2]));
  stmt.bind(3, jute::view::unsafe(argv[3]));
  stmt.step();

  db.exec(R"(
    CREATE TEMP TABLE eff_prop AS
    SELECT prop.*
    FROM prop
    JOIN pom_chain ON pom_chain.id = prop.owner_pom
    GROUP BY prop.key
    HAVING depth = MIN(depth)
  )");
  db.exec(R"(
    CREATE TEMP TABLE eff_dep AS
    SELECT dep.*
    FROM dep
    JOIN pom_chain ON pom_chain.id = dep.owner_pom
    GROUP BY dep.dep_mgmt, dep.group_id, dep.artefact_id
    HAVING depth = MIN(depth)
  )");

  do {
    db.exec(R"(
      UPDATE eff_dep
      SET version = e_version
      FROM (
        SELECT ed.id AS e_id
             , REPLACE(ed.version, '${' || propinator(ed.version) || '}', ep.value) AS e_version
        FROM eff_dep AS ed
        LEFT JOIN eff_prop AS ep
               ON ep.key = propinator(ed.version)
      )
      WHERE version LIKE '%${%'
        AND id = e_id
    )");
  } while (db.changes() > 0);

  stmt = db.prepare(R"(
    SELECT d.group_id, d.artefact_id
         , COALESCE(d.version, dm.version)
    FROM eff_dep AS d
    LEFT JOIN eff_dep AS dm
      ON d.group_id = dm.group_id
     AND d.artefact_id = dm.artefact_id
     AND dm.dep_mgmt = 1
    WHERE d.dep_mgmt = 0
      AND COALESCE(d.scope, 'compile') IN ('test', 'compile')
      AND COALESCE(d.type, 'jar') = 'jar'
      AND NOT d.optional
  )");
  while (stmt.step()) {
    putfn("%s:%s:%s",
        stmt.column_text(0),
        stmt.column_text(1),
        stmt.column_text(2));
  }
}
