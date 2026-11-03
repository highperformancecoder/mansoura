#include "mansouraHTMLCanvas.h"
#include <emscripten/bind.h>
using namespace emscripten;
using namespace std;

namespace
{
  //snaffled from some random guy in StackOverflow. TODO - treatment of exceptions?
  std::wstring utf8_to_utf16(const std::string& utf8)
  {
    std::vector<unsigned long> unicode;
    size_t i = 0;
    while (i < utf8.size())
      {
        unsigned long uni;
        size_t todo;
        bool error = false;
        unsigned char ch = utf8[i++];
        if (ch <= 0x7F)
          {
            uni = ch;
            todo = 0;
          }
        else if (ch <= 0xBF)
          {
            throw std::logic_error("not a UTF-8 string");
          }
        else if (ch <= 0xDF)
          {
            uni = ch&0x1F;
            todo = 1;
          }
        else if (ch <= 0xEF)
          {
            uni = ch&0x0F;
            todo = 2;
          }
        else if (ch <= 0xF7)
          {
            uni = ch&0x07;
            todo = 3;
          }
        else
          {
            throw std::logic_error("not a UTF-8 string");
          }
        for (size_t j = 0; j < todo; ++j)
          {
            if (i == utf8.size())
              throw std::logic_error("not a UTF-8 string");
            unsigned char ch = utf8[i++];
            if (ch < 0x80 || ch > 0xBF)
              throw std::logic_error("not a UTF-8 string");
            uni <<= 6;
            uni += ch & 0x3F;
          }
        if (uni >= 0xD800 && uni <= 0xDFFF)
          throw std::logic_error("not a UTF-8 string");
        if (uni > 0x10FFFF)
          throw std::logic_error("not a UTF-8 string");
        unicode.push_back(uni);
      }
    std::wstring utf16;
    for (size_t i = 0; i < unicode.size(); ++i)
      {
        unsigned long uni = unicode[i];
        if (uni <= 0xFFFF)
          {
            utf16 += (wchar_t)uni;
          }
        else
          {
            uni -= 0x10000;
            utf16 += (wchar_t)((uni >> 10) + 0xD800);
            utf16 += (wchar_t)((uni & 0x3FF) + 0xDC00);
          }
      }
    return utf16;
  }
}

namespace mansoura
{
  struct MansouraHTMLCanvas::Impl
  {
    emscripten::val canvas;
    Impl(const emscripten::val& canvas): canvas(canvas) {}
    double currX=0, currY=0;
    bool pathOpen=false;
  };

  MansouraHTMLCanvas::MansouraHTMLCanvas(const emscripten::val& canvas):
    impl(make_unique<MansouraHTMLCanvas::Impl>(canvas)) {}
  MansouraHTMLCanvas::~MansouraHTMLCanvas() {}
  
  void MansouraHTMLCanvas::moveTo(double x, double y) const
  {
    impl->canvas.call<void>("moveTo",x,y);
    impl->currX=x; impl->currY=y;
  }
  
  void MansouraHTMLCanvas::lineTo(double x, double y) const
  {
    impl->canvas.call<void>("lineTo",x,y);
    impl->currX=x; impl->currY=y;
  }

  void MansouraHTMLCanvas::relMoveTo(double x, double y) const
  {
    x+=impl->currX; y+=impl->currY;
    impl->canvas.call<void>("moveTo",x, y);
    impl->currX=x; impl->currY=y;
  }

  void MansouraHTMLCanvas::relLineTo(double x, double y) const
  {
    x+=impl->currX; y+=impl->currY;
    impl->canvas.call<void>("lineTo",x, y);
    impl->currX=x; impl->currY=y;
  }

  void MansouraHTMLCanvas::arc
  (double x, double y, double radius, double start, double end) const
  {
    impl->canvas.call<void>("arc",x,y,radius,start,end,false);
  }
  
  void MansouraHTMLCanvas::curveTo(double x1, double y1, double x2, double y2, double x3, double y3) const {}

  void MansouraHTMLCanvas::rectangle(double x, double y, double width, double height) const {}

  
  
  // paths
  void MansouraHTMLCanvas::newPath() const
  {
    impl->canvas.call<void>("beginPath");
  }

  void MansouraHTMLCanvas::newSubPath() const {}

  void MansouraHTMLCanvas::closePath() const
  {
    impl->canvas.call<void>("closePath");
  }

  void MansouraHTMLCanvas::getCurrentPoint(double& x, double& y) const {}
  std::unique_ptr<IPath> MansouraHTMLCanvas::copyPath() const
  {
    return nullptr;
  }
  std::vector<std::pair<double,double>> MansouraHTMLCanvas::flattenedPathPoints() const {return {};}

  void MansouraHTMLCanvas::fill(bool preserve) const
  {
    impl->canvas.call<void>("fill");
    if (!preserve) newPath();
  }

