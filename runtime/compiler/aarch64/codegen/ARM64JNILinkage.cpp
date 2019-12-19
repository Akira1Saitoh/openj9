/*******************************************************************************
 * Copyright (c) 2019, 2019 IBM Corp. and others
 *
 * This program and the accompanying materials are made available under
 * the terms of the Eclipse Public License 2.0 which accompanies this
 * distribution and is available at https://www.eclipse.org/legal/epl-2.0/
 * or the Apache License, Version 2.0 which accompanies this distribution and
 * is available at https://www.apache.org/licenses/LICENSE-2.0.
 *
 * This Source Code may also be made available under the following
 * Secondary Licenses when the conditions for such availability set
 * forth in the Eclipse Public License, v. 2.0 are satisfied: GNU
 * General Public License, version 2 with the GNU Classpath
 * Exception [1] and GNU General Public License, version 2 with the
 * OpenJDK Assembly Exception [2].
 *
 * [1] https://www.gnu.org/software/classpath/license.html
 * [2] http://openjdk.java.net/legal/assembly-exception.html
 *
 * SPDX-License-Identifier: EPL-2.0 OR Apache-2.0 OR GPL-2.0 WITH Classpath-exception-2.0 OR LicenseRef-GPL-2.0 WITH Assembly-exception
 *******************************************************************************/

#include "codegen/ARM64JNILinkage.hpp"

#include "codegen/ARM64HelperCallSnippet.hpp"
#include "codegen/GenerateInstructions.hpp"
#include "codegen/Linkage_inlines.hpp"
#include "codegen/MemoryReference.hpp"
#include "codegen/RegisterDependency.hpp"
#include "codegen/Relocation.hpp"
#include "env/VMJ9.h"
#include "il/Node.hpp"
#include "il/SymbolReference.hpp"
#include "infra/Assert.hpp"

J9::ARM64::JNILinkage::JNILinkage(TR::CodeGenerator *cg)
   : J9::ARM64::PrivateLinkage(cg)
   {
   _systemLinkage = cg->getLinkage(TR_System);
   }

int32_t J9::ARM64::JNILinkage::buildArgs(TR::Node *callNode,
   TR::RegisterDependencyConditions *dependencies)
   {
   TR_ASSERT(0, "Should call J9::ARM64::JNILinkage::buildJNIArgs instead.");
   return 0;
   }

TR::Register *J9::ARM64::JNILinkage::buildIndirectDispatch(TR::Node *callNode)
   {
   TR_ASSERT(0, "Calling J9::ARM64::JNILinkage::buildIndirectDispatch does not make sense.");
   return NULL;
   }

void J9::ARM64::JNILinkage::buildVirtualDispatch(
      TR::Node *callNode,
      TR::RegisterDependencyConditions *dependencies,
      uint32_t argSize)
   {
   TR_ASSERT(0, "Calling J9::ARM64::JNILinkage::buildVirtualDispatch does not make sense.");
   }

void J9::ARM64::JNILinkage::releaseVMAccess(TR::Node *callNode, TR::Register *vmThreadReg)
   {
   TR_UNIMPLEMENTED();
   }

void J9::ARM64::JNILinkage::acquireVMAccess(TR::Node *callNode, TR::Register *vmThreadReg)
   {
   TR_UNIMPLEMENTED();
   }

#ifdef J9VM_INTERP_ATOMIC_FREE_JNI
void J9::ARM64::JNILinkage::releaseVMAccessAtomicFree(TR::Node *callNode, TR::Register *vmThreadReg)
   {
   TR_UNIMPLEMENTED();
   }

void J9::ARM64::JNILinkage::acquireVMAccessAtomicFree(TR::Node *callNode, TR::Register *vmThreadReg)
   {
   TR_UNIMPLEMENTED();
   }
#endif /* J9VM_INTERP_ATOMIC_FREE_JNI */

