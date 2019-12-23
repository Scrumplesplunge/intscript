module;

#include <cassert>

export module compiler.codegen;

import <map>;
import <string>;
import <iostream>;
import <iomanip>;
import <sstream>;
import <span>;
import <set>;
import <optional>;
import <variant>;
import <vector>;
import as.ast;
import compiler.ast;
import util.value_ptr;

namespace compiler {

template <typename... Ts>
struct overload : Ts... { using Ts::operator()...; };
template <typename... Ts>
overload(Ts...) -> overload<Ts...>;

[[noreturn]] void die(std::string message) {
  std::cerr << "error: " << message << "\n";
  std::abort();  // std::exit(1);
}

struct context {
  std::map<std::string, int> labels;
  std::string label(std::string name) {
    int id = labels[name]++;
    return name + std::to_string(id);
  }

  std::set<std::string> variables;
  std::map<std::string, as::immediate> constants;

  bool has_global(std::string global) const {
    if (variables.contains(global)) return true;
    if (constants.contains(global)) return true;
    return false;
  }

  std::vector<as::statement> text;
  std::vector<as::statement> data;

  context();

  as::immediate eval_expr(const expression& e);

  as::immediate make_string(std::string value) {
    auto address = label("string");
    data.push_back(as::label{address});
    data.push_back(as::directive{as::ascii{std::move(value)}});
    return as::name{address};
  }

  void gen_decl(const constant& c);
  void gen_decl(const declare& d);
  void gen_decl(const function_definition& d);
  void gen_decl(const declaration& d);

  void gen_decls(std::span<const declaration> declarations);
};

struct function_context {
  context* context = nullptr;

  struct environment {
    std::map<std::string, int> variables;
    std::map<std::string, as::immediate> constants;
    std::optional<std::string> break_label, continue_label;
  };
  std::string function_name;
  std::map<std::string, int> arguments = {};
  std::vector<environment> scope = {environment{}};

  enum variable_kind {
    not_found,
    global_constant,
    global_variable,
    local_constant,
    local_variable,
    argument,
  };

  variable_kind lookup(std::string name) const {
    if (arguments.contains(name)) return argument;
    for (int i = scope.size() - 1; i >= 0; i--) {
      if (scope[i].variables.contains(name)) return local_variable;
      if (scope[i].constants.contains(name)) return local_constant;
    }
    if (context->variables.contains(name)) return global_variable;
    if (context->constants.contains(name)) return global_constant;
    return not_found;
  }

  bool has_local(std::string local) const {
    auto kind = lookup(local);
    return kind == local_variable || kind == local_constant;
  }

  int get_local_variable(std::string name) const {
    assert(lookup(name) == local_variable);
    if (auto j = arguments.find(name); j != arguments.end()) return j->second;
    for (int i = scope.size() - 1; i >= 0; i--) {
      if (auto j = scope[i].variables.find(name);
          j != scope[i].variables.end()) {
        return j->second;
      }
    }
    std::ostringstream message;
    message << "Local variable " << std::quoted(name) << " not found.";
    die(message.str());
  }

  as::immediate get_constant(std::string name) const {
    for (int i = scope.size() - 1; i >= 0; i--) {
      if (auto j = scope[i].constants.find(name);
          j != scope[i].constants.end()) {
        return j->second;
      }
    }
    return context->constants.at(name);
  }

  int local_size() {
    int size = 0;
    for (auto& layer : scope) size += layer.variables.size();
    return size;
  }

  void define_variable(std::string variable) {
    assert(!has_local(variable));
    scope.back().variables.emplace(variable, local_size());
  }

  void define_constant(std::string name, as::immediate value) {
    assert(!has_local(name));
    scope.back().constants.emplace(name, value);
  }

  void push_scope() {
    scope.push_back(
        {{}, {}, scope.back().break_label, scope.back().continue_label});
  }
  void pop_scope() { scope.pop_back(); }

  as::output_param gen_addr(const name& n);
  as::output_param gen_addr(const read& r);
  as::output_param gen_addr(const expression& e);
  as::input_param gen_expr(const literal& l);
  as::input_param gen_expr(const name& n);
  as::input_param gen_expr(const call& c);
  as::input_param gen_expr(const add& a);
  as::input_param gen_expr(const mul& m);
  as::input_param gen_expr(const sub& s);
  as::input_param gen_expr(const less_than& l);
  as::input_param gen_expr(const equals& e);
  as::input_param gen_expr(const input&);
  as::input_param gen_expr(const read& r);
  as::input_param gen_expr(const expression& e);
  as::immediate eval_expr(const expression& e);

