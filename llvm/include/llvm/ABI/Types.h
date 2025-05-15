//===- ABI/Types.h ----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the Types and related helper methods concerned to the
/// LLVMABI library which mirrors ABI related type information from
/// the LLVM frontend.
///
//===----------------------------------------------------------------------===//
#ifndef LLVM_ABI_TYPES_H
#define LLVM_ABI_TYPES_H

#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/TypeSize.h"
#include <algorithm>
#include <cstdint>

namespace llvm {
namespace abi {

enum class TypeKind {
  Void,
  MemberPointer,
  Complex,
  Integer,
  Float,
  Pointer,
  Array,
  Vector,
  Struct,
};

class Type {
private:
  TypeSize getTypeStoreSize() const {
    TypeSize StoreSizeInBits = getTypeStoreSizeInBits();
    return {StoreSizeInBits.getKnownMinValue() / 8,
            StoreSizeInBits.isScalable()};
  }
  TypeSize getTypeStoreSizeInBits() const {
    TypeSize BaseSize = getSizeInBits();
    uint64_t AlignedSizeInBits =
        alignToPowerOf2(BaseSize.getKnownMinValue(), 8);
    return {AlignedSizeInBits, BaseSize.isScalable()};
  }

protected:
  TypeKind Kind;
  TypeSize SizeInBits;
  Align ABIAlignment;

  Type(TypeKind K, TypeSize Size, Align Align)
      : Kind(K), SizeInBits(Size), ABIAlignment(Align) {}

public:
  TypeKind getKind() const { return Kind; }
  TypeSize getSizeInBits() const { return SizeInBits; }
  Align getAlignment() const { return ABIAlignment; }

  TypeSize getTypeAllocSize() const {
    return alignTo(getTypeStoreSize(), getAlignment().value());
  }

  bool isVoid() const { return Kind == TypeKind::Void; }
  bool isInteger() const { return Kind == TypeKind::Integer; }
  bool isFloat() const { return Kind == TypeKind::Float; }
  bool isPointer() const { return Kind == TypeKind::Pointer; }
  bool isArray() const { return Kind == TypeKind::Array; }
  bool isVector() const { return Kind == TypeKind::Vector; }
  bool isStruct() const { return Kind == TypeKind::Struct; }
  bool isMemberPointer() const { return Kind == TypeKind::MemberPointer; }
  bool isComplex() const { return Kind == TypeKind::Complex; }
};

class VoidType : public Type {
public:
  VoidType() : Type(TypeKind::Void, TypeSize::getFixed(0), Align(1)) {}

  static bool classof(const Type *T) { return T->getKind() == TypeKind::Void; }
};

class ComplexType : public Type {
public:
  ComplexType(const Type *ElementType, uint64_t SizeInBits, Align Alignment)
      : Type(TypeKind::Complex, TypeSize::getFixed(SizeInBits), Alignment),
        ElementType(ElementType) {}

  const Type *getElementType() const { return ElementType; }

  static bool classof(const Type *T) {
    return T->getKind() == TypeKind::Complex;
  }

private:
  const Type *ElementType;
};

class IntegerType : public Type {
private:
  bool IsSigned;
  bool IsBoolean;
  bool IsBitInt;
  bool IsPromotable;

public:
  IntegerType(uint64_t BitWidth, Align Align, bool Signed, bool IsBool = false,
              bool BitInt = false, bool IsPromotableInt = false)
      : Type(TypeKind::Integer, TypeSize::getFixed(BitWidth), Align),
        IsSigned(Signed), IsBoolean(IsBool), IsBitInt(BitInt),
        IsPromotable(IsPromotableInt) {}

  bool isSigned() const { return IsSigned; }
  bool isBool() const { return IsBoolean; }
  bool isBitInt() const { return IsBitInt; }
  bool isPromotableIntegerType() const { return IsPromotable; }

  static bool classof(const Type *T) {
    return T->getKind() == TypeKind::Integer;
  }
};

class FloatType : public Type {
private:
  const fltSemantics *Semantics;

public:
  FloatType(const fltSemantics &FloatSemantics, Align Align)
      : Type(TypeKind::Float,
             TypeSize::getFixed(APFloat::getSizeInBits(FloatSemantics)), Align),
        Semantics(&FloatSemantics) {}

