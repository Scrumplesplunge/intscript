module;

#include <cassert>

export module compiler.codegen;

import <filesystem>;
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

struct module_exports {
  std::set<std::string> variables;
  std::map<std::string, as::immediate> constants;
};

struct context {
  std::map<std::string, int> labels;
  std::string label(std::string name) {
    int id = labels[name]++;
    return name + std::to_string(id);
  }

  std::map<std::string, module_exports> modules;
  std::vector<as::statement> text;
  std::vector<as::statement> rodata, data;

  context();

  as::immediate make_string(std::string value) {
    auto address = label("string");
    rodata.push_back(as::label{address});
    rodata.push_back(as::directive{as::ascii{std::move(value)}});
    return as::name{address};
  }

  void gen_module(const module& m);

  std::vector<as::statement> finish();
};

struct module_context {
  context* context = nullptr;

  std::set<std::string> imported_variables;
  std::map<std::string, as::immediate> imported_constants = {
    {"heapstart", as::immediate{as::name{"heapstart"}}},
  };
  std::set<std::string> variables;
  std::map<std::string, as::immediate> constants;

  bool has_global(std::string global) const {
    if (imported_variables.contains(global)) return true;
    if (imported_constants.contains(global)) return true;
    if (variables.contains(global)) return true;
    if (constants.contains(global)) return true;
    return false;
  }

  module_context(struct context* context, const module& m);

  as::immediate eval_expr(const expression& e);

  void gen_decl(const constant& c);
  void gen_decl(const declare_scalar& d);
  void gen_decl(const declare_array& d);
  void gen_decl(const function_definition& d);
  void gen_decl(const declaration& d);

  void gen_decls(std::span<const declaration> declarations);
};

struct function_context {
  module_context* module = nullptr;

