#pragma leco tool
import jute;
import meeql;
import mtime;
import popen;
import print;
import tora;

static auto curry(auto fn, auto param) {
  return [=](auto ... args) {
    return fn(param, args...);
  };
}

static auto d(jute::view str) { return str.cstr(); }

static void unjar(tora::stmt * stmt, jute::view pom_path) {
  auto name = pom_path.rsplit('/').after.rsplit('.').before;
  auto jar_path = (pom_path.rsplit('.').before + ".jar").cstr();
  if (!mtime::of(jar_path.begin())) return;

  auto unzip = d("unzip");
  auto qq    = d("-qq");
  auto l     = d("-l");
  auto cls   = d("*.class");
  char * args[] { unzip.begin(), qq.begin(), l.begin(), jar_path.begin(), cls.begin(), 0 };
  p::proc p { args };
  while (p.gets()) {
    stmt->bind(1, name);
    stmt->bind(2, jute::view::unsafe(p.last_line_read()));
    stmt->step();
    stmt->reset();
  }
}

static void load(tora::db & db) {
  db.exec(R"(
    DROP TABLE IF EXISTS class;
    CREATE TABLE class (
      id    INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
      jar   TEXT NOT NULL,
      name  TEXT NOT NULL
    ) STRICT;
  )");
  db.exec("BEGIN TRANSACTION");
  auto stmt = db.prepare("INSERT INTO class (jar, name) VALUES (?, ?)");
  meeql::recurse_repo_dir(curry(unjar, &stmt));
  db.exec("END TRANSACTION");

  stmt = db.prepare("SELECT COUNT(*) FROM class");
  stmt.step();
  putln("got ", stmt.column_int(0), " classes");

  db.exec(R"(
    DROP TABLE IF EXISTS class_sfx;
    CREATE VIRTUAL TABLE class_sfx USING spellfix1;
    INSERT INTO class_sfx(word) SELECT name FROM class;
  )");
  putln("indexing done");
}

int main(int argc, char ** argv) try {
  const auto shift = [&] { return jute::view::unsafe(argc > 1 ? (--argc, *++argv) : ""); };

  tora::db db { "out/classdb.sqlite" };
  meeql::spellfix_init(db);

  auto cmd = shift();
  if (cmd == "load") return (load(db), 0);

  auto stmt = db.prepare("SELECT word FROM class_sfx WHERE word MATCH ? || '*'");
  stmt.bind(1, "Completable");
  while (stmt.step()) putln(stmt.column_view(0));
} catch (...) {
  return 1;
}