  const fltSemantics *getSemantics() const { return Semantics; }
  static bool classof(const Type *T) { return T->getKind() == TypeKind::Float; }
};

class PointerLikeType : public Type {
protected:
  unsigned AddrSpace;
  PointerLikeType(TypeKind K, TypeSize Size, Align Align, unsigned AS = 0)
      : Type(K, Size, Align), AddrSpace(AS) {}

public:
  virtual ~PointerLikeType() = default;
  unsigned getAddrSpace() const { return AddrSpace; }
  bool isMemberPointer() const { return getKind() == TypeKind::MemberPointer; }

  static bool classof(const Type *T) {
    return T->getKind() == TypeKind::Pointer ||
           T->getKind() == TypeKind::MemberPointer;
  }
};

class PointerType : public PointerLikeType {
public:
  PointerType(uint64_t Size, Align Align, unsigned AddressSpace = 0)
      : PointerLikeType(TypeKind::Pointer, TypeSize::getFixed(Size), Align,
                        AddressSpace) {}

  static bool classof(const Type *T) {
    return T->getKind() == TypeKind::Pointer;
  }
};

class MemberPointerType : public PointerLikeType {
private:
  bool IsFunctionPointer;

public:
  MemberPointerType(bool IsFunctionPointer, uint64_t SizeInBits,
                    Align Alignment, unsigned AddressSpace = 0)
      : PointerLikeType(TypeKind::MemberPointer, TypeSize::getFixed(SizeInBits),
                        Alignment, AddressSpace),
        IsFunctionPointer(IsFunctionPointer) {}
  bool isFunctionPointer() const { return IsFunctionPointer; }

  static bool classof(const Type *T) {
    return T->getKind() == TypeKind::MemberPointer;
  }
};

class ArrayType : public Type {
private:
  const Type *ElementType;
  uint64_t NumElements;
  bool IsMatrix;

public:
  ArrayType(const Type *ElemType, uint64_t NumElems, bool IsMatrixType = false)
      : Type(TypeKind::Array, ElemType->getSizeInBits() * NumElems,
             ElemType->getAlignment()),
        ElementType(ElemType), NumElements(NumElems), IsMatrix(IsMatrixType) {}

  const Type *getElementType() const { return ElementType; }
  uint64_t getNumElements() const { return NumElements; }
  bool isMatrixType() const { return IsMatrix; }

  static bool classof(const Type *T) { return T->getKind() == TypeKind::Array; }
};

class VectorType : public Type {
private:
  const Type *ElementType;
  ElementCount NumElements;

public:
  VectorType(const Type *ElemType, ElementCount NumElems, Align Align)
      : Type(TypeKind::Vector,
             TypeSize(ElemType->getSizeInBits().getFixedValue() *
                          NumElems.getKnownMinValue(),
                      NumElems.isScalable()),
             Align),
        ElementType(ElemType), NumElements(NumElems) {}

  const Type *getElementType() const { return ElementType; }
  ElementCount getNumElements() const { return NumElements; }

  static bool classof(const Type *T) {
    return T->getKind() == TypeKind::Vector;
  }
};

struct FieldInfo {
  const Type *FieldType;
  uint64_t OffsetInBits;
  bool IsBitField;
  bool IsUnnamedBitfield;
  uint64_t BitFieldWidth;
  bool IsNamed;
  bool HasNamedDataMember;

  FieldInfo(const Type *Type, uint64_t Offset = 0, bool BitField = false,
            uint64_t BFWidth = 0, bool IsUnnamedBF = false, bool Named = true,
            bool HasNamedData = true)
      : FieldType(Type), OffsetInBits(Offset), IsBitField(BitField),
        IsUnnamedBitfield(IsUnnamedBF), BitFieldWidth(BFWidth), IsNamed(Named),
        HasNamedDataMember(HasNamedData) {}
};

enum class StructPacking { Default, Packed, ExplicitPacking };

class StructType : public Type {
private:
  ArrayRef<FieldInfo> Fields;
  ArrayRef<FieldInfo> BaseClasses;
  ArrayRef<FieldInfo> VirtualBaseClasses;
  StructPacking Packing;
  bool CanPassInRegisters;
  bool IsCoercedStruct;
  bool IsUnion;
  bool IsTransparent;

