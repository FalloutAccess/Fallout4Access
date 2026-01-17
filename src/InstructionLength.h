#pragma once

#include <cstddef>
#include <cstdint>
#include <Zydis/Zydis.h>

// Utility for calculating x86-64 instruction boundaries using Zydis disassembler.
// Used by function hooks to determine how many bytes to copy to a trampoline.
namespace InstructionLength
{
    // Calculate the minimum number of bytes to copy from a function entry point
    // such that we end on an instruction boundary and have at least minBytes.
    // Returns 0 on error (decoding failure).
    inline std::size_t CalculateMinimumCopySize(const void* functionAddr, std::size_t minBytes)
    {
        if (!functionAddr || minBytes == 0) {
            return 0;
        }

        ZydisDecoder decoder;
        if (!ZYAN_SUCCESS(ZydisDecoderInit(&decoder, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64))) {
            return 0;
        }

        const auto* data = static_cast<const std::uint8_t*>(functionAddr);
        std::size_t offset = 0;
        constexpr std::size_t maxBytes = 64;  // Don't decode more than this

        while (offset < minBytes && offset < maxBytes) {
            ZydisDecodedInstruction instruction;
            ZydisDecodedOperand operands[ZYDIS_MAX_OPERAND_COUNT];

            ZyanStatus status = ZydisDecoderDecodeFull(
                &decoder,
                data + offset,
                maxBytes - offset,
                &instruction,
                operands
            );

            if (!ZYAN_SUCCESS(status)) {
                // Decoding failed - return 0 to indicate error
                return 0;
            }

            offset += instruction.length;
        }

        return offset;
    }

    // Convenience overload that takes a uintptr_t address
    inline std::size_t CalculateMinimumCopySize(std::uintptr_t functionAddr, std::size_t minBytes)
    {
        return CalculateMinimumCopySize(reinterpret_cast<const void*>(functionAddr), minBytes);
    }
}
