//=== WebAssemblyRefTypeMem2Local.cpp - WebAssembly RefType Mem2Local -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Lower reference type allocas out of linear memory, which cannot hold
/// reference values.
///
/// There are two cases:
///
///   1. The address of the alloca does not escape.  Its loads and stores can
///      be turned into local.get / local.set, so the alloca is moved to the
///      local addrspace (addrspace(1)); the backend then assigns it a
///      WebAssembly local.
///
///   2. The address of the alloca escapes (e.g. it is passed to a callee or
///      stored to memory).  A WebAssembly local has no address, so the
///      reference must instead be spilled to the externref *stack* region of
///      the linker-synthesized __externref_table (the externref analog of the
///      linear-memory stack).  The function reserves a run of table slots in
///      its prologue by subtracting from the mutable __externref_stack_pointer
///      global (the stack grows down from __externref_stack_high), and restores
///      it before returning.  Each spilled alloca is assigned one slot; its
///      address becomes that slot's index, an integer.  Loads and stores of the
///      reference through such a pointer are lowered to
///      table.get / table.set __externref_table by ISel (the pointer value is
///      the table index).
///
//===----------------------------------------------------------------------===//

#include "Utils/WasmAddressSpaces.h"
#include "Utils/WebAssemblyTypeUtilities.h"
#include "WebAssembly.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsWebAssembly.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Pass.h"
using namespace llvm;

#define DEBUG_TYPE "wasm-ref-type-mem2local"

// The name of the mutable global holding the current top of the externref
// spill stack (a slot index into __externref_table). This is the externref
// analog of __stack_pointer and is synthesized by the linker.
static const char *const ExternrefStackPointerName = "__externref_stack_pointer";

// The linker-synthesized externref table that backs the spill stack.
static const char *const ExternrefTableName = "__externref_table";

namespace {
class WebAssemblyRefTypeMem2Local final : public FunctionPass {
  StringRef getPassName() const override {
    return "WebAssembly Reference Types Memory to Local";
  }

  // Note: this pass does not preserve the CFG. When a function unwinds an
  // unhandled exception to its caller via a catchswitch, the spill epilogue is
  // injected by rerouting that edge through a new cleanup block.

  bool runOnFunction(Function &F) override;

public:
  static char ID;
  WebAssemblyRefTypeMem2Local() : FunctionPass(ID) {}
};
} // End anonymous namespace

char WebAssemblyRefTypeMem2Local::ID = 0;
INITIALIZE_PASS(WebAssemblyRefTypeMem2Local, DEBUG_TYPE,
                "Assign reference type allocas to local address space", true,
                false)

FunctionPass *llvm::createWebAssemblyRefTypeMem2Local() {
  return new WebAssemblyRefTypeMem2Local();
}

// Return true if the address of \p AI never escapes, i.e. every use is a plain
// load or store of the reference value itself. Such an alloca can become a
// WebAssembly local. Anything else (passing the pointer to a call, storing the
// pointer, computing a derived pointer, ...) makes the address observable and
// forces a spill to the externref table.
static bool addressIsLocalOnly(const AllocaInst *AI) {
  for (const User *U : AI->users()) {
    if (const auto *LI = dyn_cast<LoadInst>(U)) {
      if (LI->getPointerOperand() != AI)
        return false;
      continue;
    }
    if (const auto *SI = dyn_cast<StoreInst>(U)) {
      // The pointer escapes if it is the value being stored rather than the
      // store destination.
      if (SI->getPointerOperand() != AI)
        return false;
      continue;
    }
    if (const auto *II = dyn_cast<IntrinsicInst>(U)) {
      switch (II->getIntrinsicID()) {
      case Intrinsic::lifetime_start:
      case Intrinsic::lifetime_end:
      case Intrinsic::dbg_declare:
      case Intrinsic::dbg_value:
        continue;
      default:
        return false;
      }
    }
    return false;
  }
  return true;
}

// Move \p AI to the local addrspace so its accesses become local.get/local.set.
static void promoteToLocal(AllocaInst *AI) {
  IRBuilder<> IRB(AI);
  auto *NewAI = IRB.CreateAlloca(AI->getAllocatedType(),
                                 WebAssembly::WASM_ADDRESS_SPACE_VAR, nullptr,
                                 AI->getName() + ".var");

  // The below is basically equivalent to AI->replaceAllUsesWith(NewAI), but we
  // cannot use it because it requires the old and new types be the same, which
  // is not true here because the address spaces are different.
  if (AI->hasValueHandle())
    ValueHandleBase::ValueIsRAUWd(AI, NewAI);
  if (AI->isUsedByMetadata())
    ValueAsMetadata::handleRAUW(AI, NewAI);
  while (!AI->materialized_use_empty()) {
    Use &U = *AI->materialized_use_begin();
    U.set(NewAI);
  }

  AI->eraseFromParent();
}

