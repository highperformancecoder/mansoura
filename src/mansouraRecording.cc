/*
  @copyright Ravelation Pty Ltd 2026

  Mansoura is free software: you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
*/
#include "mansouraRecording.h"
#include "textMarkup.h"
#include <algorithm>
#include <limits>
#include <mutex>
#include <stdexcept>
using namespace std;

namespace mansoura
{
  namespace
  {
    constexpr double ptToPx=96.0/72;
    constexpr double defaultFontSizePt=12;
    // rough proportions of a typical sans serif face
    constexpr double averageCharWidth=0.55, lineHeight=1.15;

    double effectiveFontSize(const TextProperties& tp)
    {
      return (isfinite(tp.fontSize) && tp.fontSize>0)? tp.fontSize: defaultFontSizePt;
    }

    size_t codePoints(const string& s)
    {
      size_t n=0;
      for (size_t i=0; i<s.size(); i+=utf8CharLength(s,i)) ++n;
      return n;
    }

    // Default text metrics, used when nothing better is installed.
    class EstimatingMeasurer: public ITextMeasurer
    {
      static vector<TextRun> runs(const TextProperties& tp)
      {return tp.markup.empty()? plainRuns(tp.plainText): parseMarkup(tp.markup);}
      static double runPx(const TextRun& r, double basePx)
      {return r.absoluteSizePt>0? r.absoluteSizePt*ptToPx: basePx*r.sizeScale;}

      class Cache: public ICacheRender
      {
        TextProperties tp;
        double charWidth() const {return averageCharWidth*ptToPx*effectiveFontSize(tp);}
        string text() const {return plainText(runs(tp));}
      public:
        Cache(const TextProperties& tp): tp(tp) {}
        void show() override {}
        TextExtents extents() const override
        {return EstimatingMeasurer().textExtents(tp);}
        const void* context() const override {return &textMeasurer();}
        double fontSize() const override {return effectiveFontSize(tp);}
        string::size_type posToIdx(double x) const override
        {
          auto t=text();
          string::size_type last=0;
          double pos=0;
          for (string::size_type i=0; i<t.size(); i+=utf8CharLength(t,i))
            {
              last=i;
              pos+=charWidth();
              if (x<pos) return i;
            }
          return last;
        }
        double idxToPos(string::size_type idx) const override
        {
          auto t=text();
          return charWidth()*codePoints(t.substr(0,min(idx,t.size())));
        }
        TextProperties properties() const override {return tp;}
        TextProperties properties(const TextProperties& p) override {return tp=p;}
      };
    public:
      TextExtents textExtents(const TextProperties& tp) const override
      {
        const double basePx=ptToPx*effectiveFontSize(tp);
        double width=0, height=lineHeight*basePx;
        for (auto& r: runs(tp))
          {
            const double px=runPx(r,basePx);
            width+=averageCharWidth*px*codePoints(r.text);
            height=max(height,lineHeight*px+abs(r.riseEm*basePx+r.risePt*ptToPx));
          }
        return {0,0,width,height};
      }
      unique_ptr<ICacheRender> cachedRender(const TextProperties& tp) const override
      {return make_unique<Cache>(tp);}
    };

    mutex measurerMutex;
    shared_ptr<const ITextMeasurer>& installedMeasurer()
    {
      static shared_ptr<const ITextMeasurer> m;
      return m;
    }

    // axis aligned bounding box. Empty if x0>x1
    struct Bounds
    {
      double x0=numeric_limits<double>::infinity(), y0=x0;
      double x1=-numeric_limits<double>::infinity(), y1=x1;
      bool empty() const {return x0>x1 || y0>y1;}
      void add(double x, double y)
      {
        x0=min(x0,x); x1=max(x1,x);
        y0=min(y0,y); y1=max(y1,y);
      }
      void add(const Bounds& b)
      {
        if (b.empty()) return;
        add(b.x0,b.y0); add(b.x1,b.y1);
      }
      void intersect(const Bounds& b)
      {
        x0=max(x0,b.x0); x1=min(x1,b.x1);
        y0=max(y0,b.y0); y1=min(y1,b.y1);
      }
    };

    class RecordedPath: public IPath
    {
      vector<PathSeg> segs;
    public:
      RecordedPath(vector<PathSeg> segs): segs(std::move(segs)) {}
      void appendToCurrent(const IMansoura& ctx) const override
      {
        auto c=dynamic_cast<const RecordingContext*>(&ctx);
        if (!c) throw runtime_error("path can only be appended to a recording context");
        c->appendPath(segs);
      }
    };
  }

  void setTextMeasurer(shared_ptr<const ITextMeasurer> measurer)
  {
    lock_guard<mutex> lock(measurerMutex);
    installedMeasurer()=std::move(measurer);
  }

  const ITextMeasurer& textMeasurer()
  {
    lock_guard<mutex> lock(measurerMutex);
    if (auto& m=installedMeasurer(); m) return *m;
    static const EstimatingMeasurer estimator;
    return estimator;
  }

