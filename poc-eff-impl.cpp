#include "../tora/sqlite3.h"

import jute;
import tora;
import silog;

static auto v(const unsigned char * n) {
  if (!n) return jute::view {};
  return jute::view::unsafe(reinterpret_cast<const char *>(n));
}

static void prop_fn(sqlite3_context * ctx, int argc, sqlite3_value ** argv) {
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

int eff(tora::db & db, jute::view group_id, jute::view artefact_id, jute::view version, int depth) {
  auto flags = SQLITE_DETERMINISTIC | SQLITE_UTF8;
  sqlite3_create_function(db.handle(), "propinator", 1, flags, nullptr, prop_fn, nullptr, nullptr);

  auto stmt = db.prepare(R"(
    SELECT id
    FROM pom
    WHERE group_id = ?
      AND artefact_id = ?
      AND version = ?
  )");
  stmt.bind(1, group_id);
  stmt.bind(2, artefact_id);
  stmt.bind(3, version);
  if (!stmt.step()) silog::die("could not find POM");
  auto pom_id = stmt.column_int(0);

  // Creates the effective props and deps lists from the parent chain
  if (depth == 0) {
    stmt = db.prepare(R"(
      CREATE TEMP TABLE IF NOT EXISTS eff_dep AS
      SELECT d.*, f.root, f.depth
      FROM f_dep AS f
      JOIN dep AS d ON f.id = d.id
      WHERE root = ?
    )");
    stmt.bind(1, pom_id);
    stmt.step();

    stmt = db.prepare(R"(
      CREATE TEMP TABLE IF NOT EXISTS eff_prop AS
      SELECT p.*, f.root, f.depth
      FROM f_prop AS f
      JOIN prop AS p ON f.id = p.id
      WHERE root = ?
    )");
    stmt.bind(1, pom_id);
    stmt.step();
  } else {
    stmt = db.prepare(R"(
      INSERT INTO eff_dep
      SELECT d.*, f.root, f.depth + ?
      FROM f_dep AS f
      JOIN dep AS d ON f.id = d.id
      WHERE root = ?
    )");
    stmt.bind(1, depth);
    stmt.bind(2, pom_id);
    stmt.step();

    stmt = db.prepare(R"(
      INSERT INTO eff_prop
      SELECT p.*, f.root, f.depth + ?
      FROM f_prop AS f
      JOIN prop AS p ON f.id = p.id
      WHERE root = ?
    )");
    stmt.bind(1, depth);
    stmt.bind(2, pom_id);
    stmt.step();
  }

  // Recursively apply properties to effective deps
  do {
    db.exec(R"(
      UPDATE eff_dep
      SET version = e_version
      FROM (
        SELECT ed.id AS e_id
             , REPLACE(ed.version, '${' || propinator(ed.version) || '}', ep.value) AS e_version
        FROM eff_dep AS ed
        JOIN eff_prop AS ep
          ON ep.root = ed.root
         AND ep.key = propinator(ed.version)
      )
      WHERE version LIKE '%${%'
        AND id = e_id
    )");
  } while (db.changes() > 0);

  // Apply "imports"
  stmt = db.prepare(R"(
    DELETE FROM eff_dep AS ed
    WHERE scope = 'import' AND type = 'pom'
    RETURNING group_id, artefact_id, version, depth
  )");
  while (stmt.step()) {
    eff(db, 
        stmt.column_view(0),
        stmt.column_view(1),
        stmt.column_view(2),
        stmt.column_int(3));
  }

  return pom_id;
}
