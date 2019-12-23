module;

#include <cassert>

export module as.parser;

import <charconv>;
import <iomanip>;
import <iostream>;
import <optional>;
import <sstream>;
import <string_view>;
import <variant>;
import <vector>;
import as.ast;

namespace as {

struct parser {
  std::string_view file, source;
  int line = 1, column = 1;

  [[noreturn]] void die(std::string_view message) const {
    std::cerr << file << ":" << line << ":" << column << ": error: "
              << message << "\n";
    std::exit(1);
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

  literal parse_literal() {
    skip_whitespace();
    literal output;
    auto [ptr, error] = std::from_chars(
        source.data(), source.data() + source.size(), output.value);
    if (error != std::errc()) die("Expected numeric literal.");
    advance(ptr - source.data());
    return output;
  };

  name parse_name() {
    skip_whitespace();
    auto i = std::find_if(
        source.data(), source.data() + source.size(),
        [](char c) { return !std::isalnum(c); });
    name output{std::string{source.substr(0, i - source.data())}};
    if (output.value.empty()) die("Expected name.");
    if (std::isdigit(output.value[0])) die("Names cannot start with numbers.");
    advance(output.value.size());
    return output;
  }

  immediate parse_immediate() {
    skip_whitespace();
    if (source.empty()) die("Unexpected end of input.");
    return std::isalpha(source[0]) ? immediate{parse_name()}
                                   : immediate{parse_literal()};
  }

  address parse_address() {
    eat("*");
    return address{parse_immediate()};
  }

  relative parse_relative() {
    eat("base[");
    auto i = parse_immediate();
    eat("]");
    return relative{i};
  }

  input_param parse_input_param() {
    skip_whitespace();
    if (source.empty()) die("Unexpected end of input.");
    input_param result;
    if (source[0] == '*') {
      result.input = parse_address();
    } else if (source.starts_with("base[")) {
      result.input = parse_relative();
    } else {
      result.input = parse_immediate();
    }
    skip_whitespace();
    if (!source.empty() && source[0] == '@') {
      eat("@");
      result.label = parse_name().value;
    }
    return result;
  }

  output_param parse_output_param() {
    skip_whitespace();
    if (source.empty()) die("Unexpected end of input.");
    output_param result;
    if (source[0] == '*') {
      result.output = parse_address();
    } else {
      if (!source.starts_with("base[")) die("Expected *x or base[x].");
      result.output = parse_relative();
    }
    skip_whitespace();
    if (!source.empty() && source[0] == '@') {
      eat("@");
      result.label = parse_name().value;
    }
    return result;
  }

  calculation parse_calculation() {
    auto a = parse_input_param();
    eat(",");
    auto b = parse_input_param();
    eat(",");
    auto c = parse_output_param();
    return {a, b, c};
  }

  jump parse_jump() {
    auto condition = parse_input_param();
    eat(",");
    auto target = parse_input_param();
    return {condition, target};
  }

  instruction parse_instruction(std::string_view mnemonic) {
    if (mnemonic == "add") return add{parse_calculation()};
    if (mnemonic == "mul") return mul{parse_calculation()};
    if (mnemonic == "lt") return less_than{parse_calculation()};
    if (mnemonic == "eq") return equals{parse_calculation()};
    if (mnemonic == "in") return input{parse_output_param()};
    if (mnemonic == "out") return output{parse_input_param()};
    if (mnemonic == "jnz") return jump_if_true{parse_jump()};
    if (mnemonic == "jz") return jump_if_false{parse_jump()};
    if (mnemonic == "arb") return adjust_relative_base{parse_input_param()};
    if (mnemonic == "halt") return halt{};
    std::ostringstream message;
    message << "Unknown op " << std::quoted(mnemonic) << ".";
    die(message.str());
  }

  label parse_label(std::string_view name) {
    eat(":");
    return label{std::string{name}};
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

  directive parse_directive() {
    eat(".");
    auto [id] = parse_name();
    if (id == "define") {
      auto [name] = parse_name();
      auto value = parse_input_param();
      return define{name, value};
    } else if (id == "int") {
      auto value = parse_immediate();
      return integer{value};
    } else if (id == "ascii") {
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
      return ascii{std::move(value)};
    } else {
      die("Invalid directive.");
    }
  }

  statement parse_statement() {
    skip_whitespace();
    char lookahead = peek();
    if (lookahead == '.') {
      return parse_directive();
    } else if (std::isalnum(lookahead)) {
      auto [id] = parse_name();
      skip_whitespace();
      if (!source.empty() && source[0] == ':') {
        eat(":");
        return label{id};
      } else {
        return parse_instruction(id);
      }
    } else {
      die("Expected label or instruction.");
    }
  }

  void parse_newline() {
    skip_whitespace();
    if (get() != '\n') die("Expected newline.");
  }

  std::vector<statement> parse_program() {
    skip_whitespace();
    std::vector<statement> output;
    while (!source.empty()) {
      if (source[0] != '\n') output.push_back(parse_statement());
      parse_newline();
      skip_whitespace();
    }
    return output;
  }
};

export std::vector<statement> parse(
    std::string_view file, std::string_view source) {
  return parser{file, source}.parse_program();
}

}  // namespace as
