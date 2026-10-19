// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "shader_recompiler/backend/spirv/emit_spirv_instructions.h"
#include "shader_recompiler/backend/spirv/spirv_emit_context.h"
#include <cstdlib>

namespace {
// bbport: BB_UNDEF_ZERO=1 emits undefined values as zero (diagnostic: a GCN register read before
// any write holds a leftover value, SPIR-V's OpUndef lets the driver pick anything).
bool UndefZero() {
    static const bool value = [] {
        const char* env = std::getenv("BB_UNDEF_ZERO");
        return env && *env == '1';
    }();
    return value;
}
} // Anonymous namespace


namespace Shader::Backend::SPIRV {

Id EmitUndefU1(EmitContext& ctx) {
    return ctx.OpUndef(ctx.U1[1]);
}

Id EmitUndefU8(EmitContext&) {
    UNREACHABLE_MSG("SPIR-V Instruction");
}

Id EmitUndefU16(EmitContext&) {
    UNREACHABLE_MSG("SPIR-V Instruction");
}

Id EmitUndefU32(EmitContext& ctx) {
    return UndefZero() ? ctx.ConstantNull(ctx.U32[1]) : ctx.OpUndef(ctx.U32[1]);
}

Id EmitUndefU32x2(EmitContext& ctx) {
    return UndefZero() ? ctx.ConstantNull(ctx.U32[2]) : ctx.OpUndef(ctx.U32[2]);
}

Id EmitUndefU32x3(EmitContext& ctx) {
    return UndefZero() ? ctx.ConstantNull(ctx.U32[3]) : ctx.OpUndef(ctx.U32[3]);
}

Id EmitUndefU32x4(EmitContext& ctx) {
    return UndefZero() ? ctx.ConstantNull(ctx.U32[4]) : ctx.OpUndef(ctx.U32[4]);
}

Id EmitUndefF32(EmitContext& ctx) {
    return UndefZero() ? ctx.ConstantNull(ctx.F32[1]) : ctx.OpUndef(ctx.F32[1]);
}

Id EmitUndefU64(EmitContext&) {
    UNREACHABLE_MSG("SPIR-V Instruction");
}

} // namespace Shader::Backend::SPIRV
