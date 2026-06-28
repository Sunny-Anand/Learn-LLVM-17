/**
 * @file Sema.h
 * @brief Semantic analysis interface for the calc expression language.
 *
 * Semantic analysis is the third stage of the front-end pipeline:
 *
 *   AST (from Parser)  ──►  Sema::semantic()  ──►  validated AST  ──►  CodeGen
 *
 * What is validated?
 * ------------------
 * The calc language has one semantic rule beyond syntactic correctness:
 *
 *   **Variable scope** — every identifier referenced in the expression body
 *   must have been declared in the leading @c "with" clause.
 *
 * Additionally, the well-formedness of the AST is confirmed:
 *   - No @ref BinaryOp node has a @c nullptr left or right child.
 *   - A @ref WithDecl node has a non-null expression body.
 *
 * Implementation
 * --------------
 * @ref Sema delegates all work to the @c DeclCheck visitor (defined in
 * Sema.cpp).  @c DeclCheck walks the AST using the Visitor pattern:
 *   1. On @ref WithDecl — inserts each declared variable into a scope set
 *      and reports a "declared twice" error for duplicates.
 *   2. On @ref Factor (Ident) — looks up the identifier in the scope set
 *      and reports a "not declared" error if it is absent.
 *   3. On @ref BinaryOp — recursively visits both children and flags any
 *      null child as an error.
 *
 * @see Sema.cpp, AST.h
 */

#ifndef SEMA_H
#define SEMA_H

#include "AST.h"
#include "Lexer.h"

/**
 * @class Sema
 * @brief Thin façade that runs semantic analysis over a parsed AST.
 *
 * The class provides a single public method, @ref semantic(), which accepts
 * the root of the AST produced by @ref Parser and returns whether any
 * semantic errors were detected.
 *
 * Typical usage:
 * @code
 *   Sema S;
 *   if (S.semantic(tree)) {
 *     // semantic errors found — do not proceed to code generation
 *   }
 * @endcode
 */
class Sema {
public:
  /**
   * @brief Performs semantic analysis on the given AST.
   *
   * Instantiates a @c DeclCheck visitor and drives it over @p Tree via
   * the Visitor pattern.  Reports all errors to @c llvm::errs().
   *
   * @param Tree  Root of the AST to validate.  May be @c nullptr (e.g. if
   *              the parser already reported an error), in which case the
   *              method returns @c false immediately.
   * @return @c true  if one or more semantic errors were found,
   *         @c false if the AST is semantically valid (or Tree is null).
   */
  bool semantic(AST *Tree);
};

#endif
