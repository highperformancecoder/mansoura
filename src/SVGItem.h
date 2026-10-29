/*
  @copyright Russell Standish 2026
  @author Russell Standish
  This file is part of Minsky.

  Minsky is free software: you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  Minsky is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with Minsky.  If not, see <http://www.gnu.org/licenses/>.
*/
#ifndef MANSOURA_SVGITEM_H
#define MANSOURA_SVGITEM_H
#include <cairo/cairo.h>
#include <librsvg/rsvg.h>
#include <string>

namespace mansoura
{
  class SVGRenderer
  {
    RsvgHandle* svg=nullptr;
#ifdef MXE
    double m_width=0, m_height=0;
#endif
    friend class MansouraCairo;
  public:
    SVGRenderer() {}
    SVGRenderer(const std::string& resource) {setResource(resource);}
    SVGRenderer(const SVGRenderer&)=delete;
    void operator=(const SVGRenderer&)=delete;
    ~SVGRenderer();

    /// initialise object from an SVG file
    void setResource(const std::string& resource);
    /// render SVG into region of size \a width \a height
    void render(cairo_t*, double width, double height) const;
  };

  
}

#endif
