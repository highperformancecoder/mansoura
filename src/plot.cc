/*
  @copyright Ravelation 2026
  @author Russell Standish
  This file is part of Masoura

  Open source licensed under the GPL3 license. See LICENSE for details.
*/

#include "plot.h"
#include "pango.h"
#include <limits>
#include <map>
#include <fstream>
#include <sstream>
using namespace std;

using namespace mansoura;

namespace
{
  inline double sqr(double x) {return x*x;}

  Colour palette[]=
    {
      {0,0,0,1}, //black
      {1,0,0,1}, //red
      {0,0.6,0,1}, //green
      {0,0,1,1}, //blue
      {1,0,1,1}, //magenta
      {0,1,1,1}, //cyan
      {1,0.647,0,1}, //orange
      {0.627,0.125,0.941,1} //purple
      
    };

  const int paletteSz = sizeof(palette)/sizeof(palette[0]);
}

namespace mansoura
{
  vector<double> Plot::LineStyle::dashPattern() const
  {
    vector<double> r;
    switch (dashStyle)
      {
      default:
      case solid:
        return r;
      case dash:
        r.push_back(5); r.push_back(2);
        return r;
      case dot:
        r.push_back(2); r.push_back(3);
        return r;
      case dashDot:
        r.push_back(5); r.push_back(2); r.push_back(2); r.push_back(2);
        return r;
      }
  }

}

namespace 
{
  // A Pango that wraps it's show() method into an identity context
  struct IPango
  {
    const IMansoura& mans;
    double scalex=1, scaley=1;
    unique_ptr<ICacheRender> fontRender;
    IPango(const IMansoura& mans, double scalex, double scaley): mans(mans), scalex(scalex), scaley(scaley), fontRender(mans.cachedRender({})) {}
    TextProperties properties(const TextProperties& tp) {
      fontRender=mans.cachedRender(tp);
      return tp;
    }
    TextProperties properties() const {return fontRender->properties();}
    TextExtents extents() const {return fontRender->extents();}
    void setAngle(double sz) {fontRender->setAngle(sz);}
    void setFontSize(double sz) {fontRender->setFontSize(sz);}
    void setMarkup(const std::string& markup) {fontRender->setMarkup(markup);}
    void setText(const std::string& markup) {fontRender->setText(markup);}
    void show() {
      if (!fontRender) return;
      PreserveContext stash(mans);
      mans.scale(scalex,scaley);
      fontRender->show();
    }
    // draw a translucent background to make the text "pop"
    void drawBackground() const {
      if (!fontRender) return;
      double x,y;
      mans.getCurrentPoint(x,y);
      {
        PreserveContext stash(mans);
        mans.setSourceRGBA(1,1,1,0.4);
        mans.translate(x,y);
        mans.scale(scalex,scaley);
        //mans.rotate(angle); // TODO!
        auto extents=fontRender->extents();
        mans.rectangle(0,0,extents.width,extents.height);
        mans.fill();
      }
      mans.moveTo(x,y);
    }
  };

  // stroke the current path using the default linewidth
  void stroke(const IMansoura& mans)
  {
    PreserveContext stash(mans);
    mans.identityMatrix();
    mans.stroke();
  }

 // displays a string represent ×10^n
  string orderOfMag(int n)
  {
    ostringstream label;
    if (abs(n)<=3)
      label<<"×"<<pow(10.0,n);
    else
      {
#if defined(PANGO) && !defined(_WIN32)
        label<<"×10<sup>"<<n<<"</sup>";
#else
        label<<"1E"<<n;
#endif
      }
    return label.str();
  }    

  // axis labels are either just the leading digit, or an order of
  // magnitude if log
  string logAxisLabel(double x)
  {
    char label[30];
    if (x<=0) return ""; // -ve values meaningless
        if (x>=0.01 && x<1)
          {
            snprintf(label,sizeof(label),"%.2f",x);
          }
        else if (x>=1 && x<=100)
          {
            snprintf(label,sizeof(label),"%.0f",x);
          }
        else
          {
            int omag=int(log10(x));
            int lead=x*pow(10,-omag);
#if defined(PANGO) && !defined(_WIN32)
           sprintf(label,"%d×10<sup>%d</sup>",lead,omag);
#else
           sprintf(label,"%dE%d",lead,omag);
#endif
          }
        return label;
  }


  void showOrderOfMag(IPango& pango, double scale, unsigned threshold)
  {
    if (scale<=0) return; // scale shouldn't be -ve, but just in case
    scale=floor(log10(scale));
    if (abs(scale)>=threshold-1)
      {
        auto tp=pango.properties();
        tp.markup=orderOfMag(scale);
        pango.properties(tp);
        pango.show();
       }
  }
  
  /*
    compute increment and tickoffset, returning the scale

    The idea is to get the tick marks to show integral values for plot neatness
  */
  void computeIncrementAndOffset
  (double minv, double maxv, int maxTicks, double& incr, double& offset)
  {
    double scale=pow(10.0, max(int(log10(maxv-minv)), int(0.25*log10(max(abs(minv), abs(maxv))))));
    offset = scale * int(minv/scale); 
    incr = scale;
    double d=maxv-minv;
    while (d < 3*incr)
      incr *= 0.1;
    if (d > 5*maxTicks*incr)
      incr *= 10;
    else if (d > 2*maxTicks*incr)
      incr *= 5;
    else if (d > maxTicks*incr)
      incr *= 2;
  }

