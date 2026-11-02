// Forced include for building VSTGUI (Steinberg VST3 SDK) with MinGW-w64. The SDK stays unchanged.
// - std::wstring_convert lives in <codecvt>/<locale> (MSVC includes them implicitly)
// - IDCompositionVisual3 is missing in the MinGW DirectComposition header (only used by VSTGUI for the
//   opacity of animated layers, which Tiracon does not use). Declared like MinGW declares the other
//   DirectComposition interfaces: overloads in MSVC vtable order (animation variant first).
#pragma once
#ifdef __cplusplus
#include <codecvt>
#include <locale>

#if defined(__MINGW32__) && defined(VSTGUI_MINGW_DCOMP3)
#include <windows.h>
#include <dcomp.h>
#ifndef __IDCompositionVisual3_INTERFACE_DEFINED__
#define __IDCompositionVisual3_INTERFACE_DEFINED__
struct IDCompositionTransform3D;
#undef INTERFACE
#define INTERFACE IDCompositionVisual3
DECLARE_INTERFACE_IID_(IDCompositionVisual3, IDCompositionVisualDebug, "2775F462-B6C1-4015-B0BE-B3E7D6A4976D")
{
    STDMETHOD(SetDepthMode)(THIS_ int mode) PURE;
    STDMETHOD(SetOffsetZ)(THIS_ IDCompositionAnimation*) PURE;
    STDMETHOD(SetOffsetZ)(THIS_ float) PURE;
    STDMETHOD(SetOpacity)(THIS_ IDCompositionAnimation*) PURE;
    STDMETHOD(SetOpacity)(THIS_ float) PURE;
    STDMETHOD(SetTransform)(THIS_ IDCompositionTransform3D*) PURE;
    STDMETHOD(SetTransform)(THIS_ const void* matrix4x4) PURE;
    STDMETHOD(SetVisible)(THIS_ BOOL) PURE;
};
#undef INTERFACE
__CRT_UUID_DECL(IDCompositionVisual3, 0x2775f462, 0xb6c1, 0x4015, 0xb0, 0xbe, 0xb3, 0xe7, 0xd6, 0xa4, 0x97, 0x6d);
#endif
#endif
#endif
