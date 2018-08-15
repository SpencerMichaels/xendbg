//
// Created by Spencer Michaels on 8/11/18.
//

#include <iostream>
#include <stdexcept>
#include <functional>

#include "Predicate.hpp"
#include "Operator/Precedence.hpp"
#include "Parser.hpp"
#include "Tokenizer.hpp"
#include "Token/Constant.hpp"
#include "Token/Match.hpp"
#include "Predicate.hpp"
#include "Token/Symbol.hpp"
#include "Token/Variable.hpp"
#include "Expression/Constant.hpp"
#include "Expression/BinaryExpression.hpp"
#include "Expression/Label.hpp"
#include "Expression/Variable.hpp"
#include "Expression/UnaryExpression.hpp"
#include "../Util/clear.hpp"
#include "../Util/pop_ret.hpp"

using xd::parser::Parser;
using xd::parser::expr::BinaryExpression;
using xd::parser::expr::ExpressionPtr;
using xd::parser::expr::UnaryExpression;
using xd::parser::op::precedence_of;
using xd::parser::op::BinaryOperator;
using xd::parser::op::Sentinel;
using xd::parser::op::UnaryOperator;
using xd::parser::token::match_token;
using xd::parser::pred::is_binary_operator_symbol;
using xd::parser::pred::is_constant;
using xd::parser::pred::is_label;
using xd::parser::pred::is_sentinel;
using xd::parser::pred::is_symbol;
using xd::parser::pred::is_symbol_of_type;
using xd::parser::pred::is_unary_operator_symbol;
using xd::parser::pred::is_variable;
using xd::parser::token::Symbol;
using xd::parser::tokenizer::tokenize;
using xd::util::clear;
using xd::util::pop_ret;


Parser::Operator Parser::symbol_to_binop(const Symbol& symbol) {
  using Type = Symbol::Type;

  switch (symbol.type()) {
    case Type::Plus:
      return op::Add();
    case Type::Minus:
      return op::Subtract();
    case Type::Star:
      return op::Multiply();
    case Type::Slash:
      return op::Divide();
    default:
      throw std::runtime_error("Symbol does not represent a binary operator!");
  }
};

Parser::Operator Parser::symbol_to_unop(const Symbol& symbol) {
  using Type = Symbol::Type;

  switch (symbol.type()) {
    case Type::Minus:
      return op::Negate();
    case Type::Star:
      return op::Dereference();
    default:
      throw std::runtime_error("Symbol does not represent a unary operator!");
  }
};

ExpressionPtr Parser::parse(std::string input) {
  _input = input;
  clear(_operands);
  clear(_operators);
  clear(_tokens);

  tokenize(input, _tokens, _tokens_pos);

  _operators.push(Sentinel());
  parse_expression();
  if (!_tokens.empty()) {
    throw except::ExtraTokenException(_input, _tokens_pos.front());
  }

  return _operands.top();
}

void Parser::consume() {
  _tokens.pop();
  _tokens_pos.pop();
}

const Parser::Token& Parser::next_token() {
  return _tokens.front();
}

void Parser::parse_expression() {
  parse_unit();

  while (is_binary_operator_symbol(next_token())) {
    push_operator_and_merge(
        symbol_to_binop(
            std::get<token::Symbol>(next_token())));
    consume();

    if (_tokens.empty())
      throw except::MissingExpressionException(_input, _input.size());
    parse_unit();
  }

  while (!is_sentinel(_operators.top())) {
    pop_operator_and_merge();
  }
}

void Parser::parse_unit() {
  const auto& next = next_token();

  if (is_constant(next)) {
    _operands.push(
        std::make_shared<expr::Constant>(
            std::get<token::Constant>(next).value()));
    consume();
  } else if (is_variable(next)) {
    _operands.push(
        std::make_shared<expr::Variable>(
            std::get<token::Variable>(next).name()));
    consume();
  } else if (is_label(next)) {
    _operands.push(
        std::make_shared<expr::Label>(
            std::get<token::Label>(next).name()));
    consume();
  } else if (is_symbol_of_type(next, Symbol::Type::ParenLeft)) {
    consume();
    _operators.push(Sentinel());
    parse_expression();
    expect([](auto&& token) {
      return is_symbol_of_type(token, Symbol::Type::ParenRight);
    }, "Expected close-paren!");
    _operators.pop();
  } else if (is_unary_operator_symbol(next)) {
    push_operator_and_merge(
        symbol_to_unop(
            std::get<token::Symbol>(next)));
    consume();
    parse_unit();
  } else {
    throw except::MissingExpressionException(_input, _tokens_pos.front());
  }
}

void xd::parser::Parser::push_operator_and_merge(Parser::Operator op) {
  while (precedence_of(_operators.top()) > precedence_of(op)) {
    pop_operator_and_merge();
  }
  _operators.push(op);
}

void xd::parser::Parser::pop_operator_and_merge() {
  std::visit(util::overloaded {
    [](const op::Sentinel& op) {
      throw except::SentinelMergeException();
    },
    [this](const BinaryOperator& op) {
      auto x = pop_ret(_operands);
      auto y = pop_ret(_operands);
      _operands.push(std::make_shared<BinaryExpression>(op, x, y));
    },
    [this](const UnaryOperator& op) {
      auto x = pop_ret(_operands);
      _operands.push(
          std::make_shared<UnaryExpression>(op, x));
    }
  }, pop_ret(_operators));
}