  class LogScale
  {
    int div; //=2, 5 or 10
    int scaleIncr;
    double scale;
  public:
    double operator()(unsigned i) {
      return (i%div+1)*pow(10,scaleIncr*(i/div))/div;
    }

    LogScale(double minv, double maxv, int maxTicks): scaleIncr(1)
    {
      scale=pow(10.0, int(log10(minv)));
      
      double s=log10(maxv/scale) / maxTicks;
      if (s>1)
        {
          scaleIncr = s+1;
          div=1;
        }
      else if (s>0.5)
        div=2;
      else
        div=5;
    }
  };
      
  // adjust y bounds to handle pathological situations
  void adjustMinyMaxy(double& miny, double& maxy)
  {
    double amm=abs(maxy)+abs(miny);
    if (amm>10*numeric_limits<double>::min())
      {
        double dy=maxy-miny;
        if (abs(dy)>=1e-4*amm)
          {
            // adjust to make sure there is some space around edges 
            miny -= 0.1*dy;
            maxy += 0.1*dy;
          }
        else if (miny<0)
          {
            // val -ve, set the graph from 2*val to 0
            miny*=2;
            maxy=0;
          }
        else if (miny>0)
          {
            // set the graph from 0 to 2*val
            miny=0;
            maxy*=2;
          }
      }
    else
      {
        // 0 constant function
        miny=-1;
        maxy=1;
      }
  }

}

namespace mansoura
{
  Plot::Plot(): palette(paletteSz)
  {
    for (int i=0; i<paletteSz; ++i) palette[i].colour=::palette[i];
  }

//  int PlotSurface::width() const {return surface? surface->width():0;}
//  int PlotSurface::height() const {return surface? surface->height():0;}

  string Plot::axisLabel(double x, double scale, bool percent) const
  {
    char label[30];
    // change scale back to units
    int iscale=int(floor(log10(scale)));
    scale = pow(10.0, iscale);        
    if (unsigned(abs(iscale))+1>=exp_threshold)
      {
        int num=floor(x/scale+0.5);
        sprintf(label,"%d",num);
      }
    else
      {
        double num=scale*floor(x/scale+0.5);
        if (percent)
          sprintf(label,"%g%%",100*num);
        else
          sprintf(label,"%g",num);
      }

    return label;
  }

  void Plot::labelPen(unsigned pen, const string& label)
  {
    if (pen>=penTextLabel.size()) penTextLabel.resize(pen+1);
    penTextLabel[pen]=label;
  }

//  void Plot::LabelPen(unsigned pen, const cairo::SurfacePtr& surf)
//  {
//    if (pen>=penLabel.size()) penLabel.resize(pen+1);
//    penLabel[pen]=surf;
//  }

  

  void Plot::assignSide(unsigned pen, Side side)
  {
    if (pen>=penSide.size()) penSide.resize(pen+1,left);
    penSide[pen]=side;
  }

  void Plot::removePensFrom(unsigned pen)
  {
    x.resize(pen);
    y.resize(pen);
    decimation.resize(pen);
    decimate.resize(pen);
    redraw();
  }
  
  bool Plot::onlyMarkers() const
  {
    bool r=true;
    for (size_t i=0; i<x.size(); ++i)
      r &= x[i].empty() || (i<penSide.size() && penSide[i]==marker);
    return r;
  }

  void Plot::setMinMax()
  {
    assert(x.size()==y.size());
    
    // calculate min/max
    if (onlyMarkers())
      {
        minx=logx? 0.1: -1;
        maxx=1;
        miny1=miny=logy? 0.1: -1;
        maxy1=maxy=1;
      }
    else
      {
        minx=numeric_limits<double>::max();
        maxx=-numeric_limits<double>::max();
        miny=numeric_limits<double>::max();
        maxy=-numeric_limits<double>::max();
        miny1=numeric_limits<double>::max();
        maxy1=-numeric_limits<double>::max();
        
        for (size_t i=0; i<x.size(); ++i)
          {
            assert(x[i].size()==y[i].size());
            for (size_t j=0; j<x[i].size(); ++j)
              {
                if (isfinite(x[i][j]))
                  {
                    if (x[i][j]<minx) minx=x[i][j];
                    if (x[i][j]>maxx) maxx=x[i][j];
                  }
                if (isfinite(y[i][j]))
                  {
                    if (i<penSide.size() && penSide[i]==right)
                      {
                        if (y[i][j]<miny1) miny1=y[i][j];
                        if (y[i][j]>maxy1) maxy1=y[i][j];
                      }
                    else if (i>=penSide.size() || (i<penSide.size() && penSide[i]==left))
                      {
                        if (y[i][j]<miny) miny=y[i][j];
                        if (y[i][j]>maxy) maxy=y[i][j];
                      }
                  }
              }
          }
      }
    if (!logx)
      {
        double dx=maxx-minx;
        minx = dx>0? minx-0.1*dx: -1;
        maxx = dx>0? maxx+0.3*dx: 1;
      }

    adjustMinyMaxy(miny,maxy);
    adjustMinyMaxy(miny1,maxy1);

    // adjust minx, miny, miny1 etc if tickIncrement too small
    double tickIncrement, tick;
    computeIncrementAndOffset(minx, maxx, nxTicks, tickIncrement, tick);
    if ((maxx-tick)>100*tickIncrement) minx=tick;
    if (isfinite(miny) && isfinite(maxy))
      {
        computeIncrementAndOffset(miny, maxy, nyTicks, tickIncrement, tick);
        if ((maxy-tick)>100*tickIncrement) miny=tick;
      }
    if (isfinite(miny1) && isfinite(maxy1))
      {
        computeIncrementAndOffset(miny1, maxy1, nyTicks, tickIncrement, tick);
        if ((maxy1-tick)>100*tickIncrement) miny1=tick;
      }
  }

