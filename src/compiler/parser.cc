module;

#include <cassert>

export module compiler.parser;

import <charconv>;
import <filesystem>;
import <iomanip>;
import <iostream>;
import <map>;
import <optional>;
import <sstream>;
import <span>;
import <string_view>;
import <variant>;
import <vector>;
import compiler.ast;
import util.io;
import util.value_ptr;

namespace compiler {

struct parser {
  std::string_view file, source;
  int line = 1, column = 1;

  [[noreturn]] void die(std::string_view message) const {
    std::cerr << file << ":" << line << ":" << column << ": error: "
              << message << "\n";
    std::abort();
  }

  void eat(std::string_view value) {
    skip_whitespace();
    if (!source.starts_with(value)) {
      std::ostringstream message;
      message << "Expected " << std::quoted(value) << ".";
      die(message.str());
    }
    advance(value.size());
  }

  std::string_view peek_name() {
    skip_whitespace();
    auto i = std::find_if(
        source.data(), source.data() + source.size(),
        [](char c) { return !std::isalnum(c); });
    return source.substr(0, i - source.data());
  }

  bool consume_name(std::string_view value) {
    auto name = peek_name();
    if (name == value) {
      advance(value.size());
      return true;
    } else {
      return false;
    }
  }

  void eat_name(std::string_view value) {
    if (!consume_name(value)) {
      std::ostringstream message;
      message << "Expected " << std::quoted(value) << ".";
      die(message.str());
    }
  }

  std::string_view peek_symbol() {
    skip_whitespace();
    auto first = source.data(), last = first + source.size();
    auto i = std::find_if_not(first, last, [](char c) {
      constexpr std::string_view symbol_chars = "+-=<>!.&|";
      return symbol_chars.find(c) != symbol_chars.npos;
    });
    return std::string_view(first, i - first);
  }

  bool consume_symbol(std::string_view value) {
    auto name = peek_symbol();
    if (name == value) {
      advance(value.size());
      return true;
    } else {
      return false;
    }
  }

  void eat_symbol(std::string_view value) {
    if (!consume_symbol(value)) {
      std::ostringstream message;
      message << "Expected " << std::quoted(value) << ".";
      die(message.str());
    }
  }

  void advance(std::size_t amount) {
    assert(amount <= source.size());
    for (char c : source.substr(0, amount)) {
      if (c == '\n') {
        line++;
        column = 1;
      } else {
        column++;
      }
    }
    source.remove_prefix(amount);
  }

  char peek() const {
    if (source.empty()) die("Unexpected end of input.");
    return source[0];
  }

  char get() {
    char c = peek();
    advance(1);
    return c;
  }

  void parse_newline() {
    skip_whitespace();
    if (get() != '\n') die("Expected newline.");
  }

  void skip_whitespace() {
    const char* i = source.data();
    const char* const end = source.data() + source.size();
    while (true) {
      i = std::find_if(i, end, [](char c) { return c != ' '; });
      if (i == end) break;
      if (*i != '#') break;
      // Skip a comment.
      i = std::find(i, end, '\n');
    }
    advance(i - source.data());
  }

  std::int64_t parse_integer() {
    std::int64_t value;
    auto [ptr, error] = std::from_chars(
        source.data(), source.data() + source.size(), value);
    if (error != std::errc()) die("Expected numeric literal.");
    advance(ptr - source.data());
    return value;
  }

  std::string parse_string_literal() {
    eat("\"");
    std::string value;
    while (peek() != '"') {
      if (peek() == '\\') {
        advance(1);
        switch (peek()) {
          case '\\':
          case '"':
            value.push_back(get());
            break;
          case 'n':
            value.push_back('\n');
            advance(1);
            break;
          default:
            die("Invalid escape sequence.");
        }
      } else {
        value.push_back(get());
      }
    }
    assert(source[0] == '"');
    advance(1);
    return value;
  }

  literal parse_literal() {
    skip_whitespace();
    if (source.empty()) die("Unexpected end of input.");
    if (std::isdigit(source[0])) return parse_integer();
    if (source[0] == '"') return parse_string_literal();
    die("Expected a literal value.");
  }

  name parse_name() {
    name output{std::string{peek_name()}};
    if (output.value.empty()) die("Expected name.");
    if (std::isdigit(output.value[0])) die("Names cannot start with numbers.");
    advance(output.value.size());
    return output;
  }

  expression parse_term() {
    skip_whitespace();
    if (source.empty()) die("Unexpected end of input.");
    if (source[0] == '"' || std::isdigit(source[0])) {
      return expression::wrap(parse_literal());
    }
    if (source[0] == '(') {
      eat("(");
      auto result = parse_condition();
      eat(")");
      return result;
    }
    auto name = parse_name();
    if (name.value == "input") return expression::wrap(input{});
    return expression::wrap(name);
  }

