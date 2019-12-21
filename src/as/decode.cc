export module as.decode;

import "util/check.h";
import as.ast;

export instruction decode(std::span<std::int64_t> memory) {
  auto op = memory[0] % 100;
}