  bool IsCXXRecord;
  bool IsPolymorphic;
  bool HasNonTrivialCopyConstructor;
  bool HasNonTrivialDestructor;
  bool HasFlexibleArrayMember;
  bool HasUnalignedFields;

public:
  StructType(ArrayRef<FieldInfo> StructFields, ArrayRef<FieldInfo> Bases,
             ArrayRef<FieldInfo> VBases, TypeSize Size, Align Align,
             StructPacking Pack = StructPacking::Default, bool Union = false,
             bool CXXRecord = false, bool Polymorphic = false,
             bool NonTrivialCopy = false, bool NonTrivialDtor = false,
             bool FlexibleArray = false, bool UnalignedFields = false,
             bool CanPassInRegs = false, bool IsCoercedStr = false,
             bool Transparent = false)
      : Type(TypeKind::Struct, Size, Align), Fields(StructFields),
        BaseClasses(Bases), VirtualBaseClasses(VBases), Packing(Pack),
        CanPassInRegisters(CanPassInRegs), IsCoercedStruct(IsCoercedStr),
        IsUnion(Union), IsTransparent(Transparent), IsCXXRecord(CXXRecord),
        IsPolymorphic(Polymorphic),
        HasNonTrivialCopyConstructor(NonTrivialCopy),
        HasNonTrivialDestructor(NonTrivialDtor),
        HasFlexibleArrayMember(FlexibleArray),
        HasUnalignedFields(UnalignedFields) {}

  uint32_t getNumFields() const { return Fields.size(); }
  StructPacking getPacking() const { return Packing; }

  bool isUnion() const { return IsUnion; }
  bool isCXXRecord() const { return IsCXXRecord; }
  bool isPolymorphic() const { return IsPolymorphic; }
  bool hasNonTrivialCopyConstructor() const {
    return HasNonTrivialCopyConstructor;
  }
  bool isCoercedStruct() const { return IsCoercedStruct; }
  bool canPassInRegisters() const { return CanPassInRegisters; }
  bool hasNonTrivialDestructor() const { return HasNonTrivialDestructor; }
  bool hasFlexibleArrayMember() const { return HasFlexibleArrayMember; }
  bool hasUnalignedFields() const { return HasUnalignedFields; }

  uint32_t getNumBaseClasses() const { return BaseClasses.size(); }
  uint32_t getNumVirtualBaseClasses() const {
    return VirtualBaseClasses.size();
  }
  bool isTransparentUnion() const { return IsTransparent; }
  ArrayRef<FieldInfo> getFields() const { return Fields; }
  ArrayRef<FieldInfo> getBaseClasses() const { return BaseClasses; }
  ArrayRef<FieldInfo> getVirtualBaseClasses() const {
    return VirtualBaseClasses;
  }

  bool isEmptyForABI(const llvm::abi::Type *Ty) const {
    const auto *ST = dyn_cast<StructType>(Ty);
    if (!ST)
      return false;

    for (const auto &Field : ST->getFields()) {
      if (!Field.IsUnnamedBitfield)
        return false;
    }

    if (ST->isCXXRecord()) {
      for (const auto &Base : ST->getBaseClasses()) {
        if (!isEmptyForABI(Base.FieldType))
          return false;
      }

      for (const auto &VBase : ST->getVirtualBaseClasses()) {
        if (!isEmptyForABI(VBase.FieldType))
          return false;
      }
    }

    return true;
  }

