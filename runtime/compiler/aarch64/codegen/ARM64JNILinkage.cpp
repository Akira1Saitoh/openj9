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

#include <algorithm>
#include "codegen/ARM64HelperCallSnippet.hpp"
#include "codegen/CodeGeneratorUtils.hpp"
#include "codegen/GenerateInstructions.hpp"
#include "codegen/Linkage_inlines.hpp"
#include "codegen/RegisterDependency.hpp"
#include "env/StackMemoryRegion.hpp"
#include "env/VMJ9.h"
#include "il/Node.hpp"
#include "il/Node_inlines.hpp"
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
                                                 TR::Register *vmThreadReg, TR::Register *javaStackReg)
   {
   TR_UNIMPLEMENTED();
   }

void J9::ARM64::JNILinkage::restoreJNICallOutFrame(TR::Node *callNode, bool tearDownJNIFrame, TR::Register *vmThreadReg, TR::Register *javaStackReg)
   {
   TR_UNIMPLEMENTED();
   }


size_t J9::ARM64::JNILinkage::buildJNIArgs(TR::Node *callNode, TR::RegisterDependencyConditions *dependencies, bool passThread, bool passReceiver, bool killNonVolatileGPRs)
   {
   const TR::ARM64LinkageProperties &properties = _systemLinkage->getProperties();
   TR::ARM64MemoryArgument *pushToMemory = NULL;
   TR::Register *argMemReg;
   TR::Register *tempReg;
   int32_t argIndex = 0;
   int32_t numMemArgs = 0;
   int32_t argSize = 0;
   int32_t numIntegerArgs = 0;
   int32_t numFloatArgs = 0;
   int32_t totalSize;
   int32_t i;

   TR::Node *child;
   TR::DataType childType;
   TR::DataType resType = callNode->getType();

   uint32_t firstArgumentChild = callNode->getFirstArgumentIndex();
   if (passThread)
      {
      numIntegerArgs += 1;
      }
   // if fastJNI do not pass the receiver just evaluate the first child
   if (!passReceiver)
      {
      TR::Node *firstArgChild = callNode->getChild(firstArgumentChild);
      cg()->evaluate(firstArgChild);
      cg()->decReferenceCount(firstArgChild);
      firstArgumentChild += 1;
      }
   /* Step 1 - figure out how many arguments are going to be spilled to memory i.e. not in registers */
   for (i = firstArgumentChild; i < callNode->getNumChildren(); i++)
      {
      child = callNode->getChild(i);
      childType = child->getDataType();

      switch (childType)
         {
         case TR::Int8:
         case TR::Int16:
         case TR::Int32:
         case TR::Int64:
         case TR::Address:
            if (numIntegerArgs >= properties.getNumIntArgRegs())
               numMemArgs++;
            numIntegerArgs++;
            break;

         case TR::Float:
         case TR::Double:
            if (numFloatArgs >= properties.getNumFloatArgRegs())
                  numMemArgs++;
            numFloatArgs++;
            break;

         default:
            TR_ASSERT(false, "Argument type %s is not supported\n", childType.toString());
         }
      }

   // From here, down, any new stack allocations will expire / die when the function returns
   TR::StackMemoryRegion stackMemoryRegion(*trMemory());
   /* End result of Step 1 - determined number of memory arguments! */
   if (numMemArgs > 0)
      {
      pushToMemory = new (trStackMemory()) TR::ARM64MemoryArgument[numMemArgs];

      argMemReg = cg()->allocateRegister();
      }

   totalSize = numMemArgs * 8;
   // align to 16-byte boundary
   totalSize = (totalSize + 15) & (~15);

   numIntegerArgs = 0;
   numFloatArgs = 0;

   if (passThread)
      {
      // first argument is JNIenv
      numIntegerArgs += 1;
      }

   for (i = firstArgumentChild; i < callNode->getNumChildren(); i++)
      {
      TR::MemoryReference *mref = NULL;
      TR::Register *argRegister;
      TR::InstOpCode::Mnemonic op;
      bool           checkSplit = true;

      child = callNode->getChild(i);
      childType = child->getDataType();

      switch (childType)
         {
         case TR::Int8:
         case TR::Int16:
         case TR::Int32:
         case TR::Int64:
         case TR::Address:
            if (childType == TR::Address)
               {
               argRegister = pushJNIReferenceArg(child);
               checkSplit = false;
               }
            else if (childType == TR::Int64)
               argRegister = pushLongArg(child);
            else
               argRegister = pushIntegerWordArg(child);

            if (numIntegerArgs < properties.getNumIntArgRegs())
               {
               if (checkSplit && !cg()->canClobberNodesRegister(child, 0))
                  {
                  if (argRegister->containsCollectedReference())
                     tempReg = cg()->allocateCollectedReferenceRegister();
                  else
                     tempReg = cg()->allocateRegister();
                  generateMovInstruction(cg(), callNode, tempReg, argRegister);
                  argRegister = tempReg;
                  }
               if (numIntegerArgs == 0 &&
                  (resType.isAddress() || resType.isInt32() || resType.isInt64()))
                  {
                  TR::Register *resultReg;
                  if (resType.isAddress())
                     resultReg = cg()->allocateCollectedReferenceRegister();
                  else
                     resultReg = cg()->allocateRegister();

                  dependencies->addPreCondition(argRegister, TR::RealRegister::x0);
                  dependencies->addPostCondition(resultReg, TR::RealRegister::x0);
                  }
               else
                  {
                  TR::addDependency(dependencies, argRegister, properties.getIntegerArgumentRegister(numIntegerArgs), TR_GPR, cg());
                  }
               }
            else
               {
               // numIntegerArgs >= properties.getNumIntArgRegs()
               if (childType == TR::Address || childType == TR::Int64)
                  {
                  op = TR::InstOpCode::strpostx;
                  }
               else
                  {
                  op = TR::InstOpCode::strpostw;
                  }
               mref = getOutgoingArgumentMemRef(argMemReg, argRegister, op, pushToMemory[argIndex++]);
               argSize += 8; // always 8-byte aligned
               }
            numIntegerArgs++;
            break;

         case TR::Float:
         case TR::Double:
            if (childType == TR::Float)
               argRegister = pushFloatArg(child);
            else
               argRegister = pushDoubleArg(child);

            if (numFloatArgs < properties.getNumFloatArgRegs())
               {
               if (!cg()->canClobberNodesRegister(child, 0))
                  {
                  tempReg = cg()->allocateRegister(TR_FPR);
                  op = (childType == TR::Float) ? TR::InstOpCode::fmovs : TR::InstOpCode::fmovd;
                  generateTrg1Src1Instruction(cg(), op, callNode, tempReg, argRegister);
                  argRegister = tempReg;
                  }
               if ((numFloatArgs == 0 && resType.isFloatingPoint()))
                  {
                  TR::Register *resultReg;
                  if (resType.getDataType() == TR::Float)
                     resultReg = cg()->allocateSinglePrecisionRegister();
                  else
                     resultReg = cg()->allocateRegister(TR_FPR);

                  dependencies->addPreCondition(argRegister, TR::RealRegister::v0);
                  dependencies->addPostCondition(resultReg, TR::RealRegister::v0);
                  }
               else
                  {
                  TR::addDependency(dependencies, argRegister, properties.getFloatArgumentRegister(numFloatArgs), TR_FPR, cg());
                  }
               }
            else
               {
               // numFloatArgs >= properties.getNumFloatArgRegs()
               if (childType == TR::Double)
                  {
                  op = TR::InstOpCode::vstrpostd;
                  }
               else
                  {
                  op = TR::InstOpCode::vstrposts;
                  }
               mref = getOutgoingArgumentMemRef(argMemReg, argRegister, op, pushToMemory[argIndex++]);
               argSize += 8; // always 8-byte aligned
               }
            numFloatArgs++;
            break;
         } // end of switch
      } // end of for

   for (int32_t i = TR::RealRegister::FirstGPR; i <= TR::RealRegister::LastGPR; ++i)
      {
      TR::RealRegister::RegNum realReg = (TR::RealRegister::RegNum)i;
      if (getProperties().getRegisterFlags(realReg) & ARM64_Reserved)
         {
         continue;
         }
      if (properties.getPreserved(realReg))
         {
         if (killNonVolatileGPRs)
            {
            // We release VM access around the native call,
            // so even though the callee's linkage won't alter a
            // given register, we still have a problem if a stack walk needs to
            // inspect/modify that register because once we're in C-land, we have
            // no idea where that regsiter's value is located.  Therefore, we need
            // to spill even the callee-saved registers around the call.
            //
            // In principle, we only need to do this for registers that contain
            // references.  However, at this location in the code, we don't yet
            // know which real registers those would be.  Tragically, this causes
            // us to save/restore ALL preserved registers in any method containing
            // a JNI call.
            TR::addDependency(dependencies, NULL, realReg, TR_GPR, cg());
            }
         continue;
         }

      if (!dependencies->searchPreConditionRegister(realReg))
         {
         if (realReg == properties.getIntegerArgumentRegister(0) && callNode->getDataType() == TR::Address)
            {
            dependencies->addPreCondition(cg()->allocateRegister(), TR::RealRegister::x0);
            dependencies->addPostCondition(cg()->allocateCollectedReferenceRegister(), TR::RealRegister::x0);
            }
         else
            {
            TR::addDependency(dependencies, NULL, realReg, TR_GPR, cg());
            }
         }
      }

   int32_t floatRegsUsed = std::min<int32_t>(numFloatArgs, properties.getNumFloatArgRegs());
   for (i = (TR::RealRegister::RegNum)((uint32_t)TR::RealRegister::v0 + floatRegsUsed); i <= TR::RealRegister::LastFPR; i++)
      {
      if (!properties.getPreserved((TR::RealRegister::RegNum)i))
         {
         // NULL dependency for non-preserved regs
         TR::addDependency(dependencies, NULL, (TR::RealRegister::RegNum)i, TR_FPR, cg());
         }
      }

   if (numMemArgs > 0)
      {
      TR::RealRegister *sp = cg()->machine()->getRealRegister(properties.getStackPointerRegister());
      generateTrg1Src1ImmInstruction(cg(), TR::InstOpCode::subimmx, callNode, argMemReg, sp, totalSize);

      for (argIndex = 0; argIndex < numMemArgs; argIndex++)
         {
         TR::Register *aReg = pushToMemory[argIndex].argRegister;
         generateMemSrc1Instruction(cg(), pushToMemory[argIndex].opCode, callNode, pushToMemory[argIndex].argMemory, aReg);
         cg()->stopUsingRegister(aReg);
         }

      cg()->stopUsingRegister(argMemReg);
      }

   return totalSize;
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
      buildJNICallOutFrame(callNode, (resolvedMethod == comp()->getCurrentMethod()), returnLabel, vmThreadReg, javaStackReg);
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
      restoreJNICallOutFrame(callNode, tearDownJNIFrame, vmThreadReg, javaStackReg);
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
