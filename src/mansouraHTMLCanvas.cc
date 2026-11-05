#include "mansouraHTMLCanvas.h"
#include "mansouraRecording.h"
#include "textMarkup.h"
#include <emscripten/bind.h>
#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <unordered_map>
using namespace emscripten;
using namespace std;

namespace mansoura
{
  namespace
  {
    // Pango renders at 96dpi by default, so a size in points is 4/3 pixels.
    constexpr double ptToPx=96.0/72;
    // Pango's default font size when none is specified.
    constexpr double defaultFontSizePt=12;
    const char* const defaultFamily="sans-serif";

    // everything that cairo_save/cairo_restore (and canvas save/restore) preserves
    // which we need to be able to answer queries about
    struct State
    {
      Matrix ctm;
      double lineWidth=1;
      vector<double> dash;
      double dashOffset=0;
      string style="#000000"; // fill and stroke style
    };

    // a piece of text laid out in a single line
    struct Layout
    {
      vector<TextRun> runs;
      vector<double> widths;  // advance of each run
      double basePx=0;        // base font size in pixels
      string baseFamily;
      double ascent=0, descent=0; // above and below the baseline
      double width() const {double w=0; for (auto i: widths) w+=i; return w;}
      double height() const {return ascent+descent;}
    };

    string quoteFamily(const string& f)
    {
      if (f.find_first_of(" ")==string::npos || f.find_first_of("\"',")!=string::npos)
        return f;
      return "'"+f+"'";
    }

    double runSizePx(const TextRun& r, double basePx)
    {
      return r.absoluteSizePt>0? r.absoluteSizePt*ptToPx: basePx*r.sizeScale;
    }

    string fontFor(const TextRun& r, double basePx, const string& baseFamily)
    {
      char sz[40];
      snprintf(sz,sizeof(sz),"%.4gpx ",max(runSizePx(r,basePx),0.1));
      return string(r.italic? "italic ": "")+(r.bold? "bold ": "")+sz+
        quoteFamily(r.family.empty()? baseFamily: r.family);
    }

    // font size in points, using the default if none (or nonsense) specified
    double effectiveFontSize(const TextProperties& tp)
    {
      return (isfinite(tp.fontSize) && tp.fontSize>0)? tp.fontSize: defaultFontSizePt;
    }

    double risePx(const TextRun& r, double basePx)
    {return r.riseEm*basePx+r.risePt*ptToPx;}

    string rgbString(double r, double g, double b)
    {
      auto c=[](double x) {return int(lround(255*clamp(x,0.0,1.0)));};
      char s[50];
      snprintf(s,sizeof(s),"rgb(%d,%d,%d)",c(r),c(g),c(b));
      return s;
    }

    string rgbaString(double r, double g, double b, double a)
    {
      auto c=[](double x) {return int(lround(255*clamp(x,0.0,1.0)));};
      char s[70];
      snprintf(s,sizeof(s),"rgba(%d,%d,%d,%g)",c(r),c(g),c(b),clamp(a,0.0,1.0));
      return s;
    }
  }

  struct MansouraHTMLCanvas::Impl
  {
    emscripten::val canvas;
    State state;
    vector<State> stack; // states saved by save()
    PathRecorder path;
    double tolerance=0.1;
    // If a clip has been applied, we first save() the canvas so
    // that the clip can be undone by resetClip(). This is the size
    // of stack when that happened, or -1 if there is no such guard.
    int clipGuardLevel=-1;
    unordered_map<string,pair<double,double>> fontMetricsCache;

    Impl(const emscripten::val& canvas): canvas(canvas) {readState();}

    void readState()
    {
      state=State();
      if (!canvas["getTransform"].isUndefined())
        {
          val m=canvas.call<val>("getTransform");
          state.ctm.a=m["a"].as<double>(); state.ctm.b=m["b"].as<double>();
          state.ctm.c=m["c"].as<double>(); state.ctm.d=m["d"].as<double>();
          state.ctm.e=m["e"].as<double>(); state.ctm.f=m["f"].as<double>();
        }
      if (val lw=canvas["lineWidth"]; lw.isNumber())
        state.lineWidth=lw.as<double>();
      if (val fs=canvas["fillStyle"]; fs.isString())
        state.style=fs.as<string>();
    }

    // reinstate the mirrored state on the canvas (after popping the clip guard)
    void applyState()
    {
      auto& m=state.ctm;
      canvas.call<void>("setTransform",m.a,m.b,m.c,m.d,m.e,m.f);
      canvas.set("lineWidth",state.lineWidth);
      canvas.call<void>("setLineDash",val::array(state.dash.begin(),state.dash.end()));
      canvas.set("lineDashOffset",state.dashOffset);
      canvas.set("fillStyle",state.style);
      canvas.set("strokeStyle",state.style);
    }

