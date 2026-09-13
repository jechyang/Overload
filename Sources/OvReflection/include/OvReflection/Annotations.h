#pragma once

// This declaration and explicit friendship have identical meaning in both modes.
namespace OvReflection { template<class T> struct Access; }

#if defined(OV_REFLECTION_ANALYSIS)
#if !defined(__clang__)
#error OV_REFLECTION_ANALYSIS requires Clang
#endif
#define OV_REFLECT_ANNOTATE(kind, ...) __attribute__((annotate("ov." kind ":" #__VA_ARGS__)))
#else
#define OV_REFLECT_ANNOTATE(kind, ...)
#endif

#define OV_TYPE(...) OV_REFLECT_ANNOTATE("type", __VA_ARGS__)
#define OV_FIELD(...) OV_REFLECT_ANNOTATE("field", __VA_ARGS__)
#define OV_PROPERTY(...) OV_REFLECT_ANNOTATE("property", __VA_ARGS__)
#define OV_METHOD(...) OV_REFLECT_ANNOTATE("method", __VA_ARGS__)