// LLVM aggregate types can only refer to themselves recursively through a
// pointer (a leaf type here), so plain structural recursion terminates without
// needing a visited-set cycle guard.
static bool containsWebAssemblyExternrefType(Type *Ty) {
  if (WebAssembly::isWebAssemblyExternrefType(Ty))
    return true;

  if (auto *AT = dyn_cast<ArrayType>(Ty))
    return containsWebAssemblyExternrefType(AT->getElementType());

  if (auto *VT = dyn_cast<VectorType>(Ty))
    return containsWebAssemblyExternrefType(VT->getElementType());

  if (auto *ST = dyn_cast<StructType>(Ty)) {
    if (ST->isOpaque())
      return false;
    for (Type *ElemTy : ST->elements())
      if (containsWebAssemblyExternrefType(ElemTy))
        return true;
  }

  return false;
}

bool WebAssemblyRefTypeMem2Local::runOnFunction(Function &F) {
  LLVM_DEBUG(dbgs() << "********** WebAssembly RefType Mem2Local **********\n"
                       "********** Function: "
                    << F.getName() << '\n');

  if (!F.getFnAttribute("target-features")
           .getValueAsString()
           .contains("+reference-types"))
    return false;

  // Collect scalar reference-type allocas, classifying each as "local" (address
  // does not escape) or "spilled" (address escapes). Aggregates containing
  // externref are rejected below: lowering their element GEPs to table slots
  // would require assigning one table slot per element.
  SmallVector<AllocaInst *, 4> ToPromote;
  SmallVector<AllocaInst *, 4> ToSpill;
  for (Instruction &I : instructions(F)) {
    auto *AI = dyn_cast<AllocaInst>(&I);
    if (!AI)
      continue;
    if (!WebAssembly::isWebAssemblyReferenceType(AI->getAllocatedType())) {
      if (containsWebAssemblyExternrefType(AI->getAllocatedType()))
        report_fatal_error(
            "WebAssembly: cannot allocate an aggregate containing externref "
            "(spilling externref aggregates to the externref table is not yet "
            "implemented)");
      continue;
    }
    // Only an address-taken externref needs spilling to the externref table.
    // A non-escaping reference (externref or funcref) becomes a local; an
    // address-taken funcref would need a separate funcref table and is not yet
    // supported (it keeps the old behavior of being promoted).
    if (addressIsLocalOnly(AI) ||
        !WebAssembly::isWebAssemblyExternrefType(AI->getAllocatedType())) {
      ToPromote.push_back(AI);
      continue;
    }
    if (AI->isArrayAllocation())
      report_fatal_error(
          "WebAssembly: cannot take the address of an array of externref "
          "(spilling externref arrays to the externref table is not yet "
          "implemented)");
    ToSpill.push_back(AI);
  }

  if (ToPromote.empty() && ToSpill.empty())
    return false;

  for (AllocaInst *AI : ToPromote)
    promoteToLocal(AI);

  if (ToSpill.empty())
    return true;

  // Reserve one __externref_table slot per address-taken reference. The slot
  // indices are integers the width of a pointer (i32, or i64 under wasm64),
  // matching the __externref_stack_pointer global synthesized by the linker.
  Module &M = *F.getParent();
  LLVMContext &Ctx = M.getContext();
  const DataLayout &DL = M.getDataLayout();
  Type *IntPtrTy = DL.getIntPtrType(Ctx, /*AddressSpace=*/0);

  GlobalVariable *SP = M.getNamedGlobal(ExternrefStackPointerName);
  if (!SP)
    SP = new GlobalVariable(
        M, IntPtrTy, /*isConstant=*/false, GlobalValue::ExternalLinkage,
        /*Initializer=*/nullptr, ExternrefStackPointerName,
        /*InsertBefore=*/nullptr, GlobalValue::NotThreadLocal,
        WebAssembly::WASM_ADDRESS_SPACE_VAR);

  // The externref table is modeled as a zero-length array of externref in the
  // table addrspace; the table.fill intrinsic references it by this global.
  Type *ExternrefTy = PointerType::get(
      Ctx, WebAssembly::WASM_ADDRESS_SPACE_EXTERNREF);
  GlobalVariable *Table = M.getNamedGlobal(ExternrefTableName);
  if (!Table)
    Table = new GlobalVariable(
        M, ArrayType::get(ExternrefTy, 0), /*isConstant=*/false,
        GlobalValue::ExternalLinkage, /*Initializer=*/nullptr,
        ExternrefTableName, /*InsertBefore=*/nullptr,
        GlobalValue::NotThreadLocal, WebAssembly::WASM_ADDRESS_SPACE_VAR);

  const unsigned NumSlots = ToSpill.size();
  Type *Int32Ty = Type::getInt32Ty(Ctx);

  // Prologue: load the current externref stack pointer, grow the stack down by
  // NumSlots, and write it back. Loads/stores of the addrspace(1) global lower
  // to global.get/global.set __externref_stack_pointer.
  IRBuilder<> IRB(&*F.getEntryBlock().getFirstInsertionPt());
  Value *OldSP = IRB.CreateLoad(IntPtrTy, SP, "externref.sp");
  Value *NewSP = IRB.CreateSub(
      OldSP, ConstantInt::get(IntPtrTy, NumSlots), "externref.sp.new");
  IRB.CreateStore(NewSP, SP);

  // table.fill indices and counts are i32 regardless of pointer width.
  Value *FillStart = NewSP;
  if (IntPtrTy != Int32Ty)
    FillStart = IRB.CreateTrunc(NewSP, Int32Ty, "externref.sp.i32");

  // Each spilled reference gets one slot at NewSP + k; its address is that slot
  // index reinterpreted as a pointer. Compute the address just before each
  // alloca (NewSP, defined in the entry prologue, dominates them all) and then
  // remove the alloca.
  for (unsigned K = 0; K != NumSlots; ++K) {
    AllocaInst *AI = ToSpill[K];
    IRBuilder<> B(AI);
    Value *Idx = NewSP;
    if (K != 0)
      Idx = B.CreateAdd(NewSP, ConstantInt::get(IntPtrTy, K),
                        AI->getName() + ".slot");
    Value *Addr = B.CreateIntToPtr(Idx, AI->getType(), AI->getName());
    AI->replaceAllUsesWith(Addr);
    AI->eraseFromParent();
  }

  // Epilogue: before each return, restore the externref stack pointer and then
  // clear the slots this function used with table.fill ... ref.null, so the
  // references do not stay reachable (and pinned by the GC) after the frame is
  // gone.
  Function *RefNull = Intrinsic::getOrInsertDeclaration(
      &M, Intrinsic::wasm_ref_null_extern);
  Function *TableFill = Intrinsic::getOrInsertDeclaration(
      &M, Intrinsic::wasm_table_fill_externref);
  Constant *SlotCount = ConstantInt::get(Int32Ty, NumSlots);

  auto InsertEpilogue = [&](Instruction *InsertPt) {
    IRBuilder<> EpilogueB(InsertPt);
    EpilogueB.CreateStore(OldSP, SP);
    Value *Null = EpilogueB.CreateCall(RefNull, {}, "externref.null");
    EpilogueB.CreateCall(TableFill, {Table, FillStart, Null, SlotCount});
  };

  SmallVector<CatchSwitchInst *, 4> CatchSwitchesToCaller;
  for (BasicBlock &BB : F) {
    Instruction *TI = BB.getTerminator();
    if (isa<ReturnInst>(TI) || isa<ResumeInst>(TI)) {
      InsertEpilogue(TI);
      continue;
    }

    if (auto *CRI = dyn_cast<CleanupReturnInst>(TI)) {
      if (CRI->unwindsToCaller())
        InsertEpilogue(TI);
      continue;
    }

    // An exception matching none of the catch handlers leaves the function
    // through the catchswitch's unwind-to-caller edge. A catchswitch is not an
    // instruction insertion point, so we handle these after the walk.
    if (auto *CSI = dyn_cast<CatchSwitchInst>(TI))
      if (CSI->unwindsToCaller())
        CatchSwitchesToCaller.push_back(CSI);
  }

  // Reroute every catchswitch's unwind-to-caller edge through one shared
  // cleanup pad that runs the epilogue and then unwinds to the caller, so the
  // spill stack is restored on the exception-propagation path too.
  //
  // The cleanup pad sits at the top-level scope (within none): EH structural
  // rules require that all unwind edges leaving a funclet share one
  // destination, so any catchswitch that unwinds to the caller has all of its
  // enclosing catchswitches unwinding to the caller as well. A single shared
  // destination is therefore consistent for both top-level and nested
  // catchswitches. A catchswitch's unwind destination is fixed at construction
  // time, so each one must be rebuilt to point at the cleanup.
  BasicBlock *CleanupBB = nullptr;
  for (CatchSwitchInst *CSI : CatchSwitchesToCaller) {
    if (!CleanupBB) {
      CleanupBB = BasicBlock::Create(Ctx, "externref.cleanup", &F);
      IRBuilder<> CleanupB(CleanupBB);
      auto *CPI = CleanupB.CreateCleanupPad(ConstantTokenNone::get(Ctx));
      auto *CRI = CleanupB.CreateCleanupRet(CPI, /*UnwindBB=*/nullptr);
      InsertEpilogue(CRI);
    }

    CatchSwitchInst *NewCSI = CatchSwitchInst::Create(
        CSI->getParentPad(), CleanupBB, CSI->getNumHandlers(), "",
        CSI->getIterator());
    for (BasicBlock *Handler : CSI->handlers())
      NewCSI->addHandler(Handler);
    NewCSI->takeName(CSI);
    CSI->replaceAllUsesWith(NewCSI);
    CSI->eraseFromParent();
  }

  return true;
}