    void userToDevice(double& x, double& y) const {state.ctm.apply(x,y);}
    void deviceToUser(double& x, double& y) const
    {if (state.ctm.invertible()) state.ctm.inverse().apply(x,y);}

    // @{ text
    double measureWidth(const string& font, const string& text)
    {
      if (text.empty()) return 0;
      canvas.set("font",font);
      return canvas.call<val>("measureText",utf8ToWString(text))["width"].as<double>();
    }

    // ascent and descent of the font, above and below baseline
    pair<double,double> fontMetrics(const string& font, double px)
    {
      if (auto i=fontMetricsCache.find(font); i!=fontMetricsCache.end())
        return i->second;
      canvas.set("font",font);
      val m=canvas.call<val>("measureText",wstring(L"M"));
      pair<double,double> r(0.905*px, 0.212*px); // typical sans serif if browser lacks the metrics
      if (!m["fontBoundingBoxAscent"].isUndefined())
        r={m["fontBoundingBoxAscent"].as<double>(), m["fontBoundingBoxDescent"].as<double>()};
      if (fontMetricsCache.size()>1000) fontMetricsCache.clear();
      fontMetricsCache.emplace(font,r);
      return r;
    }

    Layout layout(const TextProperties& tp)
    {
      Layout l;
      l.runs= tp.markup.empty()? plainRuns(tp.plainText): parseMarkup(tp.markup);
      l.basePx=ptToPx*effectiveFontSize(tp);
      l.baseFamily=tp.fontFamily.empty()? defaultFamily: tp.fontFamily;
      // the line is always at least as tall as the base font
      TextRun base;
      tie(l.ascent,l.descent)=fontMetrics(fontFor(base,l.basePx,l.baseFamily),l.basePx);
      for (auto& r: l.runs)
        {
          auto font=fontFor(r,l.basePx,l.baseFamily);
          l.widths.push_back(measureWidth(font,r.text));
          auto [a,d]=fontMetrics(font,runSizePx(r,l.basePx));
          auto rise=risePx(r,l.basePx);
          l.ascent=max(l.ascent,a+rise);
          l.descent=max(l.descent,d-rise);
        }
      return l;
    }

    // draw with the top left corner of the text at the current point
    void show(const Layout& l, double angle)
    {
      if (l.runs.empty()) return;
      double x=0, y=0;
      if (path.hasCurrent())
        {
          tie(x,y)=path.current();
          deviceToUser(x,y);
        }
      canvas.call<void>("save"); // not our save(): this is private to this function
      canvas.call<void>("translate",x,y);
      if (angle!=0) canvas.call<void>("rotate",angle);
      canvas.set("textBaseline",string("alphabetic"));
      canvas.set("textAlign",string("left"));
      string currentStyle=state.style;
      double xpos=0;
      for (size_t i=0; i<l.runs.size(); ++i)
        {
          auto& r=l.runs[i];
          auto style=r.colour.empty()? state.style: r.colour;
          if (style!=currentStyle)
            {
              canvas.set("fillStyle",style);
              currentStyle=style;
            }
          canvas.set("font",fontFor(r,l.basePx,l.baseFamily));
          const double baseline=l.ascent-risePx(r,l.basePx);
          canvas.call<void>("fillText",utf8ToWString(r.text),xpos,baseline);
          const double px=runSizePx(r,l.basePx), thickness=max(1.0,px/16);
          if (r.underline)
            canvas.call<void>("fillRect",xpos,baseline+0.1*px,l.widths[i],thickness);
          if (r.strikethrough)
            canvas.call<void>("fillRect",xpos,baseline-0.3*px,l.widths[i],thickness);
          xpos+=l.widths[i];
        }
      canvas.call<void>("restore");
    }
    /// @}
  };

  MansouraHTMLCanvas::MansouraHTMLCanvas(const emscripten::val& canvas):
    impl(make_unique<MansouraHTMLCanvas::Impl>(canvas)) {}
  MansouraHTMLCanvas::~MansouraHTMLCanvas() {}

  void MansouraHTMLCanvas::syncState() const
  {
    // discard any clip (and drawing state changes) left over by an unbalanced frame
    if (impl->clipGuardLevel>=0)
      {
        impl->canvas.call<void>("restore");
        impl->clipGuardLevel=-1;
      }
    impl->stack.clear();
    impl->path.clear();
    impl->readState();
  }

