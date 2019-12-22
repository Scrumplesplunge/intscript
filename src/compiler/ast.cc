export module compiler.ast;

import <cstdint>;
import <iostream>;
import <iomanip>;
import <span>;
import <variant>;
import <vector>;
import <string_view>;
import util.value_ptr;

template <typename... Ts>
struct overload : Ts... { using Ts::operator()...; };
template <typename... Ts>
overload(Ts...) -> overload<Ts...>;

export using literal = std::variant<std::int64_t, std::string>;
export struct name;
export struct call;
export struct add;
export struct sub;
export struct mul;
export struct less_than;
export struct equals;
export struct input;
export struct base;
export struct read;
export struct expression {
  using type = std::variant<literal, name, call, add, sub, mul, less_than,
                            equals, input, read>;
  value_ptr<type> value;

  template <typename T> static expression wrap(T&& value) {
    return expression{std::make_unique<expression::type>(
        std::forward<T>(value))};
  }
};

export struct name { std::string value; };
export struct call { expression function; std::vector<expression> arguments; };
export struct calculation { expression a, b; };
export struct add : calculation {};
export struct sub : calculation {};
export struct mul : calculation {};
export struct less_than : calculation {};
export struct equals : calculation {};
export struct input {};
export struct base {};
export struct read { expression address; };

export expression logical_not(expression x) {
  return expression::wrap(equals{{std::move(x), expression::wrap(literal{0})}});
}

export expression greater_than(expression l, expression r) {
  return expression::wrap(less_than{{std::move(r), std::move(l)}});
}

export expression less_or_equal(expression l, expression r) {
  return logical_not(greater_than(std::move(l), std::move(r)));
}

export expression greater_or_equal(expression l, expression r) {
  return logical_not(expression::wrap(less_than{{std::move(l), std::move(r)}}));
}

export expression not_equals(expression l, expression r) {
  return logical_not(expression::wrap(equals{{std::move(l), std::move(r)}}));
}

export bool is_lvalue(const expression& e) {
  return std::visit(overload{
    [](const literal&) { return false; },
    [](const name&) { return true; },
    [](const call&) { return false; },
    [](const calculation&) { return false; },
    [](const input&) { return false; },
    [](const read&) { return true; },
  }, *e.value);
}

export struct constant;
export struct declare;
export struct assign;
export struct if_statement;
export struct while_statement;
export struct output_statement;
export struct return_statement;
export struct break_statement;
export struct continue_statement;
export struct statement {
  using type = std::variant<constant, call, declare, assign, if_statement,
                            while_statement, output_statement, return_statement,
                            break_statement, continue_statement>;
  value_ptr<type> value;

  template <typename T>
  static statement wrap(T&& value) {
    return statement{make_value<type>(std::forward<T>(value))};
  }
};

export struct constant { std::string name; expression value; };
export struct declare { std::string name; };
export struct assign { expression left, right; };
export struct if_statement {
  expression condition;
  std::vector<statement> then_branch, else_branch;
};
export struct while_statement {
  expression condition;
  std::vector<statement> body;
};
export struct output_statement { expression value; };
export struct return_statement { expression value; };
export struct break_statement {};
export struct continue_statement {};

export std::ostream& operator<<(std::ostream&, const expression&);

export std::ostream& operator<<(std::ostream& output, literal l) {
  std::visit(overload{
    [&](std::int64_t value) { output << value; },
    [&](std::string_view value) { output << std::quoted(value); },
  }, l);
  return output;
}

export std::ostream& operator<<(std::ostream& output, name n) {
  return output << n.value;
}

export std::ostream& operator<<(std::ostream& output, const call& c) {
  output << c.function << "(";
  bool first = true;
  for (const auto& argument : c.arguments) {
    if (first) {
      first = false;
    } else {
      output << ", ";
    }
    output << argument;
  }
  return output << ")";
}

std::ostream& print(std::ostream& output, std::string_view op,
                    const calculation& c) {
  return output << "(" << c.a << " " << op << " " << c.b << ")";
}

export std::ostream& operator<<(std::ostream& output, const add& a) {
  return print(output, "+", a);
}

export std::ostream& operator<<(std::ostream& output, const sub& s) {
  return print(output, "-", s);
}

