export module as.ast;

import <cstdint>;
import <iomanip>;
import <iostream>;
import <optional>;
import <string>;
import <string_view>;
import <variant>;

namespace as {

export struct literal { std::int64_t value; };
export struct name { std::string value; };
export using immediate = std::variant<literal, name>;
export struct address { immediate value; };
export struct relative { immediate value; };
export struct output_param {
  std::optional<std::string> label;
  std::variant<address, relative> output;
};
export struct input_param {
  using type = std::variant<address, immediate, relative>;
  std::optional<std::string> label;
  type input;

  input_param() = default;
  input_param(std::optional<std::string> label, type input)
      : label(std::move(label)), input(std::move(input)) {}
  input_param(const output_param& o)
      : label(o.label),
        input(std::visit([](auto&& x) { return type{x}; }, o.output)) {}
  input_param(output_param&& o)
      : label(std::move(o.label)),
        input(std::visit([](auto&& x) {
                return type{std::move(x)};
              }, o.output)) {}
};
export struct calculation { input_param a, b; output_param out; };
export struct add : calculation {};
export struct mul : calculation {};
export struct less_than : calculation {};
export struct equals : calculation {};
export struct input { output_param out; };
export struct output { input_param x; };
export struct jump { input_param condition, target; };
export struct jump_if_true : jump {};
export struct jump_if_false : jump {};
export struct adjust_relative_base { input_param amount; };
export struct halt {};
export using instruction = std::variant<literal, add, mul, input, output,
                                        jump_if_true, jump_if_false, less_than,
                                        equals, adjust_relative_base, halt>;
export struct label { std::string name; };
export struct define { std::string name; input_param value; };
export struct integer { immediate value; };
export struct ascii { std::string value; };
export using directive = std::variant<define, integer, ascii>;
export using statement = std::variant<label, instruction, directive>;

export std::ostream& operator<<(std::ostream& output, literal l) {
  return output << l.value;
}

export std::ostream& operator<<(std::ostream& output, name n) {
  return output << n.value;
}

export std::ostream& operator<<(std::ostream& output, immediate i) {
  std::visit([&](auto x) { output << x; }, i);
  return output;
}

export std::ostream& operator<<(std::ostream& output, address a) {
  return output << "*" << a.value;
}

export std::ostream& operator<<(std::ostream& output, relative r) {
  return output << "base[" << r.value << "]";
}

export std::ostream& operator<<(std::ostream& output, input_param i) {
  std::visit([&](auto x) { output << x; }, i.input);
  if (i.label) output << " @ " << *i.label;
  return output;
}

export std::ostream& operator<<(std::ostream& output, output_param o) {
  std::visit([&](auto x) { output << x; }, o.output);
  if (o.label) output << " @ " << *o.label;
  return output;
}

export std::ostream& operator<<(std::ostream& output, const calculation& c) {
  return output << c.a << ", " << c.b << ", " << c.out;
}

export std::ostream& operator<<(std::ostream& output, const add& a) {
  return output << "add " << (calculation)a;
}

export std::ostream& operator<<(std::ostream& output, const mul& m) {
  return output << "mul " << (calculation)m;
}

export std::ostream& operator<<(std::ostream& output, const less_than& l) {
  return output << "lt " << (calculation)l;
}

export std::ostream& operator<<(std::ostream& output, const equals& e) {
  return output << "eq " << (calculation)e;
}

export std::ostream& operator<<(std::ostream& output, input i) {
  return output << "in " << i.out;
}

export std::ostream& operator<<(std::ostream& output, struct output o) {
  return output << "out " << o.x;
}

export std::ostream& operator<<(std::ostream& output, const jump& j) {
  return output << j.condition << ", " << j.target;
}

export std::ostream& operator<<(std::ostream& output, const jump_if_true& j) {
  return output << "jnz " << (jump)j;
}

export std::ostream& operator<<(std::ostream& output, const jump_if_false& j) {
  return output << "jz " << (jump)j;
}

export std::ostream& operator<<(std::ostream& output, adjust_relative_base a) {
  return output << "arb " << a.amount;
}

export std::ostream& operator<<(std::ostream& output, halt) {
  return output << "halt";
}

export std::ostream& operator<<(std::ostream& output, const instruction& i) {
  std::visit([&](const auto& x) { output << x; }, i);
  return output;
}

export std::ostream& operator<<(std::ostream& output, label l) {
  return output << l.name << ':';
}

export std::ostream& operator<<(std::ostream& output, const define& d) {
  return output << ".define " << d.name << " " << d.value;
}

export std::ostream& operator<<(std::ostream& output, integer i) {
  return output << ".int " << i.value;
}

export std::ostream& operator<<(std::ostream& output, const ascii& a) {
  return output << ".ascii " << std::quoted(a.value);
}

export std::ostream& operator<<(std::ostream& output, const directive& d) {
  std::visit([&](const auto& x) { output << x; }, d);
  return output;
}

template <typename... Ts> struct overload : Ts... { using Ts::operator()...; };
template <typename... Ts> overload(Ts...) -> overload<Ts...>;

export std::ostream& operator<<(std::ostream& output, const statement& s) {
  std::visit(overload{
    [&](const label& l) { output << l; },
    [&](const auto& x) { output << "  " << x; },
  }, s);
  return output;
}

}  // namespace as
