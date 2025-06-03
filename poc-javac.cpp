#pragma leco tool

import cavan;
import jojo;
import jute;
import hai;
import print;
import tora;

[[nodiscard]] static auto resolve_deps(jute::view fname) {
  tora::db db { "out/poc-javac.sqlite" }; // TODO: move to .m2
  db.exec(R"(
    CREATE TABLE IF NOT EXISTS rdep (
      s_pom TEXT NOT NULL,
      t_jar TEXT NOT NULL
    ) STRICT
  )");

  auto stmt = db.prepare("SELECT t_jar FROM rdep WHERE s_pom = ?");
  stmt.bind(1, fname);

  hai::chain<hai::cstr> res { 1000 };
  while (stmt.step()) {
    res.push_back(stmt.column_view(0).cstr());
  }
  if (res.size()) return res;

  cavan::file_reader = jojo::read_cstr;
  auto pom = cavan::read_pom(fname);
  auto deps = cavan::resolve_transitive_deps(pom);

  stmt = db.prepare("INSERT INTO rdep (s_pom, t_jar) VALUES (?, ?)");
  for (auto i = 0; i < deps.size(); i++) {
    auto p = deps.seek(i);
    auto jar = cavan::path_of(p->grp, p->art, p->ver, "jar");

    stmt.reset();
    stmt.bind(1, fname);
    stmt.bind(2, jar);
    stmt.step();

    res.push_back(traits::move(jar));
  }
  return res;
}

[[nodiscard]] static hai::cstr pom_of(jute::view file) {
  auto [rest, ext] = file.rsplit('.');
  if (ext != "java") die("input must be a java file");

  while (rest != "") {
    auto [l, r] = rest.rsplit('/');
    rest = l;
    if (r == "java") break;
  }
  if (rest != "") {
    auto [l, r] = rest.rsplit('/');
    rest = (r == "main" || r == "test") ? l : "";
  }
  if (rest != "") {
    auto [l, r] = rest.rsplit('/');
    rest = (r == "src") ? l : "";
  }
  // TODO: should we deal with sources from generated-sources?
  if (rest == "") die("source file must be inside 'src/main/java' or 'src/test/java'");

  return (rest + "/pom.xml").cstr();
}

int main(int argc, char ** argv) try {
  const auto shift = [&] { return jute::view::unsafe(argc == 1 ? "" : (--argc, *++argv)); };

  auto file = shift();
  if (file == "") die("missing java source file");

  for (auto & p : resolve_deps(pom_of(file))) {
    putln(p);
  }
} catch (...) {
  return 3;
}

