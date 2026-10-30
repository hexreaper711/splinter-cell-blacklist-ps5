/**
 * @file        codegen/phase_discover.cpp
 * @brief       Discover phase: iterative function block discovery
 *
 * @copyright   Copyright (c) 2026 Tom Clay <tomc@tctechstuff.com>
 *              All rights reserved.
 *
 * @license     BSD 3-Clause License
 *              See LICENSE file in the project root for full license text.
 */

#include "codegen_flags.h"
#include "decoded_binary.h"
#include "ppc/instruction.h"
#include <rex/codegen/function_scanner.h>

#include <array>
#include <bitset>
#include <unordered_set>

#include <rex/codegen/phases.h>
#include "phase_helpers.h"

#include <rex/codegen/vtable_scanner.h>
#include <rex/logging.h>

#include "codegen_logging.h"
#include <rex/memory/utils.h>

#include <ppc.h>

using rex::codegen::ppc::Opcode;
using rex::memory::load_and_swap;

namespace rex::codegen {

namespace {

//=============================================================================
// Discover Phase: iterative function block discovery
//=============================================================================

void discoverFunction(CodegenContext& ctx, uint32_t funcAddr,
                      const std::unordered_set<uint32_t>& knownFunctions) {
  auto& graph = ctx.graph;
  auto& binary = ctx.binary();
  auto& decoded = ctx.decoded();

  auto* node = graph.getFunction(funcAddr);
  if (!node)
    return;

  // Skip if already discovered
  if (!node->canDiscover()) {
    REXCODEGEN_TRACE("Analyze: function 0x{:08X} already discovered, skipping", funcAddr);
    return;
  }

  // Imports don't need block discovery
  if (node->isImport()) {
    node->discoverAsImport();
    return;
  }

  REXCODEGEN_TRACE("Analyze: discovering function 0x{:08X} ({})", funcAddr, node->name());

  // Lookup pdataSize for exception handler boundary
  uint32_t pdataSize = 0;

  // For CONFIG functions: use only the explicitly declared size (if any)
  // If no size specified (size=0), let discovery find natural boundaries via region
  // Don't inherit PDATA sizes for CONFIG functions - they're user hints for entry points
  if (node->authority() == FunctionAuthority::CONFIG) {
    pdataSize = node->size();  // 0 if not specified, which is correct
    REXCODEGEN_TRACE("Analyze: 0x{:08X} is CONFIG, using declared size={}", funcAddr, pdataSize);
  } else {
    // For non-CONFIG functions, use PDATA size if available
    auto pdataIt = ctx.scan.pdataSizes.find(funcAddr);
    if (pdataIt != ctx.scan.pdataSizes.end()) {
      pdataSize = pdataIt->second;
      REXCODEGEN_TRACE("Analyze: 0x{:08X} using PDATA size={}", funcAddr, pdataSize);
    }
  }

  // Find the code region containing this function
  const CodeRegion* region = nullptr;
  for (const auto& r : ctx.scan.codeRegions) {
    if (r.contains(funcAddr)) {
      region = &r;
      break;
    }
  }
  if (!region) {
    REXCODEGEN_WARN("Analyze: function 0x{:08X} not in any code region", funcAddr);
    return;
  }

  // Pass pdataSize so forward branches within function extent are correctly identified
  auto result = discoverBlocks(decoded, funcAddr, *region, knownFunctions, pdataSize,
                               &ctx.Config().switchTables);

  if (result.blocks.empty()) {
    REXCODEGEN_WARN("Analyze: no blocks found for function 0x{:08X}", funcAddr);
    return;
  }

  // snooper the function with the discovered blocks and instructions
  node->discover(std::move(result.blocks), std::move(result.instructions),
                 std::move(result.labels));

  // Add jump tables (targets become labels in the function)
  for (const auto& jt : result.jumpTables) {
    graph.addJumpTableToFunction(funcAddr, jt);
  }

  // Register external call targets as new functions (bl only, not b)
  for (uint32_t target : result.externalCalls) {
    if (!graph.isEntryPoint(target) && !graph.isImport(target)) {
      if (binary.isInImportExportRange(target)) {
        continue;
      }
      graph.addFunction(target, 4, FunctionAuthority::DISCOVERED, true);
    }
  }

  // Add unresolved branches for later resolution
  for (const auto& branch : result.unresolvedBranches) {
    graph.addUnresolvedJumpToFunction(funcAddr, branch.site, branch.target, branch.isCall,
                                      branch.isConditional);
  }

  // Scan exception handler regions for branches not in discovered blocks
  if (pdataSize > 0) {
    std::unordered_set<uint32_t> discoveredAddrs;
    for (const auto& block : result.blocks) {
      for (uint32_t addr = block.base; addr < block.base + block.size; addr += 4) {
        discoveredAddrs.insert(addr);
      }
    }

    uint32_t pdataEnd = funcAddr + pdataSize;
    const uint8_t* funcData = binary.translate(funcAddr);
    if (funcData) {
      for (uint32_t offset = 0; offset < pdataSize; offset += 4) {
        uint32_t site = funcAddr + offset;

        // Skip if already discovered by normal control flow
        if (discoveredAddrs.count(site))
          continue;

        // Skip if marked invalid
        auto invalidIt = ctx.analysisState().invalidInstructions.find(site);
        if (invalidIt != ctx.analysisState().invalidInstructions.end()) {
          continue;
        }

        uint32_t insn = load_and_swap<uint32_t>(funcData + offset);
        uint32_t opcode = PPC_OP(insn);

        if (opcode != PPC_OP_B && opcode != PPC_OP_BC)
          continue;

        uint32_t target = 0;
        bool isCall = PPC_BL(insn);
        bool isAbsolute = PPC_BA(insn);

        if (opcode == PPC_OP_B) {
          int32_t branchOffset = PPC_BI(insn);
          target = isAbsolute ? static_cast<uint32_t>(branchOffset) : site + branchOffset;
        } else {
          int32_t branchOffset = PPC_BD(insn);
          target = isAbsolute ? static_cast<uint32_t>(branchOffset) : site + branchOffset;
        }

        // Skip internal jumps within pdata region
        if (!isCall && target >= funcAddr && target < pdataEnd) {
          continue;
        }

        graph.addUnresolvedJumpToFunction(funcAddr, site, target, isCall, false);

        // Register call targets as new functions
        if (isCall && !graph.isEntryPoint(target) && !graph.isImport(target)) {
          if (binary.isInImportExportRange(target)) {
            continue;
          }
          graph.addFunction(target, 4, FunctionAuthority::DISCOVERED, true);
        }
      }
    }
  }
}

void discoverAllFunctions(CodegenContext& ctx) {
  REXCODEGEN_TRACE("Analyze: starting iterative discovery...");

  auto& graph = ctx.graph;
  auto& binary = ctx.binary();

  // Iterative discovery
  size_t iteration = 0;
  size_t lastFunctionCount = 0;
  const size_t maxIterations = REXCVAR_GET(max_discovery_iterations);

  while (iteration < maxIterations) {
    iteration++;

    size_t currentFunctionCount = graph.functionCount();
    if (currentFunctionCount == lastFunctionCount && iteration > 1) {
      REXCODEGEN_DEBUG("Analyze: fixed point at iteration {} ({} functions)", iteration,
                       currentFunctionCount);
      break;
    }

    lastFunctionCount = currentFunctionCount;

    auto knownFunctions = buildKnownFunctions(graph);
    if (discoverPendingFunctions(ctx, knownFunctions) == 0) {
      break;
    }
  }

  REXCODEGEN_TRACE("Analyze: {} functions after call graph expansion", graph.functionCount());

  // VTable scanning
  {
    VTableScanner vtScanner(binary);
    auto vtables = vtScanner.scan();

    size_t newFunctions = 0;

    for (const auto& vt : vtables) {
      for (size_t i = 0; i < vt.slots.size(); i++) {
        uint32_t funcAddr = vt.slots[i];

        if (graph.isEntryPoint(funcAddr))
          continue;
        if (binary.isInImportExportRange(funcAddr))
          continue;

        graph.addFunction(funcAddr, 4, FunctionAuthority::VTABLE, true);
        newFunctions++;
      }
    }

    REXCODEGEN_TRACE("Analyze: VTable scan found {} vtables, {} new functions", vtables.size(),
                     newFunctions);

    // Code addresses that nothing else found as functions: stored in the image's data (vtables of
    // a title built without RTTI, which the scan above needs Complete Object Locators for;
    // callback tables), or callbacks handed to a function: lis + addi into an argument register
    // (r3-r10) and a call before that register changes (Naruto: Rise of a Ninja registers
    // hundreds of script functions so, each a two-instruction stub). Each one missed was a "call
    // to invalid or unregistered function" at run time. An address counts only where a routine
    // ends right before it (blr, bctr or a tail call) and no discovered block covers it, so jump
    // table targets and other labels inside a function stay out. (Any lis + addi, not only an
    // argument to a call, took labels by the hundred: thousands of unresolved branches.)
    size_t dataPointerFunctions = 0;
    size_t callbackFunctions = 0;
    std::string pointerList;
    // (true when it adds a function)
    auto considerAddress = [&](uint32_t target, uint32_t from, bool fromCode) {
      if ((target & 3) || !binary.isExecutable(target) || !binary.isExecutable(target - 4) ||
          graph.isEntryPoint(target) || binary.isInImportExportRange(target)) {
        return false;
      }
      const uint8_t* previous_data = binary.translate(target - 4);
      const uint8_t* target_data = binary.translate(target);
      if (!previous_data || !target_data) {
        return false;
      }
      const uint32_t previous_raw = load_and_swap<uint32_t>(previous_data);
      const uint32_t target_raw = load_and_swap<uint32_t>(target_data);
      const auto previous = ppc::decode_instruction(target - 4, previous_raw);
      // (a b only as a tail call to another function: a b to a label ends a switch case or an
      // if-branch, and jump tables point right after those. Tail call targets aren't all
      // registered yet here, so a b farther than 64 KB counts too: labels are near. For a pointer
      // in data, so does a b backwards: a function may end with a branch back into a loop of its
      // own, while a b forward over the address leaves it inside the function, a switch case.
      // Any b does for an address the code hands over: runs of short stubs each tail-call a
      // near function not registered yet.)
      const auto tail_call = [&](uint32_t branch_target) {
        const uint32_t distance = branch_target > target ? branch_target - target
                                                         : target - branch_target;
        return graph.isEntryPoint(branch_target) || distance > 0x10000;
      };
      const bool routine_ended =
          previous.is_return() || previous_raw == 0x4E800420 /* bctr */ ||
          (previous.opcode == Opcode::b && previous.branch_target.has_value() &&
           (fromCode || *previous.branch_target < target || tail_call(*previous.branch_target)));
      if (!routine_ended || target_raw == 0 || target_raw == 0xFFFFFFFF ||
          ppc::decode_instruction(target, target_raw).opcode == Opcode::kUnknown) {
        return false;
      }
      if (const FunctionNode* containing = graph.getFunctionContaining(target)) {
        for (const auto& block : containing->blocks()) {
          if (block.contains(target)) {
            return false;
          }
        }
      }
      graph.addFunction(target, 4, FunctionAuthority::VTABLE, true);
      ++newFunctions;
      return true;
    };
    auto considerPointer = [&](uint32_t target, uint32_t from, bool fromCode) {
      if (considerAddress(target, from, fromCode)) {
        pointerList += fmt::format(" {:08X}{}{:08X}", target, fromCode ? '@' : '<', from);
        ++(fromCode ? callbackFunctions : dataPointerFunctions);
      }
    };
    // Rounds until nothing new: the found functions' tail calls make new entry points, which the
    // "a routine ends right before" test of the next callback in a run of short stubs needs.
    for (int round = 0; round < 32; ++round) {
      const size_t found_before = dataPointerFunctions + callbackFunctions;
      for (const auto& section : binary.sections()) {
        if (!section.data) {
          continue;
        }
        if (!section.executable) {
          for (uint32_t offset = 0; offset + 4 <= section.size; offset += 4) {
            considerPointer(load_and_swap<uint32_t>(section.data + offset),
                            section.baseAddress + offset, false);
          }
          continue;
        }
        // lis rX,hi; addi/ori rA,rX,lo: a code address in rA, handed over while rA still holds it:
        // in an argument register (r3-r10) when a call (bl, bctrl) comes, or stored to memory (a
        // table of callbacks the game passes on). Within 32 instructions of the lis.
        uint32_t high[32] = {};
        uint32_t high_at[32] = {};
        uint32_t address[32] = {};
        uint32_t address_at[32] = {};
        uint32_t address_from[32] = {};
        for (uint32_t offset = 0; offset + 4 <= section.size; offset += 4) {
          const uint32_t raw = load_and_swap<uint32_t>(section.data + offset);
          const uint32_t index = offset / 4 + 1;
          const uint32_t primary = raw >> 26;
          const uint32_t rd = (raw >> 21) & 31, ra = (raw >> 16) & 31, imm = raw & 0xFFFF;
          const uint32_t extended = (raw >> 1) & 0x3FF;
          const bool is_call = (primary == 18 && (raw & 1)) || raw == 0x4E800421 /* bctrl */;
          if (is_call) {
            for (uint32_t r = 3; r <= 10; ++r) {
              if (address_at[r] && index - address_at[r] <= 32) {
                considerPointer(address[r], address_from[r], true);
              }
            }
            // (volatile registers don't survive the call)
            for (uint32_t r = 0; r <= 12; ++r) {
              address_at[r] = 0;
              high_at[r] = 0;
            }
            continue;
          }
          // stw, stwu (rS in the rd field), stwx, stwux
          const bool is_word_store =
              primary == 36 || primary == 37 ||
              (primary == 31 && (extended == 151 || extended == 183));
          if (is_word_store) {
            if (address_at[rd] && index - address_at[rd] <= 32) {
              considerPointer(address[rd], address_from[rd], true);
            }
            continue;
          }
          if (primary == 15 && ra == 0) {  // lis
            high[rd] = imm << 16;
            high_at[rd] = index;
            address_at[rd] = 0;
          } else if (primary == 14 && ra != 0 && high_at[ra] && index - high_at[ra] <= 32) {
            address[rd] = high[ra] + uint32_t(int32_t(int16_t(imm)));  // addi
            address_at[rd] = index;
            address_from[rd] = section.baseAddress + offset;
            if (rd != ra) {
              high_at[rd] = 0;
            }
          } else if (primary == 24 && high_at[rd] && index - high_at[rd] <= 32) {
            address[ra] = high[rd] | imm;  // ori rA,rS,lo
            address_at[ra] = index;
            address_from[ra] = section.baseAddress + offset;
            if (ra != rd) {
              high_at[ra] = 0;
            }
          } else {
            // Anything else that may write rd ends what it held (other stores, compares and
            // branches don't write it).
            const bool writes_rd = !(primary >= 36 && primary <= 47) &&
                                   !(primary >= 52 && primary <= 55) && primary != 10 &&
                                   primary != 11 && !(primary >= 16 && primary <= 19);
            if (writes_rd) {
              address_at[rd] = 0;
              high_at[rd] = 0;
            }
          }
        }
      }
      if (dataPointerFunctions + callbackFunctions == found_before) {
        break;
      }
      auto knownFunctions = buildKnownFunctions(graph);
      while (discoverPendingFunctions(ctx, knownFunctions) > 0) {
        knownFunctions = buildKnownFunctions(graph);
      }
    }
    // Virtual-call stubs (lwz rA,0(r3); lwz rB,N(rA); mtctr rB; bctr: "this->vtable[N](...)")
    // that nothing in the image points to: the game makes their addresses at run time (Splinter
    // Cell: Blacklist called 0x8313A9D8, a stub between two that were found, in its story). The
    // four instructions mean nothing else, so each run of them after the end of a routine is one.
    size_t stubFunctions = 0;
    for (const auto& section : binary.sections()) {
      if (!section.data || !section.executable) {
        continue;
      }
      for (uint32_t offset = 0; offset + 16 <= section.size; offset += 4) {
        const uint8_t* p = section.data + offset;
        const uint32_t w0 = load_and_swap<uint32_t>(p);
        const uint32_t w1 = load_and_swap<uint32_t>(p + 4);
        const uint32_t w2 = load_and_swap<uint32_t>(p + 8);
        const uint32_t w3 = load_and_swap<uint32_t>(p + 12);
        const uint32_t ra = (w0 >> 21) & 31, rb = (w1 >> 21) & 31;
        if ((w0 & 0xFC1FFFFF) != 0x80030000 /* lwz rA,0(r3) */ ||
            (w1 >> 26) != 32 /* lwz rB,N(rA) */ || ((w1 >> 16) & 31) != ra ||
            w2 != (0x7C0903A6 | (rb << 21)) /* mtctr rB */ || w3 != 0x4E800420 /* bctr */) {
          continue;
        }
        if (considerAddress(section.baseAddress + offset, section.baseAddress + offset, true)) {
          ++stubFunctions;
        }
      }
    }
    if (stubFunctions > 0) {
      auto knownFunctions = buildKnownFunctions(graph);
      while (discoverPendingFunctions(ctx, knownFunctions) > 0) {
        knownFunctions = buildKnownFunctions(graph);
      }
    }
    REXCODEGEN_INFO(
        "Analyze: {} functions found only through pointers in data, {} as callbacks handed over in "
        "code, {} virtual-call stubs nothing points to (function<pointer, function@instruction):{}",
        dataPointerFunctions, callbackFunctions, stubFunctions, pointerList);

    // Continue discovery for vtable functions
    if (newFunctions > 0) {
      size_t vtableIteration = 0;
      const size_t maxVtableIterations = REXCVAR_GET(max_vtable_iterations);

      while (vtableIteration < maxVtableIterations) {
        vtableIteration++;

        auto knownFunctions = buildKnownFunctions(graph);
        if (discoverPendingFunctions(ctx, knownFunctions) == 0)
          break;

        if (graph.functionCount() == lastFunctionCount)
          break;
        lastFunctionCount = graph.functionCount();
      }
    }
  }

  REXCODEGEN_TRACE("Analyze: {} total functions after vtable scan", graph.functionCount());
}

//=============================================================================
// Function Pointer Scan: find lis/addi pairs loading code addresses
// TODO(tomc): THIS IS WIP AND PROB A BAD IDEA LOL LETS SEE
//=============================================================================
void functionPointerScan(CodegenContext& ctx) {
  if (!ctx.hasDecoded()) {
    REXCODEGEN_WARN("functionPointerScan: DecodedBinary not initialized, skipping");
    return;
  }

  auto& graph = ctx.graph;
  auto& decoded = ctx.decoded();
  const auto& codeRegions = decoded.codeRegions();

  if (codeRegions.empty()) {
    REXCODEGEN_WARN("functionPointerScan: no code regions, skipping");
    return;
  }

  // Build set of existing functions to avoid duplicates
  std::unordered_set<uint32_t> existingFunctions;
  for (const auto& [addr, node] : graph.functions()) {
    existingFunctions.insert(addr);
  }

  // Track lis values: register -> (high_value, lis_address)
  // We scan linearly and track the most recent lis for each register
  // PPC has exactly 32 GPRs, so a fixed-size array is more efficient than a map
  std::array<std::pair<uint32_t, uint32_t>, 32> lisValues{};
  std::bitset<32> lisValid;

  size_t foundCount = 0;

  for (const auto& region : codeRegions) {
    lisValid.reset();  // Reset tracking at region boundaries

    for (uint32_t addr = region.start; addr < region.end; addr += 4) {
      auto* insn = decoded.get(addr);
      if (!insn)
        continue;

      // Track lis rD, IMM
      if (isLis(*insn)) {
        uint8_t rd = static_cast<uint8_t>(insn->D.RT);
        uint32_t hi = static_cast<uint32_t>(static_cast<int16_t>(insn->D.d)) << 16;
        lisValues[rd] = {hi, addr};
        lisValid.set(rd);
        continue;
      }

      // Check for addi rD, rA, IMM where rA was set by lis
      if (insn->opcode == rex::codegen::ppc::Opcode::addi) {
        uint8_t ra = static_cast<uint8_t>(insn->D.RA);
        if (ra == 0)
          continue;  // li pseudo-op, not addi

        if (!lisValid.test(ra))
          continue;

        uint32_t hi = lisValues[ra].first;
        int16_t lo = static_cast<int16_t>(insn->D.d);
        uint32_t fullAddr = hi + lo;  // Sign-extended add

        // PPC instructions are 4-byte aligned
        if (fullAddr & 0x3)
          continue;

        // Check if this address is in a code region
        const CodeRegion* targetRegion = decoded.regionContaining(fullAddr);
        if (!targetRegion)
          continue;

        // Skip if already a known function
        if (existingFunctions.contains(fullAddr))
          continue;

        // Skip if it's an internal address (within same function's likely range)
        // Heuristic: if target is very close to current address, probably internal label
        int32_t distance = static_cast<int32_t>(fullAddr) - static_cast<int32_t>(addr);
        if (distance > -0x1000 && distance < 0x1000) {
          // Could be local label, skip for now
          continue;
        }

        // Register as function with DISCOVERED authority and hasXrefs=true
        graph.addFunction(fullAddr, 4, FunctionAuthority::DISCOVERED, true);
        existingFunctions.insert(fullAddr);
        foundCount++;

        REXCODEGEN_TRACE("functionPointerScan: found 0x{:08X} via lis/addi at 0x{:08X}", fullAddr,
                         addr);
      }

      // Also check ori rD, rA, IMM (alternative to addi for unsigned)
      if (insn->opcode == rex::codegen::ppc::Opcode::ori) {
        uint8_t ra = static_cast<uint8_t>(insn->D.RA);
        if (!lisValid.test(ra))
          continue;

        uint32_t hi = lisValues[ra].first;
        uint16_t lo = static_cast<uint16_t>(insn->D.d);
        uint32_t fullAddr = hi | lo;  // Unsigned OR

        // PPC instructions are 4-byte aligned
        if (fullAddr & 0x3)
          continue;

        const CodeRegion* targetRegion = decoded.regionContaining(fullAddr);
        if (!targetRegion)
          continue;

        if (existingFunctions.contains(fullAddr))
          continue;

        int32_t distance = static_cast<int32_t>(fullAddr) - static_cast<int32_t>(addr);
        if (distance > -0x1000 && distance < 0x1000)
          continue;

        graph.addFunction(fullAddr, 4, FunctionAuthority::DISCOVERED, true);
        existingFunctions.insert(fullAddr);
        foundCount++;

        REXCODEGEN_TRACE("functionPointerScan: found 0x{:08X} via lis/ori at 0x{:08X}", fullAddr,
                         addr);
      }

      // Clear lis tracking if register is overwritten by other instruction
      // (Simplified: we clear on any write to the register)
      // This is conservative - could miss some patterns but avoids false positives
    }
  }

  REXCODEGEN_TRACE("functionPointerScan: found {} new function pointer targets", foundCount);
}

}  // anonymous namespace

/// Discover blocks for all pending functions (shared helper, declared in phase_helpers.h).
size_t discoverPendingFunctions(CodegenContext& ctx,
                                const std::unordered_set<uint32_t>& knownFunctions) {
  std::vector<uint32_t> pending;
  for (const auto& [addr, node] : ctx.graph.functions()) {
    if (node->canDiscover()) {
      pending.push_back(addr);
    }
  }
  for (uint32_t funcAddr : pending) {
    discoverFunction(ctx, funcAddr, knownFunctions);
  }
  return pending.size();
}

namespace phases {

VoidResult Discover(CodegenContext& ctx, ProgressReporter* reporter) {
  (void)reporter;
  discoverAllFunctions(ctx);
  return Ok();
}

}  // namespace phases

}  // namespace rex::codegen
