export module compiler.ast;

import <cstdint>;
import <filesystem>;
import <iostream>;
import <iomanip>;
import <span>;
import <variant>;
import <vector>;
import <string_view>;
import util.value_ptr;

namespace compiler {

template <typename... Ts>
struct overload : Ts... { using Ts::operator()...; };
template <typename... Ts>
overload(Ts...) -> overload<Ts...>;

export using literal = std::variant<std::int64_t, std::string>;
export struct name;
export struct qualified_name { std::vector<std::string> parts; };
export struct call;
export struct add;
export struct sub;
export struct mul;
export struct less_than;
export struct equals;
export struct input;
export struct read;
export struct logical_and;
export struct logical_or;
export struct expression {
  using type = std::variant<literal, name, call, add, sub, mul, less_than,
                            equals, input, read, logical_and, logical_or>;
  value_ptr<type> value;

  template <typename T> static expression wrap(T&& value) {
    return expression{std::make_unique<expression::type>(
        std::forward<T>(value))};
  }
};

export struct name { std::string value; };
export struct call { expression function; std::vector<expression> arguments; };
export struct calculation { expression left, right; };
export struct add : calculation {};
export struct sub : calculation {};
export struct mul : calculation {};
export struct less_than : calculation {};
export struct equals : calculation {};
export struct logical_and : calculation {};
export struct logical_or : calculation {};
export struct input {};
export struct read { expression address; };

export expression negate(expression x) {
  return expression::wrap(mul{{std::move(x), expression::wrap(literal{-1})}});
}

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
export struct declare_scalar;
export struct declare_array;
export struct assign;
export struct add_assign;
export struct mod_assign;
export struct if_statement;
export struct while_statement;
export struct output_statement;
export struct return_statement;
export struct break_statement;
export struct continue_statement;
export struct halt_statement;
export struct statement {
  using type = std::variant<constant, call, declare_scalar, declare_array,
                            assign, add_assign, if_statement, while_statement,
                            output_statement, return_statement, break_statement,
                            continue_statement, halt_statement>;
  value_ptr<type> value;

  template <typename T>
  static statement wrap(T&& value) {
    return statement{make_value<type>(std::forward<T>(value))};
  }
};

export struct constant { std::string name; expression value; };
export struct declare_scalar { std::string name; };
export struct declare_array { std::string name; expression size; };
export struct assign { expression left, right; };
export struct add_assign { expression left, right; };
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
export struct halt_statement {};

export struct function_definition {
  std::string name;
  std::vector<std::string> parameters;
  std::vector<statement> body;
};
export struct declaration {
  using type = std::variant<constant, declare_scalar, declare_array,
                            function_definition>;
  value_ptr<type> value;

  template <typename T>
  static declaration wrap(T&& value) {
    return declaration{make_value<type>(std::forward<T>(value))};
  }
};

export struct import_statement {
  std::vector<std::string> name;
  std::filesystem::path resolve(const std::filesystem::path& context) const {
    std::filesystem::path result = context;
    for (const auto& part : name) result /= part;
    result += ".is";
    return result;
  }
};
export struct module {
  std::string name;
  std::filesystem::path context() const {
    return std::filesystem::path(name).parent_path();
  }
  std::vector<import_statement> imports;
  std::vector<declaration> body;
};

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

export std::ostream& operator<<(std::ostream& output, const add& a) {
  return output << "(" << a.left << " + " << a.right << ")";
}

export std::ostream& operator<<(std::ostream& output, const sub& s) {
  return output << "(" << s.left << " - " << s.right << ")";
}

export std::ostream& operator<<(std::ostream& output, const mul& m) {
  return output << "(" << m.left << " * " << m.right << ")";
}

export std::ostream& operator<<(std::ostream& output, const less_than& l) {
  return output << "(" << l.left << " < " << l.right << ")";
}

export std::ostream& operator<<(std::ostream& output, const equals& e) {
  return output << "(" << e.left << " == " << e.right << ")";
}

export std::ostream& operator<<(std::ostream& output, const input&) {
  return output << "input";
}

export std::ostream& operator<<(std::ostream& output, const read& r) {
  return output << "*" << r.address;
}

export std::ostream& operator<<(std::ostream& output, const logical_and& a) {
  return output << "(" << a.left << " && " << a.right << ")";
}

export std::ostream& operator<<(std::ostream& output, const logical_or& o) {
  return output << "(" << o.left << " || " << o.right << ")";
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

export std::ostream& print(std::ostream& output, const declare_scalar& d, int) {
  return output << "var " << d.name << ';';
}

export std::ostream& print(std::ostream& output, const declare_array& d, int) {
  return output << "var " << d.name << '[' << d.size << "];";
}

export std::ostream& print(std::ostream& output, const assign& a, int) {
  return output << a.left << " = " << a.right << ';';
}

export std::ostream& print(std::ostream& output, const add_assign& a, int) {
  return output << a.left << " += " << a.right << ';';
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
    std::ostream& output, const halt_statement&, int) {
  return output << "halt;";
}

export std::ostream& print(
    std::ostream& output, const statement& s, int indent) {
  std::visit([&](const auto& x) { print(output, x, indent); }, *s.value);
  return output;
}

export std::ostream& operator<<(std::ostream& output, const declare_scalar& d) {
  return print(output, d, 0);
}

export std::ostream& operator<<(std::ostream& output, const declare_array& d) {
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

export std::ostream& print(
    std::ostream& output, const function_definition& f, int indent) {
  output << "function " << f.name << "(";
  bool first = true;
  for (const auto& parameter : f.parameters) {
    if (first) {
      first = false;
    } else {
      output << ", ";
    }
    output << parameter;
  }
  output << ") {";
  if (f.parameters.empty()) return output << "}";
  output << "\n" << std::string(indent + 2, ' ');
  print(output, f.body, indent + 2);
  output << "\n" << std::string(indent, ' ') << '}';
  return output;
}

export std::ostream& operator<<(
    std::ostream& output, const function_definition& f) {
  return print(output, f, 0);
}

export std::ostream& print(
    std::ostream& output, const declaration& d, int indent) {
  std::visit([&](const auto& x) { print(output, x, indent); }, *d.value);
  return output;
}

export std::ostream& operator<<(
    std::ostream& output, const declaration& d) {
  return print(output, d, 0);
}

export std::ostream& operator<<(
    std::ostream& output, const import_statement& i) {
  return output << "import ";
  bool first = true;
  for (const auto& part : i.name) {
    if (first) {
      first = false;
    } else {
      output << ".";
    }
    output << part;
  }
  return output;
}

export std::ostream& operator<<(std::ostream& output, const module& m) {
  output << "# module " << m.name << '\n';
  for (auto& i : m.imports) output << i << '\n';
  output << '\n';
  for (auto& d : m.body) output << d << '\n';
  return output;
}

}  // namespace compiler