void J9::ARM64::JNILinkage::buildJNICallOutFrame(TR::Node *callNode, bool isWrapperForJNI, TR::LabelSymbol *returnAddrLabel,
                                                 TR::Register *vmThreadReg, TR::Register *javaStackReg, TR::Register *scratchReg0, TR::Register *scratchReg1)
   {
   TR_J9VMBase *fej9 = reinterpret_cast<TR_J9VMBase *>(fe());

   // begin: mask out the magic bit that indicates JIT frames below
   loadConstant64(cg(), callNode, 0, scratchReg1);
   generateMemSrc1Instruction(cg(), TR::InstOpCode::strimmx, callNode, new (trHeapMemory()) TR::MemoryReference(vmThreadReg, fej9->thisThreadGetJavaFrameFlagsOffset(), cg()), scratchReg1);

   // Grab 5 slots in the frame.
   //
   //    4:   tag bits (savedA0)
   //    3:   empty (savedPC)
   //    2:   return address in this frame (savedCP)
   //    1:   frame flags
   //    0:   RAM method
   //
   // push tag bits (savedA0)
   int32_t tagBits = fej9->constJNICallOutFrameSpecialTag();
   // if the current method is simply a wrapper for the JNI call, hide the call-out stack frame
   if (isWrapperForJNI)
      tagBits |= fej9->constJNICallOutFrameInvisibleTag();
   loadConstant64(cg(), callNode, tagBits, scratchReg0);

   generateMemSrc1Instruction(cg(), TR::InstOpCode::strprex, callNode, new (trHeapMemory()) TR::MemoryReference(javaStackReg, -TR::Compiler->om.sizeofReferenceAddress(), cg()), scratchReg0);

   // empty saved pc slot
   generateMemSrc1Instruction(cg(), TR::InstOpCode::strprex, callNode, new (trHeapMemory()) TR::MemoryReference(javaStackReg, -TR::Compiler->om.sizeofReferenceAddress(), cg()), scratchReg1);

   // push return address (savedCP)
   generateTrg1ImmSymInstruction(cg(), TR::InstOpCode::adr, callNode, scratchReg0, 0, returnAddrLabel);
   generateMemSrc1Instruction(cg(), TR::InstOpCode::strprex, callNode, new (trHeapMemory()) TR::MemoryReference(javaStackReg, -TR::Compiler->om.sizeofReferenceAddress(), cg()), scratchReg0);

   // push flags
   loadConstant64(cg(), callNode, fej9->constJNICallOutFrameFlags(), scratchReg0);
   generateMemSrc1Instruction(cg(), TR::InstOpCode::strprex, callNode, new (trHeapMemory()) TR::MemoryReference(javaStackReg, -TR::Compiler->om.sizeofReferenceAddress(), cg()), scratchReg0);

   TR::ResolvedMethodSymbol *resolvedMethodSymbol = callNode->getSymbol()->castToResolvedMethodSymbol();
   TR_ResolvedMethod *resolvedMethod = resolvedMethodSymbol->getResolvedMethod();
   uintptrj_t methodAddr = reinterpret_cast<uintptrj_t>(resolvedMethod->resolvedMethodAddress());

   // push the RAM method for the native
   if (fej9->needClassAndMethodPointerRelocations())
      {
      // load a 64-bit constant into a register with a fixed 4 instruction sequence
      TR::Instruction *firstInstruction;

      TR::Instruction *cursor = firstInstruction = generateTrg1ImmInstruction(cg(), TR::InstOpCode::movzx, callNode, scratchReg0, methodAddr & 0x0000ffff, cursor);
      cursor = generateTrg1ImmInstruction(cg(), TR::InstOpCode::movkx, callNode, scratchReg0, ((methodAddr >> 16) & 0x0000ffff) | TR::MOV_LSL16, cursor);
      cursor = generateTrg1ImmInstruction(cg(), TR::InstOpCode::movkx, callNode, scratchReg0, ((methodAddr >> 32) & 0x0000ffff) | TR::MOV_LSL32 , cursor);
      cursor = generateTrg1ImmInstruction(cg(), TR::InstOpCode::movkx, callNode, scratchReg0, (methodAddr >> 48) | TR::MOV_LSL48 , cursor);

      TR_ExternalRelocationTargetKind reloType;
      if (resolvedMethodSymbol->isSpecial())
         reloType = TR_SpecialRamMethodConst;
      else if (resolvedMethodSymbol->isStatic())
         reloType = TR_StaticRamMethodConst;
      else if (resolvedMethodSymbol->isVirtual())
         reloType = TR_VirtualRamMethodConst;
      else
         {
         reloType = TR_NoRelocation;
         TR_ASSERT(0, "JNI relocation not supported.");
         }
      cg()->addExternalRelocation(new (trHeapMemory()) TR::BeforeBinaryEncodingExternalRelocation(
                                                            firstInstruction,
                                                            reinterpret_cast<uint8_t *>(callNode->getSymbolReference()),
                                                            reinterpret_cast<uint8_t *>(callNode->getInlinedSiteIndex()),
                                                            reloType, cg()),
                                                          __FILE__,__LINE__, callNode);

      }
   else
      {
      loadConstant64(cg(), callNode, methodAddr, scratchReg0);
      }
   generateMemSrc1Instruction(cg(), TR::InstOpCode::strprex, callNode, new (trHeapMemory()) TR::MemoryReference(javaStackReg, -TR::Compiler->om.sizeofReferenceAddress(), cg()), scratchReg0);

   // store out java sp
   generateMemSrc1Instruction(cg(), TR::InstOpCode::strimmx, callNode, new (trHeapMemory()) TR::MemoryReference(vmThreadReg, fej9->thisThreadGetJavaSPOffset(), cg()), javaStackReg);

   // store out pc and literals values indicating the callout frame
   loadConstant64(cg(), callNode, fej9->constJNICallOutFrameType(), scratchReg0);
   generateMemSrc1Instruction(cg(), TR::InstOpCode::strimmx, callNode, new (trHeapMemory()) TR::MemoryReference(vmThreadReg, fej9->thisThreadGetJavaPCOffset(), cg()), scratchReg0);

   generateMemSrc1Instruction(cg(), TR::InstOpCode::strimmx, callNode, new (trHeapMemory()) TR::MemoryReference(vmThreadReg, fej9->thisThreadGetJavaLiteralsOffset(), cg()), scratchReg1);

   }

