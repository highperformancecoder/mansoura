/*
  Pure C++ affine matrix and path recorder, used by MansouraHTMLCanvas
  to provide the parts of the IMansoura API that the HTML5 Canvas API
  cannot answer (copyPath, flattenedPathPoints, getCurrentPoint,
  userToDevice). Deliberately free of any emscripten dependency, so it
  can be unit tested natively.

  Paths are recorded in device space, as cairo does, so that
  transformation changes part way through building a path behave the
  same way as on a cairo context.

  @copyright Ravelation Pty Ltd 2026

  Mansoura is free software: you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
*/
#ifndef MANSOURA_CANVAS_PATH_H
#define MANSOURA_CANVAS_PATH_H

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace mansoura
{
  /// affine transformation, x'=a*x+c*y+e, y'=b*x+d*y+f. Same
  /// convention as DOMMatrix / CanvasRenderingContext2D.
  struct Matrix
  {
    double a=1, b=0, c=0, d=1, e=0, f=0;

    void apply(double& x, double& y) const
    {
      const double nx=a*x+c*y+e;
      y=b*x+d*y+f;
      x=nx;
    }
    /// transform a displacement (ignores translation)
    void applyDistance(double& dx, double& dy) const
    {
      const double nx=a*dx+c*dy;
      dy=b*dx+d*dy;
      dx=nx;
    }
    double determinant() const {return a*d-b*c;}
    bool invertible() const
    {
      const double det=determinant();
      return std::isfinite(det) && det!=0;
    }
    /// precondition: invertible()
    Matrix inverse() const
    {
      const double det=determinant();
      Matrix r;
      r.a=d/det; r.b=-b/det; r.c=-c/det; r.d=a/det;
      r.e=(c*f-d*e)/det;
      r.f=(b*e-a*f)/det;
      return r;
    }
    /// (M*N)(p) = M(N(p)). Post-multiplying by an elementary
    /// transform is what translate/scale/rotate do to the CTM.
    Matrix operator*(const Matrix& n) const
    {
      Matrix r;
      r.a=a*n.a+c*n.b;
      r.b=b*n.a+d*n.b;
      r.c=a*n.c+c*n.d;
      r.d=b*n.c+d*n.d;
      r.e=a*n.e+c*n.f+e;
      r.f=b*n.e+d*n.f+f;
      return r;
    }
    static Matrix translation(double tx, double ty)
    {Matrix m; m.e=tx; m.f=ty; return m;}
    static Matrix scaling(double sx, double sy)
    {Matrix m; m.a=sx; m.d=sy; return m;}
    static Matrix rotation(double angle)
    {
      Matrix m;
      m.a=std::cos(angle); m.b=std::sin(angle);
      m.c=-m.b; m.d=m.a;
      return m;
    }
  };

  struct PathSeg
  {
    enum Type {MoveTo, LineTo, CurveTo, ClosePath};
    Type type;
    /// MoveTo/LineTo use [0] only. CurveTo uses [0],[1] as control
    /// points and [2] as the end point.
    double x[3]={0,0,0}, y[3]={0,0,0};
    PathSeg(Type type): type(type) {}
  };

  /// records a path in device space.
  class PathRecorder
  {
    double curX=0, curY=0, startX=0, startY=0;
    bool m_hasCurrent=false;
  public:
    std::vector<PathSeg> segs;

    bool hasCurrent() const {return m_hasCurrent;}
    /// current point, device space. Precondition: hasCurrent()
    std::pair<double,double> current() const {return {curX,curY};}

    void clear() {segs.clear(); m_hasCurrent=false;}
    /// begin a new sub path, with no current point
    void newSubPath() {m_hasCurrent=false;}

    void moveTo(double x, double y)
    {
      PathSeg s(PathSeg::MoveTo);
      s.x[0]=x; s.y[0]=y;
      segs.push_back(s);
      curX=startX=x; curY=startY=y;
      m_hasCurrent=true;
    }
    /// as cairo, a lineTo without a current point is a moveTo
    void lineTo(double x, double y)
    {
      if (!m_hasCurrent) return moveTo(x,y);
      PathSeg s(PathSeg::LineTo);
      s.x[0]=x; s.y[0]=y;
      segs.push_back(s);
      curX=x; curY=y;
    }
    void curveTo(double x1, double y1, double x2, double y2, double x3, double y3)
    {
      if (!m_hasCurrent) moveTo(x1,y1);
      PathSeg s(PathSeg::CurveTo);
      s.x[0]=x1; s.y[0]=y1;
      s.x[1]=x2; s.y[1]=y2;
      s.x[2]=x3; s.y[2]=y3;
      segs.push_back(s);
      curX=x3; curY=y3;
    }
    void closePath()
    {
      if (!m_hasCurrent) return;
      segs.emplace_back(PathSeg::ClosePath);
      curX=startX; curY=startY;
    }

    /// Append an arc, in user space of \a ctm, running clockwise (in
    /// the direction of increasing angle) from \a start to \a end,
    /// approximated by cubic Beziers. As cairo_arc, a line is drawn
    /// from any current point to the start of the arc.
    void arc(const Matrix& ctm, double cx, double cy, double r,
             double start, double end)
    {
      const double twoPi=2*M_PI;
      if (end<start) end+=twoPi*std::ceil((start-end)/twoPi);
      double sweep=end-start;
      auto dev=[&](double x, double y) {ctm.apply(x,y); return std::make_pair(x,y);};
      auto p0=dev(cx+r*std::cos(start), cy+r*std::sin(start));
      lineTo(p0.first,p0.second); // moveTo if no current point
      // split into segments of at most 90 degrees
      const int n=std::max(1, int(std::ceil(sweep/(0.5*M_PI)-1e-9)));
      const double delta=sweep/n;
      const double k=4.0/3.0*std::tan(delta/4);
      for (int i=0; i<n; ++i)
        {
          const double t0=start+i*delta, t1=t0+delta;
          const double c0=std::cos(t0), s0=std::sin(t0), c1=std::cos(t1), s1=std::sin(t1);
          auto q1=dev(cx+r*(c0-k*s0), cy+r*(s0+k*c0));
          auto q2=dev(cx+r*(c1+k*s1), cy+r*(s1-k*c1));
          auto q3=dev(cx+r*c1, cy+r*s1);
          curveTo(q1.first,q1.second,q2.first,q2.second,q3.first,q3.second);
        }
    }

    /// as cairo_rectangle: current point is left at (x,y)
    void rectangle(const Matrix& ctm, double x, double y, double w, double h)
    {
      auto dev=[&](double px, double py) {ctm.apply(px,py); return std::make_pair(px,py);};
      auto p=dev(x,y); moveTo(p.first,p.second);
      p=dev(x+w,y); lineTo(p.first,p.second);
      p=dev(x+w,y+h); lineTo(p.first,p.second);
      p=dev(x,y+h); lineTo(p.first,p.second);
      closePath();
    }

    /// a flattened subpath, in device space
    struct Polyline
    {
      std::vector<std::pair<double,double>> pts; ///< includes the starting point
      bool closed=false;
    };

    /// The path as a list of polylines in device space, with curves
    /// flattened to within \a tolerance device units. A subpath
    /// consisting of a lone moveTo is omitted.
    std::vector<Polyline> polylines(double tolerance) const
    {
      std::vector<Polyline> r;
      Polyline cur;
      auto finish=[&]() {
        if (cur.pts.size()>1 || (cur.closed && !cur.pts.empty())) r.push_back(std::move(cur));
        cur=Polyline();
      };
      double px=0, py=0;
      for (auto& s: segs)
        switch (s.type)
          {
          case PathSeg::MoveTo:
            finish();
            px=s.x[0]; py=s.y[0];
            cur.pts.emplace_back(px,py);
            break;
          case PathSeg::LineTo:
            px=s.x[0]; py=s.y[0];
            cur.pts.emplace_back(px,py);
            break;
          case PathSeg::CurveTo:
            flattenCurve(px,py,s,tolerance,[&](double x, double y){cur.pts.emplace_back(x,y);});
            px=s.x[2]; py=s.y[2];
            break;
          case PathSeg::ClosePath:
            cur.closed=true;
            if (!cur.pts.empty()) {px=cur.pts[0].first; py=cur.pts[0].second;}
            // a following segment without a moveTo continues from the start point
            {
              auto start=cur.pts.empty()? std::make_pair(px,py): cur.pts[0];
              finish();
              cur.pts.push_back(start);
            }
            break;
          }
      finish();
      return r;
    }

    /// End points of each line segment of the path, with curves
    /// flattened to within \a tolerance (device units), converted
    /// back to user space of \a ctm (or left in device space if ctm is
    /// singular). Mirrors cairo_copy_path_flat.
    std::vector<std::pair<double,double>>
    flattened(const Matrix& ctm, double tolerance) const
    {
      std::vector<std::pair<double,double>> r;
      const bool inv=ctm.invertible();
      const Matrix back=inv? ctm.inverse(): Matrix();
      auto emit=[&](double x, double y) {
        if (inv) back.apply(x,y);
        r.emplace_back(x,y);
      };
      double px=0, py=0, sx=0, sy=0;
      for (auto& s: segs)
        switch (s.type)
          {
          case PathSeg::MoveTo:
            px=sx=s.x[0]; py=sy=s.y[0];
            break;
          case PathSeg::LineTo:
            px=s.x[0]; py=s.y[0];
            emit(px,py);
            break;
          case PathSeg::CurveTo:
            flattenCurve(px,py,s,tolerance,emit);
            px=s.x[2]; py=s.y[2];
            break;
          case PathSeg::ClosePath:
            if (px!=sx || py!=sy) emit(sx,sy); // cairo closes with an explicit line
            px=sx; py=sy;
            break;
          }
      return r;
    }

  private:
    /// call emit(x,y) for the end point of each line segment approximating the curve s starting at (px,py)
    template <class F>
    static void flattenCurve(double px, double py, const PathSeg& s, double tolerance, F emit)
    {
      // second difference bound gives the number of steps needed
      auto norm=[](double x, double y) {return std::hypot(x,y);};
      const double m=std::max
        (norm(px-2*s.x[0]+s.x[1], py-2*s.y[0]+s.y[1]),
         norm(s.x[0]-2*s.x[1]+s.x[2], s.y[0]-2*s.y[1]+s.y[2]));
      const double tol=tolerance>0? tolerance: 0.1;
      const int n=std::clamp(int(std::ceil(std::sqrt(0.75*m/tol))), 1, 64);
      for (int i=1; i<=n; ++i)
        {
          const double t=double(i)/n, u=1-t;
          emit(u*u*u*px+3*u*u*t*s.x[0]+3*u*t*t*s.x[1]+t*t*t*s.x[2],
               u*u*u*py+3*u*u*t*s.y[0]+3*u*t*t*s.y[1]+t*t*t*s.y[2]);
        }
    }
  };
}

#endif