  void Plot::drawGrid
  (const IMansoura& mans, double tick, double increment, bool vertical, const XFY& xfy) const
  {
    // draw first subgrid if necessary
    double min=vertical? minx: xfy.o;
    if (tick > min && tick-increment<=min) 
      drawGrid(mans, tick-increment, increment, vertical, xfy);

    if (vertical)
      {
        mans.moveTo(iflogx(tick),xfy(xfy.o));
        mans.lineTo(iflogx(tick),xfy(displayLHSscale()? maxy: maxy1));
      }
    else
      {
        mans.moveTo(iflogx(minx),xfy(tick));
        mans.lineTo(iflogx(maxx),xfy(tick));
      }

    PreserveContext context(mans);
    mans.setSourceRGBA(0.5,0.5,0.5,0.5);
    mans.identityMatrix();
    mans.setLineWidth(1);
    mans.stroke();

    if (subgrid && increment>0)
      {
        // subgrid lines done in grey
        if (vertical)
          {
            double dx=0.1*increment;
            for (double x=tick+dx; x<tick+increment; x+=dx) //NOLINT
              {
                mans.moveTo(iflogx(x),xfy(miny));
                mans.lineTo(iflogx(x),xfy(maxy));
              }
          }
        else
          {
            double dy=0.1*increment;
            for (double y=tick+dy; y<tick+increment; y+=dy) //NOLINT
              {
                mans.moveTo(iflogx(minx),xfy(y));
                mans.lineTo(iflogx(maxx),xfy(y));
              }
          }
        
        
        PreserveContext context(mans);
        mans.setSourceRGBA(0.8,0.8,0.8,0.5);
        mans.identityMatrix();
        mans.setLineWidth(1);
        mans.stroke();
      }

  }

  void Plot::legendSize(const IMansoura& mans,double& xoffs, double& yoffs, double& width, double& height, double plotWidth, double plotHeight) const
  {
    yoffs=0.95*plotHeight;
    switch (legendSide)
      {
      default:
      case left:
        xoffs=0.1*plotWidth;
        break;
      case right:
        xoffs=0.9*plotWidth;
        break;
      case boundingBox:
        xoffs=legendLeft*plotWidth;
        yoffs=legendTop*plotHeight;
        break;
      }

    width=0; height=0;
    double fy=legendFontSz*fontScale*plotHeight;
    for (size_t i=0; i<x.size(); ++i)
      /*if (i<penLabel.size() && penLabel[i])
        {
          height += 1.3*penLabel[i]->height();
          width = max(width, penLabel[i]->width());
        }
        else*/
      if (i<penTextLabel.size() && !penTextLabel[i].empty())
        {
          auto extents=mans.textExtents({penTextLabel[i],"",fabs(fy)});
          height += 1.5*fy; //1.3*(p2.height());
          width = max(width, extents.width);
        }
  }
  
  void Plot::drawLegend(const IMansoura& mans, double w, double h) const
  {
    double dx=maxx-minx;
    double xoffs=0;
    // compute width of labels
    double width=0, height=0;
    double fy=legendFontSz*fontScale*h;
    double yoffs=0.95*h-0.8*fy;


    PreserveContext stash(mans);
    mans.translate(0, h);
    mans.scale(1,-1);

    legendSize(mans,xoffs,yoffs,width,height,w,h);
    double labeloffs=xoffs+legendOffset*w;

    if (height>0)
      {
        PreserveContext stash(mans);
        mans.rectangle(xoffs,yoffs,width+legendOffset*w,-height);
        mans.setSourceRGB(0,0,0);
        mans.strokePreserve();
        mans.setSourceRGBA(1,1,1,0.7);
        mans.fill();
      }
    yoffs-=0.8*fy;
    for (size_t i=0; i<x.size(); ++i)
      /*if (i<penLabel.size() && penLabel[i])
        {
          const LineStyle& ls=palette[i%palette.size()];
          cairo_set_source_rgba(cairo, ls.colour.r, ls.colour.g, ls.colour.b, ls.colour.a);
          cairo_set_line_width(cairo, ls.width);
          vector<double> dashPattern=ls.dashPattern();
          cairo_set_dash(cairo, dashPattern.data(), dashPattern.size(), 0);
          cairo_move_to(cairo, xoffs, yoffs);
          cairo_rel_line_to(cairo, 0.05*dx, 0);
          stroke(cairo);

          cairo::Surface& surf=*penLabel[i];

          /// translate and scale label graphic to fit where we want to put it
          cairo_pattern_t* pat=cairo_pattern_create_for_surface(surf.surface());
          cairo_matrix_t matrix;
          cairo_pattern_get_matrix(pat,&matrix);
          cairo_matrix_translate(&matrix, -surf.left(), surf.top());
          cairo_matrix_translate(&matrix, -labeloffs, -yoffs-0.5*surf.height());
          cairo_pattern_set_matrix(pat,&matrix);
          cairo_set_source(cairo, pat);

          // clip and copy pattern into the label location
          cairo_rectangle(cairo, labeloffs, 
                          yoffs-0.5*surf.height(), 
                          surf.width(), 
                          surf.height());
          cairo_fill(cairo);
          cairo_pattern_destroy(pat);

          // advance to next label
          yoffs-=1.5*surf.height();

        }
        else*/
      if (i<penTextLabel.size() && !penTextLabel[i].empty())
        {
          // render directly from text label
          const LineStyle& ls=palette[i%palette.size()];
          mans.setSourceRGBA(ls.colour.r, ls.colour.g, ls.colour.b, ls.colour.a);
          mans.setLineWidth(ls.width);
          vector<double> dashPattern=ls.dashPattern();
          mans.setDash(dashPattern.data(), dashPattern.size(), 0);

          mans.moveTo(xoffs, yoffs);
          mans.relLineTo(0.05*w, 0);
          stroke(mans);

          IPango p2(mans,1,-1);
          p2.properties({penTextLabel[i],"",fy});
          mans.moveTo(labeloffs, yoffs+0.8*fy);
          p2.show();
          // advance to next label
          yoffs-=1.5*fy;
        }
  }

