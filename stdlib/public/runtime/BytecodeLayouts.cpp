//===--- RuntimeValueWitness.cpp - Value Witness Runtime Implementation---===//
//
// This source file is part of the Swift.org open source project
//
// Copyright (c) 2014 - 2017 Apple Inc. and the Swift project authors
// Licensed under Apache License v2.0 with Runtime Library Exception
//
// See https://swift.org/LICENSE.txt for license information
// See https://swift.org/CONTRIBUTORS.txt for the list of Swift project authors
//
//===----------------------------------------------------------------------===//
//
// Implementations of runtime determined value witness functions
// This file is intended to be statically linked into executables until it is
// fully added to the runtime.
//
//===----------------------------------------------------------------------===//

#include "BytecodeLayouts.h"
#include "../SwiftShims/swift/shims/HeapObject.h"
#include "EnumImpl.h"
#include "WeakReference.h"
#include "swift/ABI/MetadataValues.h"
#include "swift/ABI/System.h"
#include "swift/Runtime/Error.h"
#include "swift/Runtime/HeapObject.h"
#include "llvm/Support/SwapByteOrder.h"
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <type_traits>
#if SWIFT_OBJC_INTEROP
#include "swift/Runtime/ObjCBridge.h"
#include <Block.h>
#endif
#if SWIFT_PTRAUTH
#include <ptrauth.h>
#endif

using namespace swift;

static Metadata *getExistentialTypeMetadata(OpaqueValue *object) {
  return reinterpret_cast<Metadata**>(object)[NumWords_ValueBuffer];
}

template <typename FnTy>
static const FnTy readRelativeFunctionPointer(LayoutStringReader &reader) {
  static_assert(std::is_pointer<FnTy>::value);

  auto absolute = reader.layoutStr + reader.offset;
  auto relativeOffset =
      (uintptr_t)(intptr_t)(int32_t)reader.readBytes<intptr_t>();
  FnTy fn;

#if SWIFT_PTRAUTH
  fn = (FnTy)ptrauth_sign_unauthenticated(
      (void *)((uintptr_t)absolute + relativeOffset),
      ptrauth_key_function_pointer, 0);
#else
  fn = (FnTy)((uintptr_t)absolute + relativeOffset);
#endif

  return fn;
}

typedef Metadata *(*MetadataAccessor)(const Metadata *const *);

static const Metadata *getResilientTypeMetadata(const Metadata *metadata,
                                                LayoutStringReader &reader) {
  auto fn = readRelativeFunctionPointer<MetadataAccessor>(reader);
  return fn(metadata->getGenericArgs());
}

static uint64_t readTagBytes(const uint8_t *addr, uint8_t byteCount) {
  switch (byteCount) {
  case 1:
    return addr[0];
  case 2: {
    uint16_t res = 0;
    memcpy(&res, addr, sizeof(uint16_t));
    return res;
  }
  case 4: {
    uint32_t res = 0;
    memcpy(&res, addr, sizeof(uint32_t));
    return res;
  }
  case 8: {
    uint64_t res = 0;
    memcpy(&res, addr, sizeof(uint64_t));
    return res;
  }
  default:
    swift_unreachable("Unsupported tag byte length.");
  }
}

static void handleRefCountsDestroy(const Metadata *metadata,
                          LayoutStringReader &reader,
                          uintptr_t &addrOffset,
                          uint8_t *addr);

template <typename ...Params>
static void handleEnd(const Metadata *metadata,
                          LayoutStringReader &reader,
                          uintptr_t &addrOffset,
                          uint8_t *addr,
                          Params ...params) {
  return;
}

static void errorDestroyBranchless(const Metadata *metadata,
                             LayoutStringReader &reader,
                             uintptr_t &addrOffset,
                             uint8_t *addr) {
  SwiftError *error = *(SwiftError**)(addr + addrOffset);
  swift_errorRelease(error);
}

static void nativeStrongDestroyBranchless(const Metadata *metadata,
                             LayoutStringReader &reader,
                             uintptr_t &addrOffset,
                             uint8_t *addr) {
  HeapObject *object = (HeapObject*)((*(uintptr_t *)(addr + addrOffset)) & ~_swift_abi_SwiftSpareBitsMask);
  swift_release(object);
}

static void unownedDestroyBranchless(const Metadata *metadata,
                             LayoutStringReader &reader,
                             uintptr_t &addrOffset,
                             uint8_t *addr) {
  HeapObject *object = (HeapObject*)((*(uintptr_t *)(addr + addrOffset)) & ~_swift_abi_SwiftSpareBitsMask);
  swift_unownedRelease(object);
}

static void weakDestroyBranchless(const Metadata *metadata,
                             LayoutStringReader &reader,
                             uintptr_t &addrOffset,
                             uint8_t *addr) {
  swift_weakDestroy((WeakReference *)(addr + addrOffset));
}

static void unknownDestroyBranchless(const Metadata *metadata,
                             LayoutStringReader &reader,
                             uintptr_t &addrOffset,
                             uint8_t *addr) {
  void *object = *(void**)(addr + addrOffset);
  swift_unknownObjectRelease(object);
}

static void unknownUnownedDestroyBranchless(const Metadata *metadata,
                             LayoutStringReader &reader,
                             uintptr_t &addrOffset,
                             uint8_t *addr) {
  UnownedReference *object = (UnownedReference*)(addr + addrOffset);
  swift_unknownObjectUnownedDestroy(object);
}

static void unknownWeakDestroyBranchless(const Metadata *metadata,
                             LayoutStringReader &reader,
                             uintptr_t &addrOffset,
                             uint8_t *addr) {
  swift_unknownObjectWeakDestroy((WeakReference *)(addr + addrOffset));
}

static void bridgeDestroyBranchless(const Metadata *metadata,
                             LayoutStringReader &reader,
                             uintptr_t &addrOffset,
                             uint8_t *addr) {
  swift_bridgeObjectRelease(*(void **)(addr + addrOffset));
}

