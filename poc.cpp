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
    WITH RECURSIVE
      pom_chain(id, depth) AS (
        SELECT id, 0
        FROM pom
        WHERE group_id = ?
          AND artefact_id = ?
          AND version = ?
        UNION ALL
        SELECT pom.parent, pom_chain.depth + 1
        FROM pom
        JOIN pom_chain ON pom_chain.id = pom.id
        ORDER BY 2 ASC
      )
    SELECT dep.group_id, dep.artefact_id, dep.version, pom_chain.depth
    FROM dep
    JOIN pom_chain ON pom_chain.id = dep.owner_pom
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
}
