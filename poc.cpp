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
    SELECT dep.group_id, dep.artefact_id, dep.version
    FROM dep
    JOIN pom ON pom.id = dep.owner_pom
    WHERE pom.group_id = ?
      AND pom.artefact_id = ?
      AND pom.version = ?
  )");
  stmt.bind(1, jute::view::unsafe(argv[1]));
  stmt.bind(2, jute::view::unsafe(argv[2]));
  stmt.bind(3, jute::view::unsafe(argv[3]));
  while (stmt.step()) {
    silog::log(silog::debug, "%s:%s:%s",
        stmt.column_text(0),
        stmt.column_text(1),
        stmt.column_text(2));
  }
}