template<typename ...Params>
static void singlePayloadEnumSimpleBranchless(const Metadata *metadata,
                             LayoutStringReader &reader,
                             uintptr_t &addrOffset,
                             uint8_t *addr,
                             Params... params) {
  reader.modify([&](LayoutStringReader &reader) {
    uint64_t byteCountsAndOffset;
    size_t payloadSize;
    uint64_t zeroTagValue;
    size_t xiTagValues;
    size_t refCountBytes;
    size_t skip;

    reader.readBytes(byteCountsAndOffset, payloadSize, zeroTagValue, xiTagValues, refCountBytes, skip);

    auto extraTagBytesPattern = (uint8_t)(byteCountsAndOffset >> 62);
    auto xiTagBytesPattern = ((uint8_t)(byteCountsAndOffset >> 59)) & 0x7;
    auto xiTagBytesOffset =
        byteCountsAndOffset & std::numeric_limits<uint32_t>::max();

    if (SWIFT_UNLIKELY(extraTagBytesPattern)) {
      auto extraTagBytes = 1 << (extraTagBytesPattern - 1);
      auto tagBytes =
          readTagBytes(addr + addrOffset + payloadSize, extraTagBytes);
      if (tagBytes) {
        xiTagBytesPattern = 0;
      }
    }

    if (SWIFT_LIKELY(xiTagBytesPattern)) {
      auto xiTagBytes = 1 << (xiTagBytesPattern - 1);
      uint64_t tagBytes =
          readTagBytes(addr + addrOffset + xiTagBytesOffset, xiTagBytes) -
          zeroTagValue;
      if (tagBytes >= xiTagValues) {
        return;
      }
    }

    reader.skip(refCountBytes);
    addrOffset += skip;
  });
}

typedef unsigned (*GetEnumTagFn)(const uint8_t *);

template<typename ...Params>
static void singlePayloadEnumFNBranchless(const Metadata *metadata,
                             LayoutStringReader &reader,
                             uintptr_t &addrOffset,
                             uint8_t *addr,
                             Params ...params) {
  reader.modify([&](LayoutStringReader &reader) {
    GetEnumTagFn getEnumTag = readRelativeFunctionPointer<GetEnumTagFn>(reader);

    unsigned enumTag = getEnumTag(addr + addrOffset);

    if (enumTag == 0) {
      reader.skip(sizeof(size_t) * 2);
    } else {
      size_t refCountBytes;
      size_t skip;
      reader.readBytes(refCountBytes, skip);
      reader.skip(refCountBytes);
      addrOffset += skip;
    }
  });
}

template<typename ...Params>
static void singlePayloadEnumFNResolvedBranchless(const Metadata *metadata,
                             LayoutStringReader &reader,
                             uintptr_t &addrOffset,
                             uint8_t *addr,
                             Params ...params) {
  reader.modify([&](LayoutStringReader &reader) {
    GetEnumTagFn getEnumTag;
    size_t refCountBytes;
    size_t skip;
    reader.readBytes(getEnumTag, refCountBytes, skip);

    unsigned enumTag = getEnumTag(addr + addrOffset);

    if (enumTag != 0) {
      reader.skip(refCountBytes);
      addrOffset += skip;
    }
  });
}

template<typename ...Params>
static void singlePayloadEnumGenericBranchless(const Metadata *metadata,
                             LayoutStringReader &reader,
                             uintptr_t &addrOffset,
                             uint8_t *addr,
                             Params ...params) {
  reader.modify([&](LayoutStringReader &reader) {
    auto tagBytesAndOffset = reader.readBytes<uint64_t>();
    auto payloadSize = reader.readBytes<size_t>();
    auto *xiType = reader.readBytes<const Metadata *>();
    auto numEmptyCases = reader.readBytes<unsigned>();
    auto refCountBytes = reader.readBytes<size_t>();
    auto skip = reader.readBytes<size_t>();

    auto extraTagBytesPattern = (uint8_t)(tagBytesAndOffset >> 62);
    auto xiTagBytesOffset =
        tagBytesAndOffset & std::numeric_limits<uint32_t>::max();

    if (extraTagBytesPattern) {
      auto extraTagBytes = 1 << (extraTagBytesPattern - 1);
      auto tagBytes = readTagBytes(addr + addrOffset + payloadSize, extraTagBytes);

      if (tagBytes) {
        xiType = nullptr;
      }
    }

    if (xiType) {
      auto tag = xiType->vw_getEnumTagSinglePayload(
          (const OpaqueValue *)(addr + addrOffset + xiTagBytesOffset),
          numEmptyCases);
      if (tag == 0) {
        return;
      }
    }

    reader.skip(refCountBytes);
    addrOffset += skip;
  });
}

template<auto HandlerFn, typename ...Params>
static void multiPayloadEnumFNBranchless(const Metadata *metadata,
                             LayoutStringReader &reader,
                             uintptr_t &addrOffset,
                             uint8_t *addr,
                             Params ...params) {
  reader.modify([&](LayoutStringReader &reader) {
    GetEnumTagFn getEnumTag = readRelativeFunctionPointer<GetEnumTagFn>(reader);

    size_t numPayloads;
    size_t refCountBytes;
    size_t enumSize;
    reader.readBytes(numPayloads, refCountBytes, enumSize);

    unsigned enumTag = getEnumTag(addr + addrOffset);

    if (enumTag < numPayloads) {
      size_t refCountOffset = reader.peekBytes<size_t>(enumTag * sizeof(size_t));

      LayoutStringReader nestedReader = reader;
      nestedReader.skip((numPayloads * sizeof(size_t)) + refCountOffset);
      uintptr_t nestedAddrOffset = addrOffset;
      HandlerFn(metadata, nestedReader, nestedAddrOffset, addr, std::forward<Params>(params)...);
    }

    reader.skip(refCountBytes + (numPayloads * sizeof(size_t)));
    addrOffset += enumSize;
  });
}

template<auto HandlerFn, typename ...Params>
static void multiPayloadEnumFNResolvedBranchless(const Metadata *metadata,
                             LayoutStringReader &reader,
                             uintptr_t &addrOffset,
                             uint8_t *addr,
                             Params ...params) {
  reader.modify([&](LayoutStringReader &reader) {
    GetEnumTagFn getEnumTag = reader.readBytes<GetEnumTagFn>();

    size_t numPayloads;
    size_t refCountBytes;
    size_t enumSize;
    reader.readBytes(numPayloads, refCountBytes, enumSize);

    unsigned enumTag = getEnumTag(addr + addrOffset);

    if (enumTag < numPayloads) {
      size_t refCountOffset = reader.peekBytes<size_t>(enumTag * sizeof(size_t));

      LayoutStringReader nestedReader = reader;
      nestedReader.skip((numPayloads * sizeof(size_t)) + refCountOffset);
      uintptr_t nestedAddrOffset = addrOffset;
      HandlerFn(metadata, nestedReader, nestedAddrOffset, addr, std::forward<Params>(params)...);
    }

    reader.skip(refCountBytes + (numPayloads * sizeof(size_t)));
    addrOffset += enumSize;
  });
}