  void gen_stmt(const constant& c);
  void gen_stmt(const call& c);
  void gen_stmt(const declare& d);
  void gen_stmt(const assign& a);
  void gen_stmt(const if_statement& i);
  void gen_stmt(const while_statement& w);
  void gen_stmt(const output_statement& o);
  void gen_stmt(const return_statement& r);
  void gen_stmt(const break_statement&);
  void gen_stmt(const continue_statement&);
  void gen_stmt(const statement& s);

  void gen_stmts(std::span<const statement> statements);
};

context::context() {
  function_context f{this, "_start"};
  f.scope.back().constants.emplace("main", as::immediate{as::name{"main"}});
  f.gen_stmt(call{expression::wrap(name{"main"}), {}});
  text.push_back(as::instruction{as::halt{}});
}

void context::gen_decl(const constant& c) {
  if (has_global(c.name)) {
    std::ostringstream message;
    message << "Multiple definitions for " << std::quoted(c.name)
            << " at global scope.";
    die(message.str());
  }
  constants.emplace(c.name, eval_expr(c.value));
}

void context::gen_decl(const declare& d) {
  if (has_global(d.name)) {
    std::ostringstream message;
    message << "Multiple definitions for " << std::quoted(d.name)
            << " at global scope.";
    die(message.str());
  }
  data.push_back(as::label{d.name});
  data.push_back(as::integer{as::literal{0}});
  variables.emplace(d.name);
}

void context::gen_decl(const function_definition& d) {
  function_context f{this, d.name};
  for (int i = 0, n = d.parameters.size(); i < n; i++) {
    f.arguments.emplace(d.parameters[i], i - n - 2);
  }
  text.push_back(as::label{d.name});
  f.gen_stmts(d.body);
  f.gen_stmt(return_statement{expression::wrap(literal{0})});
}

void context::gen_decl(const declaration& d) {
  std::visit([&](const auto& x) { gen_decl(x); }, *d.value);
}

void context::gen_decls(std::span<const declaration> declarations) {
  for (const auto& declaration : declarations) gen_decl(declaration);
}

as::immediate context::eval_expr(const expression& e) {
  return std::visit(overload{
    [&](const literal& l) -> as::immediate {
      return std::visit(overload{
        [&](std::int64_t x) -> as::immediate { return as::literal{x}; },
        [&](std::string x) { return make_string(std::move(x)); },
      }, l);
    },
    [&](const name& n) -> as::immediate { return constants.at(n.value); },
    [&](const add& a) -> as::immediate {
      auto l = eval_expr(a.a);
      auto r = eval_expr(a.b);
      auto* x = std::get_if<as::literal>(&l);
      auto* y = std::get_if<as::literal>(&r);
      if (x && y) {
        return as::literal{x->value + y->value};
      } else {
        std::ostringstream message;
        message << "Cannot add " << a.a << " and " << a.b
                << " in a constant expression.";
        die(message.str());
      }
    },
    [&](const sub& s) -> as::immediate {
      auto l = eval_expr(s.a);
      auto r = eval_expr(s.b);
      auto* x = std::get_if<as::literal>(&l);
      auto* y = std::get_if<as::literal>(&r);
      if (x && y) {
        return as::literal{x->value - y->value};
      } else {
        std::ostringstream message;
        message << "Cannot subtract " << s.a << " from " << s.b
                << " in a constant expression.";
        die(message.str());
      }
    },
    [&](const mul& m) -> as::immediate {
      auto l = eval_expr(m.a);
      auto r = eval_expr(m.b);
      auto* x = std::get_if<as::literal>(&l);
      auto* y = std::get_if<as::literal>(&r);
      if (x && y) {
        return as::literal{x->value * y->value};
      } else {
        std::ostringstream message;
        message << "Cannot multiply " << m.a << " and " << m.b
                << " in a constant expression.";
        die(message.str());
      }
    },
    [&](const auto& x) -> as::immediate {
      std::ostringstream message;
      message << "Expression " << x << " is not a constant expression.";
      die(message.str());
    },
  }, *e.value);
}

as::output_param function_context::gen_addr(const name& n) {
  switch (lookup(n.value)) {
    case not_found: {
      std::ostringstream message;
      message << std::quoted(n.value) << " not found in function "
              << std::quoted(function_name) << ".";
      die(message.str());
    }
    case global_constant:
    case local_constant: {
      std::ostringstream message;
      message << "Cannot use constant " << std::quoted(n.value)
              << " as an lvalue in function " << std::quoted(function_name)
              << ".";
      die(message.str());
      break;
    }
    case global_variable:
      return {{}, as::address{as::name{n.value}}};
    case argument:
    case local_variable:
      return {{}, as::relative{as::literal{get_local_variable(n.value)}}};
  }
}

as::output_param function_context::gen_addr(const read& r) {
  auto value = gen_expr(r.address);
  auto label = context->label("read");
  // add 0, <value>, *label
  const auto zero = as::input_param{{}, as::literal{0}};
  const auto out = as::output_param{{}, as::address{as::name{label}}};
  context->text.push_back(as::instruction{as::add{{zero, value, out}}});
  // *0 @ label
  return as::output_param{label, as::address{as::literal{0}}};
}

as::output_param function_context::gen_addr(const expression& e) {
  return std::visit(overload{
    [&](const name& n) { return gen_addr(n); },
    [&](const read& r) { return gen_addr(r); },
    [&](const auto& x) -> as::output_param {
      std::ostringstream message;
      message << "Cannot use expression " << x << " as lvalue in function "
              << std::quoted(function_name) << ".";
      die(message.str());
    },
  }, *e.value);
}

as::input_param function_context::gen_expr(const literal& l) {
  return std::visit(overload{
    [&](std::int64_t x) {
      return as::input_param{{}, as::immediate{as::literal{x}}};
    },
    [&](std::string x) {
      return as::input_param{{}, context->make_string(std::move(x))};
    },
  }, l);
}

as::input_param function_context::gen_expr(const name& n) {
  switch (lookup(n.value)) {
    case not_found: {
      std::ostringstream message;
      message << std::quoted(n.value) << " not found in function "
              << std::quoted(function_name) << ".";
      die(message.str());
    }
    case global_constant:
    case local_constant:
      return {{}, get_constant(n.value)};
    case global_variable:
    case argument:
    case local_variable:
      return gen_addr(n);
  }
}

as::input_param function_context::gen_expr(const call& c) {
  const auto zero = as::input_param{{}, as::literal{0}};
  int start = local_size();
  const int n = c.arguments.size();
  push_scope();
  // Produce each argument.
  for (int i = 0; i < n; i++) {
    define_variable(context->label("$argument"));
    auto param = gen_expr(c.arguments[i]);
    const auto out =
        as::output_param{{}, as::relative{as::literal{start + i}}};
    context->text.push_back(as::instruction{as::add{{zero, param, out}}});
  }
  auto callee = gen_expr(c.function);
  pop_scope();
  // Store the output address.
  auto output_label = context->label("return");
  {
    const auto output_address =
        as::input_param{{}, as::immediate{as::name{output_label}}};
    const auto out =
        as::output_param{{}, as::relative{as::literal{start + n}}};
    context->text.push_back(
        as::instruction{as::add{{zero, output_address, out}}});
  }
  // Store the return address.
  auto return_label = context->label("call");
  {
    const auto return_address =
        as::input_param{{}, as::immediate{as::name{return_label}}};
    const auto out =
        as::output_param{{}, as::relative{as::literal{start + n + 1}}};
    context->text.push_back(
        as::instruction{as::add{{zero, return_address, out}}});
  }
  // Jump into the function.
  const int frame_size = start + n + 2;
  context->text.push_back(
      as::adjust_relative_base{{{}, as::immediate{as::literal{frame_size}}}});
  context->text.push_back(as::instruction{as::jump_if_false{{zero, callee}}});
  context->text.push_back(as::label{return_label});
  context->text.push_back(as::adjust_relative_base{
      {{}, as::immediate{as::literal{-frame_size}}}});
  return as::input_param{output_label, as::immediate{as::literal{0}}};
}

as::input_param function_context::gen_expr(const add& a) {
  auto l = gen_expr(a.a);
  auto r = gen_expr(a.b);
  auto result = context->label("add");
  context->text.push_back(
      as::instruction{as::add{{l, r, {{}, as::address{as::name{result}}}}}});
  return as::input_param{result, as::immediate{as::literal{0}}};
}

as::input_param function_context::gen_expr(const mul& m) {
  auto l = gen_expr(m.a);
  auto r = gen_expr(m.b);
  auto result = context->label("mul");
  context->text.push_back(
      as::instruction{as::mul{{l, r, {{}, as::address{as::name{result}}}}}});
  return as::input_param{result, as::immediate{as::literal{0}}};
}

as::input_param function_context::gen_expr(const sub& s) {
  const auto negated_b =
      expression::wrap(mul{{s.b, expression::wrap(literal{-1})}});
  return gen_expr(add{{s.a, negated_b}});
}

as::input_param function_context::gen_expr(const less_than& l) {
  auto a = gen_expr(l.a);
  auto b = gen_expr(l.b);
  auto result = context->label("mul");
  context->text.push_back(as::instruction{
      as::less_than{{a, b, {{}, as::address{as::name{result}}}}}});
  return as::input_param{result, as::immediate{as::literal{0}}};
}

as::input_param function_context::gen_expr(const equals& e) {
  auto a = gen_expr(e.a);
  auto b = gen_expr(e.b);
  auto result = context->label("mul");
  context->text.push_back(as::instruction{
      as::equals{{a, b, {{}, as::address{as::name{result}}}}}});
  return as::input_param{result, as::immediate{as::literal{0}}};
}

as::input_param function_context::gen_expr(const input&) {
  auto result = context->label("input");
  context->text.push_back(
      as::instruction{as::input{{{}, as::address{as::name{result}}}}});
  return as::input_param{result, as::immediate{as::literal{0}}};
}

as::input_param function_context::gen_expr(const read& r) {
  return gen_addr(r);
};

as::input_param function_context::gen_expr(const expression& e) {
  return std::visit([&](auto& x) { return gen_expr(x); }, *e.value);
}

as::immediate function_context::eval_expr(const expression& e) {
  return std::visit(overload{
    [&](const literal& l) -> as::immediate {
      return std::visit(overload{
        [&](std::int64_t x) -> as::immediate { return as::literal{x}; },
        [&](std::string x) { return context->make_string(std::move(x)); },
      }, l);
    },
    [&](const name& n) -> as::immediate { return get_constant(n.value); },
    [&](const add& a) -> as::immediate {
      auto l = eval_expr(a.a);
      auto r = eval_expr(a.b);
      auto* x = std::get_if<as::literal>(&l);
      auto* y = std::get_if<as::literal>(&r);
      if (x && y) {
        return as::literal{x->value + y->value};
      } else {
        std::ostringstream message;
        message << "Cannot add " << a.a << " and " << a.b
                << " in a constant expression.";
        die(message.str());
      }
    },
    [&](const sub& s) -> as::immediate {
      auto l = eval_expr(s.a);
      auto r = eval_expr(s.b);
      auto* x = std::get_if<as::literal>(&l);
      auto* y = std::get_if<as::literal>(&r);
      if (x && y) {
        return as::literal{x->value - y->value};
      } else {
        std::ostringstream message;
        message << "Cannot subtract " << s.a << " from " << s.b
                << " in a constant expression.";
        die(message.str());
      }
    },
    [&](const mul& m) -> as::immediate {
      auto l = eval_expr(m.a);
      auto r = eval_expr(m.b);
      auto* x = std::get_if<as::literal>(&l);
      auto* y = std::get_if<as::literal>(&r);
      if (x && y) {
        return as::literal{x->value * y->value};
      } else {
        std::ostringstream message;
        message << "Cannot multiply " << m.a << " and " << m.b
                << " in a constant expression.";
        die(message.str());
      }
    },
    [&](auto& x) -> as::immediate {
      std::ostringstream message;
      message << "Expression " << x << " is not a constant expression.";
      die(message.str());
    },
  }, *e.value);
}

void function_context::gen_stmt(const constant& c) {
  if (has_local(c.name)) {
    std::ostringstream message;
    message << "Multiple definitions for " << std::quoted(c.name)
            << " in function " << std::quoted(function_name) << ".";
    die(message.str());
  }
  define_constant(c.name, eval_expr(c.value));
}

void function_context::gen_stmt(const call& c) {
  auto value = gen_expr(c);
  auto self = context->label("ignore");
  const auto zero = as::input_param{{self}, as::immediate{as::literal{0}}};
  const auto out = as::output_param{{}, as::address{as::name{self}}};
  context->text.push_back(as::instruction{as::add{{value, zero, out}}});
}

void function_context::gen_stmt(const declare& d) {
  if (has_local(d.name)) {
    std::ostringstream message;
    message << "Multiple definitions for " << std::quoted(d.name)
            << " in function " << std::quoted(function_name) << ".";
    die(message.str());
  }
  define_variable(d.name);
}

void function_context::gen_stmt(const assign& a) {
  auto value = gen_expr(a.right);
  auto address = gen_addr(a.left);
  context->text.push_back(as::instruction{as::add{{
      {{}, as::immediate{as::literal{0}}}, value, address}}});
}

void function_context::gen_stmt(const if_statement& i) {
  auto condition = gen_expr(i.condition);
  auto end_if = context->label("endif");
  auto else_branch = i.else_branch.empty() ? end_if : context->label("else");
  const auto target = as::input_param{{}, as::immediate{as::name{else_branch}}};
  context->text.push_back(
      as::instruction{as::jump_if_false{{condition, target}}});
  gen_stmts(i.then_branch);
  if (!i.else_branch.empty()) {
    const auto end = as::input_param{{}, as::immediate{as::name{end_if}}};
    context->text.push_back(
        as::instruction{as::jump_if_false{{condition, end}}});
    context->text.push_back(as::label{else_branch});
    gen_stmts(i.else_branch);
  }
  context->text.push_back(as::label{end_if});
}

void function_context::gen_stmt(const while_statement& w) {
  push_scope();
  auto while_start = context->label("whilestart");
  auto while_cond = context->label("whilecond");
  auto while_end = context->label("whileend");
  scope.back().break_label = while_end;
  scope.back().continue_label = while_cond;
  const auto cond = as::input_param{{}, as::immediate{as::name{while_cond}}};
  context->text.push_back(as::instruction{
      as::jump_if_false{{{{}, as::immediate{as::literal{0}}}, cond}}});
  context->text.push_back(as::label{while_start});
  gen_stmts(w.body);
  context->text.push_back(as::label{while_cond});
  auto condition = gen_expr(w.condition);
  const auto start = as::input_param{{}, as::immediate{as::name{while_start}}};
  context->text.push_back(
      as::instruction{as::jump_if_true{{condition, start}}});
  context->text.push_back(as::label{while_end});
  pop_scope();
}

void function_context::gen_stmt(const output_statement& o) {
  auto value = gen_expr(o.value);
  context->text.push_back(as::instruction{as::output{value}});
}

void function_context::gen_stmt(const return_statement& r) {
  // Store the return value at the output address.
  auto output_label = context->label("output");
  const auto zero = as::input_param{{}, as::immediate{as::literal{0}}};
  const auto output_address =
      as::input_param{{}, as::relative{as::literal{-2}}};
  const auto temp = as::output_param{{}, as::address{as::name{output_label}}};
  context->text.push_back(
      as::instruction{as::add{{zero, output_address, temp}}});
  const auto output =
      as::output_param{output_label, as::address{as::literal{0}}};
  auto value = gen_expr(r.value);
  context->text.push_back(as::instruction{as::add{{zero, value, output}}});
  // Return to the caller.
  const auto return_address =
      as::input_param{{}, as::relative{as::literal{-1}}};
  context->text.push_back(
      as::instruction{as::jump_if_false{{zero, return_address}}});
}

void function_context::gen_stmt(const break_statement&) {
  if (!scope.back().break_label) {
    std::ostringstream message;
    message << "Illegal break statement in function "
            << std::quoted(function_name) << ".";
    die(message.str());
  }
  const auto break_label =
      as::input_param{{}, as::immediate{as::name{*scope.back().break_label}}};
  const auto zero = as::input_param{{}, as::immediate{as::literal{0}}};
  context->text.push_back(
      as::instruction{as::jump_if_false{{zero, break_label}}});
}

void function_context::gen_stmt(const continue_statement&) {
  if (!scope.back().continue_label) {
    std::ostringstream message;
    message << "Illegal continue statement in function "
            << std::quoted(function_name) << ".";
    die(message.str());
  }
  const auto zero = as::input_param{{}, as::immediate{as::literal{0}}};
  const auto continue_label = as::input_param{
      {}, as::immediate{as::name{*scope.back().continue_label}}};
  context->text.push_back(
      as::instruction{as::jump_if_false{{zero, continue_label}}});
}

void function_context::gen_stmt(const statement& s) {
  std::visit([&](const auto& x) { gen_stmt(x); }, *s.value);
}

void function_context::gen_stmts(std::span<const statement> statements) {
  push_scope();
  for (auto& s : statements) gen_stmt(s);
  pop_scope();
}

export std::vector<as::statement> generate(
    std::span<const declaration> declarations) {
  context ctx;
  ctx.gen_decls(declarations);
  auto combined = std::move(ctx.text);
  std::move(ctx.data.begin(), ctx.data.end(), std::back_inserter(combined));
  return combined;
}

}  // namespace compiler
