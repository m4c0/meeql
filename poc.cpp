#pragma leco tool

import hai;
import jute;
import meeql;
import print;
import silog;

static auto resolve(jute::view grp, jute::view art, jute::view ver, jute::view scope) {
  auto db = meeql::db();

  auto pom = meeql::eff(db, grp, art, ver, 0);

  auto stmt = db.prepare(R"(
    INSERT OR IGNORE INTO r_deps (pom, group_id, artefact_id, version)
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
  return pom;
}

static bool fetch_next(int pom, jute::heap & grp, jute::heap & art, jute::heap & ver) {
  // TODO: recursive search for next
  // with recursive from "pom", going up in r_deps_tree and using the version
  // from the min(depth) grp by (grp, art)
  auto db = meeql::db();
  auto stmt = db.prepare(R"(
    UPDATE r_deps
    SET worked = 1
    WHERE id IN (
      SELECT id
      FROM r_deps
      WHERE pom = ? AND worked = 0
      LIMIT 1
    )
    RETURNING group_id, artefact_id, version
  )");
  stmt.bind(1, pom);
  if (!stmt.step()) return false;
  grp = stmt.column_view(0);
  art = stmt.column_view(1);
  ver = stmt.column_view(2);
  return true;
}

int main(int argc, char ** argv) {
  if (argc < 3) {
    silog::log(silog::error, "requires group/artefact/version");
    return 1;
  }
  auto grp = jute::view::unsafe(argv[1]);
  auto art = jute::view::unsafe(argv[2]);
  auto ver = jute::view::unsafe(argv[3]);

  meeql::db().exec("DROP TABLE IF EXISTS r_deps");
  meeql::db().exec("DROP TABLE IF EXISTS r_deps_tree");
  meeql::db().exec(R"(
    CREATE TABLE IF NOT EXISTS r_deps (
      id          INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
      pom         INTEGER NOT NULL REFERENCES pom(id),
      group_id    TEXT NOT NULL,
      artefact_id TEXT NOT NULL,
      version     TEXT NOT NULL,
      worked      INTEGER NOT NULL DEFAULT 0,
      UNIQUE (pom, group_id, artefact_id)
    );
    CREATE TABLE IF NOT EXISTS r_deps_tree (
      parent INTEGER NOT NULL REFERENCES pom(id),
      child  INTEGER NOT NULL REFERENCES pom(id),
      UNIQUE (parent, child)
    );
  )");

  silog::trace(1);
  auto pom = resolve(grp, art, ver, "test");

  silog::trace(2);
  jute::heap group_id, artefact_id, version;
  while (fetch_next(pom, group_id, artefact_id, version)) {
    auto dpom = resolve(*group_id, *artefact_id, *version, "compile");
    if (dpom == 0) silog::die("missing dependency: %s:%s:%s", (*group_id).cstr().begin(), (*artefact_id).cstr().begin(), (*version).cstr().begin());

    auto db = meeql::db();
    auto stmt = db.prepare("INSERT OR IGNORE INTO r_deps_tree (parent, child) VALUES (?, ?)");
    stmt.bind(1, pom);
    stmt.bind(2, dpom);
    stmt.step();
  }
  silog::trace(3);
}