template<auto HandlerFn, typename ...Params>
static void multiPayloadEnumGenericBranchless(const Metadata *metadata,
                             LayoutStringReader &reader,
                             uintptr_t &addrOffset,
                             uint8_t *addr,
                             Params ...params) {
  size_t tagBytes;
  size_t numPayloads;
  size_t refCountBytes;
  size_t enumSize;
  uint64_t enumTag;
  uintptr_t nestedAddrOffset;
  LayoutStringReader nestedReader;
  reader.modify([&](LayoutStringReader &reader) {
    reader.readBytes(tagBytes, numPayloads, refCountBytes, enumSize);

    nestedReader = reader;
    nestedAddrOffset = addrOffset;
    auto tagBytesOffset = enumSize - tagBytes;

    reader.skip(refCountBytes + (numPayloads * sizeof(size_t)));
    enumTag = readTagBytes(addr + addrOffset + tagBytesOffset, tagBytes);

    addrOffset += enumSize;
  });

  if (SWIFT_LIKELY(enumTag < numPayloads)) {
    size_t refCountOffset = nestedReader.peekBytes<size_t>(enumTag * sizeof(size_t));

    nestedReader.skip((numPayloads * sizeof(size_t)) + refCountOffset);

    HandlerFn(metadata, nestedReader, nestedAddrOffset, addr, std::forward<Params>(params)...);
  }
}

#if SWIFT_OBJC_INTEROP
static void blockDestroyBranchless(const Metadata *metadata,
                             LayoutStringReader &reader,
                             uintptr_t &addrOffset,
                             uint8_t *addr) {
  _Block_release((void *)(addr + addrOffset));
}

static void objcStrongDestroyBranchless(const Metadata *metadata,
                             LayoutStringReader &reader,
                             uintptr_t &addrOffset,
                             uint8_t *addr) {
  objc_object *object = (objc_object*)(*(uintptr_t *)(addr + addrOffset));
  objc_release(object);
}
#endif

static void metatypeDestroyBranchless(const Metadata *metadata,
                               LayoutStringReader &reader,
                               uintptr_t &addrOffset,
                               uint8_t *addr) {
  auto *type = reader.readBytes<const Metadata *>();
  type->vw_destroy((OpaqueValue *)(addr + addrOffset));
}

static void existentialDestroyBranchless(const Metadata *metadata,
                               LayoutStringReader &reader,
                               uintptr_t &addrOffset,
                               uint8_t *addr) {
  OpaqueValue *object = (OpaqueValue *)(addr + addrOffset);
  auto* type = getExistentialTypeMetadata(object);
  if (type->getValueWitnesses()->isValueInline()) {
    type->vw_destroy(object);
  } else {
    swift_release(*(HeapObject**)object);
  }
}

static void resilientDestroyBranchless(const Metadata *metadata,
                               LayoutStringReader &reader,
                               uintptr_t &addrOffset,
                               uint8_t *addr) {
  auto *type = getResilientTypeMetadata(metadata, reader);
  type->vw_destroy((OpaqueValue *)(addr + addrOffset));
}

typedef void (*DestrFnBranchless)(const Metadata *metadata,
                                  LayoutStringReader &reader,
                                  uintptr_t &addrOffset,
                                  uint8_t *addr);

const DestrFnBranchless destroyTableBranchless[] = {
  &handleEnd,
  &errorDestroyBranchless,
  &nativeStrongDestroyBranchless,
  &unownedDestroyBranchless,
  &weakDestroyBranchless,
  &unknownDestroyBranchless,
  &unknownUnownedDestroyBranchless,
  &unknownWeakDestroyBranchless,
  &bridgeDestroyBranchless,
#if SWIFT_OBJC_INTEROP
  &blockDestroyBranchless,
  &objcStrongDestroyBranchless,
#else
  nullptr,
  nullptr,
#endif
  nullptr, // Custom
  &metatypeDestroyBranchless,
  nullptr, // Generic
  &existentialDestroyBranchless,
  &resilientDestroyBranchless,
  &singlePayloadEnumSimpleBranchless<>,
  &singlePayloadEnumFNBranchless<>,
  &singlePayloadEnumFNResolvedBranchless<>,
  &singlePayloadEnumGenericBranchless<>,
  &multiPayloadEnumFNBranchless<handleRefCountsDestroy>,
  &multiPayloadEnumFNResolvedBranchless<handleRefCountsDestroy>,
  &multiPayloadEnumGenericBranchless<handleRefCountsDestroy>,
};

static void handleRefCountsDestroy(const Metadata *metadata,
                          LayoutStringReader &reader,
                          uintptr_t &addrOffset,
                          uint8_t *addr) {
  uint64_t tag = 0;
  do {
    tag = reader.readBytes<uint64_t>();
    addrOffset += (tag & ~(0xFFULL << 56));
    tag >>= 56;

    destroyTableBranchless[tag](metadata, reader, addrOffset, addr);
  } while (tag != 0);
}

extern "C" void
swift_generic_destroy(swift::OpaqueValue *address, const Metadata *metadata) {
  const uint8_t *layoutStr = metadata->getLayoutString();
  LayoutStringReader reader{layoutStr, layoutStringHeaderSize};
  uintptr_t addrOffset = 0;
  handleRefCountsDestroy(metadata, reader, addrOffset, (uint8_t *)address);
}

static void handleRefCountsInitWithCopy(const Metadata *metadata,
                          LayoutStringReader &reader,
                          uintptr_t &addrOffset,
                          uint8_t *dest,
                          uint8_t *src);

static void errorRetainBranchless(const Metadata *metadata,
                             LayoutStringReader &reader,
                             uintptr_t &addrOffset,
                             uint8_t *dest,
                             uint8_t *src) {
  swift_errorRetain(*(SwiftError**)(dest + addrOffset));
}

