export module as.encode;

import <iomanip>;
import <iostream>;
import <map>;
import <span>;
import <variant>;
import <vector>;
import as.ast;

namespace as {

template <typename... Ts>
struct overload : Ts... { using Ts::operator()...; };
template <typename... Ts>
overload(Ts...) -> overload<Ts...>;

int size(const instruction& i) {
  return std::visit(overload{
    [](const literal&) { return 1; },
    [](const add&) { return 4; },
    [](const mul&) { return 4; },
    [](const less_than&) { return 4; },
    [](const equals&) { return 4; },
    [](const jump_if_true&) { return 3; },
    [](const jump_if_false&) { return 3; },
    [](const input&) { return 2; },
    [](const output&) { return 2; },
    [](const adjust_relative_base&) { return 2; },
    [](const halt&) { return 1; },
  }, i);
}

std::int64_t mode(const input_param& i) {
  return std::visit(overload{
    [](address) { return 0; },
    [](immediate) { return 1; },
    [](relative) { return 2; },
  }, i.input);
}

std::int64_t mode(const output_param& o) {
  return std::visit(overload{
    [](address) { return 0; },
    [](relative) { return 2; },
  }, o.output);
}

std::int64_t mode(const instruction& i) {
  return std::visit(overload{
    [](const literal&) -> std::int64_t { return 0; },
    [](const calculation& c) {
      return mode(c.a) + 10 * mode(c.b) + 100 * mode(c.out);
    },
    [](input i) { return mode(i.out); },
    [](output o) { return mode(o.x); },
    [](const jump& j) { return mode(j.condition) + 10 * mode(j.target); },
    [](adjust_relative_base a) { return mode(a.amount); },
    [](halt) -> std::int64_t { return 0; },
  }, i);
}

std::int64_t opcode(const instruction& i) {
  auto code = std::visit(overload{
    [](const literal& l) { return l.value; },
    [](const add&) -> std::int64_t { return 1; },
    [](const mul&) -> std::int64_t { return 2; },
    [](const input&) -> std::int64_t { return 3; },
    [](const output&) -> std::int64_t { return 4; },
    [](const jump_if_true&) -> std::int64_t { return 5; },
    [](const jump_if_false&) -> std::int64_t { return 6; },
    [](const less_than&) -> std::int64_t { return 7; },
    [](const equals&) -> std::int64_t { return 8; },
    [](const adjust_relative_base&) -> std::int64_t { return 9; },
    [](const halt&) -> std::int64_t { return 99; },
  }, i);
  return 100 * mode(i) + code;
}

std::int64_t immediate_value(const immediate& i) {
  return std::visit(overload{
    [](literal l) { return l.value; },
    [](name n) -> std::int64_t {
      std::cerr << "Unresolved immediate " << std::quoted(n.value) << ".\n";
      std::abort();
    },
  }, i);
}

std::int64_t param_value(const input_param& i) {
  return std::visit(overload{
    [](address a) { return immediate_value(a.value); },
    [](immediate i) { return immediate_value(i); },
    [](relative r) { return immediate_value(r.value); },
  }, i.input);
}

std::int64_t param_value(const output_param& o) {
  return std::visit(overload{
    [](address a) { return immediate_value(a.value); },
    [](relative r) { return immediate_value(r.value); },
  }, o.output);
}

void encode(std::vector<std::int64_t>& buffer, const instruction& i) {
  buffer.push_back(opcode(i));
  std::visit(overload{
    [](const literal&) {},
    [&](const calculation& c) {
      buffer.push_back(param_value(c.a));
      buffer.push_back(param_value(c.b));
      buffer.push_back(param_value(c.out));
    },
    [&](const input& i) {
      buffer.push_back(param_value(i.out));
    },
    [&](const output& o) {
      buffer.push_back(param_value(o.x));
    },
    [&](const jump& j) {
      buffer.push_back(param_value(j.condition));
      buffer.push_back(param_value(j.target));
    },
    [&](const adjust_relative_base& a) {
      buffer.push_back(param_value(a.amount));
    },
    [](halt) {},
  }, i);
}

template <typename Visitor>
void visit_params(Visitor&& visitor, const instruction& i) {
  std::visit(overload{
    [&](const literal&) {},
    [&](const calculation& c) {
      visitor(c.a, 1); visitor(c.b, 2); visitor(c.out, 3);
    },
    [&](const input& i) { visitor(i.out, 1); },
    [&](const output& o) { visitor(o.x, 1); },
    [&](const jump& j) { visitor(j.condition, 1); visitor(j.target, 2); },
    [&](const adjust_relative_base& a) { visitor(a.amount, 1); },
    [&](halt) {},
  }, i);
}

struct environment {
  std::map<std::string, std::int64_t> constants;
  std::map<std::string, input_param> macros;

