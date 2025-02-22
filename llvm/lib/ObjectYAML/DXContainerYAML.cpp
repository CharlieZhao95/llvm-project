//===- DXContainerYAML.cpp - DXContainer YAMLIO implementation ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines classes for handling the YAML representation of
// DXContainerYAML.
//
//===----------------------------------------------------------------------===//

#include "llvm/ObjectYAML/DXContainerYAML.h"
#include "llvm/BinaryFormat/DXContainer.h"

namespace llvm {

// This assert is duplicated here to leave a breadcrumb of the places that need
// to be updated if flags grow past 64-bits.
static_assert((uint64_t)dxbc::FeatureFlags::NextUnusedBit <= 1ull << 63,
              "Shader flag bits exceed enum size.");

DXContainerYAML::ShaderFlags::ShaderFlags(uint64_t FlagData) {
#define SHADER_FLAG(Num, Val, Str)                                             \
  Val = (FlagData & (uint64_t)dxbc::FeatureFlags::Val) > 0;
#include "llvm/BinaryFormat/DXContainerConstants.def"
}

uint64_t DXContainerYAML::ShaderFlags::getEncodedFlags() {
  uint64_t Flag = 0;
#define SHADER_FLAG(Num, Val, Str)                                             \
  if (Val)                                                                     \
    Flag |= (uint64_t)dxbc::FeatureFlags::Val;
#include "llvm/BinaryFormat/DXContainerConstants.def"
  return Flag;
}

DXContainerYAML::ShaderHash::ShaderHash(const dxbc::ShaderHash &Data)
    : IncludesSource((Data.Flags & static_cast<uint32_t>(
                                       dxbc::HashFlags::IncludesSource)) != 0),
      Digest(16, 0) {
  memcpy(Digest.data(), &Data.Digest[0], 16);
}

DXContainerYAML::PSVInfo::PSVInfo() : Version(0) {
  memset(&Info, 0, sizeof(Info));
}

DXContainerYAML::PSVInfo::PSVInfo(const dxbc::PSV::v0::RuntimeInfo *P,
                                  uint16_t Stage)
    : Version(0) {
  memset(&Info, 0, sizeof(Info));
  memcpy(&Info, P, sizeof(dxbc::PSV::v0::RuntimeInfo));

  assert(Stage < std::numeric_limits<uint8_t>::max() &&
         "Stage should be a very small number");
  // We need to bring the stage in separately since it isn't part of the v1 data
  // structure.
  Info.ShaderStage = static_cast<uint8_t>(Stage);
}

DXContainerYAML::PSVInfo::PSVInfo(const dxbc::PSV::v1::RuntimeInfo *P)
    : Version(1) {
  memset(&Info, 0, sizeof(Info));
  memcpy(&Info, P, sizeof(dxbc::PSV::v1::RuntimeInfo));
}

DXContainerYAML::PSVInfo::PSVInfo(const dxbc::PSV::v2::RuntimeInfo *P)
    : Version(2) {
  memset(&Info, 0, sizeof(Info));
  memcpy(&Info, P, sizeof(dxbc::PSV::v2::RuntimeInfo));
}

namespace yaml {

void MappingTraits<DXContainerYAML::VersionTuple>::mapping(
    IO &IO, DXContainerYAML::VersionTuple &Version) {
  IO.mapRequired("Major", Version.Major);
  IO.mapRequired("Minor", Version.Minor);
}

void MappingTraits<DXContainerYAML::FileHeader>::mapping(
    IO &IO, DXContainerYAML::FileHeader &Header) {
  IO.mapRequired("Hash", Header.Hash);
  IO.mapRequired("Version", Header.Version);
  IO.mapOptional("FileSize", Header.FileSize);
  IO.mapRequired("PartCount", Header.PartCount);
  IO.mapOptional("PartOffsets", Header.PartOffsets);
}

void MappingTraits<DXContainerYAML::DXILProgram>::mapping(
    IO &IO, DXContainerYAML::DXILProgram &Program) {
  IO.mapRequired("MajorVersion", Program.MajorVersion);
  IO.mapRequired("MinorVersion", Program.MinorVersion);
  IO.mapRequired("ShaderKind", Program.ShaderKind);
  IO.mapOptional("Size", Program.Size);
  IO.mapRequired("DXILMajorVersion", Program.DXILMajorVersion);
  IO.mapRequired("DXILMinorVersion", Program.DXILMinorVersion);
  IO.mapOptional("DXILSize", Program.DXILSize);
  IO.mapOptional("DXIL", Program.DXIL);
}

void MappingTraits<DXContainerYAML::ShaderFlags>::mapping(
    IO &IO, DXContainerYAML::ShaderFlags &Flags) {
#define SHADER_FLAG(Num, Val, Str) IO.mapRequired(#Val, Flags.Val);
#include "llvm/BinaryFormat/DXContainerConstants.def"
}

void MappingTraits<DXContainerYAML::ShaderHash>::mapping(
    IO &IO, DXContainerYAML::ShaderHash &Hash) {
  IO.mapRequired("IncludesSource", Hash.IncludesSource);
  IO.mapRequired("Digest", Hash.Digest);
}

void MappingTraits<DXContainerYAML::PSVInfo>::mapping(
    IO &IO, DXContainerYAML::PSVInfo &PSV) {
  IO.mapRequired("Version", PSV.Version);

  // Shader stage is only included in binaries for v1 and later, but we always
  // include it since it simplifies parsing and file construction.
  IO.mapRequired("ShaderStage", PSV.Info.ShaderStage);
  PSV.mapInfoForVersion(IO);
}

void MappingTraits<DXContainerYAML::Part>::mapping(IO &IO,
                                                   DXContainerYAML::Part &P) {
  IO.mapRequired("Name", P.Name);
  IO.mapRequired("Size", P.Size);
  IO.mapOptional("Program", P.Program);
  IO.mapOptional("Flags", P.Flags);
  IO.mapOptional("Hash", P.Hash);
  IO.mapOptional("PSVInfo", P.Info);
}

void MappingTraits<DXContainerYAML::Object>::mapping(
    IO &IO, DXContainerYAML::Object &Obj) {
  IO.mapTag("!dxcontainer", true);
  IO.mapRequired("Header", Obj.Header);
  IO.mapRequired("Parts", Obj.Parts);
}

} // namespace yaml

void DXContainerYAML::PSVInfo::mapInfoForVersion(yaml::IO &IO) {
  dxbc::PipelinePSVInfo &StageInfo = Info.StageInfo;
  Triple::EnvironmentType Stage = dxbc::getShaderStage(Info.ShaderStage);

  switch (Stage) {
  case Triple::EnvironmentType::Pixel:
    IO.mapRequired("DepthOutput", StageInfo.PS.DepthOutput);
    IO.mapRequired("SampleFrequency", StageInfo.PS.SampleFrequency);
    break;
  case Triple::EnvironmentType::Vertex:
    IO.mapRequired("OutputPositionPresent", StageInfo.VS.OutputPositionPresent);
    break;
  case Triple::EnvironmentType::Geometry:
    IO.mapRequired("InputPrimitive", StageInfo.GS.InputPrimitive);
    IO.mapRequired("OutputTopology", StageInfo.GS.OutputTopology);
    IO.mapRequired("OutputStreamMask", StageInfo.GS.OutputStreamMask);
    IO.mapRequired("OutputPositionPresent", StageInfo.GS.OutputPositionPresent);
    break;
  case Triple::EnvironmentType::Hull:
    IO.mapRequired("InputControlPointCount",
                   StageInfo.HS.InputControlPointCount);
    IO.mapRequired("OutputControlPointCount",
                   StageInfo.HS.OutputControlPointCount);
    IO.mapRequired("TessellatorDomain", StageInfo.HS.TessellatorDomain);
    IO.mapRequired("TessellatorOutputPrimitive",
                   StageInfo.HS.TessellatorOutputPrimitive);
    break;
  case Triple::EnvironmentType::Domain:
    IO.mapRequired("InputControlPointCount",
                   StageInfo.DS.InputControlPointCount);
    IO.mapRequired("OutputPositionPresent", StageInfo.DS.OutputPositionPresent);
    IO.mapRequired("TessellatorDomain", StageInfo.DS.TessellatorDomain);
    break;
  case Triple::EnvironmentType::Mesh:
    IO.mapRequired("GroupSharedBytesUsed", StageInfo.MS.GroupSharedBytesUsed);
    IO.mapRequired("GroupSharedBytesDependentOnViewID",
                   StageInfo.MS.GroupSharedBytesDependentOnViewID);
    IO.mapRequired("PayloadSizeInBytes", StageInfo.MS.PayloadSizeInBytes);
    IO.mapRequired("MaxOutputVertices", StageInfo.MS.MaxOutputVertices);
    IO.mapRequired("MaxOutputPrimitives", StageInfo.MS.MaxOutputPrimitives);
    break;
  case Triple::EnvironmentType::Amplification:
    IO.mapRequired("PayloadSizeInBytes", StageInfo.AS.PayloadSizeInBytes);
    break;
  default:
    break;
  }

  IO.mapRequired("MinimumWaveLaneCount", Info.MinimumWaveLaneCount);
  IO.mapRequired("MaximumWaveLaneCount", Info.MaximumWaveLaneCount);

  if (Version == 0)
    return;

  IO.mapRequired("UsesViewID", Info.UsesViewID);

  switch (Stage) {
  case Triple::EnvironmentType::Geometry:
    IO.mapRequired("MaxVertexCount", Info.GeomData.MaxVertexCount);
    break;
  case Triple::EnvironmentType::Hull:
  case Triple::EnvironmentType::Domain:
    IO.mapRequired("SigPatchConstOrPrimVectors",
                   Info.GeomData.SigPatchConstOrPrimVectors);
    break;
  case Triple::EnvironmentType::Mesh:
    IO.mapRequired("SigPrimVectors", Info.GeomData.MeshInfo.SigPrimVectors);
    IO.mapRequired("MeshOutputTopology",
                   Info.GeomData.MeshInfo.MeshOutputTopology);
    break;
  default:
    break;
  }

  IO.mapRequired("SigInputElements", Info.SigInputElements);
  IO.mapRequired("SigOutputElements", Info.SigOutputElements);
  IO.mapRequired("SigPatchConstOrPrimElements",
                 Info.SigPatchConstOrPrimElements);
  IO.mapRequired("SigInputVectors", Info.SigInputVectors);
  MutableArrayRef<uint8_t> Vec(Info.SigOutputVectors);
  IO.mapRequired("SigOutputVectors", Vec);

  if (Version == 1)
    return;

  IO.mapRequired("NumThreadsX", Info.NumThreadsX);
  IO.mapRequired("NumThreadsY", Info.NumThreadsY);
  IO.mapRequired("NumThreadsZ", Info.NumThreadsZ);
}

} // namespace llvm