  // paths
  void MansouraHTMLCanvas::moveTo(double x, double y) const
  {
    impl->canvas.call<void>("moveTo",x,y);
    impl->userToDevice(x,y);
    impl->path.moveTo(x,y);
  }

  void MansouraHTMLCanvas::lineTo(double x, double y) const
  {
    impl->canvas.call<void>("lineTo",x,y);
    impl->userToDevice(x,y);
    impl->path.lineTo(x,y);
  }

  void MansouraHTMLCanvas::relMoveTo(double dx, double dy) const
  {
    if (!impl->path.hasCurrent()) return; // an error in cairo
    double x,y;
    getCurrentPoint(x,y);
    moveTo(x+dx,y+dy);
  }

  void MansouraHTMLCanvas::relLineTo(double dx, double dy) const
  {
    if (!impl->path.hasCurrent()) return;
    double x,y;
    getCurrentPoint(x,y);
    lineTo(x+dx,y+dy);
  }

  void MansouraHTMLCanvas::arc
  (double x, double y, double radius, double start, double end) const
  {
    if (radius<0 || !isfinite(radius)) return;
    // as with cairo_arc, no line is drawn to the start of the arc
    // after newSubPath(), but canvas needs to be told
    if (!impl->path.hasCurrent())
      impl->canvas.call<void>("moveTo",x+radius*cos(start),y+radius*sin(start));
    impl->canvas.call<void>("arc",x,y,radius,start,end,false);
    impl->path.arc(impl->state.ctm,x,y,radius,start,end);
  }

  void MansouraHTMLCanvas::curveTo(double x1, double y1, double x2, double y2, double x3, double y3) const
  {
    impl->canvas.call<void>("bezierCurveTo",x1,y1,x2,y2,x3,y3);
    impl->userToDevice(x1,y1);
    impl->userToDevice(x2,y2);
    impl->userToDevice(x3,y3);
    impl->path.curveTo(x1,y1,x2,y2,x3,y3);
  }

  void MansouraHTMLCanvas::rectangle(double x, double y, double width, double height) const
  {
    impl->canvas.call<void>("rect",x,y,width,height);
    impl->path.rectangle(impl->state.ctm,x,y,width,height);
  }

  void MansouraHTMLCanvas::newPath() const
  {
    impl->canvas.call<void>("beginPath");
    impl->path.clear();
  }

  void MansouraHTMLCanvas::newSubPath() const
  {
    impl->path.newSubPath();
  }

  void MansouraHTMLCanvas::closePath() const
  {
    impl->canvas.call<void>("closePath");
    impl->path.closePath();
  }

  void MansouraHTMLCanvas::getCurrentPoint(double& x, double& y) const
  {
    x=y=0;
    if (!impl->path.hasCurrent()) return;
    tie(x,y)=impl->path.current();
    impl->deviceToUser(x,y);
  }

  namespace
  {
    class HTMLPath: public IPath
    {
      vector<PathSeg> segs;
    public:
      HTMLPath(vector<PathSeg> segs): segs(std::move(segs)) {}
      void appendToCurrent(const IMansoura& ctx) const override
      {
        auto c=dynamic_cast<const MansouraHTMLCanvas*>(&ctx);
        if (!c) throw runtime_error("path can only be appended to an HTML canvas context");
        c->appendPath(segs);
      }
    };
  }

  unique_ptr<IPath> MansouraHTMLCanvas::copyPath() const
  {
    return make_unique<HTMLPath>(impl->path.segs);
  }

  void MansouraHTMLCanvas::appendPath(const vector<PathSeg>& segs) const
  {
    // segments are in device space, so draw them with an identity
    // transformation. Canvas stores paths in device space, so the
    // transformation does not affect the path once it is added.
    auto& c=impl->canvas;
    c.call<void>("save");
    c.call<void>("resetTransform");
    for (auto& s: segs)
      switch (s.type)
        {
        case PathSeg::MoveTo:
          c.call<void>("moveTo",s.x[0],s.y[0]);
          impl->path.moveTo(s.x[0],s.y[0]);
          break;
        case PathSeg::LineTo:
          c.call<void>("lineTo",s.x[0],s.y[0]);
          impl->path.lineTo(s.x[0],s.y[0]);
          break;
        case PathSeg::CurveTo:
          c.call<void>("bezierCurveTo",s.x[0],s.y[0],s.x[1],s.y[1],s.x[2],s.y[2]);
          impl->path.curveTo(s.x[0],s.y[0],s.x[1],s.y[1],s.x[2],s.y[2]);
          break;
        case PathSeg::ClosePath:
          c.call<void>("closePath");
          impl->path.closePath();
          break;
        }
    c.call<void>("restore");
  }