static void nativeStrongRetainBranchless(const Metadata *metadata,
                             LayoutStringReader &reader,
                             uintptr_t &addrOffset,
                             uint8_t *dest,
                             uint8_t *src) {
  HeapObject *object = (HeapObject*)((*(uintptr_t *)(dest + addrOffset)) & ~_swift_abi_SwiftSpareBitsMask);
  swift_retain(object);
}

static void unownedRetainBranchless(const Metadata *metadata,
                             LayoutStringReader &reader,
                             uintptr_t &addrOffset,
                             uint8_t *dest,
                             uint8_t *src) {
  HeapObject *object = (HeapObject*)((*(uintptr_t *)(dest + addrOffset)) & ~_swift_abi_SwiftSpareBitsMask);
  swift_unownedRetain(object);
}

static void weakCopyInitBranchless(const Metadata *metadata,
                             LayoutStringReader &reader,
                             uintptr_t &addrOffset,
                             uint8_t *dest,
                             uint8_t *src) {
  swift_weakCopyInit((WeakReference *)(dest + addrOffset), (WeakReference *)(src + addrOffset));
}

static void unknownRetainBranchless(const Metadata *metadata,
                             LayoutStringReader &reader,
                             uintptr_t &addrOffset,
                             uint8_t *dest,
                             uint8_t *src) {
  void *object = *(void**)(dest + addrOffset);
  swift_unknownObjectRetain(object);
}

static void unknownUnownedCopyInitBranchless(const Metadata *metadata,
                             LayoutStringReader &reader,
                             uintptr_t &addrOffset,
                             uint8_t *dest,
                             uint8_t *src) {
  UnownedReference *objectDest = (UnownedReference*)(dest + addrOffset);
  UnownedReference *objectSrc = (UnownedReference*)(src + addrOffset);
  swift_unknownObjectUnownedCopyInit(objectDest, objectSrc);
}

static void unknownWeakCopyInitBranchless(const Metadata *metadata,
                             LayoutStringReader &reader,
                             uintptr_t &addrOffset,
                             uint8_t *dest,
                             uint8_t *src) {
  swift_unknownObjectWeakCopyInit((WeakReference *)(dest + addrOffset),
                                  (WeakReference *)(src + addrOffset));
}

static void bridgeRetainBranchless(const Metadata *metadata,
                             LayoutStringReader &reader,
                             uintptr_t &addrOffset,
                             uint8_t *dest,
                             uint8_t *src) {
  swift_bridgeObjectRetain(*(void **)(dest + addrOffset));
}

#if SWIFT_OBJC_INTEROP
static void blockCopyBranchless(const Metadata *metadata,
                             LayoutStringReader &reader,
                             uintptr_t &addrOffset,
                             uint8_t *dest,
                             uint8_t *src) {
  *(void**)dest = _Block_copy(*(void**)(src + addrOffset));
}

static void objcStrongRetainBranchless(const Metadata *metadata,
                             LayoutStringReader &reader,
                             uintptr_t &addrOffset,
                             uint8_t *dest,
                             uint8_t *src) {
  objc_object *object = (objc_object*)(*(uintptr_t *)(src + addrOffset));
  objc_retain(object);
}
#endif

static void metatypeInitWithCopyBranchless(const Metadata *metadata,
                               LayoutStringReader &reader,
                               uintptr_t &addrOffset,
                               uint8_t *dest,
                               uint8_t *src) {
  auto *type = reader.readBytes<const Metadata *>();
  type->vw_initializeWithCopy((OpaqueValue *)(dest + addrOffset),
                              (OpaqueValue *)(src + addrOffset));
}

static void existentialInitWithCopyBranchless(const Metadata *metadata,
                               LayoutStringReader &reader,
                               uintptr_t &addrOffset,
                               uint8_t *dest,
                               uint8_t *src) {
  auto* type = getExistentialTypeMetadata((OpaqueValue*)(src + addrOffset));
  type->vw_initializeBufferWithCopyOfBuffer((ValueBuffer*)(dest + addrOffset),
                                            (ValueBuffer*)(src + addrOffset));
}

static void resilientInitWithCopyBranchless(const Metadata *metadata,
                               LayoutStringReader &reader,
                               uintptr_t &addrOffset,
                               uint8_t *dest,
                               uint8_t *src) {
  auto *type = getResilientTypeMetadata(metadata, reader);
  type->vw_initializeWithCopy((OpaqueValue *)(dest + addrOffset),
                              (OpaqueValue *)(src + addrOffset));
}

typedef void (*InitFn)(const Metadata *metadata,
                                 LayoutStringReader &reader,
                                 uintptr_t &addrOffset,
                                 uint8_t *dest,
                                 uint8_t *src);

static const InitFn initWithCopyTable[] = {
  &handleEnd,
  &errorRetainBranchless,
  &nativeStrongRetainBranchless,
  &unownedRetainBranchless,
  &weakCopyInitBranchless,
  &unknownRetainBranchless,
  &unknownUnownedCopyInitBranchless,
  &unknownWeakCopyInitBranchless,
  &bridgeRetainBranchless,
#if SWIFT_OBJC_INTEROP
  &blockCopyBranchless,
  &objcStrongRetainBranchless,
#else
  nullptr,
  nullptr,
#endif
  nullptr, // Custom
  &metatypeInitWithCopyBranchless,
  nullptr, // Generic
  &existentialInitWithCopyBranchless,
  &resilientInitWithCopyBranchless,
  &singlePayloadEnumSimpleBranchless,
  &singlePayloadEnumFNBranchless,
  &singlePayloadEnumFNResolvedBranchless,
  &singlePayloadEnumGenericBranchless,
  &multiPayloadEnumFNBranchless<handleRefCountsInitWithCopy>,
  &multiPayloadEnumFNResolvedBranchless<handleRefCountsInitWithCopy>,
  &multiPayloadEnumGenericBranchless<handleRefCountsInitWithCopy>,
};

static void handleRefCountsInitWithCopy(const Metadata *metadata,
                          LayoutStringReader &reader,
                          uintptr_t &addrOffset,
                          uint8_t *dest,
                          uint8_t *src) {
  uint64_t tag = 0;
  do {
    tag = reader.readBytes<uint64_t>();
    addrOffset += (tag & ~(0xFFULL << 56));
    tag >>= 56;

    initWithCopyTable[tag](metadata, reader, addrOffset, dest, src);
  } while (tag != 0);
}

