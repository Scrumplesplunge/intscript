module;

#include <cassert>

export module intcode;

import "../util/check.h";
import util.io;
import <array>;
import <charconv>;  // bug
import <optional>;  // bug
import <span>;
import <unordered_map>;
import <variant>;
import as.ast;

using value_type = std::int64_t;

enum class mode : unsigned char {
  position = 0,
  immediate = 1,
  relative = 2,
};

constexpr bool is_mode(int x) { return 0 <= x && x <= 2; }

enum class opcode : unsigned char {
  illegal = 0,
  add = 1,
  mul = 2,
  input = 3,
  output = 4,
  jump_if_true = 5,
  jump_if_false = 6,
  less_than = 7,
  equals = 8,
  adjust_relative_base = 9,
  halt = 99,
};

constexpr bool is_opcode(int x) { return (1 <= x && x <= 9) || x == 99; }

constexpr int op_size(opcode o) {
  switch (o) {
    case opcode::illegal: return -1;
    case opcode::add:
    case opcode::mul:
    case opcode::less_than:
    case opcode::equals:
      return 4;
    case opcode::jump_if_true:
    case opcode::jump_if_false:
      return 3;
    case opcode::input:
    case opcode::output:
    case opcode::adjust_relative_base:
      return 2;
    case opcode::halt:
      return 1;
  }
}

struct op {
  opcode code = opcode::illegal;
  mode params[3];
};

constexpr auto ops = [] {
  constexpr auto parse_op = [](int x) -> op {
    op result{};
    int code = x % 100;
    if (!is_opcode(code)) return {};
    result.code = opcode(code);
    x /= 100;
    for (int i = 0; i < 3; i++) {
      if (!is_mode(x % 10)) return {};
      result.params[i] = mode(x % 10);
      x /= 10;
    }
    // If this check fails, we have more parameter modes than any opcode uses.
    if (x != 0) return {};
    switch (result.code) {
      case opcode::add:
      case opcode::mul:
      case opcode::less_than:
      case opcode::equals:
        if (result.params[2] == mode::immediate) return {};
        break;
      case opcode::input:
        if (result.params[0] == mode::immediate) return {};
        break;
      case opcode::adjust_relative_base:
      case opcode::illegal:
      case opcode::halt:
      case opcode::jump_if_false:
      case opcode::jump_if_true:
      case opcode::output:
        break;
    }
    return result;
  };
  std::array<op, 29999> ops = {};
  for (int i = 0, n = ops.size(); i < n; i++) ops[i] = parse_op(i);
  return ops;
}();

op decode_op(value_type x) {
  check(0 <= x && x < (int)ops.size());
  return ops[x];
}

class memory {
 public:
  value_type& operator[](value_type index) {
    return chunks_[index / chunk_size][index % chunk_size];
  }

  value_type& at(value_type index) {
    return chunks_.at(index / chunk_size)[index % chunk_size];
  }

  const value_type& at(value_type index) const {
    return chunks_.at(index / chunk_size)[index % chunk_size];
  }

  static as::input_param decode_input(mode m, std::int64_t arg) {
    switch (m) {
      case mode::position: return {{}, as::address{as::literal{arg}}};
      case mode::immediate: return {{}, as::immediate{as::literal{arg}}};
      case mode::relative: return {{}, as::relative{as::literal{arg}}};
    }
  }

  static as::output_param decode_output(mode m, std::int64_t arg) {
    switch (m) {
      case mode::position: return {{}, as::address{as::literal{arg}}};
      case mode::immediate: return {};
      case mode::relative: return {{}, as::relative{as::literal{arg}}};
    }
  }

  as::calculation decode_calculation(std::int64_t pc, mode a, mode b, mode c) {
    return {decode_input(a, (*this)[pc + 1]), decode_input(b, (*this)[pc + 2]),
            decode_output(c, (*this)[pc + 3])};
  }

  as::jump decode_jump(std::int64_t pc, mode condition, mode target) {
    return {decode_input(condition, (*this)[pc + 1]),
            decode_input(target, (*this)[pc + 2])};
  }

  as::instruction decode(std::int64_t pc) {
    auto op = opcode((*this)[pc] % 100);
    auto a = mode((*this)[pc] / 100 % 10);
    auto b = mode((*this)[pc] / 1000 % 10);
    auto c = mode((*this)[pc] / 10000 % 10);
    switch (op) {
      case opcode::add: return as::add{decode_calculation(pc, a, b, c)};
      case opcode::mul: return as::mul{decode_calculation(pc, a, b, c)};
      case opcode::input: return as::input{decode_output(a, (*this)[pc + 1])};
      case opcode::output: return as::output{decode_input(a, (*this)[pc + 1])};
      case opcode::jump_if_true: return as::jump_if_true{decode_jump(pc, a, b)};
      case opcode::jump_if_false:
        return as::jump_if_false{decode_jump(pc, a, b)};
      case opcode::less_than:
        return as::less_than{decode_calculation(pc, a, b, c)};
      case opcode::equals: return as::equals{decode_calculation(pc, a, b, c)};
      case opcode::adjust_relative_base:
        return as::adjust_relative_base{decode_input(a, (*this)[pc + 1])};
      case opcode::halt: return as::halt{};
      default: return as::literal{(*this)[pc]};
    }
  }

