#pragma leco tool
#include <stdlib.h>

import jute;
import tora;
import silog;

static const auto home_dir = jute::view::unsafe(getenv("HOME"));

int main(int argc, char ** argv) {
  if (argc < 3) {
    silog::log(silog::error, "requires group/artefact/version");
    return 1;
  }

  auto file = (home_dir + "/.m2/meeql").cstr();
  tora::db db { file.begin() };

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
    SELECT prop.key, prop.value
    FROM prop
    JOIN pom_chain ON pom_chain.id = prop.owner_pom
    JOIN pom ON pom_chain.root = pom.id
    GROUP BY 1
    HAVING depth = MIN(depth)
  )");

  stmt = db.prepare(R"(
    SELECT key, value
    FROM eff_prop
  )");
  while (stmt.step()) {
    silog::log(silog::debug, "%s = [%s]",
        stmt.column_text(0),
        stmt.column_text(1));
  }

#if 0
  auto stmt = db.prepare(R"(
    SELECT dep.group_id, dep.artefact_id, dep.version, pom_chain.depth
    FROM dep
    JOIN pom_chain ON pom_chain.id = dep.owner_pom
    JOIN pom ON pom_chain.root = pom.id
    WHERE pom.group_id = ?
      AND pom.artefact_id = ?
      AND pom.version = ?
  )");
  stmt.bind(1, jute::view::unsafe(argv[1]));
  stmt.bind(2, jute::view::unsafe(argv[2]));
  stmt.bind(3, jute::view::unsafe(argv[3]));
  while (stmt.step()) {
    silog::log(silog::debug, "%*s%s:%s:%s",
        stmt.column_int(3), "",
        stmt.column_text(0),
        stmt.column_text(1),
        stmt.column_text(2));
  }
#endif
}
