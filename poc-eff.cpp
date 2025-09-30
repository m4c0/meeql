#pragma leco tool
#pragma leco add_impl "poc-eff-impl.cpp"

import hai;
import jute;
import print;
import silog;
import sysstd;
import tora;

static const auto home_dir = jute::view::unsafe(sysstd::env("HOME"));
 
namespace meeql {
  auto db() {
    auto file = (home_dir + "/.m2/meeql").cstr();
    return tora::db { file.begin() };
  }

}
int eff(tora::db & db, jute::view group_id, jute::view artefact_id, jute::view version, int depth);

static void setup_aux_tables() {
  silog::log(silog::info, "setting up auxiliary tables");
  auto db = meeql::db();
  db.exec(R"(
    CREATE TABLE f_pom_tree AS
    WITH RECURSIVE
      pom_chain(id, root, depth) AS (
        SELECT id, id, 0
        FROM pom
        UNION ALL
        SELECT pom.parent, pom_chain.root, pom_chain.depth + 1
        FROM pom
        JOIN pom_chain ON pom_chain.id = pom.id
        WHERE pom.parent IS NOT NULL
      )
    SELECT * FROM pom_chain;

    CREATE TABLE f_prop AS
    SELECT t.root, prop.id, t.depth
    FROM prop
    JOIN f_pom_tree t ON t.id = prop.owner_pom
    GROUP BY t.root, prop.key
    HAVING depth = MIN(depth);

    CREATE INDEX ifp_root ON f_prop (root);

    CREATE TABLE f_dep AS
    SELECT t.root, dep.id, depth
    FROM dep
    JOIN f_pom_tree t ON t.id = dep.owner_pom
    GROUP BY t.root, dep.dep_mgmt, dep.group_id, dep.artefact_id
    HAVING depth = MIN(depth);

    CREATE INDEX ifd_root ON f_dep (root);
  )");

  auto stmt = db.prepare("SELECT COUNT(*) FROM f_prop");
  stmt.step();
  silog::trace("eff. props", stmt.column_int(0));

  stmt = db.prepare("SELECT COUNT(*) FROM f_dep");
  stmt.step();
  silog::trace("eff. deps", stmt.column_int(0));
}

static auto resolve(jute::view grp, jute::view art, jute::view ver, jute::view scope) {
  auto db = meeql::db();

  auto pom = eff(db, grp, art, ver, 0);

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
  grp = jute::heap { stmt.column_view(0) };
  art = jute::heap { stmt.column_view(1) };
  ver = jute::heap { stmt.column_view(2) };
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
 
  setup_aux_tables();

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
