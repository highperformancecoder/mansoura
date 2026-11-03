#ifndef MANSOURA_HTML_CANVAS_H
#define MANSOURA_HTML_CANVAS_H
#include "IMansoura.h"
#include <emscripten/bind.h>

namespace mansoura
{
    /// Concrete implementation of ICairoShim using the HTML5 Canvas API
  class MansouraHTMLCanvas: public IMansoura
  {
    struct Impl;
    std::unique_ptr<Impl> impl;
  public:
    MansouraHTMLCanvas(const emscripten::val& canvas);
    ~MansouraHTMLCanvas() override;
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