  void Plot::labelAxes(const IMansoura& mans, double width, double height) const
  {
    double dy=maxy-miny;
    
    // axis label font size (in pixels)
    double lh=0.03*fontScale*(height-2*offy)/(1+0.03*fontScale);

    // work out the font size we should use
    //double fontSz=0.03*fontScale;
    //double fx=0, fy=fontSz*dy;
    //cairo_user_to_device_distance(cairo,&fx,&fy);

    PreserveContext stash(mans);
    mans.translate(0.5*width,0.5*height);
    if (!xlabel.empty())
      {
        auto pango=mans.cachedRender({xlabel,0.6*lh});
        auto extents=pango->extents();
        mans.moveTo(-0.5*extents.width,0.48*height-extents.height);
        pango->show();
      }
    
    if (!ylabel.empty())
      {
        TextProperties tp(ylabel,0.6*lh);
        tp.angle=-0.5*M_PI;
        auto pango=mans.cachedRender(tp);
        auto extents=pango->extents();
        mans.moveTo(offx-0.5*width+0.5*extents.height,0.5*extents.width);
        pango->show();
      }
    
    if (!y1label.empty())
      {
        TextProperties tp(y1label,0.6*lh);
        tp.angle=-0.5*M_PI;
        auto pango=mans.cachedRender(tp);
        auto extents=pango->extents();
        mans.moveTo(0.5*width-1.5*extents.height,0.5*extents.width);
        pango->show();
      }
  }          

//  void Plot::draw(cairo::Surface& surface)
//  {
//    cairo_identity_matrix(surface.cairo());
//    //cairo_translate(surface.cairo(),-0.5*surface.height(),-0.5*surface.width());
//    if (autoscale) setMinMax();
//    // if there is no size, set size to 1, so at least a bounding box is drawn
//    if (maxx<=minx) maxx=minx+1;
//    if (maxy<=miny) 
//      {
//        if (miny1<maxy1)
//          {
//            miny=miny1;
//            maxy=maxy1;
//          }
//        else
//          maxy=miny+1;
//      }
//    draw(surface.cairo(), surface.width(), surface.height());
//    cairo_surface_flush(surface.surface());
//  }

  double Plot::lh(double width, double height) const
  {
    return 0.05*fontScale*(height-2*offy)/(1+0.03*fontScale);
  }

