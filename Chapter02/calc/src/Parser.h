/**
 * @file Parser.h
 * @brief Recursive-descent parser for the calc expression language.
 *
 * The parser is the second stage of the front-end pipeline:
 *
 *   Token stream (from Lexer)  ──►  Parser  ──►  AST
 *
 * Context-Free Grammar (CFG)
 * --------------------------
 * The calc language is defined by the following LL(1) grammar.
 * Each non-terminal maps directly to one private parse method.
 *
 *   calc    ::= ( "with" ident ( "," ident )* ":" )? expr
 *
 *   expr    ::= term  ( ( "+" | "-" ) term  )*
 *
 *   term    ::= factor ( ( "*" | "/" ) factor )*
 *
 *   factor  ::= ident
 *             | number
 *             | "(" expr ")"
 *
 * Production-to-method mapping
 * ----------------------------
 *   calc    →  parseCalc()   — handles optional "with" variable declarations.
 *   expr    →  parseExpr()   — handles additive operators (left-associative).
 *   term    →  parseTerm()   — handles multiplicative operators (left-assoc).
 *   factor  →  parseFactor() — handles leaf nodes and parenthesised sub-exprs.
 *
 * AST nodes generated
 * -------------------
 *   WithDecl  — root node when "with" bindings are present.
 *   BinaryOp  — left-fold tree of operator + left child + right child.
 *   Factor    — leaf node: either an identifier reference or a number literal.
 *
 * Error handling
 * --------------
 * Errors are reported via llvm::errs() and the HasError flag is set.
 * Parse methods attempt to recover by skipping tokens until a safe
 * synchronisation point is reached, preventing a cascade of spurious errors.
 */

#ifndef PARSER_H
#define PARSER_H

#include "AST.h"
#include "Lexer.h"
#include "llvm/Support/raw_ostream.h"

/**
 * @class Parser
 * @brief Recursive-descent, single-token lookahead (LL(1)) parser.
 *
 * Consumes the @ref Token stream produced by @ref Lexer and builds an
 * Abstract Syntax Tree (see AST.h).
 *
 * Ownership model
 * ---------------
 * All AST nodes are heap-allocated with @c new.  The caller of @ref parse()
 * takes ownership of the returned root node and is responsible for its
 * lifetime.  If an unrecoverable error occurs, @c nullptr is returned and
 * no partial tree is leaked.
 *
 * Typical usage:
 * @code
 *   Lexer Lex(sourceBuffer);
 *   Parser P(Lex);
 *   AST *tree = P.parse();
 *   if (!P.hasError() && tree) {
 *     // proceed to semantic analysis
 *   }
 * @endcode
 */
class Parser {
  Lexer &Lex;     ///< Reference to the lexer (token source).
  Token Tok;      ///< Single-token lookahead (current token).
  bool HasError;  ///< Set to true when any parse error is detected.

  /**
   * @brief Reports an unexpected-token error to stderr and sets HasError.
   *
   * Prints the text of the offending token.  Does not throw or abort —
   * parsing continues to collect further errors where possible.
   */
  void error() {
    llvm::errs() << "Unexpected: " << Tok.getText() << "\n";
    HasError = true;
  }

  /**
   * @brief Advances the lookahead by fetching the next token from the lexer.
   */
  void advance() { Lex.next(Tok); }

  /**
   * @brief Checks that the current token has the expected @p Kind.
   *
   * Does **not** consume the token.  Returns @c true (error) if the kinds
   * do not match, which allows callers to branch on the error condition.
   *
   * @param Kind  The expected token kind.
   * @return @c false on success, @c true on mismatch.
   */
  bool expect(Token::TokenKind Kind) {
    if (!Tok.is(Kind)) {
      error();
      return true;
    }
    return false;
  }

  /**
   * @brief Checks the expected kind and, on success, advances the lookahead.
   *
   * Combines @ref expect() with @ref advance() — the common pattern for
   * consuming a mandatory punctuation token (e.g. a colon after a variable
   * list).
   *
   * @param Kind  The token kind to match and consume.
   * @return @c false on success, @c true on mismatch.
   */
  bool consume(Token::TokenKind Kind) {
    if (expect(Kind))
      return true;
    advance();
    return false;
  }

  // -------------------------------------------------------------------------
  // Parse methods — one per grammar production (see CFG in file header)
  // -------------------------------------------------------------------------

  /**
   * @brief Parses the top-level @c calc production.
   *
   * Grammar:
   * @code
   *   calc ::= ( "with" ident ( "," ident )* ":" )? expr
   * @endcode
   *
   * If a @c "with" keyword is present, collects variable names into a
   * @c SmallVector and builds a @ref WithDecl root node wrapping the
   * parsed @ref Expr.  Without @c "with", returns the @ref Expr directly.
   *
   * On error: jumps to @c _error, drains remaining tokens, returns @c nullptr.
   *
   * @return Pointer to a @ref WithDecl or @ref Expr node, or @c nullptr.
   */
  AST *parseCalc();

  /**
   * @brief Parses an additive expression (the @c expr production).
   *
   * Grammar:
   * @code
   *   expr ::= term ( ( "+" | "-" ) term )*
   * @endcode
   *
   * Implements left-associative folding: each operator encountered builds
   * a new @ref BinaryOp that wraps the accumulated left sub-tree.
   *
   * @return Pointer to an @ref Expr subtree (@ref BinaryOp or leaf).
   */
  Expr *parseExpr();

  /**
   * @brief Parses a multiplicative term (the @c term production).
   *
   * Grammar:
   * @code
   *   term ::= factor ( ( "*" | "/" ) factor )*
   * @endcode
   *
   * Mirrors @ref parseExpr() for the higher-precedence @c * and @c /
   * operators.  Multiplication binds tighter than addition because
   * @c parseTerm() is called from inside @c parseExpr().
   *
   * @return Pointer to an @ref Expr subtree (@ref BinaryOp or leaf).
   */
  Expr *parseTerm();

  /**
   * @brief Parses a primary factor (the @c factor production).
   *
   * Grammar:
   * @code
   *   factor ::= ident | number | "(" expr ")"
   * @endcode
   *
   * Produces a @ref Factor leaf node for identifiers and number literals.
   * For a parenthesised expression, recursively calls @ref parseExpr() and
   * returns its result.
   *
   * Error recovery: skips tokens until a closing parenthesis or binary
   * operator is found so that the parent production can continue.
   *
   * @return Pointer to a @ref Factor or nested @ref Expr node.
   */
  Expr *parseFactor();

public:
  /**
   * @brief Constructs the parser and primes the lookahead with the first token.
   * @param Lex  The lexer to pull tokens from.  Must outlive the Parser.
   */
  Parser(Lexer &Lex) : Lex(Lex), HasError(false) {
    advance(); // Load the first token into Tok.
  }

  /**
   * @brief Entry point — parses the entire input and returns the AST root.
   *
   * Calls @ref parseCalc(), then verifies that all input has been consumed
   * by checking for @c Token::eoi.
   *
   * @return Root @ref AST node, or @c nullptr if a parse error occurred.
   */
  AST *parse();

  /// Returns @c true if any parse error was encountered during parsing.
  bool hasError() { return HasError; }
};

#endif
