/**
 * @file Parser.cpp
 * @brief Implementation of the calc language recursive-descent parser.
 *
 * Each method in this file corresponds exactly to one production in the
 * context-free grammar (CFG) defined in Parser.h.  See that file for the
 * full grammar and the production-to-method mapping.
 *
 * AST construction strategy
 * -------------------------
 * The parser constructs the AST bottom-up via left-fold loops:
 *
 *   expr  → left = parseTerm()
 *            while (+/-): left = BinaryOp(op, left, parseTerm())   ← fold
 *
 *   term  → left = parseFactor()
 *            while (*/ ): left = BinaryOp(op, left, parseFactor()) ← fold
 *
 * This naturally encodes left-associativity without explicit precedence
 * climbing — higher-precedence operators are handled deeper in the call stack.
 *
 * Operator precedence (high → low)
 * ----------------------------------
 *   1. Parentheses   ( )    — via recursive parseExpr() call in parseFactor()
 *   2. Multiply / Divide    — parseTerm() loop
 *   3. Add / Subtract       — parseExpr() loop
 */

#include "Parser.h"

// ---------------------------------------------------------------------------
// Parser::parse — top-level entry point
// ---------------------------------------------------------------------------

/**
 * @brief Drives a full parse of the input.
 *
 * Delegates to @ref parseCalc() and then asserts that the input is fully
 * consumed by checking for the @c Token::eoi sentinel.  If the stream still
 * has tokens after the top-level production, an error is reported.
 *
 * @return The root @ref AST node produced by @ref parseCalc(), or @c nullptr
 *         on error.
 */
AST *Parser::parse() {
  AST *Res = parseCalc();
  expect(Token::eoi); // Confirm the entire input was consumed.
  return Res;
}

// ---------------------------------------------------------------------------
// Parser::parseCalc — top-level "calc" production
// ---------------------------------------------------------------------------

/**
 * @brief Parses the @c calc production (optional "with" clause + expression).
 *
 * Grammar:
 * @code
 *   calc ::= ( "with" ident ( "," ident )* ":" )? expr
 * @endcode
 *
 * If the "with" keyword is present the method:
 *  1. Consumes "with".
 *  2. Reads one or more comma-separated identifiers into @c Vars.
 *  3. Consumes the mandatory ":" separator.
 *  4. Parses the expression body via @ref parseExpr().
 *  5. Returns a @ref WithDecl node that pairs the variable list with the expr.
 *
 * Without a "with" clause, the expression is returned directly as a bare
 * @ref Expr node.
 *
 * Error recovery: on any error the code jumps to @c _error, which drains all
 * remaining tokens so the parser reaches @c Token::eoi cleanly.
 *
 * @return @ref WithDecl or @ref Expr root node; @c nullptr on error.
 */
AST *Parser::parseCalc() {
  Expr *E;
  llvm::SmallVector<llvm::StringRef, 8> Vars;

  if (Tok.is(Token::KW_with)) {
    advance(); // consume "with"

    // Expect at least one identifier after "with".
    if (expect(Token::ident))
      goto _error;
    Vars.push_back(Tok.getText());
    advance(); // consume the first identifier

    // Collect additional comma-separated identifiers.
    while (Tok.is(Token::comma)) {
      advance(); // consume ","
      if (expect(Token::ident))
        goto _error;
      Vars.push_back(Tok.getText());
      advance(); // consume identifier
    }

    // Consume the mandatory ":" that separates bindings from the expression.
    if (consume(Token::colon))
      goto _error;
  }

  // Parse the arithmetic expression body.
  E = parseExpr();

  // Verify nothing unexpected follows the expression.
  if (expect(Token::eoi))
    goto _error;

  // Return just the expression if no variable bindings were declared.
  if (Vars.empty())
    return E;
  else
    // Wrap expression in a WithDecl node that carries the variable names.
    return new WithDecl(Vars, E);

_error:
  // Error recovery: drain remaining tokens to reach a clean end state.
  while (Tok.getKind() != Token::eoi)
    advance();
  return nullptr;
}

// ---------------------------------------------------------------------------
// Parser::parseExpr — "expr" production (additive)
// ---------------------------------------------------------------------------

