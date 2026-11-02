// Colours and fonts of the editor (dark like the WebApp, more compact)
#pragma once
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cdrawcontext.h"
#include <string>

namespace tiracon {
namespace theme {

using VSTGUI::CColor;
inline CColor rgb(int r, int g, int b, int a = 255) { return CColor(uint8_t(r), uint8_t(g), uint8_t(b), uint8_t(a)); }

const CColor bg = rgb(0x1b, 0x1d, 0x20);
const CColor panel = rgb(0x25, 0x28, 0x2c);
const CColor border = rgb(0x3a, 0x3e, 0x44);
const CColor text = rgb(0xd0, 0xd4, 0xd8);
const CColor dim = rgb(0x80, 0x87, 0x8f);
const CColor accent = rgb(0xf0, 0xa0, 0x30);
const CColor display = rgb(0x0b, 0x0b, 0x0c);
const CColor segOn = rgb(0xff, 0x3b, 0x2f);
const CColor segOff = rgb(0x1c, 0x0a, 0x09);         // unlit segment: just visible on the black display
const CColor keyFace = rgb(0xdc, 0xd8, 0xcf);
const CColor keyDown = rgb(0xb0, 0xab, 0x9f);
const CColor keyDead = rgb(0x6c, 0x6a, 0x66);
const CColor keyText = rgb(0x26, 0x26, 0x26);
const CColor bar = rgb(0xe8, 0x9a, 0x2c);
const CColor barBg = rgb(0x14, 0x16, 0x18);
const CColor paper = rgb(0xf4, 0xf2, 0xec);
const CColor ink = rgb(0x18, 0x18, 0x18);
const CColor inkDim = rgb(0x70, 0x70, 0x70);
const CColor newRed = rgb(0xc0, 0x20, 0x20);

inline VSTGUI::SharedPointer<VSTGUI::CFontDesc> font(double size, bool bold = false) {
#ifdef _WIN32
  const char* family = "Arial";
#else
  const char* family = "Liberation Sans";
#endif
  return VSTGUI::makeOwned<VSTGUI::CFontDesc>(family, size, bold ? VSTGUI::kBoldFace : VSTGUI::kNormalFace);
}

inline void label(VSTGUI::CDrawContext* c, const std::string& s, const VSTGUI::CRect& r, const CColor& col, double size,
                 bool bold = false, VSTGUI::CHoriTxtAlign align = VSTGUI::kLeftText) {
  c->setFont(font(size, bold));
  c->setFontColor(col);
  c->drawString(s.c_str(), r, align, true);
}

} // namespace theme
} // namespace tiracon