  expression parse_suffix() {
    expression result = parse_term();
    while (true) {
      skip_whitespace();
      if (source.empty()) break;
      if (source[0] == '[') {
        // Array index.
        eat("[");
        auto address = parse_expression();
        eat("]");
        result = expression::wrap(read{expression::wrap(add{{
            std::move(result), std::move(address)}})});
      } else if (source[0] == '(') {
        // Function call.
        eat("(");
        if (peek() == ')') {
          eat(")");
          result = expression::wrap(call{std::move(result), {}});
        } else {
          std::vector<expression> arguments = {parse_expression()};
          skip_whitespace();
          while (peek() != ')') {
            eat(",");
            arguments.push_back(parse_expression());
          }
          eat(")");
          result = expression::wrap(
              call{std::move(result), std::move(arguments)});
        }
      } else {
        break;
      }
    }
    return result;
  }

  expression parse_prefix() {
    skip_whitespace();
    if (source.empty()) die("Unexpected end of input.");
    if (source[0] == '*') {
      eat("*");
      return expression::wrap(read{parse_prefix()});
    } else if (source[0] == '-') {
      eat("-");
      return expression::wrap(
          sub{{expression::wrap(literal{0}), parse_prefix()}});
    } else {
      return parse_suffix();
    }
  }

  expression parse_product() {
    expression result = parse_prefix();
    while (peek() == '*') {
      eat("*");
      result = expression::wrap(mul{{std::move(result), parse_prefix()}});
    }
    return result;
  }

  expression parse_sum() {
    expression result = parse_product();
    while (true) {
      char lookahead = peek();
      if (lookahead == '+') {
        eat("+");
        result = expression::wrap(add{{std::move(result), parse_product()}});
      } else if (lookahead == '-') {
        eat("-");
        result = expression::wrap(sub{{std::move(result), parse_product()}});
      } else {
        break;
      }
    }
    return result;
  }

  expression parse_expression() { return parse_sum(); }

  expression parse_comparison() {
    expression left = parse_sum();
    skip_whitespace();
    if (consume_symbol("<")) {
      return expression::wrap(less_than{{std::move(left), parse_expression()}});
    } else if (consume_symbol("==")) {
      return expression::wrap(equals{{std::move(left), parse_expression()}});
    } else if (consume_symbol(">")) {
      return greater_than(std::move(left), parse_expression());
    } else if (consume_symbol("<=")) {
      return less_or_equal(std::move(left), parse_expression());
    } else if (consume_symbol(">=")) {
      return greater_or_equal(std::move(left), parse_expression());
    } else if (consume_symbol("!=")) {
      return not_equals(std::move(left), parse_expression());
    } else {
      return left;
    }
  }

  expression parse_conjunction() {
    expression left = parse_comparison();
    while (consume_symbol("&&")) {
      left = expression::wrap(
          logical_and{{std::move(left), parse_comparison()}});
    }
    return left;
  }

  expression parse_disjunction() {
    expression left = parse_conjunction();
    while (consume_symbol("||")) {
      left = expression::wrap(
          logical_or{{std::move(left), parse_conjunction()}});
    }
    return left;
  }

  expression parse_condition() { return parse_disjunction(); }

  enum declare_mode {
    declare_only,
    declare_assign,
  };

  template <typename Output, declare_mode mode>
  std::vector<Output> parse_var() {
    eat_name("var");
    std::vector<Output> output;
    while (true) {
      auto [id] = parse_name();
      skip_whitespace();
      if (peek() == '[') {
        eat("[");
        auto size = parse_expression();
        eat("]");
        output.push_back(Output::wrap(declare_array{id, std::move(size)}));
      } else {
        output.push_back(Output::wrap(declare_scalar{id}));
      }
      if constexpr (mode == declare_assign) {
        if (peek() == '=') {
          eat("=");
          output.push_back(Output::wrap(
              assign{expression::wrap(name{id}), parse_expression()}));
          skip_whitespace();
        }
      }
      if (peek() != ',') break;
      eat(",");
    }
    eat(";");
    return output;
  }

  template <typename Output>
  std::vector<Output> parse_constant() {
    eat_name("const");
    std::vector<Output> output;
    while (true) {
      auto [id] = parse_name();
      eat("=");
      output.push_back(
          Output::wrap(constant{std::move(id), parse_expression()}));
      skip_whitespace();
      if (peek() != ',') break;
      eat(",");
    }
    eat(";");
    return output;
  }

  statement parse_if_statement() {
    eat_name("if");
    auto condition = parse_condition();
    eat("{");
    parse_newline();
    auto then_branch = parse_statements();
    eat("}");
    skip_whitespace();
    std::vector<statement> else_branch;
    if (consume_name("else")) {
      if (peek_name() == "if") {
        else_branch = {parse_if_statement()};
      } else {
        eat("{");
        parse_newline();
        else_branch = parse_statements();
        eat("}");
      }
    }
    return statement::wrap(if_statement{
        std::move(condition), std::move(then_branch), std::move(else_branch)});
  }

  statement parse_while_statement() {
    eat_name("while");
    auto condition = parse_condition();
    eat("{");
    parse_newline();
    auto body = parse_statements();
    eat("}");
    skip_whitespace();
    return statement::wrap(
        while_statement{std::move(condition), std::move(body)});
  }