  vector<pair<double,double>> MansouraHTMLCanvas::flattenedPathPoints() const
  {
    return impl->path.flattened(impl->state.ctm,impl->tolerance);
  }

  void MansouraHTMLCanvas::fill(bool preserve) const
  {
    impl->canvas.call<void>("fill");
    if (!preserve) newPath();
  }

  void MansouraHTMLCanvas::fillEvenOdd(bool preserve) const
  {
    impl->canvas.call<void>("fill",string("evenodd"));
    if (!preserve) newPath();
  }

  void MansouraHTMLCanvas::clip(bool preserve) const
  {
    if (impl->clipGuardLevel<0)
      {
        impl->canvas.call<void>("save");
        impl->clipGuardLevel=impl->stack.size();
      }
    impl->canvas.call<void>("clip");
    if (!preserve) newPath();
  }

  void MansouraHTMLCanvas::resetClip() const
  {
    // The canvas cannot enlarge a clip, so we can only do this by
    // discarding the state saved before the clip was applied. That
    // is only possible if the clip was applied at this nesting level.
    if (impl->clipGuardLevel!=int(impl->stack.size())) return;
    impl->canvas.call<void>("restore");
    impl->clipGuardLevel=-1;
    impl->applyState();
  }

  void MansouraHTMLCanvas::paint() const
  {
    auto& c=impl->canvas;
    val surface=c["canvas"];
    if (surface.isUndefined()) return;
    c.call<void>("save");
    c.call<void>("resetTransform");
    c.call<void>("fillRect",0,0,surface["width"].as<double>(),surface["height"].as<double>());
    c.call<void>("restore");
  }

  void MansouraHTMLCanvas::stroke(bool preserve) const
  {
    impl->canvas.call<void>("stroke");
    if (!preserve) newPath();
  }

  void MansouraHTMLCanvas::setLineWidth(double w) const
  {
    impl->state.lineWidth=w;
    impl->canvas.set("lineWidth",w);
  }

  double MansouraHTMLCanvas::getLineWidth() const {return impl->state.lineWidth;}

  void MansouraHTMLCanvas::setDash(const double* dashes, int num_dashes, double offset) const
  {
    auto& s=impl->state;
    s.dash.assign(dashes, dashes+max(num_dashes,0));
    s.dashOffset=offset;
    impl->canvas.call<void>("setLineDash",val::array(s.dash.begin(),s.dash.end()));
    impl->canvas.set("lineDashOffset",offset);
  }

  // sources
  void MansouraHTMLCanvas::setSourceRGB(double r, double g, double b) const
  {
    impl->state.style=rgbString(r,g,b);
    impl->canvas.set("fillStyle",impl->state.style);
    impl->canvas.set("strokeStyle",impl->state.style);
  }

  void MansouraHTMLCanvas::setSourceRGBA(double r, double g, double b,double a) const
  {
    impl->state.style=rgbaString(r,g,b,a);
    impl->canvas.set("fillStyle",impl->state.style);
    impl->canvas.set("strokeStyle",impl->state.style);
  }

  // text. Argument is in UTF8 encoding
  void MansouraHTMLCanvas::showText(const TextProperties& tp) const
  {
    if (tp.markup.empty() && tp.plainText.empty()) return;
    impl->show(impl->layout(tp), tp.angle);
  }

  TextExtents MansouraHTMLCanvas::textExtents(const TextProperties& tp) const
  {
    auto l=impl->layout(tp);
    return {0,0,l.width(),l.height()};
  }

  // matrix transformation
  void MansouraHTMLCanvas::identityMatrix() const
  {
    impl->canvas.call<void>("resetTransform");
    impl->state.ctm=Matrix();
  }

  void MansouraHTMLCanvas::translate(double x, double y) const
  {
    impl->canvas.call<void>("translate",x,y);
    impl->state.ctm=impl->state.ctm*Matrix::translation(x,y);
  }

  void MansouraHTMLCanvas::scale(double sx, double sy) const
  {
    impl->canvas.call<void>("scale",sx,sy);
    impl->state.ctm=impl->state.ctm*Matrix::scaling(sx,sy);
  }

  void MansouraHTMLCanvas::rotate(double angle) const
  {
    impl->canvas.call<void>("rotate",angle);
    impl->state.ctm=impl->state.ctm*Matrix::rotation(angle);
  }

