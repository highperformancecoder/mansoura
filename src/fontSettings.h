/*
  Font settings shared by all backends, whether they lay text out
  with Pango (MansouraCairo) or by other means (MansouraHTMLCanvas).
  Set by Minsky::defaultFont() and Minsky::fontScale().

  @copyright Ravelation Pty Ltd 2026

  Mansoura is free software: you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
*/
#ifndef MANSOURA_FONT_SETTINGS_H
#define MANSOURA_FONT_SETTINGS_H

namespace mansoura
{
  struct GlobalFontSettings
  {
    /// font family used when text doesn't specify one, or null for the backend's default
    inline static const char* defaultFamily=nullptr;
    /// an additional font size scale factor to help rendering on Windows high DPI displays.
    /// Currently only applied by the Pango (cairo) backend.
    inline static double scaleFactor=1;
  };
}

#endif
