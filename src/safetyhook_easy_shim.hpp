//
// Created by Seamus on 3/11/2026.
//
// safetyhook 的 easy API（create_inline）定义在 easy.cpp 中，而 easy.cpp 依赖
// MidHook（需要 MASM 汇编，MinGW 无法编译）。本项目只用 InlineHook，因此这里提供
// create_inline 的内联实现，绕过 easy.cpp / mid_hook.cpp。
#pragma once

#include "SafetyHook.hpp"

namespace safetyhook {

// 注：默认参数已在 safetyhook/easy.hpp 的声明中给出，此处不再重复
[[nodiscard]] inline InlineHook create_inline(void* target, void* destination, InlineHook::Flags flags) {
    if (auto hook = InlineHook::create(target, destination, flags)) {
        return std::move(*hook);
    }
    return {};
}

} // namespace safetyhook