  void Plot::draw(const IMansoura& mans, double width, double height) const
  {  
    const char* errMsg=NULL;
    if (logx && minx<=0)
      errMsg = "logx requires positive range";
    //else if (logy && ((displayLHSscale() && miny<=0) || (displayRHSscale() && miny1<=0)))
        //errMsg = "logy requires positive range";
    // ! used here, rather than reverse comparison to allow for NaNs, which always compare false
    else if (!(maxx>minx) || (!displayLHSscale() && !displayRHSscale()))
      errMsg = "no data";

    if (errMsg)
      {
        auto pango=mans.cachedRender({errMsg});
        auto extents=pango->extents();
        mans.moveTo(0.5*(width-extents.width),0.5*(height-extents.height));
        pango->show();
        return;
      }
      
    double dx=iflogx(maxx)-iflogx(minx);
    auto mm=miny, mm1=miny1;
    
    if (logy) // calculate miny from the minimum positive value
      {
        mm=maxy, mm1=maxy1;
        for (size_t pen=0; pen<y.size(); ++pen)
          if (pen>=penSide.size() || penSide[pen]==left)
            {
              for (auto j: y[pen])
                if (j>0 && j<mm)
                  mm=j;
            }
          else
            for (auto j: y[pen])
              if (j>0 && j<mm1)
                mm1=j;
      }
    double dy=iflogy(maxy)-iflogy(mm);
    double dy1=iflogy(maxy1)-iflogy(mm1);
        
    // axis label font size (in pixels)
    // offsets to allow space for axis labels
    double loffx=lh(width,height)*!ylabel.empty(), 
      loffx1=lh(width,height)*!y1label.empty(), 
      loffy=lh(width,height)*!xlabel.empty();
    

    
    labelAxes(mans,width,height);

    {
      PreserveContext stash(mans);
      auto w=width-offx-loffx-loffx1;
      auto h=height-2*offy-loffy;
      double sx=w/dx, sy=displayLHSscale()? h/dy: h/dy1;
      double rhsScale = displayLHSscale()? dy/dy1: 1;

      // work out the font size we should use
      double fontSz=0.02*fontScale;
      IPango pango(mans,1/sx,-1);

      pango.setFontSize(0.6*fontSz*h);
    
      mans.translate(offx+loffx-iflogx(minx)*sx, h);

      // NB do not use cairo scaling in y direction, but rather manually scale before passing to cairo.
      // See ticket #693
      mans.scale(sx, -1);
      mans.newPath();

      mans.setSourceRGBA(0,0,0,1); //black
      mans.rectangle(iflogx(minx),0,dx,h);
      mans.clipPreserve();
      mans.stroke();
      

      mans.setSourceRGBA(0, 0, 0, 1);



      XFY aff(logy, sy, displayLHSscale()? mm: mm1, 0); //manual affine transform - see ticket #693
      double rightMargin=0.05*dx;

      if (logy)
        {
          if (displayLHSscale())
            {
              LogScale ls(mm, maxy, nyTicks);
              int i=0;
              for (double ytick=ls(0); ytick<maxy; i++, ytick=ls(i)) //NOLINT
                if (aff(ytick)>=fontSz*h)
                  {
                    pango.setMarkup(logAxisLabel(ytick));
                    mans.newPath();
                    mans.moveTo(iflogx(minx),aff(ytick));
                    mans.lineTo(iflogx(minx)+fontSz*dx,aff(ytick));
                    stroke(mans);
                    mans.moveTo(iflogx(minx),aff(ytick));
                    pango.show();
                    if (grid)
                      drawGrid(mans, ytick, ls(i+1)-ytick, false, aff);
                  }
            }
          if (displayRHSscale())
            {
              LogScale ls(mm1, maxy1, nyTicks);
              int i=0;
              for (double ytick=ls(0); ytick<maxy1; i++, ytick=ls(i)) //NOLINT
                if (aff(ytick)>=fontSz*h)
                  {
                    pango.setMarkup(logAxisLabel(ytick));
                  
                    mans.newPath();
                    double yt=aff((ytick-mm1)*rhsScale+aff.o);
                    mans.moveTo(maxx,yt);
                    mans.lineTo(minx+0.95*dx,yt);
                    stroke(mans);
                    auto extents=pango.extents();
                    mans.moveTo(maxx-(extents.width*fontSz*dx)/extents.height-rightMargin,yt);
                    pango.show();
                    if (grid && !displayLHSscale())
                      drawGrid(mans, ytick, ls(i+1)-ytick, false, aff);
                  }
            }
        }
      else
        {
          double ytickIncrement, ytick;
          if (displayLHSscale())
            {
              computeIncrementAndOffset(mm, maxy, nyTicks, ytickIncrement, ytick);
              if (ytickIncrement<=0) return; //avoid infinite loop

              mans.moveTo(minx+0.01*dx, aff(maxy));
              showOrderOfMag(pango, ytickIncrement, exp_threshold);
              
              for (; ytick<maxy; ytick+=ytickIncrement) //NOLINT
                if (aff(ytick)>=fontSz*h)
                  {
                    pango.setMarkup(axisLabel(ytick,ytickIncrement,percent));

                    mans.newPath();
                    mans.moveTo(iflogx(minx),aff(ytick));
                    mans.lineTo(iflogx(minx)+fontSz*dx,aff(ytick));
                    stroke(mans);
                    mans.moveTo(iflogx(minx),aff(ytick));
                    pango.show();
                    if (grid)
                      drawGrid(mans, ytick, ytickIncrement, false, aff);
                  }
            }

          if (displayRHSscale())
            {
              // draw scale on right hand side
              computeIncrementAndOffset(mm1, maxy1, nyTicks, ytickIncrement, ytick);
              if (ytickIncrement<0) return; //avoid infinite loop

              pango.setText("XXXXX");
              auto extents=pango.extents();
              mans.moveTo(maxx-(extents.width*fontSz*dx)/extents.height-rightMargin, aff(maxy));
              showOrderOfMag(pango, ytickIncrement, exp_threshold);

              for (; ytick<maxy1; ytick+=ytickIncrement) //NOLINT
                if (ytick>=mm1+fontSz*dy1)
                  {
                    pango.setMarkup(axisLabel(ytick,ytickIncrement,percent));

                    mans.newPath();
                    double yt=aff((ytick-mm1)*rhsScale+aff.o);
                    mans.moveTo(maxx,yt);
                    mans.lineTo(minx+0.95*dx,yt);
                    stroke(mans);
                    auto extents=pango.extents();
                    mans.moveTo(maxx-(extents.width*fontSz*dx)/extents.height-rightMargin,yt);
                    pango.show();
                    if (grid && !displayLHSscale())
                      drawGrid(mans, ytick, ytickIncrement, false, aff);
                  }
            }
        }
    
      if (!x.empty())
        for (size_t i=0; i<x.size(); ++i)
          {
            if (x[i].empty()) continue;
            const LineStyle& ls=palette[i%palette.size()];

            // transform y coordinates (handles RHS being a different scale)
            XFY xfy=aff;
            Side side=left;
            if (i<penSide.size() && penSide[i]==right)
              {
                xfy.scale*=rhsScale;
                xfy.o=mm1;
                side=right;
              }

            auto plotType=ls.plotType==automatic? this->plotType: ls.plotType;
            switch (plotType)
              {
              case line:
              case line_scatter:
                    if (x[i].size()>1)
                    {
                      PreserveContext cs(mans);
                      mans.setSourceRGBA(ls.colour.r, ls.colour.g, ls.colour.b, ls.colour.a);
                      mans.setLineWidth(ls.width);
                      vector<double> dashPattern=ls.dashPattern();
                      mans.setDash(dashPattern.data(), dashPattern.size(), 0);

                      mans.newPath();
                      auto xfyyij=xfy(y[i][0]);
                      if (isfinite(xfyyij))
                        mans.moveTo(iflogx(x[i][0]), xfy(y[i][0]));
                      for (size_t j=1; j<x[i].size(); ++j)
                        {
                          xfyyij=xfy(y[i][j]);
                          if (inBounds(x[i][j-1], y[i][j-1], side) && inBounds(x[i][j], y[i][j], side))
                            mans.lineTo(iflogx(x[i][j]), xfyyij);
                          else if (isfinite(xfyyij))
                            mans.moveTo(iflogx(x[i][j]), xfyyij);
                        }

                      // markers don't need the leading marker.
                      if (leadingMarker && (i>=penSide.size() || penSide[i]!=marker) && isfinite(xfy(y[i].back())))
                        mans.rectangle
                          (iflogx(x[i].back()), xfy(y[i].back()), 
                           0.01*dx,  0.01*dy*sy);
                      stroke(mans);
                    }
                    break;
                  case bar:
                    {
                      // make bars translucent - see Minsky ticket #893
                      Colour barColour={ls.colour.r, ls.colour.g, ls.colour.b, 0.5*ls.colour.a};
                      Colour shadow={0.4, 0.4, 0.6, 0.025};

                      auto showBox=[&](double x, double y, double w, double h, Colour& c) {
                        mans.setSourceRGBA(c.r, c.g, c.b, c.a);
                        mans.rectangle(x, y, w, h);
                        mans.fill();
                      };
                      auto shadowShowBox=[&](double x, double y, double w) {
                        PreserveContext cs(mans);
                        showBox(x,0,w,y,barColour);
                        // outline box element
                        if (w*sx>10)
                          {
                            {
                              PreserveContext cs(mans);
                              mans.scale(1/sx,1);
                              mans.setLineWidth(1);
                              mans.rectangle(x*sx,0,w*sx,y);
                              mans.setSourceRGB(barColour.r, barColour.g, barColour.b);
                              mans.stroke();
                            }
                            // use a clip path to mask out the bar from the shadow
                            mans.moveTo(x,y);
                            mans.relLineTo(w,0);
                            mans.relLineTo(0,-y);
                            mans.relLineTo(10/sx,0);
                            mans.relLineTo(0,y+10);
                            mans.relLineTo(-w-10/sx,0);
                            mans.clip();
                            // emulate blur by overlapping multiple drop shadows
                            for (double i=3; i<8; i+=0.5)
                              showBox(x+i/sx,0,w,y+i,shadow);
                          }
                      };
                      
                      // compute minimum gap between successive points to determine bar width
                      double w=iflogx(maxx)-iflogx(minx);
                      for (size_t j=1; j<x[i].size(); ++j)
                        w=std::min(w, iflogx(x[i][j])-iflogx(x[i][j-1]));
                      w*=ls.barWidth;

                      size_t j=0;
                      if (inBounds(iflogx(x[i][j]), y[i][j], side))
                        {
                          auto xx=x[i].size()>1? iflogx(x[i][0])-0.5*w: iflogx(minx);
                          shadowShowBox(xx,xfy(y[i][0]),w);
                        }
                      for (++j; j<x[i].size()-1; ++j)
                        if (inBounds(iflogx(x[i][j]), y[i][j], side))
                          shadowShowBox(iflogx(x[i][j])-0.5*w, xfy(y[i][j]), w);
                      if (x[i].size()>1 && inBounds(iflogx(x[i][j]), y[i][j], side))
                        shadowShowBox(iflogx(x[i][j])-0.5*w, xfy(y[i][j]), w);
                    }
              default: break;
              }
            // now handle scatter options
            switch (plotType)
              {
              case scatter: case line_scatter:
                {
                  PreserveContext cs(mans);
                  mans.scale(1/sx,1);
                  mans.setSourceRGBA(ls.colour.r, ls.colour.g, ls.colour.b, ls.colour.a);
                  for (size_t j=0; j<x[i].size(); j+=symbolEvery)
                    if (inBounds(iflogx(x[i][j]), y[i][j], side))
                      {
                        mans.arc(sx*iflogx(x[i][j]), xfy(y[i][j]), 1.5*ls.width, 0, 2*M_PI);
                        mans.fill();
                      }
                }
                break;
              default: break;
              }
          }

      if (xticks.size())
        {
          unsigned startTick=0, endTick;
          for (startTick=0; startTick<xticks.size() && xticks[startTick].first < minx; startTick++);
          for (endTick=startTick; endTick<xticks.size() && xticks[endTick].first < maxx; endTick++);
          unsigned tickIncr=(endTick-startTick)/nxTicks+1;
          double xtick=0, incr=0;
          pango.setAngle(xtickAngle*M_PI/180.0);
          for (unsigned i=startTick; i<endTick; i+=tickIncr)
            {
              const std::pair<double,std::string>& xt=xticks[i];
              xtick=logx? log10(xt.first): xt.first;
              mans.newPath();
              mans.moveTo(xtick,0);
              mans.lineTo(xtick,fontSz*h);
              stroke(mans);
              mans.moveTo(xtick,fontSz*h*2);
              pango.setMarkup(xt.second);
              pango.drawBackground();
              pango.show();
              if (grid)
                {
                  if (i<xticks.size()-tickIncr)
                    {
                      double nextX=xticks[i+tickIncr].first;
                      if (logx) nextX=log10(nextX);
                      incr=fabs(nextX-xtick);
                    }
                  drawGrid(mans, xtick, incr, true, aff);
                }
            }
          pango.setAngle(0);
          if (grid && incr) // extend grid all the way
            while ((xtick+=incr)<maxx)
              drawGrid(mans, xtick, incr, true, aff);
        }
      else if (logx)
        {
          LogScale ls(minx, maxx, nxTicks);
          int i=0;
          for (double xtick=ls(0); xtick<maxx; i++, xtick=ls(i)) //NOLINT
            if (xtick>=minx)
              {
                pango.setMarkup(logAxisLabel(xtick));
                mans.newPath();
                mans.moveTo(log10(xtick),0);
                mans.lineTo(log10(xtick),fontSz*h);
                stroke(mans);
                mans.moveTo(log10(xtick),fontSz*h*2);
                pango.drawBackground();
                pango.show();

                if (grid)
                  drawGrid(mans, xtick, ls(i+1)-xtick, true, aff);
              }
        }
      else
        {
          double xtickIncrement, xtick;
          computeIncrementAndOffset(minx, maxx, nxTicks, xtickIncrement, xtick);
          if (xtickIncrement<0) return; //avoid infinite loop
          mans.moveTo(maxx-0.05*dx*(1+fontScale), aff(mm+0.04*dy*(1+fontScale)));
          showOrderOfMag(pango, xtickIncrement, exp_threshold);

          for (; xtick<maxx; xtick+=xtickIncrement) //NOLINT
            if (xtick>=minx)
              {
                pango.setMarkup(axisLabel(xtick,xtickIncrement));

                mans.newPath();
                mans.moveTo(xtick,aff(mm));
                mans.lineTo(xtick,aff(mm)+fontSz*h);
                stroke(mans);
                mans.moveTo(xtick,aff(mm)+fontSz*h*2);
                pango.drawBackground();
                pango.show();

                if (grid)
                  drawGrid(mans, xtick, xtickIncrement, true, aff);
              }
        }

      // display value near mouse pointer
      if (!valueString.empty())
      {
        PreserveContext cs(mans);
        mans.setSourceRGB(0,0,0);

        // if on RHS of plot, send label leftwards
        auto mx=iflogx(mouseX); // convert to user coordinates;
        XFY xfy=aff;
        if (mousePen<penSide.size() && penSide[mousePen]==right)
          {
            xfy.scale*=rhsScale;
            xfy.o=mm1;
          }
        auto my=xfy(mouseY);
        // put a marker where the found location is (mouse can be slightly different)
        const LineStyle& ls=palette[mousePen%palette.size()];
        mans.setSourceRGBA(ls.colour.r, ls.colour.g, ls.colour.b, 0.5*ls.colour.a);
        {
          PreserveContext cs(mans);
          mans.scale(1/sx,1);
          mans.arc(sx*mx,my,1.5*ls.width,0,2*M_PI);
          mans.fill();
        }

        pango.setMarkup(valueString);
        // slight offsets to avoid obscuring the marker
        auto extents=pango.extents();
        mx+=0.1*extents.width/sx;
        if (mouseX>0.5*(minx+maxx)) mx-=1.2*extents.width/sx;
        mans.rectangle(mx,my,extents.width/sx,extents.height);
        mans.setSourceRGB(1,1,1);
        mans.fill();
        mans.setSourceRGB(0,0,0);
        mans.moveTo(mx,my+extents.height);
        pango.show();
      }
    }

    if (legend) drawLegend(mans,width,height);
  }
  
