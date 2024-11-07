#pragma leco tool

import hai;
import jute;
import meeql;
import print;
import silog;

static auto v(const unsigned char * n) {
  if (!n) return jute::view {};
  return jute::view::unsafe(reinterpret_cast<const char *>(n));
}

static void work(jute::view grp, jute::view art, jute::view ver, jute::view scope) {
  auto db = meeql::db();

  silog::trace(1);
  meeql::eff(db, grp, art, ver, 0);

  silog::trace(2);
  auto stmt = db.prepare(R"(
    DELETE FROM eff_dep AS ed
    WHERE scope = 'import' AND type = 'pom'
    RETURNING group_id, artefact_id, version, depth
  )");
  while (stmt.step()) {
    meeql::eff(db, 
        v(stmt.column_text(0)),
        v(stmt.column_text(1)),
        v(stmt.column_text(2)),
        stmt.column_int(3));
  }
  silog::trace(5);

  stmt = db.prepare(R"(
    INSERT OR IGNORE INTO r_deps (root, group_id, artefact_id, version)
    SELECT d.root, d.group_id, d.artefact_id
         , COALESCE(d.version, dm.version)
    FROM eff_dep AS d
    LEFT JOIN eff_dep AS dm
      ON d.group_id = dm.group_id
     AND d.artefact_id = dm.artefact_id
     AND dm.dep_mgmt = 1
    WHERE d.dep_mgmt = 0
      AND COALESCE(d.scope, 'compile') IN (?, 'compile')
      AND COALESCE(d.type, 'jar') = 'jar'
      AND NOT d.optional
    GROUP BY d.group_id, d.artefact_id
    HAVING d.depth = MIN(d.depth)
  )");
  stmt.bind(1, scope);
  stmt.step();

  silog::trace(6);
}

int main(int argc, char ** argv) {
  if (argc < 3) {
    silog::log(silog::error, "requires group/artefact/version");
    return 1;
  }

  meeql::db().exec(R"(
    DROP TABLE IF EXISTS r_deps;
    CREATE TABLE r_deps (
      id INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
      root        INTEGER NOT NULL REFERENCES pom(id),
      group_id    TEXT NOT NULL,
      artefact_id TEXT NOT NULL,
      version     TEXT NOT NULL,
      worked      INT NOT NULL DEFAULT 0,
      UNIQUE (group_id, artefact_id)
    );
  )");

  work(
    jute::view::unsafe(argv[1]),
    jute::view::unsafe(argv[2]),
    jute::view::unsafe(argv[3]),
    "test"
  );

  while (true) {
    hai::cstr grp, art, ver;
    {
      auto db = meeql::db();
      auto stmt = db.prepare(R"(
        UPDATE r_deps
        SET worked = 1
        WHERE id IN (
          SELECT id
          FROM r_deps
          WHERE worked = 0
          ORDER BY id ASC
          LIMIT 1
        )
        RETURNING group_id, artefact_id, version
      )");
      if (!stmt.step()) break;
      grp = v(stmt.column_text(0)).cstr();
      art = v(stmt.column_text(1)).cstr();
      ver = v(stmt.column_text(2)).cstr();
      putfn("%s:%s:%s", grp.begin(), art.begin(), ver.begin());
    }
    work(grp, art, ver, "compile");
  }
}