extern "C" swift::OpaqueValue *
swift_generic_initWithCopy(swift::OpaqueValue *dest, swift::OpaqueValue *src,
                           const Metadata *metadata) {
  size_t size = metadata->vw_size();
  memcpy(dest, src, size);

  const uint8_t *layoutStr = metadata->getLayoutString();
  LayoutStringReader reader{layoutStr, layoutStringHeaderSize};
  uintptr_t addrOffset = 0;
  handleRefCountsInitWithCopy(metadata, reader, addrOffset, (uint8_t *)dest, (uint8_t *)src);

  return dest;
}

static void handleRefCountsInitWithTake(const Metadata *metadata,
                          LayoutStringReader &reader,
                          uintptr_t &addrOffset,
                          uint8_t *dest,
                          uint8_t *src);

static void unknownWeakInitWithTake(const Metadata *metadata,
                             LayoutStringReader &reader,
                             uintptr_t &addrOffset,
                             uint8_t *dest,
                             uint8_t *src) {
  swift_unknownObjectWeakTakeInit((WeakReference *)(dest + addrOffset),
                                  (WeakReference *)(src + addrOffset));
}

static void metatypeInitWithTake(const Metadata *metadata,
                          LayoutStringReader &reader,
                          uintptr_t &addrOffset,
                          uint8_t *dest,
                          uint8_t *src) {
  auto *type = reader.readBytes<const Metadata *>();
  if (SWIFT_UNLIKELY(!type->getValueWitnesses()->isBitwiseTakable())) {
    type->vw_initializeWithTake(
        (OpaqueValue *)(dest + addrOffset),
        (OpaqueValue *)(src + addrOffset));
  }
}

static void existentialInitWithTake(const Metadata *metadata,
                               LayoutStringReader &reader,
                               uintptr_t &addrOffset,
                               uint8_t *dest,
                               uint8_t *src) {
  auto* type = getExistentialTypeMetadata((OpaqueValue*)(src + addrOffset));
  if (SWIFT_UNLIKELY(!type->getValueWitnesses()->isBitwiseTakable())) {
    type->vw_initializeWithTake((OpaqueValue *)(dest + addrOffset),
                                (OpaqueValue *)(src + addrOffset));
  }
}

static void resilientInitWithTake(const Metadata *metadata,
                          LayoutStringReader &reader,
                          uintptr_t &addrOffset,
                          uint8_t *dest,
                          uint8_t *src) {
  auto *type = getResilientTypeMetadata(metadata, reader);
  if (SWIFT_UNLIKELY(!type->getValueWitnesses()->isBitwiseTakable())) {
    type->vw_initializeWithTake(
        (OpaqueValue *)(dest + addrOffset),
        (OpaqueValue *)(src + addrOffset));
  }
}

static const InitFn initWithTakeTable[] = {
  &handleEnd,
  nullptr,
  nullptr,
  nullptr,
  nullptr,
  nullptr,
  nullptr,
  &unknownWeakInitWithTake,
  &bridgeRetainBranchless,
  nullptr,
  nullptr,
  nullptr, // Custom
  &metatypeInitWithTake,
  nullptr, // Generic
  &existentialInitWithTake,
  &resilientInitWithTake,
  &singlePayloadEnumSimpleBranchless,
  &singlePayloadEnumFNBranchless,
  &singlePayloadEnumFNResolvedBranchless,
  &singlePayloadEnumGenericBranchless,
  &multiPayloadEnumFNBranchless<handleRefCountsInitWithTake>,
  &multiPayloadEnumFNResolvedBranchless<handleRefCountsInitWithTake>,
  &multiPayloadEnumGenericBranchless<handleRefCountsInitWithTake>,
};

static void handleRefCountsInitWithTake(const Metadata *metadata,
                          LayoutStringReader &reader,
                          uintptr_t &addrOffset,
                          uint8_t *dest,
                          uint8_t *src) {
  uint64_t tag = 0;
  do {
    tag = reader.readBytes<uint64_t>();
    addrOffset += (tag & ~(0xFFULL << 56));
    tag >>= 56;

    if (auto handler = initWithTakeTable[tag])
      handler(metadata, reader, addrOffset, dest, src);
  } while (tag != 0);
}

extern "C" swift::OpaqueValue *
swift_generic_initWithTake(swift::OpaqueValue *dest, swift::OpaqueValue *src,
                           const Metadata *metadata) {
  size_t size = metadata->vw_size();

  memcpy(dest, src, size);

  if (SWIFT_LIKELY(metadata->getValueWitnesses()->isBitwiseTakable())) {
    return dest;
  }

  const uint8_t *layoutStr = metadata->getLayoutString();
  LayoutStringReader reader{layoutStr, layoutStringHeaderSize};
  uintptr_t addrOffset = 0;

  handleRefCountsInitWithTake(metadata, reader, addrOffset, (uint8_t *)dest, (uint8_t *)src);

  return dest;
}

extern "C" swift::OpaqueValue *
swift_generic_assignWithCopy(swift::OpaqueValue *dest, swift::OpaqueValue *src,
                             const Metadata *metadata) {
  swift_generic_destroy(dest, metadata);
  return swift_generic_initWithCopy(dest, src, metadata);
}

extern "C" swift::OpaqueValue *
swift_generic_assignWithTake(swift::OpaqueValue *dest, swift::OpaqueValue *src,
                             const Metadata *metadata) {
  swift_generic_destroy(dest, metadata);
  return swift_generic_initWithTake(dest, src, metadata);
}

extern "C" unsigned swift_singletonEnum_getEnumTag(swift::OpaqueValue *address,
                                                   const Metadata *metadata) {
  return 0;
}

extern "C" void swift_singletonEnum_destructiveInjectEnumTag(
    swift::OpaqueValue *address, unsigned tag, const Metadata *metadata) {
  return;
}