  void Plot::clear()
  {
    x.clear(); y.clear();
    decimation.clear(); decimate.clear();
    redraw();
  }

  void Plot::addPt(unsigned pen, double xx, double yy)
  {
    checkAddPen(pen);
    if (decimate[pen]==0)
      {
        x[pen].push_back(xx);
        y[pen].push_back(yy);
        if (x[pen].size()>maxPoints)
          {
            decimation[pen]*=2;
            for (size_t i=0; i<x[pen].size(); i+=2)
              {
                x[pen][i/2]=x[pen][i];
                y[pen][i/2]=y[pen][i];
              }
            x[pen].resize(x[pen].size()/2);
            y[pen].resize(y[pen].size()/2);
          }
      }
    if (++decimate[pen]>=decimation[pen])
      decimate[pen]=0;
  }

//  void Plot::add(cairo::Surface& surf, unsigned pen, 
//                 double x1, double y1)
//  {
//    addPt(pen,x1,y1);
//
//    addNew(surf, x1<minx || x1>maxx || y1<miny || y1>maxy, &pen, &pen+1, 1);
//  }
//
//  void Plot::add(cairo::Surface& surf, const array<unsigned>& pens, 
//                 double x1, const array<double>& y1)
//  {
//    bool doRedraw=x1<minx||x1>maxx;
//    unsigned maxPen=max(pens);
//    checkAddPen(maxPen);
//    for (size_t p=0; p<y1.size(); ++p)
//      {
//        unsigned pen=pens[p];
//        double yy=y1[pen];
//        doRedraw|=yy<miny || yy>maxy;
//        x[pen].push_back(x1);
//        y[pen].push_back(yy);
//      }
//    addNew(surf, doRedraw, pens.begin(), pens.end(), 1);
//  }      

