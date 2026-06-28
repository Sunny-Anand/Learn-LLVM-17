/**
 * @file Lexer.cpp
 * @brief Implementation of the calc language lexer.
 *
 * Tokenisation pipeline
 * ---------------------
 *
 *   Raw source buffer (llvm::StringRef)
 *           │
 *           ▼
 *   Lexer::next()  ◄─── called once per token by the parser
 *           │
 *     ┌─────┴──────┐
 *     │ skip       │ charinfo::isWhitespace() — advance past spaces/tabs/…
 *     │ whitespace │
 *     └─────┬──────┘
 *           │
 *     ┌─────▼──────┐
 *     │ classify   │
 *     │ first char │
 *     └─────┬──────┘
 *           │
 *   ┌───────┼───────────┬──────────────┐
 *   │       │           │              │
 * letter  digit      symbol          '\0'
 *   │       │           │              │
 * ident/  number    operator/       Token::eoi
 * KW_with            delimiter
 *           │
 *           ▼
 *   Lexer::formToken() — stamp Kind + Text slice, advance BufferPtr
 */

#include "Lexer.h"

// ---------------------------------------------------------------------------
// charinfo — internal character-classification helpers
// ---------------------------------------------------------------------------

/**
 * @namespace charinfo
 * @brief Private helpers for ASCII character classification.
 *
 * Each function is marked @c LLVM_READNONE (pure, no side-effects) and
 * @c inline so the compiler can eliminate every call at optimisation time.
 * Using a dedicated namespace prevents accidental name collision with
 * standard-library functions such as @c std::isdigit.
 */
namespace charinfo {

/**
 * @brief Returns @c true for any standard whitespace character.
 *
 * Recognised characters: space (@c ' '), horizontal tab (@c '\\t'),
 * form feed (@c '\\f'), vertical tab (@c '\\v'), carriage return (@c '\\r'),
 * and newline (@c '\\n').
 *
 * @c LLVM_READNONE guarantees no memory reads/writes; the compiler may
 * hoist, cache, or eliminate this call freely.
 */
LLVM_READNONE inline bool isWhitespace(char c) {
  return c == ' ' || c == '\t' || c == '\f' || c == '\v' ||
         c == '\r' || c == '\n';
}

/**
 * @brief Returns @c true when @p c is an ASCII decimal digit (@c '0'–@c '9').
 *
 * Used to scan integer number literals.
 */
LLVM_READNONE inline bool isDigit(char c) {
  return c >= '0' && c <= '9';
}

/**
 * @brief Returns @c true when @p c is an ASCII upper- or lower-case letter.
 *
 * Used to scan identifiers and the reserved keyword @c "with".
 * Note: digits within identifiers are not supported by the calc grammar,
 * so only pure-letter sequences are recognised.
 */
LLVM_READNONE inline bool isLetter(char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

} // namespace charinfo

// ---------------------------------------------------------------------------
// Lexer::next — main tokenisation entry point
// ---------------------------------------------------------------------------

/**
 * @brief Scans the next token from the source buffer.
 *
 * Algorithm (in order):
 *
 * 1. **Whitespace skipping** — advance @c BufferPtr past any whitespace.
 *
 * 2. **End-of-input** — if @c *BufferPtr is the null terminator, emit
 *    @c Token::eoi and return.  The null terminator is inserted by LLVM's
 *    @c MemoryBuffer and acts as a safe sentinel.
 *
 * 3. **Identifier / keyword scan** — when the first character is a letter,
 *    consume consecutive letters, then check whether the resulting string
 *    equals the single reserved word @c "with":
 *    - Match  → emit @c Token::KW_with
 *    - No match → emit @c Token::ident
 *
 * 4. **Number literal scan** — when the first character is a digit, consume
 *    all consecutive digits and emit @c Token::number.
 *
 * 5. **Single-character operators and delimiters** — a @c switch handles
 *    each punctuation character in one step using the @c CASE helper macro.
 *    Any unrecognised character emits @c Token::unknown for error recovery.
 *
 * In every branch, @ref formToken() is used to finalise the token.
 *
 * @param[out] token  The @ref Token object to be filled in.
 */
void Lexer::next(Token &token) {
  // Step 1: skip all leading whitespace.
  while (*BufferPtr && charinfo::isWhitespace(*BufferPtr)) {
    ++BufferPtr;
  }

  // Step 2: end-of-input sentinel — null terminator inserted by MemoryBuffer.
  if (!*BufferPtr) {
    token.Kind = Token::eoi;
    return;
  }

  // Step 3: identifier or keyword — starts with a letter.
  if (charinfo::isLetter(*BufferPtr)) {
    const char *end = BufferPtr + 1;
    // Consume all consecutive letters (digits not allowed in this language).
    while (charinfo::isLetter(*end))
      ++end;
    llvm::StringRef Name(BufferPtr, end - BufferPtr);
    // Keyword check: the only reserved word is "with".
    Token::TokenKind kind =
        Name == "with" ? Token::KW_with : Token::ident;
    formToken(token, end, kind);
    return;
  }
  // Step 4: number literal — starts with a digit.
  else if (charinfo::isDigit(*BufferPtr)) {
    const char *end = BufferPtr + 1;
    // Consume all consecutive decimal digits.
    while (charinfo::isDigit(*end))
      ++end;
    formToken(token, end, Token::number);
    return;
  }
  // Step 5: single-character operators and delimiters.
  else {
    switch (*BufferPtr) {
/// Helper macro — matches character @p ch and emits token kind @p tok,
/// then breaks out of the switch.  Advances BufferPtr by exactly 1.
#define CASE(ch, tok) \
case ch: formToken(token, BufferPtr + 1, tok); break
      CASE('+', Token::plus);
      CASE('-', Token::minus);
      CASE('*', Token::star);
      CASE('/', Token::slash);
      CASE('(', Token::Token::l_paren);
      CASE(')', Token::Token::r_paren);
      CASE(':', Token::Token::colon);
      CASE(',', Token::Token::comma);
#undef CASE
      default:
        // Unknown character — emit Token::unknown and advance 1 byte so the
        // lexer always makes forward progress (prevents infinite loops).
        formToken(token, BufferPtr + 1, Token::unknown);
    }
    return;
  }
}

// ---------------------------------------------------------------------------
// Lexer::formToken — token finalisation helper
// ---------------------------------------------------------------------------

/**
 * @brief Stamps a token's kind and source-text slice, then advances the scan
 *        position.
 *
 * This is called at the end of every successful branch in @ref next() to
 * uniformly handle three concerns:
 *  1. Assign the resolved @p Kind.
 *  2. Build a @c StringRef that spans exactly [@c BufferPtr, @p TokEnd) —
 *     a zero-copy view into the original source buffer.
 *  3. Advance @c BufferPtr to @p TokEnd so the next call to @ref next()
 *     begins scanning from the correct position.
 *
 * @param Tok     Token to populate (kind + text).
 * @param TokEnd  One-past-the-end pointer of the matched source text.
 * @param Kind    The @ref Token::TokenKind to assign.
 */
void Lexer::formToken(Token &Tok, const char *TokEnd,
                      Token::TokenKind Kind) {
  Tok.Kind = Kind;
  Tok.Text = llvm::StringRef(BufferPtr, TokEnd - BufferPtr);
  BufferPtr = TokEnd;
}
