#pragma leco tool
import cavan;
import jute;
import hai;
import meeql;
import mtime;
import print;
import tora;

[[nodiscard]] static auto db_init() {
  tora::db db { ":memory:" };
  db.exec(R"(
    CREATE TABLE queue (
      id      INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
      pomfile TEXT NOT NULL
    ) STRICT;
  )");
  return db;
}
[[nodiscard]] static auto db_enqueue(tora::db * db, jute::view file) {
  auto stmt = db->prepare("INSERT INTO queue (pomfile) VALUES (?)");
  stmt.bind(1, file);
  stmt.step();
}
[[nodiscard]] static hai::cstr db_dequeue(tora::db * db) {
  auto stmt = db->prepare(R"(
    DELETE FROM queue 
    WHERE id = (SELECT MIN(id) FROM queue)
    RETURNING pomfile
  )");
  if (!stmt.step()) return {};
  return stmt.column_view(0).cstr();
}

static void preload_modules(cavan::pom * pom) {
  cavan::read_parent_chain(pom);

  auto _ = cavan::read_modules(pom);

  if (pom->ppom) preload_modules(pom->ppom);
}

int main(int argc, char ** argv) try {
  const auto shift = [&] { return jute::view::unsafe(argc == 1 ? "" : (--argc, *++argv)); };

  auto file = shift();
  if (file == "") die("missing file");

  preload_modules(cavan::read_pom(file));

  auto db = db_init();
  db_enqueue(&db, file);

  hai::cstr next_file {};
  while ((next_file = db_dequeue(&db)).size()) {
    putln(next_file);

    auto pom = cavan::read_pom(next_file);
    cavan::eff_pom(pom);

    for (auto &[d, _]: pom->deps) {
      if (d.scp != "compile") continue;

      auto dpom = cavan::read_pom(*d.grp, d.art, *d.ver);
      db_enqueue(&db, dpom->filename);
    }
  }
} catch (...) {
  return 13;
}