void J9::ARM64::JNILinkage::restoreJNICallOutFrame(TR::Node *callNode, bool tearDownJNIFrame, TR::Register *vmThreadReg, TR::Register *javaStackReg, TR::Register *scratchReg)
   {
   TR_J9VMBase *fej9 = reinterpret_cast<TR_J9VMBase *>(fe());

   // restore stack pointer: need to deal with growable stack -- stack may already be moved.
   generateTrg1MemInstruction(cg(), TR::InstOpCode::ldrimmx, callNode, scratchReg, new (trHeapMemory()) TR::MemoryReference(vmThreadReg, fej9->thisThreadGetJavaLiteralsOffset(), cg()));
   generateTrg1MemInstruction(cg(),TR::InstOpCode::ldrimmx, callNode, javaStackReg, new (trHeapMemory()) TR::MemoryReference(vmThreadReg, fej9->thisThreadGetJavaSPOffset(), cg()));
   generateTrg1Src2Instruction(cg(), TR::InstOpCode::addx, callNode, javaStackReg, scratchReg, javaStackReg);

   if (tearDownJNIFrame)
      {
      // must check to see if the ref pool was used and clean them up if so--or we
      // leave a bunch of pinned garbage behind that screws up the gc quality forever
      TR::LabelSymbol      *refPoolRestartLabel = generateLabelSymbol(cg());
      TR::LabelSymbol      *refPoolSnippetLabel = generateLabelSymbol(cg());
      TR::SymbolReference *collapseSymRef = cg()->getSymRefTab()->findOrCreateRuntimeHelper(TR_ARM64jitCollapseJNIReferenceFrame, false, false, false);
      TR::Snippet *snippet = new (trHeapMemory()) TR::ARM64HelperCallSnippet(cg(), callNode, refPoolSnippetLabel, collapseSymRef, refPoolRestartLabel);
      cg()->addSnippet(snippet);
      generateTrg1MemInstruction(cg(), TR::InstOpCode::ldrimmx, callNode, scratchReg, new (trHeapMemory()) TR::MemoryReference(javaStackReg, fej9->constJNICallOutFrameFlagsOffset(), cg()));
      generateTestImmInstruction(cg(), callNode, scratchReg, fej9->constJNIReferenceFrameAllocatedFlags(), true);
      generateConditionalBranchInstruction(cg(), TR::InstOpCode::b_cond, callNode, refPoolSnippetLabel, TR::CC_NE);
      generateLabelInstruction(cg(), TR::InstOpCode::label, callNode, refPoolRestartLabel);
      }

   // Restore the JIT frame
   generateTrg1Src1ImmInstruction(cg(), TR::InstOpCode::addimmx, callNode, javaStackReg, javaStackReg, 5*TR::Compiler->om.sizeofReferenceAddress());
   }

