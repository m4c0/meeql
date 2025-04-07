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

  unsigned count {};
  auto unzip = d("unzip");
  auto qq    = d("-qq");
  auto l     = d("-l");
  auto cls   = d("*.class");
  char * args[] { unzip.begin(), qq.begin(), l.begin(), jar_path.begin(), cls.begin(), 0 };
  p::proc p { args };
  while (p.gets()) {
    stmt->bind(1, jar_path);
    stmt->bind(2, jute::view::unsafe(p.last_line_read()));
    stmt->step();
    stmt->reset();
    count++;
  }
  putln(name, " ", count);
}

int main() try {
  tora::db db { ":memory:" };

  db.exec(R"(
    CREATE TABLE class (
      id    INTEGER NOT NULL PRIMARY KEY AUTOINCREMENT,
      jar   TEXT NOT NULL,
      name  TEXT NOT NULL
    ) STRICT;
  )");
  auto stmt = db.prepare("INSERT INTO class (jar, name) VALUES (?, ?)");
  meeql::recurse_repo_dir(curry(unjar, &stmt));
} catch (...) {
  return 1;
}
