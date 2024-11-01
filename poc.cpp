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
    SELECT *
    FROM f_dep
    WHERE root = ?
  )");
  stmt.bind(1, pom_id);
  stmt.step();

  stmt = db.prepare(R"(
    CREATE TEMP TABLE eff_prop AS
    SELECT *
    FROM f_prop
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