/**
 * @brief Parses a left-associative sequence of additive operations.
 *
 * Grammar:
 * @code
 *   expr ::= term ( ( "+" | "-" ) term )*
 * @endcode
 *
 * The loop iterates as long as the lookahead is @c + or @c -.  On each
 * iteration:
 *  - The operator is recorded.
 *  - The lookahead is advanced.
 *  - @ref parseTerm() provides the right operand.
 *  - The accumulated @c Left is replaced by a new @ref BinaryOp node.
 *
 * Because @ref parseTerm() is called for both operands, multiplication and
 * division are implicitly given higher precedence.
 *
 * @return Root of the expression subtree (@ref BinaryOp or the result of
 *         @ref parseTerm() when no additive operator is present).
 */
Expr *Parser::parseExpr() {
  Expr *Left = parseTerm();
  while (Tok.isOneOf(Token::plus, Token::minus)) {
    BinaryOp::Operator Op = Tok.is(Token::plus)
                                ? BinaryOp::Plus
                                : BinaryOp::Minus;
    advance(); // consume "+" or "-"
    Expr *Right = parseTerm();
    // Left-fold: new BinaryOp becomes the new left sub-tree.
    Left = new BinaryOp(Op, Left, Right);
  }
  return Left;
}

// ---------------------------------------------------------------------------
// Parser::parseTerm — "term" production (multiplicative)
// ---------------------------------------------------------------------------

/**
 * @brief Parses a left-associative sequence of multiplicative operations.
 *
 * Grammar:
 * @code
 *   term ::= factor ( ( "*" | "/" ) factor )*
 * @endcode
 *
 * Structurally identical to @ref parseExpr() but handles @c * and @c / at
 * a higher precedence level by calling @ref parseFactor() for each operand.
 *
 * @return Root of the term subtree (@ref BinaryOp or a leaf @ref Factor).
 */
Expr *Parser::parseTerm() {
  Expr *Left = parseFactor();
  while (Tok.isOneOf(Token::star, Token::slash)) {
    BinaryOp::Operator Op =
        Tok.is(Token::star) ? BinaryOp::Mul : BinaryOp::Div;
    advance(); // consume "*" or "/"
    Expr *Right = parseFactor();
    // Left-fold: new BinaryOp becomes the new left sub-tree.
    Left = new BinaryOp(Op, Left, Right);
  }
  return Left;
}

// ---------------------------------------------------------------------------
// Parser::parseFactor — "factor" production (primary expressions)
// ---------------------------------------------------------------------------

/**
 * @brief Parses a primary factor: an identifier, a number, or a parenthesised
 *        sub-expression.
 *
 * Grammar:
 * @code
 *   factor ::= ident | number | "(" expr ")"
 * @endcode
 *
 * Cases:
 *  - @c Token::number  — creates a @ref Factor node with @c Factor::Number.
 *  - @c Token::ident   — creates a @ref Factor node with @c Factor::Ident.
 *  - @c Token::l_paren — consumes "(", recursively calls @ref parseExpr(),
 *                         then consumes ")".
 *  - Anything else     — triggers error recovery; skips tokens until a
 *                         synchronisation point (@c ) * + - / eoi) is found
 *                         so the parent production can continue safely.
 *
 * @return Pointer to the leaf @ref Factor or parenthesised @ref Expr.
 */
Expr *Parser::parseFactor() {
  Expr *Res = nullptr;
  switch (Tok.getKind()) {

  case Token::number:
    // Leaf node: integer literal.
    Res = new Factor(Factor::Number, Tok.getText());
    advance();
    break;

  case Token::ident:
    // Leaf node: variable reference — semantic analysis will verify it is
    // declared in the enclosing "with" clause.
    Res = new Factor(Factor::Ident, Tok.getText());
    advance();
    break;

  case Token::l_paren:
    // Parenthesised sub-expression: recurse into parseExpr() for the inner
    // expression, then consume the closing ")".
    advance();        // consume "("
    Res = parseExpr();
    if (!consume(Token::r_paren)) break; // consume ")" on success
    // Fall through to error recovery if ")" is missing.

  default:
    // Error recovery: report the unexpected token, then skip ahead until a
    // token that can legally follow a factor is found.
    if (!Res)
      error();
    while (!Tok.isOneOf(Token::r_paren, Token::star,
                        Token::plus, Token::minus,
                        Token::slash, Token::eoi))
      advance();
  }
  return Res;
}