  namespace
  {
    struct State
    {
      Matrix ctm;
      double lineWidth=2; // cairo's default
      bool hasClip=false;
      Bounds clip;        // device space
    };
  }

  struct RecordingContext::Impl
  {
    State state;
    vector<State> stack;
    PathRecorder path;
    Bounds ink;
    double tolerance=0.1;

    // add \a b (device space) to the ink, subject to the clip
    void addInk(Bounds b)
    {
      if (b.empty()) return;
      if (state.hasClip) b.intersect(state.clip);
      ink.add(b);
    }
    void addUserPoint(Bounds& b, double x, double y) const
    {
      state.ctm.apply(x,y);
      b.add(x,y);
    }

    // extent of the area enclosed by the current path
    Bounds pathBounds() const
    {
      Bounds b;
      for (auto& pl: path.polylines(tolerance))
        for (auto& p: pl.pts) b.add(p.first,p.second);
      return b;
    }

    Bounds strokeBounds() const
    {
      const auto& m=state.ctm;
      double scale=sqrt(abs(m.determinant()));
      if (!(scale>0)) scale=max(hypot(m.a,m.b),hypot(m.c,m.d));
      const double half=0.5*state.lineWidth*scale;
      Bounds b;
      auto addSegment=[&](pair<double,double> p, pair<double,double> q) {
        const double dx=q.first-p.first, dy=q.second-p.second, len=hypot(dx,dy);
        if (!(len>0)) return;
        const double nx=-dy/len*half, ny=dx/len*half;
        for (auto& pt: {p,q})
          {
            b.add(pt.first+nx,pt.second+ny);
            b.add(pt.first-nx,pt.second-ny);
          }
      };
      for (auto& pl: path.polylines(tolerance))
        {
          for (size_t i=1; i<pl.pts.size(); ++i) addSegment(pl.pts[i-1],pl.pts[i]);
          if (pl.closed && pl.pts.size()>1) addSegment(pl.pts.back(),pl.pts.front());
        }
      return b;
    }

    void recordText(const TextProperties& tp, const TextExtents& e)
    {
      if (tp.markup.empty() && tp.plainText.empty()) return;
      double cx=0, cy=0;
      if (path.hasCurrent())
        {
          tie(cx,cy)=path.current();
          if (state.ctm.invertible()) state.ctm.inverse().apply(cx,cy);
        }
      // the text is rotated about its anchor, the current point
      const double c=cos(tp.angle), s=sin(tp.angle);
      Bounds b;
      for (auto [x,y]: {make_pair(e.left,e.top), make_pair(e.left+e.width,e.top),
                        make_pair(e.left+e.width,e.top+e.height), make_pair(e.left,e.top+e.height)})
        addUserPoint(b,cx+x*c-y*s,cy+x*s+y*c);
      addInk(b);
    }
  };

  RecordingContext::RecordingContext(): impl(make_unique<Impl>()) {}
  RecordingContext::~RecordingContext() {}

  InkExtents RecordingContext::inkExtents() const
  {
    auto& b=impl->ink;
    if (b.empty()) return {0,0,0,0};
    return {b.x0,b.y0,b.x1-b.x0,b.y1-b.y0};
  }

  void RecordingContext::moveTo(double x, double y) const
  {
    impl->state.ctm.apply(x,y);
    impl->path.moveTo(x,y);
  }

  void RecordingContext::lineTo(double x, double y) const
  {
    impl->state.ctm.apply(x,y);
    impl->path.lineTo(x,y);
  }

  void RecordingContext::relMoveTo(double dx, double dy) const
  {
    if (!impl->path.hasCurrent()) return;
    impl->state.ctm.applyDistance(dx,dy);
    auto [x,y]=impl->path.current();
    impl->path.moveTo(x+dx,y+dy);
  }

  void RecordingContext::relLineTo(double dx, double dy) const
  {
    if (!impl->path.hasCurrent()) return;
    impl->state.ctm.applyDistance(dx,dy);
    auto [x,y]=impl->path.current();
    impl->path.lineTo(x+dx,y+dy);
  }

  void RecordingContext::arc(double x, double y, double radius, double start, double end) const
  {
    if (radius<0 || !isfinite(radius)) return;
    impl->path.arc(impl->state.ctm,x,y,radius,start,end);
  }

  void RecordingContext::curveTo(double x1, double y1, double x2, double y2, double x3, double y3) const
  {
    auto& m=impl->state.ctm;
    m.apply(x1,y1); m.apply(x2,y2); m.apply(x3,y3);
    impl->path.curveTo(x1,y1,x2,y2,x3,y3);
  }

  void RecordingContext::rectangle(double x, double y, double w, double h) const
  {impl->path.rectangle(impl->state.ctm,x,y,w,h);}

  void RecordingContext::newPath() const {impl->path.clear();}
  void RecordingContext::newSubPath() const {impl->path.newSubPath();}
  void RecordingContext::closePath() const {impl->path.closePath();}

  void RecordingContext::getCurrentPoint(double& x, double& y) const
  {
    x=y=0;
    if (!impl->path.hasCurrent()) return;
    tie(x,y)=impl->path.current();
    if (impl->state.ctm.invertible()) impl->state.ctm.inverse().apply(x,y);
  }

