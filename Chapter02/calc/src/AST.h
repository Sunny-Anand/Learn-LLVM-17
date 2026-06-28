/**
 * @file AST.h
 * @brief Abstract Syntax Tree (AST) node hierarchy for the calc language.
 *
 * The AST is the central data structure produced by the @ref Parser and
 * consumed by both @ref Sema (semantic analysis) and @ref CodeGen (IR
 * generation).  Each node represents a syntactic construct in the source.
 *
 * Node hierarchy
 * --------------
 *
 *   AST  (abstract base)
 *   ├── Expr  (abstract — all value-producing expressions)
 *   │   ├── Factor    — leaf: an integer literal or variable reference
 *   │   └── BinaryOp  — interior: a binary arithmetic operation
 *   └── WithDecl  — root when "with" variable bindings are present
 *
 * Visitor pattern
 * ---------------
 * Traversal is implemented with the classic Visitor design pattern
 * (@ref ASTVisitor).  Any pass that needs to walk the tree — semantic
 * analysis, code generation, pretty-printing — subclasses @ref ASTVisitor
 * and overrides the three concrete @c visit() methods.
 *
 * Every concrete node class implements @c accept(ASTVisitor &V), which
 * calls @c V.visit(*this), enabling double-dispatch: the correct @c visit()
 * overload is selected at runtime based on the concrete node type.
 *
 * Lifetime / ownership
 * --------------------
 * All AST nodes are heap-allocated by the @ref Parser using @c new.
 * The caller of @c Parser::parse() owns the root node and is responsible
 * for eventual deletion (or arena-based bulk free).
 */

#ifndef AST_H
#define AST_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"

// Forward declarations — allow ASTVisitor to reference all node types before
// their full definitions appear below.
class AST;
class Expr;
class Factor;
class BinaryOp;
class WithDecl;

// ---------------------------------------------------------------------------
// ASTVisitor — abstract base for all AST traversal passes
// ---------------------------------------------------------------------------

/**
 * @class ASTVisitor
 * @brief Abstract base class implementing the Visitor pattern over the AST.
 *
 * Passes that traverse the tree (e.g. semantic analysis, code generation)
 * subclass @c ASTVisitor and provide concrete implementations for the three
 * pure-virtual @c visit() overloads.
 *
 * Default implementations for @c AST and @c Expr are no-ops (empty body),
 * because those are abstract base types — visitors act on concrete nodes.
 * The three concrete-node overloads are pure virtual and must be implemented.
 *
 * Usage:
 * @code
 *   struct MyPass : public ASTVisitor {
 *     void visit(Factor &F)   override { ... }
 *     void visit(BinaryOp &B) override { ... }
 *     void visit(WithDecl &W) override { ... }
 *   };
 *   MyPass P;
 *   root->accept(P);   // dispatches to the right visit() at runtime
 * @endcode
 */
class ASTVisitor {
public:
  virtual void visit(AST &){};      ///< Default no-op for the abstract base.
  virtual void visit(Expr &){};     ///< Default no-op for the abstract Expr.
  virtual void visit(Factor &) = 0;   ///< Handle a leaf Factor node.
  virtual void visit(BinaryOp &) = 0; ///< Handle a BinaryOp interior node.
  virtual void visit(WithDecl &) = 0; ///< Handle a WithDecl root node.
};

// ---------------------------------------------------------------------------
// AST — abstract root of the node hierarchy
// ---------------------------------------------------------------------------

/**
 * @class AST
 * @brief Abstract base class for every node in the syntax tree.
 *
 * Provides the virtual @c accept() interface required by the Visitor pattern
 * and a virtual destructor for safe polymorphic deletion.
 */
class AST {
public:
  virtual ~AST() {}

  /**
   * @brief Entry point for the Visitor pattern (double-dispatch).
   *
   * Each concrete subclass implements this to call @c V.visit(*this),
   * which selects the correct @c visit() overload in the visitor.
   *
   * @param V  The visitor to dispatch to.
   */
  virtual void accept(ASTVisitor &V) = 0;
};

// ---------------------------------------------------------------------------
// Expr — abstract base for value-producing expression nodes
// ---------------------------------------------------------------------------

/**
 * @class Expr
 * @brief Abstract base for all expression nodes (i.e. nodes that produce a
 *        numeric value).
 *
 * All arithmetic constructs — leaves and binary operations — inherit from
 * @c Expr.  @c WithDecl does **not** inherit @c Expr because it is a
 * declaration wrapper, not a value.
 */
class Expr : public AST {
public:
  Expr() {}
};

// ---------------------------------------------------------------------------
// Factor — leaf node (identifier or number literal)
// ---------------------------------------------------------------------------

/**
 * @class Factor
 * @brief Leaf node in the expression tree: either an integer literal or a
 *        variable reference.
 *
 * A @c Factor stores:
 *  - **Kind** — distinguishes between @c Ident (variable name) and
 *    @c Number (integer literal).
 *  - **Val**  — a non-owning @c StringRef slice of the original source
 *               text (e.g. @c "42" or @c "x").
 *
 * During semantic analysis (@ref Sema / @ref DeclCheck), any @c Ident factor
 * is checked to ensure the referenced variable was declared in a @c "with"
 * clause.  During code generation (@ref CodeGen), @c Number factors are
 * converted to @c ConstantInt IR values and @c Ident factors look up a
 * runtime-provided variable value.
 */
