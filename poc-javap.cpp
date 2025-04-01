#pragma leco tool

import jute;
import meeql;
import mtime;
import popen;
import silog;
import tora;

static void setup_schema(tora::db & db) {
  db.exec(R"(
    CREATE TABLE jar (
      id    INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
      name  TEXT NOT NULL UNIQUE,
      file  TEXT NOT NULL
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
  auto name = pom_path.rsplit('/').after.rsplit('-').before;
  auto jar_path = jute::heap { pom_path.rsplit('.').before } + ".jar";
  if (!mtime::of((*jar_path).cstr().begin())) return;

  stmt->reset();
  stmt->bind(1, name);
  stmt->bind(2, *jar_path);
  stmt->step();
}

static void import_local_repo(tora::db & db) {
  silog::log(silog::info, "indexing jar files");
  auto stmt = db.prepare(R"(
    INSERT OR IGNORE INTO jar (name, file)
    VALUES (?, ?)
  )");
  meeql::recurse_repo_dir(curry(add_jar, &stmt));

  stmt = db.prepare("SELECT COUNT(*) FROM jar");
  stmt.step();
  silog::log(silog::info, "imported %d jar files", stmt.column_int(0));
}

static auto d(jute::view str) { return str.cstr(); }

static void import_classes(tora::db & db) {
  silog::log(silog::info, "indexing classes");

  auto i_stmt = db.prepare(R"(
    INSERT INTO class (name, jar)
    VALUES (?, ?)
  )");

  auto stmt = db.prepare("SELECT id, file FROM jar ORDER BY file");
  while (stmt.step()) {
    auto id   = stmt.column_int(0);
    auto name = stmt.column_view(1).cstr();

    unsigned count {};
    auto unzip = d("unzip");
    auto qq    = d("-qq");
    auto l     = d("-l");
    auto cls   = d("*.class");
    char * args[] { unzip.begin(), qq.begin(), l.begin(), name.begin(), cls.begin(), 0 };
    p::proc p { args };
    while (p.gets()) {
      auto name = jute::view::unsafe(p.last_line_read()).rsplit(' ').after.split('\n').before;
      i_stmt.reset();
      i_stmt.bind(1, name);
      i_stmt.bind(2, id);
      i_stmt.step();
      count++;
    }
    while (p.gets_err()) {
      silog::log(silog::error, "%s", p.last_line_read());
    }
    switch (p.wait()) {
      case 0:
      case 11: // "no class found"
        silog::log(silog::info, "found %d classes in [%s]", count, name.begin());
        break;
      default:
        silog::log(silog::error, "Failed to unzip [%s]", name.begin());
        throw 0;
    }
  }
}

int main() {
  tora::db db { ":memory:" };

  setup_schema(db);
  import_local_repo(db);
  import_classes(db);

  silog::log(silog::info, "done");
}