  void MansouraHTMLCanvas::userToDevice(double& x, double& y) const
  {
    impl->userToDevice(x,y);
  }

  // context manipulation
  void MansouraHTMLCanvas::save() const
  {
    impl->canvas.call<void>("save");
    impl->stack.push_back(impl->state);
  }

  void MansouraHTMLCanvas::restore() const
  {
    if (impl->stack.empty()) return; // unbalanced restore is an error in cairo
    if (impl->clipGuardLevel==int(impl->stack.size()))
      {
        // clip was applied since the matching save(): unwind it too
        impl->canvas.call<void>("restore");
        impl->clipGuardLevel=-1;
      }
    impl->canvas.call<void>("restore");
    impl->state=std::move(impl->stack.back());
    impl->stack.pop_back();
  }

  void MansouraHTMLCanvas::setTolerance(double tolerance) const
  {
    if (tolerance>0) impl->tolerance=tolerance;
  }

  void MansouraHTMLCanvas::renderSVG(const SVGRenderer& svgRenderer, double width, double height) const
  {
    // TODO: draw the SVG resource via an Image/Path2D. Icons are omitted for now.
  }

  class HTMLCacheRender: public ICacheRender
  {
    const MansouraHTMLCanvas& man;
    TextProperties tp;
    mutable Layout cached;
    mutable bool valid=false;

    const Layout& layout() const
    {
      if (!valid) {cached=man.impl->layout(tp); valid=true;}
      return cached;
    }
    // width of the first \a n bytes of run \a i
    double prefixWidth(const Layout& l, size_t i, size_t n) const
    {
      auto& r=l.runs[i];
      if (n>=r.text.size()) return l.widths[i];
      return man.impl->measureWidth(fontFor(r,l.basePx,l.baseFamily),r.text.substr(0,n));
    }
  public:
    HTMLCacheRender(const MansouraHTMLCanvas& man, const TextProperties& tp):
      man(man), tp(tp) {}
    void show() override {man.impl->show(layout(), tp.angle);}
    TextExtents extents() const override
    {
      auto& l=layout();
      return {0,0,l.width(),l.height()};
    }
    const void* context() const override {return &man;}
    double fontSize() const override
    {return effectiveFontSize(tp);}

    // indices are byte offsets into the text with markup removed, as with Pango
    string::size_type posToIdx(double x) const override
    {
      auto& l=layout();
      string::size_type off=0, lastChar=0;
      double start=0;
      for (size_t i=0; i<l.runs.size(); ++i)
        {
          auto& t=l.runs[i].text;
          if (x<start+l.widths[i] || i+1==l.runs.size())
            for (string::size_type j=0; j<t.size(); j+=utf8CharLength(t,j))
              {
                lastChar=off+j;
                if (x<start+prefixWidth(l,i,j+utf8CharLength(t,j)))
                  return lastChar;
              }
          if (!t.empty()) lastChar=off+t.size()-utf8CharLength(t,t.size()-1);
          off+=t.size();
          start+=l.widths[i];
        }
      return lastChar; // like Pango, beyond the end clamps to the last character
    }

    double idxToPos(string::size_type idx) const override
    {
      auto& l=layout();
      string::size_type off=0;
      double start=0;
      for (size_t i=0; i<l.runs.size(); ++i)
        {
          auto n=l.runs[i].text.size();
          if (idx<off+n) return start+prefixWidth(l,i,idx-off);
          off+=n;
          start+=l.widths[i];
        }
      return start;
    }
    TextProperties properties() const override {return tp;}
    TextProperties properties(const TextProperties& p) override
    {
      valid=false;
      return tp=p;
    }
  };

  unique_ptr<ICacheRender>
  MansouraHTMLCanvas::cachedRender(const TextProperties& tp) const {
    return make_unique<HTMLCacheRender>(*this,tp);
  }

  void installHTMLTextMeasurer(const val& ctx)
  {
    setTextMeasurer(make_shared<ContextTextMeasurer>(make_shared<MansouraHTMLCanvas>(ctx)));
  }

  bool installHTMLTextMeasurer()
  {
    val global=val::global();
    val ctx=val::undefined();
    if (!global["OffscreenCanvas"].isUndefined())
      ctx=val::global("OffscreenCanvas").new_(1,1).call<val>("getContext",string("2d"));
    else if (!global["document"].isUndefined())
      ctx=global["document"].call<val>("createElement",string("canvas")).call<val>("getContext",string("2d"));
    if (ctx.isUndefined() || ctx.isNull()) return false;
    installHTMLTextMeasurer(ctx);
    return true;
  }
}
