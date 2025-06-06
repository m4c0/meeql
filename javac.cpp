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

int main(int argc, char ** argv) try {
  const auto shift = [&] { return jute::view::unsafe(argc == 1 ? "" : (--argc, *++argv)); };

  bool use_cache = true;
  auto file = shift();
  if (file == "") die("missing java source file");

  if (file == "-r") {
    use_cache = false;
    file = shift();
  }

  hai::cstr realpath { 10240 };
  sysstd::fullpath(file.begin(), realpath.begin(), realpath.size());

  auto [root, test] = root_of(jute::view::unsafe(realpath.begin()));
  auto tp = test ? jute::view{"test-"} : jute::view{};

  auto gen_path = (root + "/target/generated-" + tp + "sources/annotations").cstr();
  auto out_path = (root + "/target/" + tp + "classes").cstr();

  auto cp = meeql::resolve_classpath(file.begin(), use_cache);

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