  string stripPangoMarkup(const string& markedUptext)
  {
    char *tmp=nullptr;
    if (pango_parse_markup(markedUptext.c_str(),-1,0,nullptr,&tmp,nullptr,nullptr))
      {
        string ret(tmp);
        g_free(tmp);
        return ret;
      }
    return markedUptext; // failed to parse, return original text
  }

  struct Round
  {
    double scale, iscale;
    // round data to 5 significant places
    Round(const Plot& p): scale(1e5/(p.maxx-p.minx)), iscale(1/scale) {}
    double operator()(double x) {return iscale*round(x*scale);}
  };
  
  void Plot::exportAsCSV(const std::string& filename, const string& separator) const
  {
    map<double,string> tickLabels(xticks.begin(),xticks.end());

    Round round(*this);
    
    // assemble the data into a map, keyed by the x values
    map<double,vector<double>> values;
    for (size_t i=0, j=0; i<x.size(); ++i)
      if (!x[i].empty())
        {
          for (size_t k=0; k<x[i].size(); ++k)
            {
              auto& v=values[round(x[i][k])]; // rounding used to merge neighbouring x values
              v.resize(j+1,nan("")); // use NaN to represent missing values
              v[j]=y[i][k];
            }
          j++;
        }

    ofstream of(filename.c_str());
    of<<"#"<<ylabel<<" by "<<xlabel<<endl;
    of<<"#x";
    if (!xticks.empty())
      of<<separator<<"x-label";
    for (size_t i=0; i<x.size(); ++i)
      if (!x[i].empty())
        {
          if (i<penTextLabel.size() && !penTextLabel[i].empty())
            of<<separator << stripPangoMarkup(penTextLabel[i]);
          else
            of<<separator <<"y"<<i;
        }

    of<<endl;

    for (auto i: values)
      {
        of<<i.first;
        if (!xticks.empty())
          {
            of<<separator;
            auto iter=tickLabels.lower_bound(i.first);
            if (iter==tickLabels.end()) --iter;
            // select label closest to i.first
            auto prior=iter; if (prior!=tickLabels.begin()) --prior;
            if (abs(i.first-prior->first)<abs(i.first-iter->first)) iter=prior;
            of<<iter->second;
          }
        for (auto j: i.second)
          {
            of<<separator;
            if (isfinite(j)) of<<j;
          }
        of<<endl;
      }
    if (!of)
      throw runtime_error("exporting to "+filename+" failed");
  }

