#include "mansouraCairo.h"
#define CAIRO_WIN32_STATIC_BUILD
#include <cairo.h>
#undef CAIRO_WIN32_STATIC_BUILD
#include <pango.h>
#include <librsvg/rsvg.h>
#include <cairo/cairo-ps.h>
#include <cairo/cairo-pdf.h>
#include <cairo/cairo-svg.h>
#ifdef _WIN32
#include <windows.h>
#include <wingdi.h>
#include <cairo/cairo-win32.h>
#endif
#include <stdexcept>

// if not #ifdef protected, you get a deprecated warning, which is
// made fatal by -Werror
#ifndef RSVG_CAIRO_H
#include <librsvg/rsvg-cairo.h>
#endif

using namespace std;

namespace mansoura
{
  const char *Pango::defaultFamily=nullptr;
  double Pango::scaleFactor=1;

  MansouraCairo::MansouraCairo(cairo_t* c) : cairo(c) {}
  
  MansouraCairo::~MansouraCairo() = default;

  // Drawing operations
  void MansouraCairo::moveTo(double x, double y) const
  {cairo_move_to(cairo,x,y);}

  void MansouraCairo::lineTo(double x, double y) const
  {cairo_line_to(cairo,x,y);}

  void MansouraCairo::relMoveTo(double x, double y) const
  {cairo_rel_move_to(cairo,x,y);}

  void MansouraCairo::relLineTo(double x, double y) const
  {cairo_rel_line_to(cairo,x,y);}

  void MansouraCairo::arc(double x, double y, double radius, double start, double end) const
  {cairo_arc(cairo,x,y,radius,start,end);}

  void MansouraCairo::curveTo(double x1, double y1, double x2, double y2, double x3, double y3) const
  {cairo_curve_to(cairo,x1,y1,x2,y2,x3,y3);}

  void MansouraCairo::rectangle(double x, double y, double width, double height) const
  {cairo_rectangle(cairo,x,y,width,height);}

  // Path operations
  void MansouraCairo::newPath() const
  {cairo_new_path(cairo);}

  void MansouraCairo::newSubPath() const
  {cairo_new_sub_path(cairo);}

  void MansouraCairo::closePath() const
  {cairo_close_path(cairo);}

  void MansouraCairo::getCurrentPoint(double& x, double& y) const
  {cairo_get_current_point(cairo, &x, &y);}

  // Fill and stroke operations
  void MansouraCairo::fill() const
  {cairo_fill(cairo);}

  void MansouraCairo::fillPreserve() const
  {cairo_fill_preserve(cairo);}
  
  void MansouraCairo::clip() const
  {cairo_clip(cairo);}

  void MansouraCairo::clipPreserve() const
  {cairo_clip_preserve(cairo);}

  void MansouraCairo::resetClip() const
  {cairo_reset_clip(cairo);}

  void MansouraCairo::stroke() const
  {cairo_stroke(cairo);}
  
  void MansouraCairo::strokePreserve() const
  {cairo_stroke_preserve(cairo);}

  void MansouraCairo::paint() const
  {cairo_paint(cairo);}

  // Line properties
  void MansouraCairo::setLineWidth(double w) const
  {cairo_set_line_width(cairo, w);}

  double MansouraCairo::getLineWidth() const
  {return cairo_get_line_width(cairo);}

  void MansouraCairo::setDash(const double* dashes, int num_dashes, double offset) const
  {cairo_set_dash(cairo, dashes, num_dashes, offset);}

  void MansouraCairo::setFillRule(cairo_fill_rule_t fill_rule) const
  {cairo_set_fill_rule(cairo, fill_rule);}

  // Color operations
  void MansouraCairo::setSourceRGB(double r, double g, double b) const
  {cairo_set_source_rgb(cairo,r,g,b);}

  void MansouraCairo::setSourceRGBA(double r, double g, double b, double a) const
  {cairo_set_source_rgba(cairo,r,g,b,a);}