  environment(std::span<const statement> input) {
    auto set = [](auto& output, std::string name, auto value) {
      auto [i, done] = output.emplace(name, value);
      if (!done) {
        std::cerr << "Duplicate definition for " << std::quoted(name) << ".\n";
        std::exit(1);
      }
    };
    std::int64_t offset = 0;
    for (const auto& statement : input) {
      std::visit(overload{
        [&](const label& l) { set(constants, l.name, offset); },
        [&](const instruction& i) {
          visit_params([&](const auto& param, int index) {
            if (param.label) set(constants, *param.label, offset + index);
          }, i);
          offset += size(i);
        },
        [&](const directive& d) {
          std::visit(overload{
            [&](const define& d) { set(macros, d.name, d.value); },
            [&](const integer&) { offset++; },
            [&](const ascii& a) { offset += a.value.size() + 1; },
          }, d);
        },
      }, statement);
    }
  }

  void resolve(immediate& x) const {
    std::visit(overload{
      [](literal&) {},
      [&](name& n) {
        if (auto i = constants.find(n.value); i != constants.end()) {
          x = literal{i->second};
        } else {
          std::cerr << "Undefined name " << std::quoted(n.value) << ".\n";
          std::exit(1);
        }
      },
    }, x);
  }

  void resolve(input_param& i) const {
    std::visit(overload{
      [&](address& a) { resolve(a.value); },
      [&](immediate& i) { resolve(i); },
      [&](relative& r) { resolve(r.value); },
    }, i.input);
  }

  void resolve(output_param& o) const {
    std::visit(overload{
      [&](address& a) { resolve(a.value); },
      [&](relative& r) { resolve(r.value); },
    }, o.output);
  }

  void resolve(instruction& i) const {
    std::visit(overload{
      [&](literal&) {},
      [&](calculation& c) { resolve(c.a); resolve(c.b); resolve(c.out); },
      [&](input& i) { resolve(i.out); },
      [&](output& o) { resolve(o.x); },
      [&](jump& j) { resolve(j.condition); resolve(j.target); },
      [&](adjust_relative_base& a) { resolve(a.amount); },
      [](halt) {},
    }, i);
  }

  void resolve(integer& i) const { resolve(i.value); }
};

export std::vector<std::int64_t> encode(std::span<const statement> input) {
  environment environment(input);
  std::vector<std::int64_t> output;
  for (const auto& statement : input) {
    std::visit(overload{
      [&](const label&) {},
      [&](const instruction& i) {
        instruction temp = i;
        environment.resolve(temp);
        encode(output, temp);
      },
      [&](const directive& d) {
        std::visit(overload{
          [&](const define&) {},
          [&](const integer& i) {
            integer x = i;
            environment.resolve(x);
            output.push_back(immediate_value(x.value));
          },
          [&](const ascii& a) {
            std::copy(a.value.begin(), a.value.end() + 1,
                      std::back_inserter(output));
          },
        }, d);
      },
    }, statement);
  }
  return output;
}

}  // namespace as
