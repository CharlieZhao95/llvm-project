//===- DXContainer.h - DXContainer file implementation ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the DXContainerFile class, which implements the ObjectFile
// interface for DXContainer files.
//
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_DXCONTAINER_H
#define LLVM_OBJECT_DXCONTAINER_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/DXContainer.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBufferRef.h"
#include "llvm/TargetParser/Triple.h"

namespace llvm {
namespace object {

namespace DirectX {
class PSVRuntimeInfo {
  StringRef Data;
  uint32_t Size;
  using InfoStruct =
      std::variant<std::monostate, dxbc::PSV::v0::RuntimeInfo,
                   dxbc::PSV::v1::RuntimeInfo, dxbc::PSV::v2::RuntimeInfo>;
  InfoStruct BasicInfo;

public:
  PSVRuntimeInfo(StringRef D) : Data(D), Size(0) {}

  // Parsing depends on the shader kind
  Error parse(uint16_t ShaderKind);

  uint32_t getSize() const { return Size; }
  uint32_t getVersion() const {
    return Size >= sizeof(dxbc::PSV::v2::RuntimeInfo)
               ? 2
               : (Size >= sizeof(dxbc::PSV::v1::RuntimeInfo) ? 1 : 0);
  }

  const InfoStruct &getInfo() const { return BasicInfo; }
};

} // namespace DirectX

class DXContainer {
public:
  using DXILData = std::pair<dxbc::ProgramHeader, const char *>;

private:
  DXContainer(MemoryBufferRef O);

  MemoryBufferRef Data;
  dxbc::Header Header;
  SmallVector<uint32_t, 4> PartOffsets;
  std::optional<DXILData> DXIL;
  std::optional<uint64_t> ShaderFlags;
  std::optional<dxbc::ShaderHash> Hash;
  std::optional<DirectX::PSVRuntimeInfo> PSVInfo;

  Error parseHeader();
  Error parsePartOffsets();
  Error parseDXILHeader(StringRef Part);
  Error parseShaderFlags(StringRef Part);
  Error parseHash(StringRef Part);
  Error parsePSVInfo(StringRef Part);
  friend class PartIterator;

public:
  // The PartIterator is a wrapper around the iterator for the PartOffsets
  // member of the DXContainer. It contains a refernce to the container, and the
  // current iterator value, as well as storage for a parsed part header.
  class PartIterator {
    const DXContainer &Container;
    SmallVectorImpl<uint32_t>::const_iterator OffsetIt;
    struct PartData {
      dxbc::PartHeader Part;
      uint32_t Offset;
      StringRef Data;
    } IteratorState;

    friend class DXContainer;

    PartIterator(const DXContainer &C,
                 SmallVectorImpl<uint32_t>::const_iterator It)
        : Container(C), OffsetIt(It) {
      if (OffsetIt == Container.PartOffsets.end())
        updateIteratorImpl(Container.PartOffsets.back());
      else
        updateIterator();
    }

    // Updates the iterator's state data. This results in copying the part
    // header into the iterator and handling any required byte swapping. This is
    // called when incrementing or decrementing the iterator.
    void updateIterator() {
      if (OffsetIt != Container.PartOffsets.end())
        updateIteratorImpl(*OffsetIt);
    }

    // Implementation for updating the iterator state based on a specified
    // offest.
    void updateIteratorImpl(const uint32_t Offset);

  public:
    PartIterator &operator++() {
      if (OffsetIt == Container.PartOffsets.end())
        return *this;
      ++OffsetIt;
      updateIterator();
      return *this;
    }

    PartIterator operator++(int) {
      PartIterator Tmp = *this;
      ++(*this);
      return Tmp;
    }

    bool operator==(const PartIterator &RHS) const {
      return OffsetIt == RHS.OffsetIt;
    }

    bool operator!=(const PartIterator &RHS) const {
      return OffsetIt != RHS.OffsetIt;
    }

    const PartData &operator*() { return IteratorState; }
    const PartData *operator->() { return &IteratorState; }
  };

  PartIterator begin() const {
    return PartIterator(*this, PartOffsets.begin());
  }

  PartIterator end() const { return PartIterator(*this, PartOffsets.end()); }

  StringRef getData() const { return Data.getBuffer(); }
  static Expected<DXContainer> create(MemoryBufferRef Object);

  const dxbc::Header &getHeader() const { return Header; }

  const std::optional<DXILData> &getDXIL() const { return DXIL; }

  std::optional<uint64_t> getShaderFlags() const { return ShaderFlags; }

  std::optional<dxbc::ShaderHash> getShaderHash() const { return Hash; }

  const std::optional<DirectX::PSVRuntimeInfo> &getPSVInfo() const {
    return PSVInfo;
  };
};

} // namespace object
} // namespace llvm

#endif // LLVM_OBJECT_DXCONTAINER_H