template <typename T>
static inline T handleSinglePayloadEnumSimpleTag(
    LayoutStringReader &reader, uint8_t *addr,
    std::function<std::optional<T>(size_t, size_t, uint8_t)>
        extraTagBytesHandler,
    std::function<T(size_t, uint64_t, uint8_t, unsigned, size_t, uint8_t)>
        xiHandler) {
  auto byteCountsAndOffset = reader.readBytes<uint64_t>();
  auto extraTagBytesPattern = (uint8_t)(byteCountsAndOffset >> 62);
  auto xiTagBytesPattern = ((uint8_t)(byteCountsAndOffset >> 59)) & 0x7;
  auto xiTagBytesOffset =
      byteCountsAndOffset & std::numeric_limits<uint32_t>::max();
  auto numExtraTagBytes = 1 << (extraTagBytesPattern - 1);
  auto payloadSize = reader.readBytes<size_t>();
  auto zeroTagValue = reader.readBytes<uint64_t>();
  auto payloadNumExtraInhabitants = reader.readBytes<size_t>();

  if (extraTagBytesPattern) {
    if (auto result = extraTagBytesHandler(payloadNumExtraInhabitants,
                                           payloadSize, numExtraTagBytes)) {
      return *result;
    }
  }

  return xiHandler(payloadNumExtraInhabitants, zeroTagValue, xiTagBytesPattern,
                   xiTagBytesOffset, payloadSize, numExtraTagBytes);
}

extern "C" unsigned swift_enumSimple_getEnumTag(swift::OpaqueValue *address,
                                                const Metadata *metadata) {
  auto addr = reinterpret_cast<uint8_t *>(address);
  LayoutStringReader reader{metadata->getLayoutString(),
                            layoutStringHeaderSize + sizeof(uint64_t)};

  auto extraTagBytesHandler =
      [addr](size_t payloadNumExtraInhabitants, size_t payloadSize,
             uint8_t numExtraTagBytes) -> std::optional<unsigned> {
    auto tagBytes = readTagBytes(addr + payloadSize, numExtraTagBytes);
    if (tagBytes) {
      unsigned caseIndexFromExtraTagBits =
          payloadSize >= 4 ? 0 : (tagBytes - 1U) << (payloadSize * 8U);
      unsigned caseIndexFromValue = loadEnumElement(addr, payloadSize);
      unsigned noPayloadIndex =
          (caseIndexFromExtraTagBits | caseIndexFromValue) +
          payloadNumExtraInhabitants;
      return noPayloadIndex + 1;
    }

    return std::nullopt;
  };

  auto xihandler = [addr](size_t payloadNumExtraInhabitants,
                          uint64_t zeroTagValue, uint8_t xiTagBytesPattern,
                          unsigned xiTagBytesOffset, size_t payloadSize,
                          uint8_t numExtraTagBytes) -> unsigned {
    auto xiTagBytes = 1 << (xiTagBytesPattern - 1);
    uint64_t tagBytes =
        readTagBytes(addr + xiTagBytesOffset, xiTagBytes) -
        zeroTagValue;
    if (tagBytes < payloadNumExtraInhabitants) {
      return tagBytes + 1;
    }

    return 0;
  };

  return handleSinglePayloadEnumSimpleTag<unsigned>(
      reader, addr, extraTagBytesHandler, xihandler);
}

extern "C" void swift_enumSimple_destructiveInjectEnumTag(
    swift::OpaqueValue *address, unsigned tag, const Metadata *metadata) {
  auto addr = reinterpret_cast<uint8_t *>(address);
  LayoutStringReader reader{metadata->getLayoutString(),
                            layoutStringHeaderSize + sizeof(uint64_t)};

  auto extraTagBytesHandler =
      [addr, tag](size_t payloadNumExtraInhabitants, size_t payloadSize,
                  uint8_t numExtraTagBytes) -> std::optional<bool> {
    if (tag <= payloadNumExtraInhabitants) {
      return std::nullopt;
    }

    unsigned noPayloadIndex = tag - 1;
    unsigned caseIndex = noPayloadIndex - payloadNumExtraInhabitants;
    unsigned payloadIndex, extraTagIndex;
    if (payloadSize >= 4) {
      extraTagIndex = 1;
      payloadIndex = caseIndex;
    } else {
      unsigned payloadBits = payloadSize * 8U;
      extraTagIndex = 1U + (caseIndex >> payloadBits);
      payloadIndex = caseIndex & ((1U << payloadBits) - 1U);
    }

    // Store into the value.
    if (payloadSize)
      storeEnumElement(addr, payloadIndex, payloadSize);
    if (numExtraTagBytes)
      storeEnumElement(addr + payloadSize, extraTagIndex, numExtraTagBytes);

    return true;
  };

  auto xihandler = [addr, tag](size_t payloadNumExtraInhabitants,
                               uint64_t zeroTagValue, uint8_t xiTagBytesPattern,
                               unsigned xiTagBytesOffset, size_t payloadSize,
                               uint8_t numExtraTagBytes) -> bool {
    auto xiTagBytes = 1 << (xiTagBytesPattern - 1);
    if (tag <= payloadNumExtraInhabitants) {
      if (numExtraTagBytes != 0)
        storeEnumElement(addr + payloadSize, 0, numExtraTagBytes);

      if (tag == 0)
        return true;

      storeEnumElement(addr + xiTagBytesOffset, tag - 1 + zeroTagValue,
                       xiTagBytes);
    }
    return true;
  };

  handleSinglePayloadEnumSimpleTag<bool>(reader, addr, extraTagBytesHandler,
                                         xihandler);
}

extern "C"
unsigned swift_enumFn_getEnumTag(swift::OpaqueValue *address,
                                 const Metadata *metadata) {
  auto addr = reinterpret_cast<const uint8_t *>(address);
  LayoutStringReader reader{metadata->getLayoutString(),
                            layoutStringHeaderSize + sizeof(uint64_t)};
  auto getEnumTag = readRelativeFunctionPointer<GetEnumTagFn>(reader);

  return getEnumTag(addr);
}

extern "C" unsigned
swift_multiPayloadEnumGeneric_getEnumTag(swift::OpaqueValue *address,
                                         const Metadata *metadata) {
  auto addr = reinterpret_cast<const uint8_t *>(address);
  LayoutStringReader reader{metadata->getLayoutString(),
                            layoutStringHeaderSize + sizeof(uint64_t)};

  auto tagBytes = reader.readBytes<size_t>();
  auto numPayloads = reader.readBytes<size_t>();
  reader.skip(sizeof(size_t));
  auto enumSize = reader.readBytes<size_t>();
  auto payloadSize = enumSize - tagBytes;

  auto enumTag = (unsigned)readTagBytes(addr + payloadSize, tagBytes);
  if (enumTag < numPayloads) {
    return enumTag;
  }

  auto payloadValue = loadEnumElement(addr, payloadSize);

  if (payloadSize >= 4) {
    return numPayloads + payloadValue;
  } else {
    unsigned numPayloadBits = payloadSize * CHAR_BIT;
    return (payloadValue | (enumTag - numPayloads) << numPayloadBits) +
           numPayloads;
  }
}

