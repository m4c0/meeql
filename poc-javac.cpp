#pragma leco tool

import cavan;
import jojo;
import jute;
import hai;
import print;
import sysstd;
import tora;

[[nodiscard]] static constexpr auto root_of(jute::view file) {
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

  return rest;
}
static_assert(root_of("blah/bleh/blih/src/main/java/com/bloh/Bluh.java") == "blah/bleh/blih");

[[nodiscard]] static auto resolve_deps(auto fname_) {
  auto fname = fname_.cstr();

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
    // TODO: deal with modules in the same reactor
    auto jar = cavan::path_of(p->grp, p->art, p->ver, "jar");

    stmt.reset();
    stmt.bind(1, fname);
    stmt.bind(2, jar);
    stmt.step();

    res.push_back(traits::move(jar));
  }
  return res;
}

int main(int argc, char ** argv) try {
  const auto shift = [&] { return jute::view::unsafe(argc == 1 ? "" : (--argc, *++argv)); };

  auto file = shift();
  if (file == "") die("missing java source file");

  auto root = root_of(file);

  // TODO: equivalent for tests
  auto gen_path = (root + "/target/generated-sources/annotations").cstr();
  auto out_path = (root + "/target/classes").cstr();

  auto jars = resolve_deps(root + "/pom.xml");
  unsigned sz = 0;
  for (auto & p : jars) sz += p.size() + 1;
  hai::cstr cp { sz };
  auto cpp = cp.begin();
  for (auto & p : jars) {
    *cpp++ = ':';
    for (auto c : p) *cpp++ = c;
  }

  const char * args[] {
    "java",
    "-s",
    gen_path.begin(),
    "-d",
    out_path.begin(),
    "-source",
    "21",
    "-cp",
    cp.begin(),
    file.begin(),
    0,
  };
  return sysstd::spawn("javac", args);
} catch (...) {
  return 3;
}