  const FieldInfo *getElementContainingOffset(unsigned OffsetInBits) const {
    std::vector<std::pair<unsigned, const FieldInfo *>> AllElements;

    for (const auto &Base : BaseClasses) {
      if (!isEmptyForABI(Base.FieldType))
        AllElements.emplace_back(Base.OffsetInBits, &Base);
    }

    for (const auto &VBase : VirtualBaseClasses) {
      if (!isEmptyForABI(VBase.FieldType))
        AllElements.emplace_back(VBase.OffsetInBits, &VBase);
    }

    for (const auto &Field : Fields) {
      if (Field.IsUnnamedBitfield)
        continue;
      AllElements.emplace_back(Field.OffsetInBits, &Field);
    }

    std::stable_sort(
        AllElements.begin(), AllElements.end(),
        [](const auto &A, const auto &B) { return A.first < B.first; });

    auto It =
        std::upper_bound(AllElements.begin(), AllElements.end(), OffsetInBits,
                         [](unsigned Offset, const auto &Element) {
                           return Offset < Element.first;
                         });

    if (It == AllElements.begin())
      return nullptr;

    --It;

    const FieldInfo *Candidate = It->second;
    unsigned ElementStart = It->first;
    unsigned ElementSize =
        Candidate->FieldType->getSizeInBits().getFixedValue();

    if (OffsetInBits >= ElementStart &&
        OffsetInBits < ElementStart + ElementSize)
      return Candidate;

    return nullptr;
  }
  static bool classof(const Type *T) {
    return T->getKind() == TypeKind::Struct;
  }
};

// Union metadata to preserve original field information

// API for creating ABI Types
class TypeBuilder {
private:
  BumpPtrAllocator &Allocator;

public:
  explicit TypeBuilder(BumpPtrAllocator &Alloc) : Allocator(Alloc) {}

  const VoidType *getVoidType() {
    return new (Allocator.Allocate<VoidType>()) VoidType();
  }

  const IntegerType *getIntegerType(uint64_t BitWidth, Align Align, bool Signed,
                                    bool IsBoolean = false,
                                    bool IsBitInt = false,
                                    bool IsPromotable = false) {
    return new (Allocator.Allocate<IntegerType>())
        IntegerType(BitWidth, Align, Signed, IsBoolean, IsBitInt, IsPromotable);
  }

  const FloatType *getFloatType(const fltSemantics &Semantics, Align Align) {
    return new (Allocator.Allocate<FloatType>()) FloatType(Semantics, Align);
  }

  const PointerType *getPointerType(uint64_t Size, Align Align,
                                    unsigned Addrspace = 0) {
    return new (Allocator.Allocate<PointerType>())
        PointerType(Size, Align, Addrspace);
  }

  const ArrayType *getArrayType(const Type *ElementType, uint64_t NumElements,
                                bool IsMatrixType = false) {
    return new (Allocator.Allocate<ArrayType>())
        ArrayType(ElementType, NumElements, IsMatrixType);
  }

  const VectorType *getVectorType(const Type *ElementType,
                                  ElementCount NumElements, Align Align) {
    return new (Allocator.Allocate<VectorType>())
        VectorType(ElementType, NumElements, Align);
  }