extern "C" void swift_multiPayloadEnumGeneric_destructiveInjectEnumTag(
    swift::OpaqueValue *address, unsigned tag, const Metadata *metadata) {
  auto addr = reinterpret_cast<uint8_t *>(address);
  LayoutStringReader reader{metadata->getLayoutString(),
                            layoutStringHeaderSize + sizeof(uint64_t)};

  auto numTagBytes = reader.readBytes<size_t>();
  auto numPayloads = reader.readBytes<size_t>();
  reader.skip(sizeof(size_t));
  auto enumSize = reader.readBytes<size_t>();
  auto payloadSize = enumSize - numTagBytes;

  if (tag < numPayloads) {
    // For a payload case, store the tag after the payload area.
    auto tagBytes = addr + payloadSize;
    storeEnumElement(tagBytes, tag, numTagBytes);
  } else {
    // For an empty case, factor out the parts that go in the payload and
    // tag areas.
    unsigned whichEmptyCase = tag - numPayloads;
    unsigned whichTag, whichPayloadValue;
    if (payloadSize >= 4) {
      whichTag = numPayloads;
      whichPayloadValue = whichEmptyCase;
    } else {
      unsigned numPayloadBits = payloadSize * CHAR_BIT;
      whichTag = numPayloads + (whichEmptyCase >> numPayloadBits);
      whichPayloadValue = whichEmptyCase & ((1U << numPayloadBits) - 1U);
    }
    auto tagBytes = addr + payloadSize;
    storeEnumElement(tagBytes, whichTag, numTagBytes);
    storeEnumElement(addr, whichPayloadValue, payloadSize);
  }
}

template <typename T>
static inline T handleSinglePayloadEnumGenericTag(
    LayoutStringReader &reader, uint8_t *addr,
    std::function<std::optional<T>(const Metadata *, size_t, uint8_t)>
        extraTagBytesHandler,
    std::function<T(const Metadata *, unsigned, unsigned, size_t, uint8_t)>
        xiHandler) {
  auto tagBytesAndOffset = reader.readBytes<uint64_t>();
  auto extraTagBytesPattern = (uint8_t)(tagBytesAndOffset >> 62);
  auto xiTagBytesOffset =
      tagBytesAndOffset & std::numeric_limits<uint32_t>::max();
  auto numExtraTagBytes = 1 << (extraTagBytesPattern - 1);
  auto payloadSize = reader.readBytes<size_t>();
  auto xiType = reader.readBytes<const Metadata *>();

  if (extraTagBytesPattern) {
    if (auto result =
            extraTagBytesHandler(xiType, payloadSize, numExtraTagBytes)) {
      return *result;
    }
  }

  auto numEmptyCases = reader.readBytes<unsigned>();

  return xiHandler(xiType, xiTagBytesOffset, numEmptyCases, payloadSize,
                   numExtraTagBytes);
}

extern "C" unsigned
swift_singlePayloadEnumGeneric_getEnumTag(swift::OpaqueValue *address,
                                          const Metadata *metadata) {
  auto addr = reinterpret_cast<uint8_t *>(address);
  LayoutStringReader reader{metadata->getLayoutString(),
                            layoutStringHeaderSize + sizeof(uint64_t)};

  auto extraTagBytesHandler =
      [addr](const Metadata *xiType, size_t payloadSize,
             uint8_t numExtraTagBytes) -> std::optional<unsigned> {
    auto tagBytes = readTagBytes(addr + payloadSize, numExtraTagBytes);
    if (tagBytes) {
      unsigned payloadNumExtraInhabitants =
          xiType ? xiType->vw_getNumExtraInhabitants() : 0;
      unsigned caseIndexFromExtraTagBits =
          payloadSize >= 4 ? 0 : (tagBytes - 1U) << (payloadSize * 8U);
      unsigned caseIndexFromValue = loadEnumElement(addr, payloadSize);
      unsigned noPayloadIndex =
          (caseIndexFromExtraTagBits | caseIndexFromValue) +
          payloadNumExtraInhabitants;
      return noPayloadIndex + 1;
    }

    return std::nullopt;
  };

  auto xihandler = [addr](const Metadata *xiType, unsigned xiTagBytesOffset,
                          unsigned numEmptyCases, size_t payloadSize,
                          uint8_t numExtraTagBytes) -> unsigned {
    if (xiType) {
      return xiType->vw_getEnumTagSinglePayload(
          (const OpaqueValue *)(addr + xiTagBytesOffset), numEmptyCases);
    }

    return 0;
  };

  return handleSinglePayloadEnumGenericTag<unsigned>(
      reader, addr, extraTagBytesHandler, xihandler);
}

extern "C" void swift_singlePayloadEnumGeneric_destructiveInjectEnumTag(
    swift::OpaqueValue *address, unsigned tag, const Metadata *metadata) {
  auto addr = reinterpret_cast<uint8_t *>(address);
  LayoutStringReader reader{metadata->getLayoutString(),
                            layoutStringHeaderSize + sizeof(uint64_t)};

  auto extraTagBytesHandler =
      [=](const Metadata *xiType, size_t payloadSize,
          uint8_t numExtraTagBytes) -> std::optional<bool> {
    unsigned payloadNumExtraInhabitants =
        xiType ? xiType->vw_getNumExtraInhabitants() : 0;
    if (tag <= payloadNumExtraInhabitants) {
      return std::nullopt;
    }

    unsigned noPayloadIndex = tag - 1;
    unsigned caseIndex = noPayloadIndex - payloadNumExtraInhabitants;
    unsigned payloadIndex, extraTagIndex;
    if (payloadSize >= 4) {
      extraTagIndex = 1;
      payloadIndex = caseIndex;
    } else {
      unsigned payloadBits = payloadSize * 8U;
      extraTagIndex = 1U + (caseIndex >> payloadBits);
      payloadIndex = caseIndex & ((1U << payloadBits) - 1U);
    }

    // Store into the value.
    if (payloadSize)
      storeEnumElement(addr, payloadIndex, payloadSize);
    if (numExtraTagBytes)
      storeEnumElement(addr + payloadSize, extraTagIndex, numExtraTagBytes);

    return true;
  };

  auto xihandler = [=](const Metadata *xiType, unsigned xiTagBytesOffset,
                       unsigned numEmptyCases, size_t payloadSize,
                       uint8_t numExtraTagBytes) -> bool {
    unsigned payloadNumExtraInhabitants =
        xiType ? xiType->vw_getNumExtraInhabitants() : 0;
    if (tag <= payloadNumExtraInhabitants) {
      if (numExtraTagBytes != 0)
        storeEnumElement(addr + payloadSize, 0, numExtraTagBytes);

      if (tag == 0)
        return true;

      xiType->vw_storeEnumTagSinglePayload(
          (swift::OpaqueValue *)(addr + xiTagBytesOffset), tag, numEmptyCases);
    }
    return true;
  };

  handleSinglePayloadEnumGenericTag<bool>(reader, addr, extraTagBytesHandler,
                                          xihandler);
}