  // Text operations
  void MansouraCairo::initPango(Pango& pango,const TextProperties& tp) const
  {
    if (!tp.markup.empty())
      pango.setMarkup(tp.markup);
    else if (!tp.plainText.empty())
      pango.setText(tp.plainText);
    pango.angle=tp.angle;
    if (isfinite(tp.fontSize))
      pango.setFontSize(tp.fontSize);
    if (!tp.fontFamily.empty())
      pango.setFontFamily(tp.fontFamily.c_str());
  }
  
  void MansouraCairo::showText(const TextProperties& tp) const
  {
    if (tp.markup.empty() && tp.plainText.empty()) return;
    Pango pango(cairo);
    initPango(pango,tp);
    pango.show();
  }

  TextExtents MansouraCairo::textExtents(const TextProperties& tp) const
  {
    Pango pango(cairo);
    initPango(pango,tp);
    return {pango.left(), pango.top(), pango.width(), pango.height()};
  }

  // Transformation operations
  void MansouraCairo::identityMatrix() const
  {cairo_identity_matrix(cairo);}
  
  void MansouraCairo::translate(double x, double y) const
  {cairo_translate(cairo,x,y);}

  void MansouraCairo::scale(double sx, double sy) const
  {cairo_scale(cairo,sx,sy);}

  void MansouraCairo::rotate(double angle) const
  {cairo_rotate(cairo,angle);}

  void MansouraCairo::userToDevice(double& x, double& y) const
  {cairo_user_to_device(cairo, &x, &y);}

  namespace
  {
    class CairoPath: public IPath
    {
      cairo_path_t* path;
      cairo_matrix_t transformation;
    public:
      CairoPath(cairo_t* cr): path(cairo_copy_path(cr))
      {cairo_get_matrix(cr,&transformation);}
      ~CairoPath() override {cairo_path_destroy(path);}
      CairoPath(const CairoPath&)=delete;
      void operator=(const CairoPath&)=delete;
      void appendToCurrent(const IMansoura& ctx) const override
      {
        auto cr=static_cast<cairo_t*>(ctx.context());
        // apply saved transformation along with path
        cairo_save(cr);
        cairo_set_matrix(cr,&transformation);
        cairo_append_path(cr,path);
        cairo_restore(cr);
      }
    };
  }

  std::unique_ptr<IPath> MansouraCairo::copyPath() const
  {return std::make_unique<CairoPath>(cairo);}

  std::vector<std::pair<double,double>> MansouraCairo::flattenedPathPoints() const
  {
    std::vector<std::pair<double,double>> r;
    auto path=cairo_copy_path_flat(cairo);
    for (int j=0; j<path->num_data; j+=path->data[j].header.length)
      {
        auto data=&path->data[j];
        if (data->header.type==CAIRO_PATH_LINE_TO)
          r.emplace_back(data[1].point.x,data[1].point.y);
      }
    cairo_path_destroy(path);
    return r;
  }

  // Context state operations
  void MansouraCairo::save() const
  {cairo_save(cairo);}

  void MansouraCairo::restore() const
  {cairo_restore(cairo);}

  // Tolerance
  void MansouraCairo::setTolerance(double tolerance) const
  {cairo_set_tolerance(cairo, tolerance);}

  // SVG rendering support
  void MansouraCairo::renderSVG(const SVGRenderer& svgRenderer, double width, double height) const
  {
    if (!svgRenderer.svg) return;
#ifdef MXE // MXE doesn't currently have a Rust compiler, so librsvg can be no later than 2.40.21
    RsvgDimensionData dims;
    rsvg_handle_get_dimensions(svgRenderer.svg, &dims);
    cairo_scale(cairo, width/dims.width, height/dims.height);
    rsvg_handle_render_cairo(svgRenderer.svg, cairo);
#else
    GError* err=nullptr;
    const RsvgRectangle rect{0,0,width,height};
    rsvg_handle_render_document(svgRenderer.svg, cairo, &rect, &err);
    if (err)
      g_error_free(err);
#endif
  }