 private:
  static constexpr int chunk_size = 1024;
  std::unordered_map<value_type, std::array<value_type, chunk_size>> chunks_;
};

export class program {
 public:
  static constexpr int max_size = 5000;
  using value_type = ::value_type;
  using buffer = std::array<value_type, max_size>;
  using span = std::span<value_type>;
  using const_span = std::span<const value_type>;

  static span load(std::string_view source, span buffer) {
    check(!buffer.empty());
    scanner scanner(source);
    (scanner >> buffer[0]).check_ok();
    unsigned n = 1;
    while (!scanner.done()) {
      check(n < buffer.size());
      (scanner >> exact(",") >> buffer[n++]).check_ok();
    }
    return buffer.first(n);
  }

  program() = default;

  explicit program(const_span source, bool debug = false)
      : debug_(debug) {
    for (value_type i = 0, n = source.size(); i < n; i++) {
      memory_[i] = source[i];
    }
  }

  enum state {
    ready,
    waiting_for_input,
    output,
    halt,
  };

  bool done() const { return state_ == halt; }

  void provide_input(value_type x) {
    check(state_ == waiting_for_input);
    state_ = ready;
    memory_[input_address_] = x;
    pc_ += 2;
  }

  value_type get_output() {
    check(state_ == output);
    state_ = ready;
    pc_ += 2;
    return output_;
  }

  state resume() {
    check(state_ == ready);
    while (true) {
      const auto op = decode_op(memory_[pc_]);
      auto get = [&](int param_index) {
        value_type x = memory_[pc_ + param_index + 1];
        switch (op.params[param_index]) {
          case mode::position: return memory_[x];
          case mode::immediate: return x;
          case mode::relative: return memory_[relative_base_ + x];
        }
        assert(false);
      };
      auto put = [&](int param_index, value_type value) {
        value_type x = memory_[pc_ + param_index + 1];
        switch (op.params[param_index]) {
          case mode::position: memory_[x] = value; return;
          case mode::immediate: std::abort();
          case mode::relative: memory_[relative_base_ + x] = value; return;
        }
      };
      if (debug_) std::cerr << memory_.decode(pc_) << '\n';
      switch (op.code) {
        case opcode::illegal:
          std::cerr << "illegal instruction " << memory_[pc_]
                    << " at pc_=" << pc_ << "\n";
          std::abort();
        case opcode::add:
          put(2, get(0) + get(1));
          pc_ += 4;
          break;
        case opcode::mul:
          put(2, get(0) * get(1));
          pc_ += 4;
          break;
        case opcode::input:
          switch (op.params[0]) {
            case mode::position:
              input_address_ = memory_[pc_ + 1];
              break;
            case mode::immediate:
              std::abort();
            case mode::relative:
              input_address_ = relative_base_ + memory_[pc_ + 1];
              break;
          }
          return state_ = waiting_for_input;
        case opcode::output:
          output_ = get(0);
          return state_ = output;
        case opcode::jump_if_true:
          pc_ = get(0) ? get(1) : pc_ + 3;
          break;
        case opcode::jump_if_false:
          pc_ = get(0) ? pc_ + 3 : get(1);
          break;
        case opcode::less_than:
          put(2, get(0) < get(1));
          pc_ += 4;
          break;
        case opcode::equals:
          put(2, get(0) == get(1));
          pc_ += 4;
          break;
        case opcode::adjust_relative_base:
          relative_base_ += get(0);
          pc_ += 2;
          break;
        case opcode::halt:
          return state_ = halt;
        default:
          std::cerr << "illegal instruction " << memory_[pc_]
                    << " at pc_=" << pc_ << "\n";
          std::abort();
      }
    }
  }

  span run(const_span input, span output) {
    unsigned output_size = 0;
    while (true) {
      switch (resume()) {
        case state::ready:
          continue;
        case state::waiting_for_input:
          check(!input.empty());
          provide_input(input.front());
          input = input.subspan(1);
          break;
        case state::output:
          check(output_size < output.size());
          output[output_size++] = get_output();
          break;
        case state::halt:
          return output.subspan(0, output_size);
      }
    }
  }

 private:
  const bool debug_ = false;
  state state_ = ready;
  value_type pc_ = 0, input_address_ = 0, output_ = 0, relative_base_ = 0;
  memory memory_;
};