  string Plot::defaultFormatter(const string& label, double x, double y)
  {
    ostringstream r;
    r.precision(3);
    r<<label<<":("<<x<<","<<y<<")";
    return r.str();
  }
    
  bool Plot::mouseMove(double mx, double my, double tolerance, Formatter formatter)
  {
    // user coordinates
    double xu,yu,yu1;
    if (logx)
      xu=minx*exp(mx*log(maxx/minx));
    else
      xu=mx*(maxx-minx)+minx;
    if (logy)
      {
        // calculate miny from the minimum positive value
        auto mm=maxy, mm1=maxy1;
        for (size_t pen=0; pen<y.size(); ++pen)
          if (pen>=penSide.size() || penSide[pen]==left)
            {
              for (auto j: y[pen])
                if (j>0 && j<mm)
                  mm=j;
            }
          else
            for (auto j: y[pen])
              if (j>0 && j<mm1)
                mm1=j;
        yu=mm*exp(my*log(maxy/mm));
        yu1=mm1*exp(my*log(maxy1/mm1));
      }
    else
      {
        yu=my*(maxy-miny)+miny;
        yu1=my*(maxy1-miny1)+miny1;
      }
    
    double xp=std::numeric_limits<double>::max(), yp=xp;
    double mind=std::numeric_limits<double>::max();
    string penLabel;
    assert(x.size()==y.size());
    for (size_t pen=0; pen<x.size(); ++pen)
      {
        assert(x[pen].size()==y[pen].size());
        auto side=pen<penSide.size()? penSide[pen]: left;
        for (size_t i=0; i<x[pen].size(); ++i)
          {
            double dy = y[pen][i] - (side==left? yu: yu1);
            double sy = side==left? maxy-miny: maxy1-miny1;
            if (abs(x[pen][i]-xu) >= tolerance*(maxx-minx) ||
                abs(dy) >= tolerance*sy)
              continue; // ignore any data outside of tolerance
            double d=sqr(x[pen][i]-xu)+sqr(dy);
            if (d<mind)
              {
                xp=x[pen][i];
                yp=y[pen][i];
                penLabel=pen<penTextLabel.size()? penTextLabel[pen]: "";
                mousePen=pen;
                mind=d;
              }
          }
      }
    string valueString1;
    if (xp<maxx)
      valueString1=formatter(penLabel,xp,yp);
    if (valueString1!=valueString)
      {
        valueString=valueString1;
        mouseX=xp;
        mouseY=yp;
        return true;
      }
    return false;
  }
}
