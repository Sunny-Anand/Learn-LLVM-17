/**
 * @file Sema.cpp
 * @brief Implementation of semantic analysis for the calc expression language.
 *
 * This file implements the single semantic rule of the calc language:
 *
 *   **Every identifier referenced in the expression body must have been
 *   declared in the leading "with" clause, and no identifier may be
 *   declared more than once.**
 *
 * Architecture
 * ------------
 * The analysis is structured as an @ref ASTVisitor subclass (@c DeclCheck)
 * that traverses the AST in declaration order:
 *
 *   WithDecl::accept()
 *       └─ DeclCheck::visit(WithDecl)
 *             1. Insert each declared variable into Scope (StringSet).
 *                → Error if a name is already present ("declared twice").
 *             2. Recurse into the expression body:
 *                └─ BinaryOp / Factor subtree traversal
 *                      └─ DeclCheck::visit(Factor)
 *                            → Error if Ident is not in Scope ("not declared").
 *
 * Scope representation
 * --------------------
 * @c llvm::StringSet<> provides O(1) insertion and lookup keyed on
 * @c StringRef values.  Because @c StringRef is a non-owning view into the
 * original source buffer, no string copies are made during the analysis.
 *
 * Error reporting
 * ---------------
 * Errors are emitted to @c llvm::errs() and the @c HasError flag is latched.
 * Analysis continues after an error so that all issues in a single expression
 * are reported in one compiler invocation.
 */

#include "Sema.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/Support/raw_ostream.h"

namespace {

// ---------------------------------------------------------------------------
// DeclCheck — ASTVisitor that enforces variable declaration rules
// ---------------------------------------------------------------------------

/**
 * @class DeclCheck
 * @brief AST visitor that checks variable declaration and usage rules.
 *
 * Implements the @ref ASTVisitor interface and is instantiated privately
 * inside @ref Sema::semantic().  It encapsulates all state needed for one
 * analysis run:
 *
 *  - @c Scope  — the set of variable names introduced by the "with" clause.
 *  - @c HasError — latched flag; set on the first error, never cleared.
 *
 * The visitor is intentionally anonymous (inside an unnamed namespace) so
 * that no external code can create or reuse it directly.
 */
class DeclCheck : public ASTVisitor {
  /// Set of variable names declared in the "with" clause of the current calc
  /// expression.  Populated by visit(WithDecl) before the expression is walked.
  llvm::StringSet<> Scope;

  /// Latched error flag — true if at least one semantic error has been found.
  bool HasError;

  /// Enumeration of the two declaration errors this visitor can detect.
  enum ErrorType {
    Twice, ///< A variable was declared more than once in the "with" clause.
    Not    ///< A variable was used in the expression but never declared.
  };

  /**
   * @brief Emits a diagnostic to stderr and latches HasError.
   *
   * @param ET  @c Twice ("already declared") or @c Not ("not declared").
   * @param V   The variable name involved in the error.
   */
  void error(ErrorType ET, llvm::StringRef V) {
    llvm::errs() << "Variable " << V << " "
                 << (ET == Twice ? "already" : "not")
                 << " declared\n";
    HasError = true;
  }

public:
  DeclCheck() : HasError(false) {}

  /// Returns @c true if any semantic error was detected during the traversal.
  bool hasError() { return HasError; }

  // -------------------------------------------------------------------------
  // ASTVisitor overrides
  // -------------------------------------------------------------------------

  /**
   * @brief Validates a @ref Factor leaf node.
   *
   * Only @c Factor::Ident nodes require a check — a number literal is always
   * valid.  For identifiers, the method looks up the name in @c Scope:
   *  - Found     → no error; the variable was properly declared.
   *  - Not found → emits a "not declared" error.
   *
   * @param Node  The Factor node being visited.
   */
  virtual void visit(Factor &Node) override {
    if (Node.getKind() == Factor::Ident) {
      // Verify the identifier exists in the "with" declaration scope.
      if (Scope.find(Node.getVal()) == Scope.end())
        error(Not, Node.getVal());
    }
  };

  /**
   * @brief Validates a @ref BinaryOp interior node.
   *
   * Recursively visits both the left and right child subtrees.  A null child
   * pointer (which should never arise from a correct parse, but is defended
   * against here) causes @c HasError to be latched directly without emitting
   * a variable-name diagnostic.
   *
   * @param Node  The BinaryOp node being visited.
   */
  virtual void visit(BinaryOp &Node) override {
    if (Node.getLeft())
      Node.getLeft()->accept(*this);  // recurse into left subtree
    else
      HasError = true; // malformed AST: missing left operand
    if (Node.getRight())
      Node.getRight()->accept(*this); // recurse into right subtree
    else
      HasError = true; // malformed AST: missing right operand
  };

  /**
   * @brief Processes a @ref WithDecl root node.
   *
   * Two-phase operation:
   *
   * **Phase 1 — build the scope:**
   * Iterates over the declared variable names and inserts each into
   * @c Scope via @c StringSet::insert().  If @c insert() returns @c false
   * the name was already present → emits "already declared" error.
   *
   * **Phase 2 — validate the expression body:**
   * Calls @c accept() on the expression subtree, which recursively visits
   * all @ref Factor and @ref BinaryOp nodes using the populated @c Scope.
   *
   * @param Node  The WithDecl root node being visited.
   */
  virtual void visit(WithDecl &Node) override {
    // Phase 1: populate scope with all declared variables.
    for (auto I = Node.begin(), E = Node.end(); I != E; ++I) {
      if (!Scope.insert(*I).second)
        // insert() returns false when the key already exists → duplicate decl.
        error(Twice, *I);
    }
    // Phase 2: walk the expression body, checking every identifier usage.
    if (Node.getExpr())
      Node.getExpr()->accept(*this);
    else
      HasError = true; // malformed AST: WithDecl has no expression body
  };
};

} // anonymous namespace

// ---------------------------------------------------------------------------
// Sema::semantic — public entry point
// ---------------------------------------------------------------------------

/**
 * @brief Runs the full semantic analysis pass over the given AST.
 *
 * Creates a @c DeclCheck visitor, triggers the traversal by calling
 * @c Tree->accept(Check), and returns the error status.
 *
 * If @p Tree is @c nullptr (i.e. the parser already reported a fatal error),
 * the function returns @c false — there is nothing to validate and the
 * caller should not proceed to code generation anyway.
 *
 * @param Tree  Root AST node returned by @ref Parser::parse().
 * @return @c true  — semantic errors were found; do not generate code.
 *         @c false — no semantic errors (or Tree was null).
 */
bool Sema::semantic(AST *Tree) {
  if (!Tree)
    return false; // Nothing to check; parse already failed.
  DeclCheck Check;
  Tree->accept(Check); // Drive the visitor over the entire AST.
  return Check.hasError();
}
