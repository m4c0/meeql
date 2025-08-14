#pragma leco tool

import cavan;
import jojo;
import jute;
import hai;
import meeql;
import print;
import sysstd;
import tora;

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

  auto [root, test] = meeql::root_of(jute::view::unsafe(realpath.begin()));
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