  void MansouraHTMLCanvas::fillEvenOdd(bool preserve) const
  {
    impl->canvas.call<void>("fill",val("evenodd"));
    if (!preserve) newPath();
  }

  void MansouraHTMLCanvas::clip(bool preserve) const
  {
    impl->canvas.call<void>("clip");
    if (!preserve) newPath();
  }

  void MansouraHTMLCanvas::resetClip() const {}
  void MansouraHTMLCanvas::paint() const {}

  void MansouraHTMLCanvas::stroke(bool preserve) const
  {
    impl->canvas.call<void>("stroke");
    if (!preserve) newPath();
  }
  
  void MansouraHTMLCanvas::setLineWidth(double w) const
  {
    impl->canvas.set("lineWidth",w);
  }

  double MansouraHTMLCanvas::getLineWidth() const {return 1;}
  void MansouraHTMLCanvas::setDash(const double* dashes, int num_dashes, double offset) const {}
  
  // sources
  void MansouraHTMLCanvas::setSourceRGB
  (double r, double g, double b) const
  {
    char rgbSpec[50];
    snprintf(rgbSpec,sizeof(rgbSpec),"rgb(%d,%d,%d)",int(255*r),int(255*g),int(255*b));
    impl->canvas.set("fillStyle",rgbSpec);
    impl->canvas.set("strokeStyle",rgbSpec);
  }

  void MansouraHTMLCanvas::setSourceRGBA
  (double r, double g, double b,double a) const
  {
    char rgbSpec[50];
    snprintf(rgbSpec,sizeof(rgbSpec),"rgb(%d,%d,%d,%f)",int(255*r),int(255*g),int(255*b),a);
    impl->canvas.set("fillStyle",rgbSpec);
    impl->canvas.set("strokeStyle",rgbSpec);
  }

  // return markup or plain text, depending on setting
  string getText(const TextProperties& tp)
  {
    if (!tp.markup.empty()) return tp.markup;
    return tp.plainText;
  }
  
  // text. Argument is in UTF8 encoding
  void MansouraHTMLCanvas::showText(const TextProperties& tp) const
  {
    // TODO set font family and size
    if (auto text=getText(tp); !text.empty()) // TODO - create a Pango markup processor.
      impl->canvas.call<void>("fillText",utf8_to_utf16(text),impl->currX,impl->currY);
  }

  TextExtents MansouraHTMLCanvas::textExtents(const TextProperties& tp) const
  {
    // TODO set font family and size
    val textExtents=impl->canvas.call<val>("measureText",getText(tp));
    return {0,0,textExtents["width"].as<double>(),10 /*height not available in impl->current browsers*/};
  }

  // matrix transformation
  void MansouraHTMLCanvas::identityMatrix() const
  {
    impl->canvas.call<void>("resetTransform");
  }
  
  void MansouraHTMLCanvas::translate(double x, double y) const
  {
    impl->canvas.call<void>("translate",x,y);
  }

  void MansouraHTMLCanvas::scale(double sx, double sy) const
  {
    impl->canvas.call<void>("scale",sx,sy);
  }


  void MansouraHTMLCanvas::rotate(double angle) const
  {
    impl->canvas.call<void>("rotate",angle);
    // rotate the impl->current position to refer to new coordinates. For showText
    auto x=impl->currX, y=impl->currY;
    impl->currX=x*cos(angle)+y*sin(angle);
    impl->currY=y*cos(angle)-x*sin(angle);
  }
  
  void MansouraHTMLCanvas::userToDevice(double& x, double& y) const {}

    // context manipulation
  void MansouraHTMLCanvas::save() const
  {
    impl->canvas.call<void>("save");
  }

  void MansouraHTMLCanvas::restore() const
  {
    impl->canvas.call<void>("restore");
  }

  void MansouraHTMLCanvas::setTolerance(double tolerance) const {}
  void MansouraHTMLCanvas::renderSVG(const SVGRenderer& svgRenderer, double width, double height) const {}

  struct CacheRender: public ICacheRender
  {
    const MansouraHTMLCanvas& man;
    TextProperties tp;
    CacheRender(const MansouraHTMLCanvas& man, const TextProperties& tp):
      man(man), tp(tp) {}
    void show() override {man.showText(tp);}
    TextExtents extents() const override {return man.textExtents(tp);}
    const void* context() const override {return &man;}
    double fontSize() const override {return tp.fontSize;}
    string::size_type posToIdx(double x) const override {return 0;} //not implemented
    double idxToPos(string::size_type idx) const override {return 0;} //not implemented
    TextProperties properties() const override {return tp;}
    TextProperties properties(const TextProperties& p) override {return tp=p;}
  };

  std::unique_ptr<ICacheRender>
  MansouraHTMLCanvas::cachedRender(const TextProperties& tp) const {
    return make_unique<CacheRender>(*this,tp);
  }


}