extern "C" swift::OpaqueValue *
swift_generic_initializeBufferWithCopyOfBuffer(swift::ValueBuffer *dest,
                                               swift::ValueBuffer *src,
                                               const Metadata *metadata) {
  if (metadata->getValueWitnesses()->isValueInline()) {
    return swift_generic_initWithCopy((swift::OpaqueValue *)dest,
                                      (swift::OpaqueValue *)src, metadata);
  } else {
    memcpy(dest, src, sizeof(swift::HeapObject *));
    swift_retain(*(swift::HeapObject **)src);
    return (swift::OpaqueValue *)&(*(swift::HeapObject **)dest)[1];
  }
}

void swift::swift_resolve_resilientAccessors(uint8_t *layoutStr,
                                             size_t layoutStrOffset,
                                             const uint8_t *fieldLayoutStr,
                                             const Metadata *fieldType) {
  LayoutStringWriter writer{layoutStr, layoutStrOffset};
  LayoutStringReader reader{fieldLayoutStr, 0};
  while (true) {
    size_t currentOffset = reader.offset + layoutStringHeaderSize;
    uint64_t size = reader.readBytes<uint64_t>();
    RefCountingKind tag = (RefCountingKind)(size >> 56);
    size &= ~(0xffULL << 56);

    switch (tag) {
    case RefCountingKind::End:
      return;
    case RefCountingKind::Resilient: {
      auto *type = getResilientTypeMetadata(fieldType, reader);
      writer.offset = layoutStrOffset + currentOffset - layoutStringHeaderSize;
      uint64_t tagAndOffset =
          (((uint64_t)RefCountingKind::Metatype) << 56) | size;
      writer.writeBytes(tagAndOffset);
      writer.writeBytes(type);
      break;
    }
    case RefCountingKind::Metatype:
      reader.skip(sizeof(uintptr_t));
      break;
    case RefCountingKind::SinglePayloadEnumSimple:
      reader.skip((2 * sizeof(uint64_t)) + (4 * sizeof(size_t)));
      break;

    case RefCountingKind::SinglePayloadEnumFN: {
      auto getEnumTag = readRelativeFunctionPointer<GetEnumTagFn>(reader);
      writer.offset = layoutStrOffset + currentOffset - layoutStringHeaderSize;
      uint64_t tagAndOffset =
          (((uint64_t)RefCountingKind::SinglePayloadEnumFNResolved) << 56) |
          size;
      writer.writeBytes(tagAndOffset);
      writer.writeBytes(getEnumTag);
      reader.skip(2 * sizeof(size_t));
      break;
    }

    case RefCountingKind::SinglePayloadEnumFNResolved:
      reader.skip(3 * sizeof(size_t));
      break;

    case RefCountingKind::SinglePayloadEnumGeneric: {
      reader.skip(sizeof(uint64_t) +  // tag + offset
                  sizeof(uint64_t) +  // extra tag bytes + XI offset
                  sizeof(size_t) +    // payload size
                  sizeof(uintptr_t) + // XI metadata
                  sizeof(unsigned));  // num empty cases
      auto refCountBytes = reader.readBytes<size_t>();
      reader.skip(sizeof(size_t) + // bytes to skip if no payload case
                  refCountBytes);
      break;
    }

    case RefCountingKind::MultiPayloadEnumFN: {
      auto getEnumTag = readRelativeFunctionPointer<GetEnumTagFn>(reader);
      writer.offset = layoutStrOffset + currentOffset - layoutStringHeaderSize;
      uint64_t tagAndOffset =
          (((uint64_t)RefCountingKind::MultiPayloadEnumFNResolved) << 56) |
          size;
      writer.writeBytes(tagAndOffset);
      writer.writeBytes(getEnumTag);

      size_t numCases = reader.readBytes<size_t>();
      auto refCountBytes = reader.readBytes<size_t>();

      // skip enum size
      reader.skip(sizeof(size_t));

      size_t casesBeginOffset = layoutStrOffset + reader.offset +
                                (numCases * sizeof(size_t));

      auto fieldCasesBeginOffset = fieldLayoutStr + (numCases * sizeof(size_t)) + reader.offset;
      for (size_t j = 0; j < numCases; j++) {
        size_t caseOffset = reader.readBytes<size_t>();
        const uint8_t *caseLayoutString = fieldCasesBeginOffset +
                                          caseOffset;
        swift_resolve_resilientAccessors(layoutStr,
                                         casesBeginOffset + caseOffset,
                                         caseLayoutString, fieldType);
      }
      reader.skip(refCountBytes);
      break;
    }

    case RefCountingKind::MultiPayloadEnumFNResolved: {
      // skip function pointer
      reader.skip(sizeof(uintptr_t));
      size_t numCases = reader.readBytes<size_t>();
      size_t refCountBytes = reader.readBytes<size_t>();
      // skip enum size, offsets and ref counts
      reader.skip(sizeof(size_t) + (numCases * sizeof(size_t)) + refCountBytes);
      break;
    }

    case RefCountingKind::MultiPayloadEnumGeneric: {
      reader.skip(sizeof(size_t));
      auto numPayloads = reader.readBytes<size_t>();
      auto refCountBytes = reader.readBytes<size_t>();
      reader.skip(sizeof(size_t) * (numPayloads + 1) + refCountBytes);
      break;
    }

    default:
      break;
    }
  }
}

extern "C"
void swift_generic_instantiateLayoutString(const uint8_t* layoutStr,
                                           Metadata* type) {
  type->setLayoutString(layoutStr);
}
