//===-- ExecutorUtil.cpp --------------------------------------------------===//
//
//                     The KLEE Symbolic Virtual Machine
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Executor.h"

#include "Context.h"

#include "klee/Expr.h"
#include "klee/Interpreter.h"
#include "klee/Solver.h"

#include "klee/Config/Version.h"
#include "klee/Internal/Module/KModule.h"

#include "klee/util/GetElementPtrTypeIterator.h"

#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 3)
#include "llvm/IR/Function.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/DataLayout.h"
#else
#include "llvm/Constants.h"
#include "llvm/Function.h"
#include "llvm/Instructions.h"
#include "llvm/Module.h"
#if LLVM_VERSION_CODE <= LLVM_VERSION(3, 1)
#include "llvm/Target/TargetData.h"
#else
#include "llvm/DataLayout.h"
#endif
#endif

#include <cassert>

using namespace klee;
using namespace llvm;

namespace klee {

  ref<klee::Expr> Executor::evalConstantExpr(const llvm::ConstantExpr *ce) {
    LLVM_TYPE_Q llvm::Type *type = ce->getType();

    ref<Expr> op1(0), op2(0), op3(0);
    int numOperands = ce->getNumOperands();

    if (numOperands > 0) op1 = evalConstant(ce->getOperand(0));
    if (numOperands > 1) op2 = evalConstant(ce->getOperand(1));
    if (numOperands > 2) op3 = evalConstant(ce->getOperand(2));

    switch (ce->getOpcode()) {
    default :
      ce->dump();
      llvm::errs() << "error: unknown ConstantExpr type\n"
                << "opcode: " << ce->getOpcode() << "\n";
      abort();

    case Instruction::Trunc: 
      return cast<ConstantExpr>(op1)->Extract(0, getWidthForLLVMType(type));
    case Instruction::ZExt:  return cast<ConstantExpr>(op1)->ZExt(getWidthForLLVMType(type));
    case Instruction::SExt:  return cast<ConstantExpr>(op1)->SExt(getWidthForLLVMType(type));
    case Instruction::Add:   return cast<ConstantExpr>(op1)->Add(cast<ConstantExpr>(op2));
    case Instruction::Sub:   return cast<ConstantExpr>(op1)->Sub(cast<ConstantExpr>(op2));
    case Instruction::Mul:   return cast<ConstantExpr>(op1)->Mul(cast<ConstantExpr>(op2));
    case Instruction::SDiv:  return cast<ConstantExpr>(op1)->SDiv(cast<ConstantExpr>(op2));
    case Instruction::UDiv:  return cast<ConstantExpr>(op1)->UDiv(cast<ConstantExpr>(op2));
    case Instruction::SRem:  return cast<ConstantExpr>(op1)->SRem(cast<ConstantExpr>(op2));
    case Instruction::URem:  return cast<ConstantExpr>(op1)->URem(cast<ConstantExpr>(op2));
    case Instruction::And:   return cast<ConstantExpr>(op1)->And(cast<ConstantExpr>(op2));
    case Instruction::Or:    return cast<ConstantExpr>(op1)->Or(cast<ConstantExpr>(op2));
    case Instruction::Xor:   return cast<ConstantExpr>(op1)->Xor(cast<ConstantExpr>(op2));
    case Instruction::Shl:   return cast<ConstantExpr>(op1)->Shl(cast<ConstantExpr>(op2));
    case Instruction::LShr:  return cast<ConstantExpr>(op1)->LShr(cast<ConstantExpr>(op2));
    case Instruction::AShr:  return cast<ConstantExpr>(op1)->AShr(cast<ConstantExpr>(op2));
    case Instruction::BitCast:  return cast<ConstantExpr>(op1);

    case Instruction::IntToPtr:
      return cast<ConstantExpr>(op1)->ZExt(getWidthForLLVMType(type));

    case Instruction::PtrToInt:
      return cast<ConstantExpr>(op1)->ZExt(getWidthForLLVMType(type));

    case Instruction::GetElementPtr: {
      ref<ConstantExpr> base = cast<ConstantExpr>(op1)->ZExt(Context::get().getPointerWidth());

      for (gep_type_iterator ii = gep_type_begin(ce), ie = gep_type_end(ce);
           ii != ie; ++ii) {
        ref<ConstantExpr> addend = 
          ConstantExpr::alloc(0, Context::get().getPointerWidth());

        if (LLVM_TYPE_Q StructType *st = dyn_cast<StructType>(*ii)) {
          const StructLayout *sl = kmodule->targetData->getStructLayout(st);
          const ConstantInt *ci = cast<ConstantInt>(ii.getOperand());

          addend = ConstantExpr::alloc(sl->getElementOffset((unsigned)
                                                            ci->getZExtValue()),
                                       Context::get().getPointerWidth());
        } else {
          const SequentialType *set = cast<SequentialType>(*ii);
          ref<ConstantExpr> index = 
            cast<ConstantExpr>(evalConstant(cast<Constant>(ii.getOperand())));
          unsigned elementSize = 
            kmodule->targetData->getTypeAllocSize(set->getElementType());

          index = index->ZExt(Context::get().getPointerWidth());
          addend = index->Mul(ConstantExpr::alloc(elementSize, 
                                                  Context::get().getPointerWidth()));
        }

        base = base->Add(addend);
      }

      return base;
    }

    case Instruction::ICmp: {
      switch(ce->getPredicate()) {
      default: assert(0 && "unhandled ICmp predicate");
      case ICmpInst::ICMP_EQ:  return cast<ConstantExpr>(op1)->Eq(cast<ConstantExpr>(op2));
      case ICmpInst::ICMP_NE:  return cast<ConstantExpr>(op1)->Ne(cast<ConstantExpr>(op2));
      case ICmpInst::ICMP_UGT: return cast<ConstantExpr>(op1)->Ugt(cast<ConstantExpr>(op2));
      case ICmpInst::ICMP_UGE: return cast<ConstantExpr>(op1)->Uge(cast<ConstantExpr>(op2));
      case ICmpInst::ICMP_ULT: return cast<ConstantExpr>(op1)->Ult(cast<ConstantExpr>(op2));
      case ICmpInst::ICMP_ULE: return cast<ConstantExpr>(op1)->Ule(cast<ConstantExpr>(op2));
      case ICmpInst::ICMP_SGT: return cast<ConstantExpr>(op1)->Sgt(cast<ConstantExpr>(op2));
      case ICmpInst::ICMP_SGE: return cast<ConstantExpr>(op1)->Sge(cast<ConstantExpr>(op2));
      case ICmpInst::ICMP_SLT: return cast<ConstantExpr>(op1)->Slt(cast<ConstantExpr>(op2));
      case ICmpInst::ICMP_SLE: return cast<ConstantExpr>(op1)->Sle(cast<ConstantExpr>(op2));
      }
    }

    case Instruction::Select:
      return op1->isTrue() ? op2 : op3;

    case Instruction::FAdd:    return cast<FConstantExpr>(op1)->FAdd(cast<FConstantExpr>(op2), APFloat::roundingMode::rmNearestTiesToEven);
    case Instruction::FSub:    return cast<FConstantExpr>(op1)->FSub(cast<FConstantExpr>(op2), APFloat::roundingMode::rmNearestTiesToEven);
    case Instruction::FMul:    return cast<FConstantExpr>(op1)->FMul(cast<FConstantExpr>(op2), APFloat::roundingMode::rmNearestTiesToEven);
    case Instruction::FDiv:    return cast<FConstantExpr>(op1)->FDiv(cast<FConstantExpr>(op2), APFloat::roundingMode::rmNearestTiesToEven);
    case Instruction::FRem:    return cast<FConstantExpr>(op1)->FRem(cast<FConstantExpr>(op2), APFloat::roundingMode::rmNearestTiesToEven);
    case Instruction::FPTrunc:
	[[fallthrough]]
    case Instruction::FPExt:   return cast<FConstantExpr>(op1)->FExt(getWidthForLLVMType(type), APFloat::roundingMode::rmNearestTiesToEven);
    case Instruction::UIToFP:  return cast<ConstantExpr>(op1)->UToF(getWidthForLLVMType(type), APFloat::roundingMode::rmNearestTiesToEven);
    case Instruction::SIToFP:  return cast<ConstantExpr>(op1)->SToF(getWidthForLLVMType(type), APFloat::roundingMode::rmNearestTiesToEven);
    case Instruction::FPToUI:  return cast<FConstantExpr>(op1)->FToU(getWidthForLLVMType(type), APFloat::roundingMode::rmNearestTiesToEven);
    case Instruction::FPToSI:  return cast<FConstantExpr>(op1)->FToS(getWidthForLLVMType(type), APFloat::roundingMode::rmNearestTiesToEven);
    case Instruction::FCmp: {
      switch(ce->getPredicate()) {
      default: assert(0 && "unhandled FCmp predicate");
      case FCmpInst::FCMP_OEQ: return cast<FConstantExpr>(op1)->FOeq(cast<FConstantExpr>(op2));
      case FCmpInst::FCMP_OGT: return cast<FConstantExpr>(op1)->FOgt(cast<FConstantExpr>(op2));
      case FCmpInst::FCMP_OGE: return cast<FConstantExpr>(op1)->FOge(cast<FConstantExpr>(op2));
      case FCmpInst::FCMP_OLT: return cast<FConstantExpr>(op1)->FOlt(cast<FConstantExpr>(op2));
      case FCmpInst::FCMP_OLE: return cast<FConstantExpr>(op1)->FOle(cast<FConstantExpr>(op2));
      case FCmpInst::FCMP_ONE: return cast<FConstantExpr>(op1)->FOne(cast<FConstantExpr>(op2));
      case FCmpInst::FCMP_ORD: return cast<FConstantExpr>(op1)->FOrd(cast<FConstantExpr>(op2));
      case FCmpInst::FCMP_UNO: return cast<FConstantExpr>(op1)->FUno(cast<FConstantExpr>(op2));
      case FCmpInst::FCMP_UEQ: return cast<FConstantExpr>(op1)->FUeq(cast<FConstantExpr>(op2));
      case FCmpInst::FCMP_UGT: return cast<FConstantExpr>(op1)->FUgt(cast<FConstantExpr>(op2));
      case FCmpInst::FCMP_UGE: return cast<FConstantExpr>(op1)->FUge(cast<FConstantExpr>(op2));
      case FCmpInst::FCMP_ULT: return cast<FConstantExpr>(op1)->FUlt(cast<FConstantExpr>(op2));
      case FCmpInst::FCMP_ULE: return cast<FConstantExpr>(op1)->FUle(cast<FConstantExpr>(op2));
      case FCmpInst::FCMP_UNE: return cast<FConstantExpr>(op1)->FUne(cast<FConstantExpr>(op2));
      }
    }
    }
#if LLVM_VERSION_CODE >= LLVM_VERSION(3, 1)
    llvm_unreachable("Unsupported expression in evalConstantExpr");
#else
    assert(0 && "Unsupported expression in evalConstantExpr");
#endif
    return op1;
  }
}