  unique_ptr<IPath> RecordingContext::copyPath() const
  {return make_unique<RecordedPath>(impl->path.segs);}

  void RecordingContext::appendPath(const vector<PathSeg>& segs) const
  {
    for (auto& s: segs)
      switch (s.type)
        {
        case PathSeg::MoveTo: impl->path.moveTo(s.x[0],s.y[0]); break;
        case PathSeg::LineTo: impl->path.lineTo(s.x[0],s.y[0]); break;
        case PathSeg::CurveTo: impl->path.curveTo(s.x[0],s.y[0],s.x[1],s.y[1],s.x[2],s.y[2]); break;
        case PathSeg::ClosePath: impl->path.closePath(); break;
        }
  }

  vector<pair<double,double>> RecordingContext::flattenedPathPoints() const
  {return impl->path.flattened(impl->state.ctm,impl->tolerance);}

  void RecordingContext::fill(bool preserve) const
  {
    impl->addInk(impl->pathBounds());
    if (!preserve) newPath();
  }

  void RecordingContext::fillEvenOdd(bool preserve) const {fill(preserve);}

  void RecordingContext::stroke(bool preserve) const
  {
    impl->addInk(impl->strokeBounds());
    if (!preserve) newPath();
  }

  void RecordingContext::clip(bool preserve) const
  {
    auto b=impl->pathBounds(); // empty if nothing in the path, which clips everything away
    auto& s=impl->state;
    if (s.hasClip) b.intersect(s.clip);
    s.clip=b;
    s.hasClip=true;
    if (!preserve) newPath();
  }

  void RecordingContext::resetClip() const {impl->state.hasClip=false;}

  void RecordingContext::paint() const
  {
    // paints everywhere within the clip. Without one the area is unbounded, which we don't record.
    if (impl->state.hasClip) impl->addInk(impl->state.clip);
  }

  void RecordingContext::setLineWidth(double w) const {impl->state.lineWidth=w;}
  double RecordingContext::getLineWidth() const {return impl->state.lineWidth;}
  void RecordingContext::setDash(const double*, int, double) const {}
  void RecordingContext::setSourceRGB(double, double, double) const {}
  void RecordingContext::setSourceRGBA(double, double, double, double) const {}

  void RecordingContext::showText(const TextProperties& tp) const
  {
    if (tp.markup.empty() && tp.plainText.empty()) return;
    impl->recordText(tp,textMeasurer().textExtents(tp));
  }

  TextExtents RecordingContext::textExtents(const TextProperties& tp) const
  {return textMeasurer().textExtents(tp);}

  void RecordingContext::identityMatrix() const {impl->state.ctm=Matrix();}
  void RecordingContext::translate(double x, double y) const
  {impl->state.ctm=impl->state.ctm*Matrix::translation(x,y);}
  void RecordingContext::scale(double sx, double sy) const
  {impl->state.ctm=impl->state.ctm*Matrix::scaling(sx,sy);}
  void RecordingContext::rotate(double angle) const
  {impl->state.ctm=impl->state.ctm*Matrix::rotation(angle);}
  void RecordingContext::userToDevice(double& x, double& y) const
  {impl->state.ctm.apply(x,y);}

  void RecordingContext::save() const {impl->stack.push_back(impl->state);}
  void RecordingContext::restore() const
  {
    if (impl->stack.empty()) return;
    impl->state=impl->stack.back();
    impl->stack.pop_back();
  }

  void RecordingContext::setTolerance(double t) const {if (t>0) impl->tolerance=t;}

  void RecordingContext::renderSVG(const SVGRenderer&, double width, double height) const
  {
    Bounds b;
    for (auto [x,y]: {make_pair(0.0,0.0), make_pair(width,0.0), make_pair(width,height), make_pair(0.0,height)})
      impl->addUserPoint(b,x,y);
    impl->addInk(b);
  }

  // text metrics come from the measurer, but "showing" the text only records ink
  class RecordingCacheRender: public ICacheRender
  {
    const RecordingContext& rec;
    unique_ptr<ICacheRender> inner;
  public:
    RecordingCacheRender(const RecordingContext& rec, unique_ptr<ICacheRender> inner):
      rec(rec), inner(std::move(inner)) {}
    void show() override {rec.impl->recordText(inner->properties(),inner->extents());}
    TextExtents extents() const override {return inner->extents();}
    const void* context() const override {return &rec;}
    double fontSize() const override {return inner->fontSize();}
    string::size_type posToIdx(double x) const override {return inner->posToIdx(x);}
    double idxToPos(string::size_type idx) const override {return inner->idxToPos(idx);}
    TextProperties properties() const override {return inner->properties();}
    TextProperties properties(const TextProperties& p) override {return inner->properties(p);}
  };

  unique_ptr<ICacheRender> RecordingContext::cachedRender(const TextProperties& tp) const
  {return make_unique<RecordingCacheRender>(*this,textMeasurer().cachedRender(tp));}
}