  struct environment {
    int size = 0;
    std::map<std::string, int> variables;
    std::map<std::string, as::immediate> constants;
    std::optional<std::string> break_label, continue_label;
  };
  std::string function_name;
  std::set<std::string> arguments = {};
  std::vector<environment> scope = {environment{}};
  int max_size = 0;

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
    if (module->variables.contains(name)) return global_variable;
    if (module->constants.contains(name)) return global_constant;
    if (module->imported_variables.contains(name)) return global_variable;
    if (module->imported_constants.contains(name)) return global_constant;
    return not_found;
  }

  bool has_local(std::string local) const {
    auto kind = lookup(local);
    return kind == local_variable || kind == local_constant;
  }

  as::output_param get_local_variable(std::string name) const {
    assert(lookup(name) == local_variable || lookup(name) == argument);
    if (arguments.contains(name)) {
      return {{}, as::address{as::name{"arg_" + function_name + "_" + name}}};
    }
    for (int i = scope.size() - 1; i >= 0; i--) {
      if (auto j = scope[i].variables.find(name);
          j != scope[i].variables.end()) {
        const auto label =
            "lv_" + function_name + "_" + std::to_string(j->second);
        return {{}, as::address{as::name{label}}};
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
    if (auto i = module->constants.find(name); i != module->constants.end()) {
      return i->second;
    } else {
      return module->imported_constants.at(name);
    }
  }

  void define_scalar(std::string variable) {
    assert(!has_local(variable));
    auto& current = scope.back();
    current.variables.emplace(variable, current.size);
    current.size++;
    if (current.size > max_size) max_size = current.size;
  }

  void define_array(std::string variable, int size) {
    assert(!has_local(variable));
    auto& current = scope.back();
    const auto label =
        "lv_" + function_name + "_" + std::to_string(current.size);
    current.constants.emplace(variable, as::immediate{as::name{label}});
    current.size += size;
    if (current.size > max_size) max_size = current.size;
  }

  void define_constant(std::string name, as::immediate value) {
    assert(!has_local(name));
    scope.back().constants.emplace(name, value);
  }

  void push_scope() {
    const auto& current = scope.back();
    scope.push_back(
        {current.size, {}, {}, current.break_label, current.continue_label});
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
  as::input_param gen_expr(const logical_and& a);
  as::input_param gen_expr(const logical_or& o);
  as::input_param gen_expr(const expression& e);
  as::immediate eval_expr(const expression& e);

  void gen_stmt(const constant& c);
  void gen_stmt(const call& c);
  void gen_stmt(const declare_scalar& d);
  void gen_stmt(const declare_array& d);
  void gen_stmt(const assign& a);
  void gen_stmt(const add_assign& a);
  void gen_stmt(const if_statement& i);
  void gen_stmt(const while_statement& w);
  void gen_stmt(const output_statement& o);
  void gen_stmt(const return_statement& r);
  void gen_stmt(const break_statement&);
  void gen_stmt(const continue_statement&);
  void gen_stmt(const halt_statement&);
  void gen_stmt(const statement& s);

  void gen_stmts(std::span<const statement> statements);
};

context::context() {
  module_context root{this, {}};
  function_context f{&root, "_start"};
  f.scope.back().constants.emplace(
      "main", as::immediate{as::name{"func_main"}});
  f.gen_stmt(call{expression::wrap(name{"main"}), {}});
  text.push_back(as::instruction{as::halt{}});
}

std::vector<as::statement> context::finish() {
  auto output = std::move(text);
  output.reserve(output.size() + data.size() + 1);
  std::move(rodata.begin(), rodata.end(), std::back_inserter(output));
  std::move(data.begin(), data.end(), std::back_inserter(output));
  output.push_back(as::label{"heapstart"});
  return output;
}

void context::gen_module(const module& m) {
  module_context module{this, m};
  module.gen_decls(m.body);
  modules.emplace(m.name, module_exports{std::move(module.variables),
                                         std::move(module.constants)});
}

module_context::module_context(struct context* context, const module& m)
    : context(context) {
  const auto path_context = std::filesystem::path(m.name).parent_path();
  for (const auto& import : m.imports) {
    const auto& dependency = context->modules.at(import.resolve(path_context));
    imported_variables.insert(
        dependency.variables.begin(), dependency.variables.end());
    imported_constants.insert(
        dependency.constants.begin(), dependency.constants.end());
  }
}

void module_context::gen_decl(const constant& c) {
  if (has_global(c.name)) {
    std::ostringstream message;
    message << "Multiple definitions for " << std::quoted(c.name)
            << " at global scope.";
    die(message.str());
  }
  constants.emplace(c.name, eval_expr(c.value));
}

void module_context::gen_decl(const declare_scalar& d) {
  if (has_global(d.name)) {
    std::ostringstream message;
    message << "Multiple definitions for " << std::quoted(d.name)
            << " at global scope.";
    die(message.str());
  }
  context->data.push_back(as::label{"gv_" + d.name});
  context->data.push_back(as::integer{as::literal{0}});
  variables.emplace(d.name);
}

void module_context::gen_decl(const declare_array& d) {
  if (has_global(d.name)) {
    std::ostringstream message;
    message << "Multiple definitions for " << std::quoted(d.name)
            << " at global scope.";
    die(message.str());
  }
  auto size = eval_expr(d.size);
  if (!std::holds_alternative<as::literal>(size)) {
    die("Array size is not a constant expression.");
  }
  context->data.push_back(as::label{"gv_" + d.name});
  for (int i = 0, n = std::get<as::literal>(size).value; i < n; i++) {
    context->data.push_back(as::integer{as::literal{0}});
  }
  constants.emplace(d.name, as::immediate{as::name{"gv_" + d.name}});
}

void module_context::gen_decl(const function_definition& d) {
  function_context f{this, d.name};
  for (const auto& parameter : d.parameters) {
    context->text.push_back(as::label{"arg_" + d.name + "_" + parameter});
    context->text.push_back(as::directive{as::integer{as::literal{0}}});
    f.arguments.emplace(parameter);
  }
  context->text.push_back(as::label{"func_" + d.name + "_output"});
  context->text.push_back(as::directive{as::integer{as::literal{0}}});
  context->text.push_back(as::label{"func_" + d.name + "_return"});
  context->text.push_back(as::directive{as::integer{as::literal{0}}});
  context->text.push_back(as::label{"func_" + d.name});
  f.gen_stmts(d.body);
  f.gen_stmt(return_statement{expression::wrap(literal{0})});
  constants.emplace(d.name, as::name{"func_" + d.name});
  for (int i = 0; i < f.max_size; i++) {
    context->data.push_back(
        as::label{"lv_" + f.function_name + "_" + std::to_string(i)});
    context->data.push_back(as::directive{as::integer{as::literal{0}}});
  }
}

void module_context::gen_decl(const declaration& d) {
  std::visit([&](const auto& x) { gen_decl(x); }, *d.value);
}

void module_context::gen_decls(std::span<const declaration> declarations) {
  for (const auto& declaration : declarations) gen_decl(declaration);
}

as::immediate module_context::eval_expr(const expression& e) {
  return std::visit(overload{
    [&](const literal& l) -> as::immediate {
      return std::visit(overload{
        [&](std::int64_t x) -> as::immediate { return as::literal{x}; },
        [&](std::string x) { return context->make_string(std::move(x)); },
      }, l);
    },
    [&](const name& n) -> as::immediate { return constants.at(n.value); },
    [&](const add& a) -> as::immediate {
      auto l = eval_expr(a.left);
      auto r = eval_expr(a.right);
      auto* x = std::get_if<as::literal>(&l);
      auto* y = std::get_if<as::literal>(&r);
      if (x && y) {
        return as::literal{x->value + y->value};
      } else {
        std::ostringstream message;
        message << "Cannot add " << a.left << " and " << a.right
                << " in a constant expression.";
        die(message.str());
      }
    },
    [&](const sub& s) -> as::immediate {
      auto l = eval_expr(s.left);
      auto r = eval_expr(s.right);
      auto* x = std::get_if<as::literal>(&l);
      auto* y = std::get_if<as::literal>(&r);
      if (x && y) {
        return as::literal{x->value - y->value};
      } else {
        std::ostringstream message;
        message << "Cannot subtract " << s.left << " from " << s.right
                << " in a constant expression.";
        die(message.str());
      }
    },
    [&](const mul& m) -> as::immediate {
      auto l = eval_expr(m.left);
      auto r = eval_expr(m.right);
      auto* x = std::get_if<as::literal>(&l);
      auto* y = std::get_if<as::literal>(&r);
      if (x && y) {
        return as::literal{x->value * y->value};
      } else {
        std::ostringstream message;
        message << "Cannot multiply " << m.left << " and " << m.right
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
      return {{}, as::address{as::name{"gv_" + n.value}}};
    case argument:
      return {{},
              as::address{as::name{"arg_" + function_name + "_" + n.value}}};
    case local_variable:
      return get_local_variable(n.value);
  }
}

as::output_param function_context::gen_addr(const read& r) {
  auto value = gen_expr(r.address);
  auto label = module->context->label("read");
  // add 0, <value>, *label
  const auto zero = as::input_param{{}, as::literal{0}};
  const auto out = as::output_param{{}, as::address{as::name{label}}};
  module->context->text.push_back(as::instruction{as::add{{zero, value, out}}});
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
      return as::input_param{{}, module->context->make_string(std::move(x))};
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
      return {{}, as::address{as::name{"gv_" + n.value}}};
    case argument:
      return {{},
              as::address{as::name{"arg_" + function_name + "_" + n.value}}};
    case local_variable:
      return get_local_variable(n.value);
  }
}

as::input_param function_context::gen_expr(const call& c) {
  const auto zero = as::input_param{{}, as::literal{0}};
  const int n = c.arguments.size();
  // Compute the function address.
  auto callee = gen_expr(c.function);
  if (!callee.label) {
    auto out = module->context->label("callee");
    module->context->text.push_back(as::instruction{
        as::add{{zero, callee, {{}, as::address{as::name{out}}}}}});
    callee = {out, as::immediate{as::literal{0}}};
  }
  auto get_callee = as::input_param{{}, as::address{as::name{*callee.label}}};
  // Adjust the relative base to point at the start of the arguments.
  auto args = module->context->label("args");
  module->context->text.push_back(as::instruction{
      as::add{{get_callee, {{}, as::immediate{as::literal{-(n + 2)}}},
               {{}, as::address{as::name{args}}}}}});
  module->context->text.push_back(as::instruction{
      as::adjust_relative_base{{args, as::immediate{as::literal{0}}}}});
  // Compute the arguments.
  for (int i = 0; i < n; i++) {
    auto param = gen_expr(c.arguments[i]);
    auto out = as::output_param{{}, as::relative{as::literal{i}}};
    module->context->text.push_back(
        as::instruction{as::add{{zero, param, out}}});
  }
  // Store the output address.
  auto output_label = module->context->label("return");
  {
    const auto output_address =
        as::input_param{{}, as::immediate{as::name{output_label}}};
    const auto out = as::output_param{{}, as::relative{as::literal{n}}};
    module->context->text.push_back(
        as::instruction{as::add{{zero, output_address, out}}});
  }
  // Store the return address.
  auto return_label = module->context->label("call");
  {
    const auto return_address =
        as::input_param{{}, as::immediate{as::name{return_label}}};
    const auto out = as::output_param{{}, as::relative{as::literal{n + 1}}};
    module->context->text.push_back(
        as::instruction{as::add{{zero, return_address, out}}});
  }
  // Revert the relative base.
  auto args2 = module->context->label("revertargs");
  module->context->text.push_back(as::instruction{
      as::mul{{{{}, as::address{as::name{args}}},
               {{}, as::immediate{as::literal{-1}}},
               {{}, as::address{as::name{args2}}}}}});
  module->context->text.push_back(as::instruction{
      as::adjust_relative_base{{args2, as::immediate{as::literal{0}}}}});
  // Jump into the function.
  module->context->text.push_back(
      as::instruction{as::jump_if_false{{zero, callee}}});
  module->context->text.push_back(as::label{return_label});
  return as::input_param{output_label, as::immediate{as::literal{0}}};
}

as::input_param function_context::gen_expr(const add& a) {
  auto l = gen_expr(a.left);
  auto r = gen_expr(a.right);
  auto result = module->context->label("add");
  module->context->text.push_back(
      as::instruction{as::add{{l, r, {{}, as::address{as::name{result}}}}}});
  return as::input_param{result, as::immediate{as::literal{0}}};
}

as::input_param function_context::gen_expr(const mul& m) {
  auto l = gen_expr(m.left);
  auto r = gen_expr(m.right);
  auto result = module->context->label("mul");
  module->context->text.push_back(
      as::instruction{as::mul{{l, r, {{}, as::address{as::name{result}}}}}});
  return as::input_param{result, as::immediate{as::literal{0}}};
}

as::input_param function_context::gen_expr(const sub& s) {
  const auto negated_b =
      expression::wrap(mul{{s.right, expression::wrap(literal{-1})}});
  return gen_expr(add{{s.left, negated_b}});
}

as::input_param function_context::gen_expr(const less_than& l) {
  auto a = gen_expr(l.left);
  auto b = gen_expr(l.right);
  auto result = module->context->label("lt");
  module->context->text.push_back(as::instruction{
      as::less_than{{a, b, {{}, as::address{as::name{result}}}}}});
  return as::input_param{result, as::immediate{as::literal{0}}};
}

as::input_param function_context::gen_expr(const equals& e) {
  auto a = gen_expr(e.left);
  auto b = gen_expr(e.right);
  auto result = module->context->label("eq");
  module->context->text.push_back(as::instruction{
      as::equals{{a, b, {{}, as::address{as::name{result}}}}}});
  return as::input_param{result, as::immediate{as::literal{0}}};
}

as::input_param function_context::gen_expr(const input&) {
  auto result = module->context->label("input");
  module->context->text.push_back(
      as::instruction{as::input{{{}, as::address{as::name{result}}}}});
  return as::input_param{result, as::immediate{as::literal{0}}};
}

as::input_param function_context::gen_expr(const read& r) {
  return gen_addr(r);
}

as::input_param function_context::gen_expr(const logical_and& a) {
  auto result = module->context->label("and");
  auto short_circuit = module->context->label("andfalse");
  auto end = module->context->label("andend");
  // Initialize the output to true.
  const auto zero = as::input_param{{}, as::literal{0}};
  const auto one = as::input_param{{}, as::literal{1}};
  module->context->text.push_back(
      as::add{{zero, one, {{}, as::address{as::name{result}}}}});
  auto l = gen_expr(a.left);
  module->context->text.push_back(as::instruction{
      as::jump_if_false{{l, {{}, as::immediate{as::name{short_circuit}}}}}});
  auto r = gen_expr(a.right);
  module->context->text.push_back(as::instruction{
      as::jump_if_true{{r, {{}, as::immediate{as::name{end}}}}}});
  module->context->text.push_back(as::label{short_circuit});
  module->context->text.push_back(
      as::add{{zero, zero, {{}, as::address{as::name{result}}}}});
  module->context->text.push_back(as::label{end});
  return {{result}, as::immediate{as::literal{0}}};
}

as::input_param function_context::gen_expr(const logical_or& o) {
  auto result = module->context->label("or");
  auto short_circuit = module->context->label("ortrue");
  auto end = module->context->label("orend");
  // Initialize the output to false.
  const auto zero = as::input_param{{}, as::literal{0}};
  const auto one = as::input_param{{}, as::literal{1}};
  module->context->text.push_back(
      as::add{{zero, zero, {{}, as::address{as::name{result}}}}});
  auto l = gen_expr(o.left);
  module->context->text.push_back(as::instruction{
      as::jump_if_true{{l, {{}, as::immediate{as::name{short_circuit}}}}}});
  auto r = gen_expr(o.right);
  module->context->text.push_back(as::instruction{
      as::jump_if_false{{r, {{}, as::immediate{as::name{end}}}}}});
  module->context->text.push_back(as::label{short_circuit});
  module->context->text.push_back(
      as::add{{zero, one, {{}, as::address{as::name{result}}}}});
  module->context->text.push_back(as::label{end});
  return {{result}, as::immediate{as::literal{0}}};
}

as::input_param function_context::gen_expr(const expression& e) {
  return std::visit([&](auto& x) { return gen_expr(x); }, *e.value);
}

as::immediate function_context::eval_expr(const expression& e) {
  return std::visit(overload{
    [&](const literal& l) -> as::immediate {
      return std::visit(overload{
        [&](std::int64_t x) -> as::immediate { return as::literal{x}; },
        [&](std::string x) {
          return module->context->make_string(std::move(x));
        },
      }, l);
    },
    [&](const name& n) -> as::immediate { return get_constant(n.value); },
    [&](const add& a) -> as::immediate {
      auto l = eval_expr(a.left);
      auto r = eval_expr(a.right);
      auto* x = std::get_if<as::literal>(&l);
      auto* y = std::get_if<as::literal>(&r);
      if (x && y) {
        return as::literal{x->value + y->value};
      } else {
        std::ostringstream message;
        message << "Cannot add " << a.left << " and " << a.right
                << " in a constant expression.";
        die(message.str());
      }
    },
    [&](const sub& s) -> as::immediate {
      auto l = eval_expr(s.left);
      auto r = eval_expr(s.right);
      auto* x = std::get_if<as::literal>(&l);
      auto* y = std::get_if<as::literal>(&r);
      if (x && y) {
        return as::literal{x->value - y->value};
      } else {
        std::ostringstream message;
        message << "Cannot subtract " << s.left << " from " << s.right
                << " in a constant expression.";
        die(message.str());
      }
    },
    [&](const mul& m) -> as::immediate {
      auto l = eval_expr(m.left);
      auto r = eval_expr(m.right);
      auto* x = std::get_if<as::literal>(&l);
      auto* y = std::get_if<as::literal>(&r);
      if (x && y) {
        return as::literal{x->value * y->value};
      } else {
        std::ostringstream message;
        message << "Cannot multiply " << m.left << " and " << m.right
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
  auto self = module->context->label("ignore");
  const auto zero = as::input_param{{self}, as::immediate{as::literal{0}}};
  const auto out = as::output_param{{}, as::address{as::name{self}}};
  module->context->text.push_back(as::instruction{as::add{{value, zero, out}}});
}

void function_context::gen_stmt(const declare_scalar& d) {
  if (has_local(d.name)) {
    std::ostringstream message;
    message << "Multiple definitions for " << std::quoted(d.name)
            << " in function " << std::quoted(function_name) << ".";
    die(message.str());
  }
  define_scalar(d.name);
}

void function_context::gen_stmt(const declare_array& d) {
  if (has_local(d.name)) {
    std::ostringstream message;
    message << "Multiple definitions for " << std::quoted(d.name)
            << " in function " << std::quoted(function_name) << ".";
    die(message.str());
  }
  auto size = eval_expr(d.size);
  if (!std::holds_alternative<as::literal>(size)) {
    die("Array size is not a compile-time constant.");
  }
  define_array(d.name, std::get<as::literal>(size).value);
}

void function_context::gen_stmt(const assign& a) {
  auto value = gen_expr(a.right);
  auto address = gen_addr(a.left);
  module->context->text.push_back(as::instruction{as::add{{
      {{}, as::immediate{as::literal{0}}}, value, address}}});
}

void function_context::gen_stmt(const add_assign& a) {
  auto value = gen_expr(a.right);
  auto address = gen_addr(a.left);
  module->context->text.push_back(as::instruction{as::add{{
      address, value, {{}, address.output}}}});
}

void function_context::gen_stmt(const if_statement& i) {
  auto condition = gen_expr(i.condition);
  auto end_if = module->context->label("endif");
  auto else_branch =
      i.else_branch.empty() ? end_if : module->context->label("else");
  const auto target = as::input_param{{}, as::immediate{as::name{else_branch}}};
  module->context->text.push_back(
      as::instruction{as::jump_if_false{{condition, target}}});
  gen_stmts(i.then_branch);
  if (!i.else_branch.empty()) {
    const auto end = as::input_param{{}, as::immediate{as::name{end_if}}};
    const auto zero = as::input_param{{}, as::immediate{as::literal{0}}};
    module->context->text.push_back(
        as::instruction{as::jump_if_false{{zero, end}}});
    module->context->text.push_back(as::label{else_branch});
    gen_stmts(i.else_branch);
  }
  module->context->text.push_back(as::label{end_if});
}

void function_context::gen_stmt(const while_statement& w) {
  push_scope();
  auto while_start = module->context->label("whilestart");
  auto while_cond = module->context->label("whilecond");
  auto while_end = module->context->label("whileend");
  scope.back().break_label = while_end;
  scope.back().continue_label = while_cond;
  const auto cond = as::input_param{{}, as::immediate{as::name{while_cond}}};
  module->context->text.push_back(as::instruction{
      as::jump_if_false{{{{}, as::immediate{as::literal{0}}}, cond}}});
  module->context->text.push_back(as::label{while_start});
  gen_stmts(w.body);
  module->context->text.push_back(as::label{while_cond});
  auto condition = gen_expr(w.condition);
  const auto start = as::input_param{{}, as::immediate{as::name{while_start}}};
  module->context->text.push_back(
      as::instruction{as::jump_if_true{{condition, start}}});
  module->context->text.push_back(as::label{while_end});
  pop_scope();
}

void function_context::gen_stmt(const output_statement& o) {
  auto value = gen_expr(o.value);
  module->context->text.push_back(as::instruction{as::output{value}});
}

void function_context::gen_stmt(const return_statement& r) {
  // Store the return value at the output address.
  auto output_label = module->context->label("output");
  const auto zero = as::input_param{{}, as::immediate{as::literal{0}}};
  const auto output_address = as::input_param{
      {}, as::address{as::name{"func_" + function_name + "_output"}}};
  const auto temp = as::output_param{{}, as::address{as::name{output_label}}};
  module->context->text.push_back(
      as::instruction{as::add{{zero, output_address, temp}}});
  const auto output =
      as::output_param{output_label, as::address{as::literal{0}}};
  auto value = gen_expr(r.value);
  module->context->text.push_back(
      as::instruction{as::add{{zero, value, output}}});
  // Return to the caller.
  const auto return_address = as::input_param{
      {}, as::address{as::name{"func_" + function_name + "_return"}}};
  module->context->text.push_back(
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
  module->context->text.push_back(
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
  module->context->text.push_back(
      as::instruction{as::jump_if_false{{zero, continue_label}}});
}

void function_context::gen_stmt(const halt_statement&) {
  module->context->text.push_back(as::instruction{as::halt{}});
}

void function_context::gen_stmt(const statement& s) {
  std::visit([&](const auto& x) { gen_stmt(x); }, *s.value);
}

void function_context::gen_stmts(std::span<const statement> statements) {
  push_scope();
  for (auto& s : statements) gen_stmt(s);
  pop_scope();
}

std::vector<std::string> dependency_order(
    const std::map<std::string, module>& modules) {
  std::vector<std::string> output;
  std::set<std::string> outstanding;
  for (const auto& [k, v] : modules) outstanding.insert(k);
  while (!outstanding.empty()) {
    for (auto i = outstanding.begin(); i != outstanding.end();) {
      const auto& module = modules.at(*i);
      const auto context = module.context();
      bool ready = true;
      for (const auto& dependency : module.imports) {
        if (outstanding.contains(dependency.resolve(context))) {
          ready = false;
          break;
        }
      }
      if (ready) {
        output.push_back(std::move(*i));
        i = outstanding.erase(i);
      } else {
        ++i;
      }
    }
  }
  return output;
}

export std::vector<as::statement> generate(
    const std::map<std::string, module>& modules) {
  context context;
  for (const auto& module : dependency_order(modules)) {
    context.gen_module(modules.at(module));
  }
  return context.finish();
}

}  // namespace compiler
