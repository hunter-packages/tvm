/*!
 *  Copyright (c) 2019 by Contributors
 * \file tvm/arithmetic/const_int_bound.cc
 */
#include <tvm/arithmetic.h>
#include <tvm/ir_functor_ext.h>
#include <algorithm>
#include "int_op_overflow.h"

namespace tvm {
namespace arith {

using namespace ir;

TVM_REGISTER_NODE_TYPE(ConstIntBoundNode);

ConstIntBound ConstIntBoundNode::make(
    int64_t min_value, int64_t max_value) {
  auto node = make_node<ConstIntBoundNode>();
  node->min_value = min_value;
  node->max_value = max_value;
  return ConstIntBound(node);
}

TVM_STATIC_IR_FUNCTOR(IRPrinter, vtable)
.set_dispatch<ConstIntBoundNode>([](const ConstIntBoundNode *op, IRPrinter *p) {
    p->stream << "ConstIntBound"
              << "[" << op->min_value << ", "
              << op->max_value << ']';
  });

// internal entry for const int bound
struct ConstIntBoundAnalyzer::Entry {
  int64_t min_value;
  int64_t max_value;

  bool is_const(int64_t value) const {
    return min_value == max_value && min_value == value;
  }

  bool operator==(const Entry& other) const {
    return min_value == other.min_value && max_value == other.max_value;
  }
};

class ConstIntBoundAnalyzer::Impl :
      public ExprFunctor<ConstIntBoundAnalyzer::Entry(const Expr&)> {
 public:
  void Bind(const Var& var, const Range& range) {
    Entry a = VisitExpr(range->min);
    Entry b = VisitExpr(range->extent);
    Entry ret;
    ret.min_value = a.min_value;
    ret.max_value = InfAwareAdd(a.max_value, InfAwareAdd(b.max_value, -1));
    Update(var, ret, false);
  }

  void Update(const Var& var,
              const Entry& info,
              bool override) {
    if (!override) {
      auto it = var_map_.find(var);
      if (it != var_map_.end()) {
        CHECK(it->second == info)
          << "var \'" << var << "\' already updated.";
      }
    }
    var_map_[var] = info;
  }

  void Update(const Var& var,
              const ConstIntBound& info,
              bool override) {
    Update(var, MakeBound(info->min_value, info->max_value), override);
  }

  // Override visitor behaviors
  Entry VisitExprDefault_(const Node* op) final {
    return Everything(
        static_cast<const ir::BaseExprNode*>(op)->type);
  }

  Entry VisitExpr_(const Cast* op) final {
    Entry a = VisitExpr(op->value);
    Entry b = Everything(op->type);
    return Intersect(a, b);
  }

  Entry VisitExpr_(const IntImm* op) final {
    return MakeBound(op->value, op->value);
  }

  Entry VisitExpr_(const UIntImm* op) final {
    if (op->value <= static_cast<uint64_t>(kPosInf)) {
      return MakeBound(op->value, op->value);
    } else {
      return Everything(op->type);
    }
  }

  Entry VisitExpr_(const Add* op) final {
    Entry a = VisitExpr(op->a);
    Entry b = VisitExpr(op->b);
    Entry ret;
    ret.min_value = InfAwareAdd(a.min_value, b.min_value);
    ret.max_value = InfAwareAdd(a.max_value, b.max_value);
    return ret;
  }

  Entry VisitExpr_(const Sub* op) final {
    Entry a = VisitExpr(op->a);
    Entry b = VisitExpr(op->b);
    Entry ret;
    ret.min_value = InfAwareAdd(a.min_value, -b.max_value);
    ret.max_value = InfAwareAdd(a.max_value, -b.min_value);
    return ret;
  }

  Entry VisitExpr_(const Mul* op) final {
    Entry a = VisitExpr(op->a);
    Entry b = VisitExpr(op->b);
    return BinaryOpBoundry(a, b, InfAwareMul);
  }

  Entry VisitExpr_(const Div* op) final {
    Entry a = VisitExpr(op->a);
    Entry b = VisitExpr(op->b);
    CHECK(!b.is_const(0)) << "divide by zero";
    // assume no division by 0
    if (b.min_value == 0) b.min_value = 1;
    if (b.max_value == 0) b.max_value = -1;
    return BinaryOpBoundry(a, b, InfAwareDiv);
  }

  Entry VisitExpr_(const Mod* op) final {
    Entry a = VisitExpr(op->a);
    Entry b = VisitExpr(op->b);
    if (b.min_value > 0) {
      int64_t b_max_cap = InfAwareAdd(b.max_value, -1);
      if (a.min_value >= 0) {
        // 0 <= [a_min, a_max] < b_min
        if (a.max_value < b.min_value) return a;
        // other case, we can get close to 0
        return MakeBound(0,
                         std::min(a.max_value, b_max_cap));
      } else {
        return MakeBound(std::max(a.min_value, -b_max_cap),
                         std::min(a.max_value, b_max_cap));
      }
    } else {
      CHECK(!b.is_const(0)) << "mod by zero";
      // mod by negative value is rare,
      // and we just use the simpliest rule.
      return Everything(op->type);
    }
  }

  Entry VisitExpr_(const Min* op) final {
    Entry a = VisitExpr(op->a);
    Entry b = VisitExpr(op->b);
    Entry ret;
    ret.min_value = std::min(a.min_value, b.min_value);
    ret.max_value = std::min(a.max_value, b.max_value);
    return ret;
  }

  Entry VisitExpr_(const Max* op) final {
    Entry a = VisitExpr(op->a);
    Entry b = VisitExpr(op->b);
    Entry ret;
    ret.min_value = std::max(a.min_value, b.min_value);
    ret.max_value = std::max(a.max_value, b.max_value);
    return ret;
  }

  Entry VisitExpr_(const Select* op) final {
    Entry a = VisitExpr(op->true_value);
    Entry b = VisitExpr(op->false_value);
    return Union(a, b);
  }

  Entry VisitExpr_(const Call* op) final {
    // only special handle >> and & which can be
    // used for index calculation.
    if (op->is_intrinsic(Call::shift_right)) {
      return VisitRightShift(op);
    } else if (op->is_intrinsic(Call::bitwise_and)) {
      return VisitBitwiseAnd(op);
    } else {
      return Everything(op->type);
    }
  }

  Entry VisitExpr_(const Variable* op) final {
    Var v = GetRef<Var>(op);
    auto it = var_map_.find(v);
    if (it != var_map_.end()) {
      return it->second;
    } else {
      return Everything(op->type);
    }
  }

  Entry VisitRightShift(const Call* op) {
    Entry a = VisitExpr(op->args[0]);
    Entry b = VisitExpr(op->args[1]);
    return BinaryOpBoundry(a, b, InfAwareRightShift);
  }

  Entry VisitBitwiseAnd(const Call* op) {
    Entry a = VisitExpr(op->args[0]);
    Entry b = VisitExpr(op->args[1]);
    // handle positive index case.
    if (a.min_value >= 0 && b.min_value >= 0) {
      return MakeBound(0, std::min(a.max_value, b.max_value));
    } else {
      if (b.min_value >= 0) {
        return MakeBound(0, b.max_value);
      }
      if (a.min_value >= 0) {
        return MakeBound(0, a.max_value);
      }
      return Everything(op->type);
    }
  }

 private:
  // internal variable map
  std::unordered_map<Var, Entry, ExprHash, ExprEqual> var_map_;
  // constants: the limit value means umlimited
  // NOTE: kNegInf/kPosInf are used to represent infinity.
  static const constexpr int64_t kNegInf = ConstIntBoundNode::kNegInf;
  static const constexpr int64_t kPosInf = ConstIntBoundNode::kPosInf;
  static_assert(-kNegInf == kPosInf, "invariant of inf");
  // internal helper functions
  /*!
   * \brief Get boundary of binary op who are monotonic wrt to one argument.
   * \param param a The entry of the left operand.
   * \param param a The entry of the right operand.
   * \param op The operator.
   * \tparam F the operator function type.
   * \return The result.
   */
  template<typename F>
  static Entry BinaryOpBoundry(Entry a, Entry b, const F& op) {
    Entry ret;
    // The boundary point must be shihft of the original boundary.
    int64_t v1 = op(a.min_value, b.min_value);
    int64_t v2 = op(a.max_value, b.max_value);
    int64_t v3 = op(a.min_value, b.max_value);
    int64_t v4 = op(a.max_value, b.min_value);
    ret.min_value = std::min(std::min(std::min(v1, v2), v3), v4);
    ret.max_value = std::max(std::max(std::max(v1, v2), v3), v4);
    return ret;
  }
  /*!
   * \brief Compute x + y, aware of inf.
   * \param x The left operand.
   * \param y The right operand.
   * \return the result.
   */
  static int64_t InfAwareAdd(int64_t x, int64_t y) {
    if (x == kPosInf) {
      CHECK(y != kNegInf);
      return kPosInf;
    }
    if (x == kNegInf) {
      CHECK(y != kPosInf);
      return kNegInf;
    }
    if (y == kPosInf || y == kNegInf) return y;
    if (WillOverflow<Add>(x, y, kNegInf, kPosInf)) {
      if (x > 0) return kPosInf;
      return kNegInf;
    }
    return x + y;
  }
  /*!
   * \brief Compute x * y, aware of inf.
   * \param x The left operand.
   * \param y The right operand.
   * \return the result.
   */
  static int64_t InfAwareMul(int64_t x, int64_t y) {
    if (!WillOverflow<Mul>(x, y, kNegInf, kPosInf)) return x * y;
    if ((x > 0 && y > 0) || (x < 0 && y < 0)) return kPosInf;
    return kNegInf;
  }
  /*!
   * \brief Compute x / y, aware of inf.
   * \param x The left operand.
   * \param y The right operand.
   * \return the result.
   */
  static int64_t InfAwareDiv(int64_t x, int64_t y) {
    CHECK_NE(y, 0);
    if (x == kPosInf || x == kNegInf) {
      if (y > 0) return x;
      return -x;
    }
    return x / y;
  }
  /*!
   * \brief Compute x / y, aware of inf.
   * \param x The left operand.
   * \param y The right operand.
   * \return the result.
   */
  static int64_t InfAwareRightShift(int64_t x, int64_t y) {
    if (x == kPosInf || x == kNegInf) return x;
    return x >> y;
  }
  /*!
   * \brief Make a new bound entry.
   */
  static Entry MakeBound(int64_t min_value, int64_t max_value) {
    Entry e;
    e.min_value = min_value;
    e.max_value = max_value;
    return e;
  }
  /*!
   * \brief Create union of two sets.
   * \param a The left operand.
   * \param b the right operand.
   */
  static Entry Union(Entry a, Entry b) {
    Entry ret;
    ret.min_value = std::min(a.min_value, b.min_value);
    ret.max_value = std::max(a.max_value, b.max_value);
    return ret;
  }
  /*!
   * \brief Create intersect of two sets.
   * \param a The left operand.
   * \param b the right operand.
   */
  static Entry Intersect(Entry a, Entry b) {
    Entry ret;
    ret.min_value = std::max(a.min_value, b.min_value);
    ret.max_value = std::min(a.max_value, b.max_value);
    return ret;
  }
  /*!
   * \brief return everything dtype can represent.
   * \param dtype The data type.
   * \return Bound that represent everything dtype can represent.
   */
  static Entry Everything(Type dtype) {
    if (!dtype.is_int() && !dtype.is_uint()) {
      return MakeBound(kNegInf, kPosInf);
    }
    Entry ret;
    int64_t vbits = dtype.bits() - static_cast<int>(dtype.is_int());
    if (dtype.is_uint()) {
      ret.min_value = 0;
    } else {
      if (vbits >= 63) {
        ret.min_value = kNegInf;
      } else {
        ret.min_value = -(static_cast<int64_t>(1) << vbits);
      }
    }
    if (vbits >= 63) {
      ret.max_value = kPosInf;
    } else {
      ret.max_value = (static_cast<int64_t>(1) << vbits) - 1;
    }
    return ret;
  }
};

ConstIntBound ConstIntBoundAnalyzer::operator()(const Expr& expr) {
  Entry ret = impl_->VisitExpr(expr);
  return ConstIntBoundNode::make(ret.min_value, ret.max_value);
}

void ConstIntBoundAnalyzer::Update(const Var& var,
                                   const ConstIntBound& info,
                                   bool override) {
  impl_->Update(var, info, override);
}

void ConstIntBoundAnalyzer::Bind(const Var& var, const Range& range) {
  impl_->Bind(var, range);
}

std::function<void()> ConstIntBoundAnalyzer::EnterConstraint(const Expr& constraint) {
  return nullptr;
}

ConstIntBoundAnalyzer::ConstIntBoundAnalyzer(Analyzer* parent)
    : impl_(new Impl()) {
}

ConstIntBoundAnalyzer::~ConstIntBoundAnalyzer() {
  delete impl_;
}

}  // namespace arith
}  // namespace tvm