class Factor : public Expr {
public:
  /// Distinguishes a variable reference from an integer literal.
  enum ValueKind {
    Ident,  ///< Variable reference — validated against the "with" scope.
    Number  ///< Integer literal — converted to a constant during codegen.
  };

private:
  ValueKind Kind;       ///< Whether this is a variable or a number.
  llvm::StringRef Val;  ///< Source text of the literal or identifier name.

public:
  /**
   * @brief Constructs a Factor leaf node.
   * @param Kind  @c Ident or @c Number.
   * @param Val   Non-owning slice of the source buffer for this token.
   */
  Factor(ValueKind Kind, llvm::StringRef Val)
      : Kind(Kind), Val(Val) {}

  /// Returns @c Ident or @c Number.
  ValueKind getKind() { return Kind; }

  /// Returns the source text (variable name or numeric digits).
  llvm::StringRef getVal() { return Val; }

  /// Visitor dispatch — calls @c V.visit(*this) selecting the Factor overload.
  virtual void accept(ASTVisitor &V) override {
    V.visit(*this);
  }
};

// ---------------------------------------------------------------------------
// BinaryOp — interior node (arithmetic operator with two children)
// ---------------------------------------------------------------------------

/**
 * @class BinaryOp
 * @brief Interior expression node representing a binary arithmetic operation.
 *
 * Stores:
 *  - **Op**    — one of Plus, Minus, Mul, Div.
 *  - **Left**  — left operand subtree.
 *  - **Right** — right operand subtree.
 *
 * The tree is left-associative: for an expression such as @c a+b+c the
 * parser builds:
 * @code
 *       +
 *      / \
 *     +   c
 *    / \
 *   a   b
 * @endcode
 *
 * During code generation, a post-order traversal of BinaryOp nodes emits
 * the corresponding LLVM IR arithmetic instructions (@c add, @c sub,
 * @c mul, @c sdiv).
 */
class BinaryOp : public Expr {
public:
  /// The four arithmetic operators supported by the calc language.
  enum Operator {
    Plus,   ///< Addition       ( + )
    Minus,  ///< Subtraction    ( - )
    Mul,    ///< Multiplication ( * )
    Div     ///< Division       ( / )
  };

private:
  Expr *Left;    ///< Left operand subtree (owned by the tree).
  Expr *Right;   ///< Right operand subtree (owned by the tree).
  Operator Op;   ///< The arithmetic operator.

public:
  /**
   * @brief Constructs a BinaryOp node.
   * @param Op  The operator (@c Plus, @c Minus, @c Mul, or @c Div).
   * @param L   Left operand subtree.
   * @param R   Right operand subtree.
   */
  BinaryOp(Operator Op, Expr *L, Expr *R)
      : Op(Op), Left(L), Right(R) {}

  Expr *getLeft()         { return Left; }   ///< Returns the left child.
  Expr *getRight()        { return Right; }  ///< Returns the right child.
  Operator getOperator()  { return Op; }     ///< Returns the operator.

  /// Visitor dispatch — calls @c V.visit(*this) selecting the BinaryOp overload.
  virtual void accept(ASTVisitor &V) override {
    V.visit(*this);
  }
};

// ---------------------------------------------------------------------------
// WithDecl — root node for expressions with variable bindings
// ---------------------------------------------------------------------------

/**
 * @class WithDecl
 * @brief Root AST node for a calc expression that begins with @c "with".
 *
 * Encapsulates:
 *  - **Vars** — a @c SmallVector of variable names declared after @c "with".
 *  - **E**    — the expression body that may reference those variables.
 *
 * Example source:  @code with x, y : x + y * 2 @endcode
 *
 * The corresponding AST:
 * @code
 *   WithDecl { Vars = ["x", "y"],
 *     E = BinaryOp(Plus,
 *           Factor(Ident, "x"),
 *           BinaryOp(Mul, Factor(Ident, "y"), Factor(Number, "2")))
 *   }
 * @endcode
 *
 * Semantic analysis (@ref DeclCheck) processes @c WithDecl by inserting
 * all declared variable names into a scope set, then verifying that every
 * @c Factor::Ident in the expression @c E refers to one of those names.
 *
 * Code generation (@ref CodeGen) emits a @c printf-based runtime that reads
 * each declared variable from @c argv and then evaluates the expression.
 */
class WithDecl : public AST {
  using VarVector = llvm::SmallVector<llvm::StringRef, 8>;
  VarVector Vars;  ///< Ordered list of declared variable names.
  Expr *E;         ///< The expression body.

public:
  /**
   * @brief Constructs a WithDecl node.
   * @param Vars  The list of variable names declared after @c "with".
   * @param E     The expression body that uses those variables.
   */
  WithDecl(llvm::SmallVector<llvm::StringRef, 8> Vars,
           Expr *E)
      : Vars(Vars), E(E) {}

  /// Iterator to the beginning of the declared variable list.
  VarVector::const_iterator begin() { return Vars.begin(); }

  /// Iterator to the end of the declared variable list.
  VarVector::const_iterator end()   { return Vars.end(); }

  /// Returns the expression body associated with these variable bindings.
  Expr *getExpr() { return E; }

  /// Visitor dispatch — calls @c V.visit(*this) selecting the WithDecl overload.
  virtual void accept(ASTVisitor &V) override {
    V.visit(*this);
  }
};
#endif