export std::ostream& operator<<(std::ostream& output, const mul& m) {
  return print(output, "*", m);
}

export std::ostream& operator<<(std::ostream& output, const less_than& l) {
  return print(output, "<", l);
}

export std::ostream& operator<<(std::ostream& output, const equals& e) {
  return print(output, "==", e);
}

export std::ostream& operator<<(std::ostream& output, const input&) {
  return output << "input";
}

export std::ostream& operator<<(std::ostream& output, const read& r) {
  return output << "*" << r.address;
}

export std::ostream& operator<<(std::ostream& output, const expression& e) {
  std::visit([&](const auto& x) { output << x; }, *e.value);
  return output;
}

export std::ostream& print(std::ostream&, const statement&, int indent);

export std::ostream& print(
    std::ostream& output, std::span<const statement> statements, int indent) {
  bool first = true;
  for (const auto& statement : statements) {
    if (first) {
      first = false;
    } else {
      output << '\n' << std::string(indent, ' ');
    }
    print(output, statement, indent);
  }
  return output;
}

export std::ostream& print(std::ostream& output, const constant& c, int) {
  return output << "const " << c.name << " = " << c.value << ';';
}

export std::ostream& print(std::ostream& output, const call& c, int) {
  return output << c << ';';
}

export std::ostream& print(std::ostream& output, const declare& d, int) {
  return output << "var " << d.name << ';';
}

export std::ostream& print(std::ostream& output, const assign& a, int) {
  return output << a.left << " = " << a.right << ';';
}

export std::ostream& print(
    std::ostream& output, const if_statement& i, int indent) {
  output << "if " << i.condition << " {";
  if (!i.then_branch.empty()) {
    output << "\n" << std::string(indent + 2, ' ');
    print(output, i.then_branch, indent + 2);
  }
  output << '\n' << std::string(indent, ' ') << '}';
  if (!i.else_branch.empty()) {
    output << " else {\n" << std::string(indent + 2, ' ');
    print(output, i.else_branch, indent + 2);
    output << '\n' << std::string(indent, ' ') << '}';
  }
  return output;
}

export std::ostream& print(
    std::ostream& output, const while_statement& w, int indent) {
  output << "while " << w.condition << " {";
  if (!w.body.empty()) {
    output << "\n" << std::string(indent + 2, ' ');
    print(output, w.body, indent + 2);
  }
  output << '\n' << std::string(indent, ' ') << '}';
  return output;
}

export std::ostream& print(
    std::ostream& output, const output_statement& o, int) {
  return output << "output " << o.value << ';';
}

export std::ostream& print(
    std::ostream& output, const return_statement& o, int) {
  return output << "return " << o.value << ';';
}

export std::ostream& print(std::ostream& output, const break_statement&, int) {
  return output << "break;";
}

export std::ostream& print(
    std::ostream& output, const continue_statement&, int) {
  return output << "continue;";
}

export std::ostream& print(
    std::ostream& output, const statement& s, int indent) {
  std::visit([&](const auto& x) { print(output, x, indent); }, *s.value);
  return output;
}

export std::ostream& operator<<(std::ostream& output, const declare& d) {
  return print(output, d, 0);
}

export std::ostream& operator<<(std::ostream& output, const assign& a) {
  return print(output, a, 0);
}

export std::ostream& operator<<(std::ostream& output, const if_statement& i) {
  return print(output, i, 0);
}

export std::ostream& operator<<(
    std::ostream& output, const while_statement& w) {
  return print(output, w, 0);
}

export std::ostream& operator<<(
    std::ostream& output, const output_statement& o) {
  return print(output, o, 0);
}

export std::ostream& operator<<(
    std::ostream& output, const return_statement& r) {
  return print(output, r, 0);
}

export std::ostream& operator<<(
    std::ostream& output, const break_statement& b) {
  return print(output, b, 0);
}

export std::ostream& operator<<(
    std::ostream& output, const continue_statement& c) {
  return print(output, c, 0);
}

export std::ostream& operator<<(
    std::ostream& output, const statement& s) {
  return print(output, s, 0);
}

export std::ostream& operator<<(
    std::ostream& output, std::span<const statement> statements) {
  return print(output, statements, 0);
}
