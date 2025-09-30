#pragma leco tool
import cavan;
import jute;
import hai;
import meeql;
import mtime;
import print;
import tora;

[[nodiscard]] static auto db_init() {
  tora::db db { "out/depdb.sql" };
  db.exec(R"(
    DROP TABLE IF EXISTS queue;
    DROP TABLE IF EXISTS q_excl;
    DROP TABLE IF EXISTS resolved;

    CREATE TABLE queue (
      id      INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
      done    INTEGER NOT NULL DEFAULT 0,
      pomfile TEXT NOT NULL
    ) STRICT;

    CREATE TABLE q_excl (
      qid INTEGER NOT NULL REFERENCES queue(id),
      grp TEXT NOT NULL,
      art TEXT NOT NULL,
      debug INTEGER,
      PRIMARY KEY (qid, grp, art)
    ) STRICT;

    CREATE TABLE resolved (
      grp TEXT NOT NULL,
      art TEXT NOT NULL,
      ver TEXT NOT NULL,
      PRIMARY KEY (grp, art)
    ) STRICT;
  )");
  return db;
}
[[nodiscard]] static auto db_enqueue(tora::db * db, const cavan::pom * pom, int parent_qid) {
  auto stmt = db->prepare("INSERT INTO queue (pomfile) VALUES (?) RETURNING id");
  stmt.bind(1, pom->filename);
  stmt.step();
  auto qid = stmt.column_int(0);

  stmt = db->prepare(R"(
    INSERT INTO q_excl (qid, grp, art, debug)
    SELECT ?, grp, art, ?
    FROM q_excl
    WHERE qid = ?
  )");
  stmt.bind(1, qid);
  stmt.bind(2, parent_qid);
  stmt.bind(3, parent_qid);

  stmt = db->prepare("INSERT INTO resolved (grp, art, ver) VALUES (?, ?, ?)");
  stmt.bind(1, pom->grp);
  stmt.bind(2, pom->art);
  stmt.bind(3, pom->ver);
  stmt.step();

  return qid;
}
struct queue_item {
  int id;
  hai::cstr fname;
};
[[nodiscard]] static queue_item db_dequeue(tora::db * db) {
  auto stmt = db->prepare(R"(
    UPDATE queue 
    SET done = 1
    WHERE id = (SELECT MIN(id) FROM queue WHERE done = 0)
    RETURNING id, pomfile
  )");
  if (!stmt.step()) return {};
  return {
    .id    = stmt.column_int(0),
    .fname = stmt.column_view(1).cstr(),
  };
}

static void db_add_excl(tora::db * db, int qid, jute::view grp, jute::view art) {
  auto stmt = db->prepare("INSERT INTO q_excl (qid, grp, art) VALUES (?, ?, ?)");
  stmt.bind(1, qid);
  stmt.bind(2, grp);
  stmt.bind(3, art);
  stmt.step();
}
static bool db_excl(tora::db * db, int qid, jute::view grp, jute::view art) {
  auto stmt = db->prepare(R"(
    SELECT 1 FROM q_excl
    WHERE qid = ? AND grp = ? AND art = ?
  )");
  stmt.bind(1, qid);
  stmt.bind(2, grp);
  stmt.bind(3, art);
  return stmt.step();
}

[[nodiscard]] static bool db_resolved(tora::db * db, jute::view grp, jute::view art) {
  auto stmt = db->prepare(R"(
    SELECT 1 FROM resolved
    WHERE grp = ? AND art = ?
  )");
  stmt.bind(1, grp);
  stmt.bind(2, art);
  return stmt.step();
}

static void preload_modules(cavan::pom * pom) {
  cavan::read_parent_chain(pom);

  auto _ = cavan::read_modules(pom);

  if (pom->ppom) preload_modules(pom->ppom);
}

int main(int argc, char ** argv) try {
  tora::on_error = [](auto msg) { die(msg); };

  const auto shift = [&] { return jute::view::unsafe(argc == 1 ? "" : (--argc, *++argv)); };

  auto file = shift();
  if (file == "") die("missing file");

  auto pom = cavan::read_pom(file);
  preload_modules(pom);

  auto db = db_init();
  auto _ = db_enqueue(&db, pom, 0);

  while (true) {
    auto [qid, next_file] = db_dequeue(&db);
    if (next_file.size() == 0) break;

    putln("  ", next_file);

    auto pom = cavan::read_pom(next_file);
    cavan::eff_pom(pom);

    for (auto &[d, _]: pom->deps) {
      if (d.scp != "compile") continue;
      if (db_resolved(&db, *d.grp, d.art)) continue;
      if (db_excl(&db, qid, *d.grp, d.art)) continue;

      auto dpom = cavan::read_pom(*d.grp, d.art, *d.ver);
      auto dqid = db_enqueue(&db, dpom, qid);

      // TODO: deal with exclusions
      if (d.exc) for (auto &[g, a]: *d.exc) db_add_excl(&db, dqid, g, a);
    }
  }
} catch (...) {
  return 13;
}
