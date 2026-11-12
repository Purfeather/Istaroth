#pragma once
#include <assert.h>
// Retain assertion checks and expression diagnostics without embedding build paths.
#define IM_ASSERT(expression) ((expression) ? (void)0 : _wassert(_CRT_WIDE(#expression), L"ImGui", (unsigned)__LINE__))