  // TODO: clean up this function
  const StructType *
  getStructType(ArrayRef<FieldInfo> Fields, TypeSize Size, Align Align,
                StructPacking Pack = StructPacking::Default,
                ArrayRef<FieldInfo> BaseClasses = {},
                ArrayRef<FieldInfo> VirtualBaseClasses = {},
                bool CXXRecord = false, bool Polymorphic = false,
                bool NonTrivialCopy = false, bool NonTrivialDtor = false,
                bool FlexibleArray = false, bool UnalignedFields = false,
                bool CanPassInRegister = false) {
    FieldInfo *FieldArray = Allocator.Allocate<FieldInfo>(Fields.size());
    std::copy(Fields.begin(), Fields.end(), FieldArray);

    FieldInfo *BaseArray = nullptr;
    if (!BaseClasses.empty()) {
      BaseArray = Allocator.Allocate<FieldInfo>(BaseClasses.size());
      std::copy(BaseClasses.begin(), BaseClasses.end(), BaseArray);
    }

    FieldInfo *VBaseArray = nullptr;
    if (!VirtualBaseClasses.empty()) {
      VBaseArray = Allocator.Allocate<FieldInfo>(VirtualBaseClasses.size());
      std::copy(VirtualBaseClasses.begin(), VirtualBaseClasses.end(),
                VBaseArray);
    }

    ArrayRef<FieldInfo> FieldsRef(FieldArray, Fields.size());
    ArrayRef<FieldInfo> BasesRef(BaseArray, BaseClasses.size());
    ArrayRef<FieldInfo> VBasesRef(VBaseArray, VirtualBaseClasses.size());

    return new (Allocator.Allocate<StructType>())
        StructType(FieldsRef, BasesRef, VBasesRef, Size, Align, Pack, false,
                   CXXRecord, Polymorphic, NonTrivialCopy, NonTrivialDtor,
                   FlexibleArray, UnalignedFields, CanPassInRegister);
  }
  const StructType *
  getCoercedStructType(ArrayRef<FieldInfo> Fields, TypeSize Size, Align Align,
                       StructPacking Pack = StructPacking::Default,
                       ArrayRef<FieldInfo> BaseClasses = {},
                       ArrayRef<FieldInfo> VirtualBaseClasses = {},
                       bool CXXRecord = false, bool Polymorphic = false,
                       bool NonTrivialCopy = false, bool NonTrivialDtor = false,
                       bool FlexibleArray = false, bool UnalignedFields = false,
                       bool CanPassInRegister = false) {
    FieldInfo *FieldArray = Allocator.Allocate<FieldInfo>(Fields.size());
    std::copy(Fields.begin(), Fields.end(), FieldArray);

    FieldInfo *BaseArray = nullptr;
    if (!BaseClasses.empty()) {
      BaseArray = Allocator.Allocate<FieldInfo>(BaseClasses.size());
      std::copy(BaseClasses.begin(), BaseClasses.end(), BaseArray);
    }

    FieldInfo *VBaseArray = nullptr;
    if (!VirtualBaseClasses.empty()) {
      VBaseArray = Allocator.Allocate<FieldInfo>(VirtualBaseClasses.size());
      std::copy(VirtualBaseClasses.begin(), VirtualBaseClasses.end(),
                VBaseArray);
    }

    ArrayRef<FieldInfo> FieldsRef(FieldArray, Fields.size());
    ArrayRef<FieldInfo> BasesRef(BaseArray, BaseClasses.size());
    ArrayRef<FieldInfo> VBasesRef(VBaseArray, VirtualBaseClasses.size());
    return new (Allocator.Allocate<StructType>())
        StructType(FieldsRef, BasesRef, VBasesRef, Size, Align, Pack, false,
                   CXXRecord, Polymorphic, NonTrivialCopy, NonTrivialDtor,
                   FlexibleArray, UnalignedFields, CanPassInRegister, true);
  }
  const StructType *getUnionType(ArrayRef<FieldInfo> Fields, TypeSize Size,
                                 Align Align,
                                 StructPacking Pack = StructPacking::Default,
                                 bool IsTransparent = false,
                                 bool CanPassInRegs = false,
                                 bool CXXRecord = false) {
    FieldInfo *FieldArray = Allocator.Allocate<FieldInfo>(Fields.size());

    for (size_t I = 0; I < Fields.size(); ++I) {
      const FieldInfo &Field = Fields[I];
      new (&FieldArray[I])
          FieldInfo(Field.FieldType, 0, Field.IsBitField, Field.BitFieldWidth,
                    Field.IsUnnamedBitfield);
    }

    ArrayRef<FieldInfo> FieldsRef(FieldArray, Fields.size());

    return new (Allocator.Allocate<StructType>())
        StructType(FieldsRef, ArrayRef<FieldInfo>(), ArrayRef<FieldInfo>(),
                   Size, Align, Pack, true, CXXRecord, false, false, false,
                   false, false, CanPassInRegs, false, IsTransparent);
  }

  const ComplexType *getComplexType(const Type *ElementType, Align Align) {
    // Complex types have two elements (real and imaginary parts)
    uint64_t ElementSize = ElementType->getSizeInBits().getFixedValue();
    uint64_t ComplexSize = ElementSize * 2;

    return new (Allocator.Allocate<ComplexType>())
        ComplexType(ElementType, ComplexSize, Align);
  }

  const MemberPointerType *getMemberPointerType(bool IsFunctionPointer,
                                                uint64_t SizeInBits,
                                                Align Align) {
    return new (Allocator.Allocate<MemberPointerType>())
        MemberPointerType(IsFunctionPointer, SizeInBits, Align);
  }
};

} // namespace abi
} // namespace llvm

#endif
