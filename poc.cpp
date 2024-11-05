#pragma leco tool
#include "../tora/sqlite3.h"

import jute;
import meeql;
import print;
import silog;

static auto v(const unsigned char * n) {
  if (!n) return jute::view {};
  return jute::view::unsafe(reinterpret_cast<const char *>(n));
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

  auto db = meeql::db();

  auto flags = SQLITE_DETERMINISTIC | SQLITE_UTF8;
  sqlite3_create_function(db.handle(), "propinator", 1, flags, nullptr, prop_fn, nullptr, nullptr);

  auto stmt = db.prepare(R"(
    SELECT id
    FROM pom
    WHERE group_id = ?
      AND artefact_id = ?
      AND version = ?
  )");
  stmt.bind(1, jute::view::unsafe(argv[1]));
  stmt.bind(2, jute::view::unsafe(argv[2]));
  stmt.bind(3, jute::view::unsafe(argv[3]));
  if (!stmt.step()) silog::die("could not find POM");
  auto pom_id = stmt.column_int(0);

  // Creates the effective props and deps lists from the parent chain
  silog::trace(1);
  stmt = db.prepare(R"(
    CREATE TEMP TABLE eff_dep AS
    SELECT d.*, f.root, f.depth
    FROM f_dep AS f
    JOIN dep AS d ON f.id = d.id
    WHERE root = ?
  )");
  stmt.bind(1, pom_id);
  stmt.step();

  stmt = db.prepare(R"(
    CREATE TEMP TABLE eff_prop AS
    SELECT p.*, f.root, f.depth
    FROM f_prop AS f
    JOIN prop AS p ON f.id = p.id
    WHERE root = ?
  )");
  stmt.bind(1, pom_id);
  stmt.step();

  silog::trace(2);
  // Recursively apply properties to effective deps
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
  silog::trace(3);

  db.exec(R"(
    CREATE TEMP TABLE x AS
    SELECT dep.*, ed.depth + 1
    FROM eff_dep AS ed
    JOIN pom
      ON pom.group_id = ed.group_id
     AND pom.artefact_id = ed.artefact_id
     AND pom.version = ed.version
    JOIN dep
      ON dep.owner_pom = pom.id
    WHERE ed.scope = 'import' AND ed.type = 'pom'
      AND dep.dep_mgmt = 1
  )");

  silog::trace(4);
  stmt = db.prepare(R"(
    SELECT group_id || ":" || artefact_id || ":" || version
    FROM x
  )");
  while (stmt.step()) {
    putln((const char *)stmt.column_text(0));
  }
  silog::trace(5);

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
