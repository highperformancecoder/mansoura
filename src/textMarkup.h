/*
  Parser for the subset of Pango markup that Minsky generates (see
  latexToPango), reduced to a sequence of uniformly styled runs that
  a backend without Pango (eg HTML5 canvas) can lay out. Free of any
  emscripten dependency so it can be unit tested natively.

  @copyright Ravelation Pty Ltd 2026

  Mansoura is free software: you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
*/
#ifndef MANSOURA_TEXT_MARKUP_H
#define MANSOURA_TEXT_MARKUP_H

#include <string>
#include <vector>

namespace mansoura
{
  /// a run of text sharing one style
  struct TextRun
  {
    std::string text;            ///< UTF-8, entities already decoded
    bool bold=false, italic=false;
    bool underline=false, strikethrough=false;
    double sizeScale=1;          ///< relative to the base font size
    double absoluteSizePt=0;     ///< if nonzero, overrides sizeScale
    double riseEm=0;             ///< baseline shift (up positive), in units of the base font size
    double risePt=0;             ///< additional baseline shift in points
    std::string family;          ///< empty means inherit the base family
    std::string colour;          ///< CSS colour, empty means inherit
  };

  /// Parse Pango markup. Lenient: unknown tags are ignored, a stray
  /// '<' or '&' is treated as literal text, unclosed tags are closed
  /// at the end of the string.
  std::vector<TextRun> parseMarkup(const std::string& markup);

  /// a single run containing \a text verbatim
  std::vector<TextRun> plainRuns(const std::string& text);

  /// concatenation of the text of all \a runs (the text Pango
  /// reports indices into)
  std::string plainText(const std::vector<TextRun>& runs);

  /// Convert UTF-8 to a wide string of Unicode code points (wchar_t
  /// must be 32 bit, as with emscripten). Invalid sequences are
  /// replaced by U+FFFD rather than throwing.
  std::wstring utf8ToWString(const std::string& utf8);

  /// number of bytes in the UTF-8 character starting at \a s[i]
  /// (clamped to the string length; 1 for an invalid lead byte)
  std::string::size_type utf8CharLength(const std::string& s, std::string::size_type i);
}

#endif
