#pragma leco tool
#pragma leco add_impl spellfix
import jute;
import meeql;
import mtime;
import print;
import tora;

extern "C" int sqlite3_spellfix_init(void * db, char ** err, const void * api);

static auto curry(auto fn, auto param) {
  return [=](auto ... args) {
    return fn(param, args...);
  };
}

static void add_jar(tora::stmt * stmt, jute::view pom_path) {
  auto name = pom_path.rsplit('/').after.rsplit('.').before;
  auto jar_path = jute::heap { pom_path.rsplit('.').before } + ".jar";
  if (!mtime::of((*jar_path).cstr().begin())) return;

  stmt->reset();
  stmt->bind(1, *jar_path);
  stmt->bind(2, name);
  stmt->step();
}

static auto init() {
  tora::db db { ":memory:" };

  char * err {};
  sqlite3_spellfix_init(db.handle(), &err, nullptr);
  if (err) die("Error initing spellfix: %s", err);

  db.exec(R"(
    CREATE TABLE jar (
      id        INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
      index_ts  INTEGER NOT NULL DEFAULT -1,
      path      TEXT NOT NULL UNIQUE,
      name      TEXT NOT NULL
    ) STRICT;
  )");
  auto stmt = db.prepare(R"(
    INSERT OR IGNORE INTO jar (path, name)
    VALUES (?, ?)
  )");
  meeql::recurse_repo_dir(curry(add_jar, &stmt));

  db.exec(R"(
    CREATE VIRTUAL TABLE jar_sfx USING spellfix1;
    INSERT INTO jar_sfx(word) SELECT name FROM jar;
  )");

  return db;
}

int main(int argc, char ** argv) {
  auto db = init();

  auto stmt = db.prepare("SELECT word FROM jar_sfx WHERE word MATCH 'Program*'");
  while (stmt.step()) {
    putln(stmt.column_view(0));
  }
}