size_t J9::ARM64::JNILinkage::buildJNIArgs(TR::Node *callNode, TR::RegisterDependencyConditions *deps, bool passThread, bool passReceiver, bool killNonVolatileGPRs)
   {
   TR_UNIMPLEMENTED();
   return 0;
   }

TR::Register *J9::ARM64::JNILinkage::getReturnRegisterFromDeps(TR::Node *callNode, TR::RegisterDependencyConditions *deps)
   {
   TR_UNIMPLEMENTED();
   return NULL;
   }

TR::Register *J9::ARM64::JNILinkage::pushJNIReferenceArg(TR::Node *child)
   {
   TR_UNIMPLEMENTED();
   return NULL;
   }

void J9::ARM64::JNILinkage::adjustReturnValue(TR::Node *callNode, bool wrapRefs, TR::Register *returnRegister)
   {
   TR_UNIMPLEMENTED();
   }

void J9::ARM64::JNILinkage::checkForJNIExceptions(TR::Node *callNode, TR::Register *vmThreadReg)
   {
   TR_UNIMPLEMENTED();
   }

TR::Instruction *J9::ARM64::JNILinkage::generateMethodDispatch(TR::Node *callNode, bool isJNIGCPoint,
                                                               TR::RegisterDependencyConditions *deps, uintptrj_t targetAddress)
   {
   TR_UNIMPLEMENTED();
   return NULL;
   }

