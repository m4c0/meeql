#pragma leco tool

import jute;
import meeql;
import print;

static void find_start(jute::view base) {
  putln(base.index_of(' '));
}
static void list_options(jute::view base) {
  putln("uga");
  putln("buga");
}

int main(int argc, char ** argv) try {
  if (argc != 3) throw 0;

  auto a1 = jute::view::unsafe(argv[1]);
  auto a2 = jute::view::unsafe(argv[2]);

  if (a1 == "1") find_start(a2);
  else list_options(a2);

  return 0;
} catch (...) {
  return 1;
}
