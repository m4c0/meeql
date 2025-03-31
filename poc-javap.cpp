#pragma leco tool

import jute;
import meeql;
import silog;
import tora;

static void setup_schema(tora::db & db) {
  db.exec(R"(
    CREATE TABLE jar (
      id    INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
      name  TEXT NOT NULL
    ) STRICT;

    CREATE TABLE class (
      id    INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
      name  TEXT NOT NULL,
      jar   INTEGER NOT NULL REFERENCES jar(id)
    ) STRICT;
  )");
}

auto curry(auto fn, auto param) {
  return [=](auto ... args) {
    return fn(param, args...);
  };
}

static void add_jar(tora::stmt * stmt, jute::view pom_path) {
  auto jar_path = jute::heap { pom_path.rsplit('.').before } + ".jar";
  stmt->reset();
  stmt->bind(1, *jar_path);
  stmt->step();
  silog::trace(jar_path);
}

static void import_local_repo(tora::db & db) {
  silog::log(silog::info, "parsing and importing local repo");
  auto stmt = db.prepare(R"(
    INSERT INTO jar (name)
    VALUES (?)
  )");
  meeql::recurse_repo_dir(curry(add_jar, &stmt));
}

int main() {
  tora::db db { ":memory:" };

  setup_schema(db);
  import_local_repo(db);

  silog::log(silog::info, "done");
}
