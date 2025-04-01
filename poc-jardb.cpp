#pragma leco tool
import jute;
import meeql;
import mtime;
import print;
import tora;

static auto curry(auto fn, auto param) {
  return [=](auto ... args) {
    return fn(param, args...);
  };
}

static void add_jar(tora::stmt * stmt, jute::view pom_path) {
  auto jar_path = jute::heap { pom_path.rsplit('.').before } + ".jar";
  if (!mtime::of((*jar_path).cstr().begin())) return;

  stmt->reset();
  stmt->bind(1, *jar_path);
  stmt->step();
}

int main() {
  tora::db db { ":memory:" };

  db.exec(R"(
    CREATE TABLE jar (
      id        INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
      indexable INTEGER NOT NULL DEFAULT 0,
      path      TEXT NOT NULL UNIQUE
    ) STRICT;
  )");

  auto stmt = db.prepare(R"(
    INSERT OR IGNORE INTO jar (path)
    VALUES (?)
  )");
  meeql::recurse_repo_dir(curry(add_jar, &stmt));

  stmt = db.prepare("SELECT COUNT(*) FROM jar");
  stmt.step();
  putfn("imported %d jar files", stmt.column_int(0));
}