  struct PangoCache: public ICacheRender, public Pango
  {
    TextProperties m_properties;
    PangoCache(cairo_t* cr, const TextProperties& tp):
      Pango(cr), m_properties(tp)
    {properties(tp);}                                                 
    void show() override {Pango::show();}
    TextExtents extents() const override
    {return TextExtents{left(),top(),width(),height()};}
      
    void* context() const override
    {return cairoContext();}
    double fontSize() const override {return getFontSize();}
    std::string::size_type posToIdx(double x) const override {return Pango::posToIdx(x);}
    double idxToPos(std::string::size_type idx) const override {return Pango::idxToPos(idx);}
    TextProperties properties() const override
    {return m_properties;}
    TextProperties properties(const TextProperties& tp) override
    {
      if (!tp.markup.empty())
        Pango::setMarkup(tp.markup);
      else
        Pango::setText(tp.plainText);
      Pango::setFontSize(tp.fontSize);
      if (!tp.fontFamily.empty())
        Pango::setFontFamily(tp.fontFamily.c_str());
      Pango::angle=tp.angle;
      return tp;
    }
  };
  
  std::unique_ptr<ICacheRender>
  MansouraCairo::cachedRender(const TextProperties& tp) const
  {return make_unique<PangoCache>(cairo,tp);}

  const IMansoura& measuringContext()
  {
    struct Measuring
    {
      cairo_surface_t* surf=cairo_image_surface_create(CAIRO_FORMAT_A1, 100, 100);
      cairo_t* cr=cairo_create(surf);
      MansouraCairo shim{cr};
      ~Measuring() {cairo_destroy(cr); cairo_surface_destroy(surf);}
    };
    static Measuring m;
    return m.shim;
  }

  namespace
  {
    /// a MansouraCairo that owns its surface and context
    class OwningMansoura: public MansouraCairo
    {
      cairo_surface_t* surf;
      cairo_t* cr;
      OwningMansoura(cairo_surface_t* s, cairo_t* c): MansouraCairo(c), surf(s), cr(c) {}
    public:
      static std::unique_ptr<OwningMansoura> create(cairo_surface_t* s)
      {
        if (cairo_surface_status(s)!=CAIRO_STATUS_SUCCESS)
          {
            std::string msg=cairo_status_to_string(cairo_surface_status(s));
            cairo_surface_destroy(s);
            throw std::runtime_error("cairo rendering error: "+msg);
          }
        return std::unique_ptr<OwningMansoura>(new OwningMansoura(s,cairo_create(s)));
      }
      ~OwningMansoura() {cairo_destroy(cr); cairo_surface_destroy(surf);}
      cairo_surface_t* surface() const {return surf;}
    };

    void checkStatus(cairo_surface_t* s)
    {
      auto status=cairo_surface_status(s);
      if (status!=CAIRO_STATUS_SUCCESS)
        throw std::runtime_error(std::string("cairo rendering error: ")+cairo_status_to_string(status));
    }

#ifdef _WIN32
    struct SurfAndDC
    {
      HDC hdc;
      cairo_surface_t* surf;
    };
    
    cairo_user_data_key_t closeKey;
    void closeFile(void *x)
    {
      SurfAndDC* s(static_cast<SurfAndDC*>(x));
      cairo_surface_flush(s->surf);
      // nb the Delete... function deletes the handle created by Close...
      DeleteEnhMetaFile(CloseEnhMetaFile(s->hdc));
      DeleteDC(s->hdc);
      delete s;
    }
    
