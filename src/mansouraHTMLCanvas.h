#ifndef MANSOURA_HTML_CANVAS_H
#define MANSOURA_HTML_CANVAS_H
#include "IMansoura.h"
#include "canvasPath.h"
#include <emscripten/bind.h>

namespace mansoura
{
  class HTMLCacheRender;

  /// Use \a ctx (a CanvasRenderingContext2D, ideally one belonging to
  /// an offscreen canvas) to measure text for contexts that have no
  /// canvas of their own: the contexts used by inkExtents(),
  /// makeRecordingContext() and measuringContext(). Without this they
  /// fall back to a crude estimate of text size.
  void installHTMLTextMeasurer(const emscripten::val& ctx);

  /// As above, but creates an offscreen canvas (OffscreenCanvas, or
  /// failing that a detached <canvas> element) to measure with.
  /// @return false if neither is available, in which case the
  /// estimate remains in force.
  bool installHTMLTextMeasurer();

  /// Concrete implementation of IMansoura using the HTML5 Canvas API.
  ///
  /// The canvas API is write-only for much of what IMansoura needs
  /// (current point, path contents, transformation), so this class
  /// mirrors the transformation, line and colour state and the
  /// current path in C++. Anything that changes that state behind
  /// this object's back (eg the page translating the context between
  /// frames) must be followed by a call to syncState().
  ///
  /// Text is laid out from a subset of Pango markup (see
  /// textMarkup.h), using measureText for metrics.
  ///
  /// Known limitations:
  ///  - resetClip() can only undo clips made at the current save()
  ///    nesting level (the canvas API cannot enlarge a clip)
  ///  - renderSVG() draws nothing yet
  class MansouraHTMLCanvas: public IMansoura
  {
    struct Impl;
    std::unique_ptr<Impl> impl;
    friend class HTMLCacheRender;
  public:
    /// @param canvas a CanvasRenderingContext2D
    MansouraHTMLCanvas(const emscripten::val& canvas);
    ~MansouraHTMLCanvas() override;

    /// Resynchronise the mirrored state with the canvas, and release
    /// any clip left over from a previous frame. Call before
    /// rendering a frame.
    void syncState() const;

    /// append a path (in device space, as returned by copyPath) to the current path
    void appendPath(const std::vector<PathSeg>& path) const;

    // Drawing operations
    void moveTo(double x, double y) const override;
    void lineTo(double x, double y) const override;
    void relMoveTo(double x, double y) const override;
    void relLineTo(double x, double y) const override;
    void arc(double x, double y, double radius, double start, double end) const override;
    void curveTo(double x1, double y1, double x2, double y2, double x3, double y3) const override;
    void rectangle(double x, double y, double width, double height) const override;

    // Path operations
    void newPath() const override;
    void newSubPath() const override;
    void closePath() const override;
    void getCurrentPoint(double& x, double& y) const override;
    std::unique_ptr<IPath> copyPath() const override;
    std::vector<std::pair<double,double>> flattenedPathPoints() const override;

    // Fill and stroke operations
    void fill(bool) const override;
    void fillEvenOdd(bool) const override;
    void stroke(bool) const override;
    void clip(bool) const override;
    void resetClip() const override;
    void paint() const override;

    // Line properties
    void setLineWidth(double width) const override;
    double getLineWidth() const override;
    void setDash(const double* dashes, int num_dashes, double offset) const override;

    // Color operations
    void setSourceRGB(double r, double g, double b) const override;
    void setSourceRGBA(double r, double g, double b, double a) const override;

    // Text operations
    void showText(const TextProperties& text) const override;
    TextExtents textExtents(const TextProperties& text) const override;

    // Transformation operations
    void identityMatrix() const override;
    void translate(double x, double y) const override;
    void scale(double sx, double sy) const override;
    void rotate(double angle) const override;
    void userToDevice(double& x, double& y) const override;

    // Context state operations
    void save() const override;
    void restore() const override;

    // Tolerance
    void setTolerance(double tolerance) const override;

    // SVG rendering support
    void renderSVG(const SVGRenderer& svgRenderer, double width, double height) const override;

    const void* context() const override {return this;}

    std::unique_ptr<ICacheRender>
    cachedRender(const TextProperties& tp) const override;

  };
}

#endif
