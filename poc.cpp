#pragma leco tool

import jute;
import meeql;
import print;
import silog;

static auto v(const unsigned char * n) {
  if (!n) return jute::view {};
  return jute::view::unsafe(reinterpret_cast<const char *>(n));
}

int main(int argc, char ** argv) {
  if (argc < 3) {
    silog::log(silog::error, "requires group/artefact/version");
    return 1;
  }

  auto db = meeql::db();

  silog::trace(1);
  auto grp = jute::view::unsafe(argv[1]);
  auto art = jute::view::unsafe(argv[2]);
  auto ver = jute::view::unsafe(argv[3]);
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
