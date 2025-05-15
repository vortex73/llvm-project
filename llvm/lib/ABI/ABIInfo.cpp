#include "llvm/ABI/ABIInfo.h"

using namespace llvm::abi;
bool ABIInfo::isAggregateTypeForABI(const Type *Ty) const {
  // Check for fundamental scalar types
  if (Ty->isInteger() || Ty->isFloat() || Ty->isPointer() || Ty->isVector())
    return false;

  // Everything else is treated as aggregate
  return true;
}

// Create indirect return with natural alignment
ABIArgInfo ABIInfo::getNaturalAlignIndirect(const Type *Ty, bool ByVal) const {
  return ABIArgInfo::getIndirect(Ty->getAlignment().value(), ByVal);
}
RecordArgABI ABIInfo::getRecordArgABI(const StructType *ST) const {
  if (ST && !ST->canPassInRegisters())
    return RAA_Indirect;
  return RAA_Default;
}

RecordArgABI ABIInfo::getRecordArgABI(const StructType *ST,
                                      bool IsCxxRecord) const {
  if (!IsCxxRecord) {
    if (!ST->canPassInRegisters())
      return RAA_Indirect;
    return RAA_Default;
  }
  return getRecordArgABI(ST);
}

RecordArgABI ABIInfo::getRecordArgABI(const Type *Ty) const {
  const StructType *ST = dyn_cast<StructType>(Ty);
  if (!ST)
    return RAA_Default;
  return getRecordArgABI(ST, ST->isCXXRecord());
}

bool ABIInfo::isZeroSizedType(const Type *Ty) const {
  return Ty->getSizeInBits().getFixedValue() == 0;
}

bool ABIInfo::isEmptyRecord(const StructType *ST) const {
  if (ST->hasFlexibleArrayMember() || ST->isPolymorphic() ||
      ST->getNumVirtualBaseClasses() != 0)
    return false;

  for (unsigned I = 0; I < ST->getNumBaseClasses(); ++I) {
    const Type *BaseTy = ST->getBaseClasses()[I].FieldType;
    auto *BaseST = dyn_cast<StructType>(BaseTy);
    if (!BaseST || !isEmptyRecord(BaseST))
      return false;
  }

  for (unsigned I = 0; I < ST->getNumFields(); ++I) {
    const FieldInfo &FI = ST->getFields()[I];

    if (FI.IsBitField && FI.BitFieldWidth == 0)
      continue;
    if (FI.IsUnnamedBitfield)
      continue;

    if (!isZeroSizedType(FI.FieldType))
      return false;
  }
  return true;
}

bool ABIInfo::isEmptyField(const FieldInfo &FI) const {
  if (FI.IsUnnamedBitfield)
    return true;
  if (FI.IsBitField && FI.BitFieldWidth == 0)
    return true;

  const Type *Ty = FI.FieldType;
  while (auto *AT = dyn_cast<ArrayType>(Ty)) {
    if (AT->getNumElements() != 1)
      break;
    Ty = AT->getElementType();
  }

  if (auto *ST = dyn_cast<StructType>(Ty))
    return isEmptyRecord(ST);

  return isZeroSizedType(Ty);
}