    cairo_surface_t* createEMFSurface(const char* filename, double width, double height)
    {
      HDC hdc=CreateEnhMetaFileA(nullptr,filename,nullptr,"Minsky\0");
      cairo_surface_t* surf=cairo_win32_surface_create_with_format(hdc,CAIRO_FORMAT_ARGB32);
      // initialise the image background 
      cairo_t* cairo=cairo_create(surf);
      // extend the background a bit beyond the bounding box to allow
      // for additional black stuff inserted by GDI
      cairo_rectangle(cairo,0,0,width+50,height+50);
      cairo_set_source_rgb(cairo,1,1,1);
      cairo_clip_preserve(cairo);
      cairo_fill(cairo);
      cairo_destroy(cairo);
      // set up a callback to flush and close the EMF file
      cairo_surface_set_user_data(surf,&closeKey,new SurfAndDC{hdc,surf},closeFile);
      return surf;
    }
#endif

    cairo_surface_t* pngDummy(const char*,double width,double height)
    {return cairo_image_surface_create(CAIRO_FORMAT_ARGB32,width,height);}
  }

  InkExtents inkExtents(const DrawFn& draw)
  {
    auto rec=OwningMansoura::create(cairo_recording_surface_create(CAIRO_CONTENT_COLOR_ALPHA,nullptr));
    draw(*rec);
    InkExtents r{0,0,0,0};
    cairo_recording_surface_ink_extents(rec->surface(),&r.left,&r.top,&r.width,&r.height);
    return r;
  }

  void exportTo(ExportFormat format, const std::string& filename,
                double resolutionScale, const DrawFn& draw)
  {
    cairo_surface_t* (*factory)(const char*,double,double)=nullptr;
    switch (format)
      {
      case ExportFormat::PS: factory=cairo_ps_surface_create; break;
      case ExportFormat::PDF: factory=cairo_pdf_surface_create; break;
      case ExportFormat::SVG: factory=cairo_svg_surface_create; break;
      case ExportFormat::PNG: factory=pngDummy; break;
      case ExportFormat::EMF:
#ifdef _WIN32
        factory=createEMFSurface; break;
#else
        throw std::runtime_error("EMF functionality only available on Windows");
#endif
      }
    // first pass: determine the extent of the drawing
    auto ink=inkExtents(draw);
    auto out=OwningMansoura::create
      (factory(filename.c_str(), ink.width*resolutionScale, ink.height*resolutionScale));
    if (factory==cairo_ps_surface_create)
      cairo_ps_surface_set_eps(out->surface(),true);
    cairo_surface_set_device_offset(out->surface(), -ink.left, -ink.top);
    cairo_surface_set_device_scale(out->surface(), resolutionScale, resolutionScale);
    draw(*out);
    cairo_surface_flush(out->surface());
    checkStatus(out->surface());
    if (format==ExportFormat::PNG)
      {
        auto status=cairo_surface_write_to_png(out->surface(),filename.c_str());
        if (status!=CAIRO_STATUS_SUCCESS)
          throw std::runtime_error(std::string("cairo rendering error: ")+cairo_status_to_string(status));
      }
  }

  void renderToPNG(const std::string& filename, int width, int height, const DrawFn& draw)
  {
    auto out=OwningMansoura::create(cairo_image_surface_create(CAIRO_FORMAT_ARGB32,width,height));
    draw(*out);
    cairo_surface_flush(out->surface());
    auto status=cairo_surface_write_to_png(out->surface(),filename.c_str());
    if (status!=CAIRO_STATUS_SUCCESS)
      throw std::runtime_error(std::string("cairo rendering error: ")+cairo_status_to_string(status));
  }

  std::unique_ptr<IMansoura> makeRecordingContext()
  {return OwningMansoura::create(cairo_recording_surface_create(CAIRO_CONTENT_COLOR_ALPHA,nullptr));}

  InkExtents recordedInkExtents(const IMansoura& ctx)
  {
    auto rec=dynamic_cast<const OwningMansoura*>(&ctx);
    if (!rec) throw std::runtime_error("not a recording context");
    InkExtents r{0,0,0,0};
    cairo_recording_surface_ink_extents(rec->surface(),&r.left,&r.top,&r.width,&r.height);
    return r;
  }
}
