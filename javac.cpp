#pragma leco tool

import cavan;
import jojo;
import jute;
import hai;
import meeql;
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

  struct {
    jute::view root;
    bool test = false;
  } res;

  if (rest != "") {
    auto [l, r] = rest.rsplit('/');
    res.test = r == "test";
    rest = (r == "main" || r == "test") ? l : "";
  }
  if (rest != "") {
    auto [l, r] = rest.rsplit('/');
    rest = (r == "src") ? l : "";
  }
  // TODO: should we deal with sources from generated-sources?
  if (rest == "") die("source file must be inside 'src/main/java' or 'src/test/java'");

  res.root = rest;
  return res;
}
static_assert(root_of("blah/bleh/blih/src/main/java/com/bloh/Bluh.java").root == "blah/bleh/blih");
static_assert(!root_of("blah/bleh/blih/src/main/java/com/bloh/Bluh.java").test);
static_assert(root_of("blah/bleh/blih/src/test/java/com/bloh/Bluh.java").test);

[[nodiscard]] static auto init_db() {
  auto fname = meeql::m2_dir() + "/meeql-javac.sqlite\0";
  tora::db db { fname.begin() }; // TODO: move to .m2
  db.exec(R"(
    CREATE TABLE IF NOT EXISTS rdep (
      s_pom TEXT NOT NULL,
      t_jar TEXT NOT NULL
    ) STRICT
  )");
  return db;
}

[[nodiscard]] static auto resolve_deps(auto fname_) {
  auto fname = fname_.cstr();

  auto db = init_db();

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

  const auto m2dir = meeql::repo_dir();

  stmt = db.prepare("INSERT INTO rdep (s_pom, t_jar) VALUES (?, ?)");
  for (auto i = 0; i < deps.size(); i++) {
    auto p = deps.seek(i);

    hai::cstr cp;

    jute::view fn { p->filename };
    if (fn.starts_with(*m2dir)) {
      cp = cavan::path_of(p->grp, p->art, p->ver, "jar");
    } else {
      // TODO: test variant of this
      cp = (fn.rsplit('/').before + "/target/classes").cstr();
    }

    stmt.reset();
    stmt.bind(1, fname);
    stmt.bind(2, cp);
    stmt.step();

    res.push_back(traits::move(cp));
  }
  return res;
}

int main(int argc, char ** argv) try {
  const auto shift = [&] { return jute::view::unsafe(argc == 1 ? "" : (--argc, *++argv)); };

  auto file = shift();
  if (file == "") die("missing java source file");

  if (file == "-r") {
    file = shift();

    auto db = init_db();
    auto stmt = db.prepare("DELETE FROM rdep WHERE s_pom = ?");
    stmt.bind(1, file);
    stmt.step();
  }

  hai::cstr realpath { 10240 };
  sysstd::fullpath(file.begin(), realpath.begin(), realpath.size());

  auto [root, test] = root_of(jute::view::unsafe(realpath.begin()));
  auto tp = test ? jute::view{"test-"} : jute::view{};

  auto gen_path = (root + "/target/generated-" + tp + "sources/annotations").cstr();
  auto out_path = (root + "/target/" + tp + "classes").cstr();

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
    "javac",
    "-proc:full",
    "-s",
    gen_path.begin(),
    "-d",
    out_path.begin(),
    "-source",
    "21",
    "-cp",
    cp.begin(),
    realpath.begin(),
    0,
  };
  return sysstd::spawn("javac", args);
} catch (...) {
  return 3;
}