  statement parse_output_statement() {
    eat_name("output");
    auto value = parse_expression();
    eat(";");
    return statement::wrap(output_statement{std::move(value)});
  }

  statement parse_return_statement() {
    eat_name("return");
    auto value = parse_expression();
    eat(";");
    return statement::wrap(return_statement{std::move(value)});
  }

  statement parse_break_statement() {
    eat_name("break");
    eat(";");
    return statement::wrap(break_statement{});
  }

  statement parse_continue_statement() {
    eat_name("return");
    eat(";");
    return statement::wrap(continue_statement{});
  }

  statement parse_halt_statement() {
    eat_name("halt");
    eat(";");
    return statement::wrap(halt_statement{});
  }

  std::vector<statement> parse_statements() {
    skip_whitespace();
    std::vector<statement> output;
    auto parse_line = [&] {
      if (source.empty()) die("Unexpected end of input.");
      if (std::isalpha(source[0])) {
        auto word = peek_name();
        if (word == "const") {
          auto x = parse_constant<statement>();
          std::move(x.begin(), x.end(), std::back_inserter(output));
          return;
        } else if (word == "var") {
          auto x = parse_var<statement, declare_assign>();
          std::move(x.begin(), x.end(), std::back_inserter(output));
          return;
        } else if (word == "if") {
          output.push_back(parse_if_statement());
          return;
        } else if (word == "while") {
          output.push_back(parse_while_statement());
          return;
        } else if (word == "output") {
          output.push_back(parse_output_statement());
          return;
        } else if (word == "return") {
          output.push_back(parse_return_statement());
          return;
        } else if (word == "break") {
          output.push_back(parse_break_statement());
          return;
        } else if (word == "continue") {
          output.push_back(parse_continue_statement());
          return;
        } else if (word == "halt") {
          output.push_back(parse_halt_statement());
          return;
        }
      }
      auto expr = parse_expression();
      skip_whitespace();
      if (!source.empty() && source[0] == '=') {
        if (!is_lvalue(expr)) {
          std::ostringstream message;
          message << expr << " is not an lvalue.";
          die(message.str());
        }
        eat("=");
        auto value = parse_expression();
        eat(";");
        output.push_back(
            statement::wrap(assign{std::move(expr), std::move(value)}));
      } else if (auto* c = std::get_if<call>(expr.value.get())) {
        eat(";");
        output.push_back(statement::wrap(std::move(*c)));
      } else {
        die("Only call expressions can be performed as statements.");
      }
    };
    while (!source.empty() && source[0] != '}') {
      parse_line();
      eat("\n");
      skip_whitespace();
    }
    return output;
  }

  function_definition parse_function_definition() {
    eat_name("function");
    auto [name] = parse_name();
    eat("(");
    std::vector<std::string> arguments;
    while (true) {
      skip_whitespace();
      if (peek() == ')') break;
      auto [argument] = parse_name();
      arguments.push_back(std::move(argument));
      skip_whitespace();
      if (peek() != ',') break;
      eat(",");
    }
    eat(")");
    eat("{");
    parse_newline();
    auto body = parse_statements();
    eat("}");
    return {std::move(name), std::move(arguments), std::move(body)};
  }

  import_statement parse_import() {
    eat_name("import");
    std::vector<std::string> parts = {parse_name().value};
    while (peek_symbol() == ".") {
      eat_symbol(".");
      parts.push_back(parse_name().value);
    }
    eat(";");
    return import_statement{std::move(parts)};
  }

  module parse_module() {
    module output;
    output.name = file;
    while (peek_name() == "import") {
      output.imports.push_back(parse_import());
      parse_newline();
    }
    while (true) {
      skip_whitespace();
      if (source.empty()) break;
      if (source[0] == '\n') {
        parse_newline();
        continue;
      }
      auto name = peek_name();
      if (name == "const") {
        auto x = parse_constant<declaration>();
        std::move(x.begin(), x.end(), std::back_inserter(output.body));
      } else if (name == "var") {
        auto x = parse_var<declaration, declare_only>();
        std::move(x.begin(), x.end(), std::back_inserter(output.body));
      } else if (name == "function") {
        output.body.push_back(declaration::wrap(parse_function_definition()));
      } else {
        die("Expected declaration.");
      }
      parse_newline();
    }
    return output;
  }
};

export module parse(std::string_view file, std::string_view source) {
  return parser{file, source}.parse_module();
}

void load_recursive(
    std::map<std::string, module>& output, const char* filename) {
  if (output.contains(filename)) return;
  auto [i, _] = output.emplace(filename, parse(filename, contents(filename)));
  const auto context = i->second.context();
  for (const auto& import : i->second.imports) {
    const auto path = import.resolve(context);
    if (!std::filesystem::exists(path)) {
      std::cerr << "Cannot find dependency " << std::quoted(path.c_str())
                << " required by " << std::quoted(filename) << ".\n";
      std::exit(1);
    }
    load_recursive(output, path.c_str());
  }
}

export std::map<std::string, module> load(const char* filename) {
  std::map<std::string, module> modules;
  load_recursive(modules, filename);
  return modules;
}

}  // namespace compiler
