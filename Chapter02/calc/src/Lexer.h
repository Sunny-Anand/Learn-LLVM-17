/**
 * @file Lexer.h
 * @brief Lexical analysis (tokenisation) for the calc expression language.
 *
 * The lexer is the first stage of the front-end pipeline:
 *
 *   Source text  ──►  Lexer  ──►  Token stream  ──►  Parser
 *
 * Responsibilities
 * ----------------
 * - Scan the raw source buffer character-by-character.
 * - Classify each run of characters into a typed @ref Token.
 * - Skip whitespace; recognise the single keyword "with".
 *
 * The calc language token vocabulary (as a regular grammar):
 *
 *   whitespace  ::= ' ' | '\t' | '\f' | '\v' | '\r' | '\n'
 *   digit       ::= [0-9]
 *   letter      ::= [a-zA-Z]
 *   number      ::= digit+
 *   ident       ::= letter+
 *   KW_with     ::= "with"          -- reserved word
 *   operator    ::= '+' | '-' | '*' | '/'
 *   delimiter   ::= '(' | ')' | ':' | ','
 */

#ifndef LEXER_H
#define LEXER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/MemoryBuffer.h"

/// Forward declaration required so Token can grant Lexer friendship before
/// the Lexer class is fully defined.
class Lexer;

// ---------------------------------------------------------------------------
// Token
// ---------------------------------------------------------------------------

/**
 * @class Token
 * @brief Represents a single lexical unit produced by the @ref Lexer.
 *
 * A token bundles two pieces of information:
 *  - **Kind**  – what category the token belongs to (see @ref TokenKind).
 *  - **Text**  – a non-owning @c llvm::StringRef slice of the original source
 *                buffer (zero-copy; valid as long as the source buffer lives).
 *
 * Construction and mutation are private; only @ref Lexer (via friendship)
 * may create or modify tokens, which ensures no token is ever in an
 * inconsistent state visible to higher compiler stages.
 */
class Token {
  friend class Lexer; ///< Lexer fills in Kind and Text directly.

public:
  /**
   * @enum TokenKind
   * @brief Enumeration of every distinct token type in the calc language.
   *
   * The underlying type is @c unsigned @c short to keep Token objects small.
   */
  enum TokenKind : unsigned short {
    eoi,      ///< End of input — emitted when the lexer has consumed all source characters.
    unknown,  ///< Unrecognised character; used for error recovery.
    ident,    ///< Identifier: one or more ASCII letters (e.g. "x", "abc").
    number,   ///< Integer literal: one or more decimal digits (e.g. "42").
    comma,    ///< ',' — separates variable names in a "with" declaration.
    colon,    ///< ':' — separates the variable list from the expression.
    plus,     ///< '+' — addition operator.
    minus,    ///< '-' — subtraction operator.
    star,     ///< '*' — multiplication operator.
    slash,    ///< '/' — division operator.
    l_paren,  ///< '(' — opens a sub-expression.
    r_paren,  ///< ')' — closes a sub-expression.
    KW_with   ///< Reserved keyword "with", used to introduce variable bindings.
  };

private:
  TokenKind Kind;      ///< The category of this token.
  llvm::StringRef Text;///< Source slice corresponding to this token (non-owning).

public:
  /// Returns the kind of this token.
  TokenKind getKind() const { return Kind; }

  /// Returns the raw source text that was scanned to form this token.
  llvm::StringRef getText() const { return Text; }

  /// Returns @c true when this token's kind equals @p K.
  bool is(TokenKind K) const { return Kind == K; }

  /// Returns @c true when this token's kind is either @p K1 or @p K2.
  bool isOneOf(TokenKind K1, TokenKind K2) const {
    return is(K1) || is(K2);
  }

  /// Variadic overload — returns @c true if the token matches any of the
  /// supplied kinds.  Used by the parser to check several alternatives in
  /// one call (e.g. @c isOneOf(plus, minus)).
  template <typename... Ts>
  bool isOneOf(TokenKind K1, TokenKind K2, Ts... Ks) const {
    return is(K1) || isOneOf(K2, Ks...);
  }
};

// ---------------------------------------------------------------------------
// Lexer
// ---------------------------------------------------------------------------

/**
 * @class Lexer
 * @brief Incremental scanner that converts a source buffer into @ref Token
 *        values on demand.
 *
 * The Lexer holds two raw pointers into the source buffer:
 *  - @c BufferStart — the immutable beginning of the input.
 *  - @c BufferPtr   — the current scan position; advances with each token.
 *
 * The interface is deliberately minimal: call @ref next() repeatedly to pull
 * one token at a time.  The final token returned will always have kind
 * @c Token::eoi.
 *
 * Example usage:
 * @code
 *   Lexer Lex(Buffer);
 *   Token Tok;
 *   do {
 *     Lex.next(Tok);
 *   } while (!Tok.is(Token::eoi));
 * @endcode
 */
class Lexer {
  const char *BufferStart; ///< Pointer to the first byte of the source buffer.
  const char *BufferPtr;   ///< Pointer to the next unscanned byte.

public:
  /**
   * @brief Constructs the Lexer and positions it at the start of @p Buffer.
   * @param Buffer  A @c StringRef over the entire source input.
   *                The buffer must outlive the Lexer.
   */
  Lexer(const llvm::StringRef &Buffer) {
    BufferStart = Buffer.begin();
    BufferPtr = BufferStart;
  }

  /**
   * @brief Scans the next token from the source and stores it in @p token.
   *
   * On each call the lexer:
   *  1. Skips leading whitespace.
   *  2. Checks for end-of-input → sets kind to @c Token::eoi.
   *  3. Dispatches on the first character:
   *     - Letter → scans an identifier or the keyword "with".
   *     - Digit  → scans an integer number literal.
   *     - Symbol → maps a single character to its operator/delimiter token.
   *     - Other  → sets kind to @c Token::unknown (error recovery).
   *
   * @param[out] token  Receives the scanned token.
   */
  void next(Token &token);

private:
  /**
   * @brief Finalises a token by recording its kind and source-text slice.
   *
   * Sets @p Result.Kind to @p Kind, sets @p Result.Text to the source slice
   * [@c BufferPtr, @p TokEnd), then advances @c BufferPtr to @p TokEnd.
   *
   * @param Result   The token to populate.
   * @param TokEnd   One-past-the-end pointer of the token's source text.
   * @param Kind     The @ref Token::TokenKind to assign.
   */
  void formToken(Token &Result, const char *TokEnd,
                 Token::TokenKind Kind);
};
#endif