TR::Register *J9::ARM64::JNILinkage::buildDirectDispatch(TR::Node *callNode)
   {
   TR::LabelSymbol *returnLabel = generateLabelSymbol(cg());
   TR::SymbolReference *callSymRef = callNode->getSymbolReference();
   TR::MethodSymbol *callSymbol = callSymRef->getSymbol()->castToMethodSymbol();
   TR::ResolvedMethodSymbol *resolvedMethodSymbol = callNode->getSymbol()->castToResolvedMethodSymbol();
   TR_ResolvedMethod *resolvedMethod = resolvedMethodSymbol->getResolvedMethod();
   uintptrj_t targetAddress = reinterpret_cast<uintptrj_t>(resolvedMethod->startAddressForJNIMethod(comp()));
   TR_J9VMBase *fej9 = reinterpret_cast<TR_J9VMBase *>(fe());

   bool dropVMAccess = !fej9->jniRetainVMAccess(resolvedMethod);
   bool isJNIGCPoint = !fej9->jniNoGCPoint(resolvedMethod);
   bool killNonVolatileGPRs = isJNIGCPoint;
   bool checkExceptions = !fej9->jniNoExceptionsThrown(resolvedMethod);
   bool createJNIFrame = !fej9->jniNoNativeMethodFrame(resolvedMethod);
   bool tearDownJNIFrame = !fej9->jniNoSpecialTeardown(resolvedMethod);
   bool wrapRefs = !fej9->jniDoNotWrapObjects(resolvedMethod);
   bool passReceiver = !fej9->jniDoNotPassReceiver(resolvedMethod);
   bool passThread = !fej9->jniDoNotPassThread(resolvedMethod);

   if (resolvedMethodSymbol->canDirectNativeCall())
      {
      dropVMAccess = false;
      killNonVolatileGPRs = false;
      isJNIGCPoint = false;
      checkExceptions = false;
      createJNIFrame = false;
      tearDownJNIFrame = false;
      }
   else if (resolvedMethodSymbol->isPureFunction())
      {
      dropVMAccess = false;
      isJNIGCPoint = false;
      checkExceptions = false;
      }

   cg()->machine()->setLinkRegisterKilled(true);

   const int maxRegisters = getProperties()._numAllocatableIntegerRegisters + getProperties()._numAllocatableFloatRegisters;
   TR::RegisterDependencyConditions *deps = new (trHeapMemory()) TR::RegisterDependencyConditions(maxRegisters, maxRegisters, trMemory());

   size_t spSize = buildJNIArgs(callNode, deps, passThread, passReceiver, killNonVolatileGPRs);
   TR::RealRegister *sp = machine()->getRealRegister(_systemLinkage->getProperties().getStackPointerRegister());

   if (spSize > 0)
      {
      if (constantIsUnsignedImm12(spSize))
         {
         generateTrg1Src1ImmInstruction(cg(), TR::InstOpCode::subimmx, callNode, sp, sp, spSize);
         }
      else
         {
         TR_ASSERT_FATAL(false, "Too many arguments.");
         }
      }

   TR::Register *returnRegister = getReturnRegisterFromDeps(callNode, deps);
   auto postLabelDeps = deps->clonePost(cg());
   TR::RealRegister *vmThreadReg = machine()->getRealRegister(getProperties().getMethodMetaDataRegister());   // x19
   TR::RealRegister *javaStackReg = machine()->getRealRegister(getProperties().getStackPointerRegister());    // x20
   TR::Register *x9Reg = deps->searchPreConditionRegister(TR::RealRegister::x9);
   TR::Register *x10Reg = deps->searchPreConditionRegister(TR::RealRegister::x10);
   TR::Register *x11Reg = deps->searchPreConditionRegister(TR::RealRegister::x11);
   TR::Register *x12Reg = deps->searchPreConditionRegister(TR::RealRegister::x12);

   if (createJNIFrame)
      {
      buildJNICallOutFrame(callNode, (resolvedMethod == comp()->getCurrentMethod()), returnLabel, vmThreadReg, javaStackReg, x9Reg, x10Reg);
      }

   if (passThread)
      {
      TR::RealRegister *vmThread = machine()->getRealRegister(getProperties().getMethodMetaDataRegister());
      TR::Register *x0Reg = deps->searchPreConditionRegister(TR::RealRegister::x0);
      generateMovInstruction(cg(), callNode, x0Reg, vmThread);
      }

   if (dropVMAccess)
      {
#ifdef J9VM_INTERP_ATOMIC_FREE_JNI
      releaseVMAccessAtomicFree(callNode, vmThreadReg);
#else
      releaseVMAccess(callNode, vmThreadReg);
#endif
      }


   TR::Instruction *callInstr = generateMethodDispatch(callNode, isJNIGCPoint, deps, targetAddress);
   generateLabelInstruction(cg(), TR::InstOpCode::label, callNode, returnLabel, deps, callInstr);

   if (spSize > 0)
      {
      if (constantIsUnsignedImm12(spSize))
         {
         generateTrg1Src1ImmInstruction(cg(), TR::InstOpCode::addimmx, callNode, sp, sp, spSize);
         }
      else
         {
         TR_ASSERT_FATAL(false, "Too many arguments.");
         }
      }

   if (dropVMAccess)
      {
#ifdef J9VM_INTERP_ATOMIC_FREE_JNI
      acquireVMAccessAtomicFree(callNode, vmThreadReg);
#else
      acquireVMAccess(callNode, vmThreadReg);
#endif
      }

   if (returnRegister != NULL)
      {
      adjustReturnValue(callNode, wrapRefs, returnRegister);
      }

   if (createJNIFrame)
      {
      restoreJNICallOutFrame(callNode, tearDownJNIFrame, vmThreadReg, javaStackReg, x9Reg);
      }

   if (checkExceptions)
      {
      checkForJNIExceptions(callNode, vmThreadReg);
      }

   TR::LabelSymbol *depLabel = generateLabelSymbol(cg());
   generateLabelInstruction(cg(), TR::InstOpCode::label, callNode, depLabel, postLabelDeps);

   callNode->setRegister(returnRegister);
   return returnRegister;
   }
