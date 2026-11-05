/*
  A cairo-free IMansoura context that records the extent of the ink
  laid down by drawing operations, along with pluggable text metrics.
  It provides inkExtents() and friends (see mansouraNoCairo.cc) on
  platforms without cairo, such as emscripten.

  @copyright Ravelation Pty Ltd 2026

  Mansoura is free software: you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
*/
#ifndef MANSOURA_RECORDING_H
#define MANSOURA_RECORDING_H

#include "IMansoura.h"
#include "canvasPath.h"
#include <memory>

namespace mansoura
{
  /// source of text metrics for contexts that have no drawing
  /// surface of their own to measure text with
  class ITextMeasurer
  {
  public:
    virtual ~ITextMeasurer()=default;
    virtual TextExtents textExtents(const TextProperties&) const=0;
    virtual std::unique_ptr<ICacheRender> cachedRender(const TextProperties&) const=0;
  };

  /// Measures text using another context, for example a
  /// MansouraHTMLCanvas drawing to an offscreen canvas.
  class ContextTextMeasurer: public ITextMeasurer
  {
    std::shared_ptr<const IMansoura> ctx;

    // keeps the context alive for as long as the cache object lives
    class Cache: public ICacheRender
    {
      std::shared_ptr<const IMansoura> ctx;
      std::unique_ptr<ICacheRender> inner;
    public:
      Cache(std::shared_ptr<const IMansoura> ctx, std::unique_ptr<ICacheRender> inner):
        ctx(std::move(ctx)), inner(std::move(inner)) {}
      void show() override {inner->show();}
      TextExtents extents() const override {return inner->extents();}
      const void* context() const override {return inner->context();}
      double fontSize() const override {return inner->fontSize();}
      std::string::size_type posToIdx(double x) const override {return inner->posToIdx(x);}
      double idxToPos(std::string::size_type idx) const override {return inner->idxToPos(idx);}
      TextProperties properties() const override {return inner->properties();}
      TextProperties properties(const TextProperties& p) override {return inner->properties(p);}
    };
  public:
    ContextTextMeasurer(std::shared_ptr<const IMansoura> ctx): ctx(std::move(ctx)) {}
    TextExtents textExtents(const TextProperties& tp) const override
    {return ctx->textExtents(tp);}
    std::unique_ptr<ICacheRender> cachedRender(const TextProperties& tp) const override
    {return std::make_unique<Cache>(ctx,ctx->cachedRender(tp));}
  };

  /// Install the source of text metrics used by recording and
  /// measuring contexts. Passing null reinstates the default, which
  /// estimates metrics from the character count: adequate for tests,
  /// but not for layout.
  void setTextMeasurer(std::shared_ptr<const ITextMeasurer> measurer);

  /// the measurer currently in force (never null)
  const ITextMeasurer& textMeasurer();

  /// IMansoura context that draws nothing, but accumulates the extent
  /// of the ink that would have been laid down. Like cairo's recording
  /// surfaces, extents are reported in device space.
  ///
  /// Approximations compared with a real renderer: strokes assume
  /// butt caps and ignore joins and dashes; clips are tracked as
  /// bounding boxes; text ink is the layout box, not the glyph
  /// outlines; SVG ink is the box it is rendered into.
  class RecordingContext: public IMansoura
  {
    struct Impl;
    std::unique_ptr<Impl> impl;
    friend class RecordingCacheRender;
  public:
    RecordingContext();
    ~RecordingContext() override;

    /// bounding box of everything drawn so far
    InkExtents inkExtents() const;

    /// append a path (device space, as returned by copyPath) to the current path
    void appendPath(const std::vector<PathSeg>& path) const;

    void moveTo(double x, double y) const override;
    void lineTo(double x, double y) const override;
    void relMoveTo(double x, double y) const override;
    void relLineTo(double x, double y) const override;
    void arc(double x, double y, double radius, double start, double end) const override;
    void curveTo(double x1, double y1, double x2, double y2, double x3, double y3) const override;
    void rectangle(double x, double y, double width, double height) const override;
    void newPath() const override;
    void newSubPath() const override;
    void closePath() const override;
    void getCurrentPoint(double& x, double& y) const override;
    std::unique_ptr<IPath> copyPath() const override;
    std::vector<std::pair<double,double>> flattenedPathPoints() const override;
    void fill(bool preserve=false) const override;
    void fillEvenOdd(bool preserve=false) const override;
    void stroke(bool preserve=false) const override;
    void clip(bool preserve=false) const override;
    void resetClip() const override;
    void paint() const override;
    void setLineWidth(double width) const override;
    double getLineWidth() const override;
    void setDash(const double* dashes, int num_dashes, double offset) const override;
    void setSourceRGB(double r, double g, double b) const override;
    void setSourceRGBA(double r, double g, double b, double a) const override;
    void showText(const TextProperties& text) const override;
    using IMansoura::showText;
    TextExtents textExtents(const TextProperties& text) const override;
    void identityMatrix() const override;
    void translate(double x, double y) const override;
    void scale(double sx, double sy) const override;
    void rotate(double angle) const override;
    void userToDevice(double& x, double& y) const override;
    void save() const override;
    void restore() const override;
    void setTolerance(double tolerance) const override;
    void renderSVG(const SVGRenderer& svgRenderer, double width, double height) const override;
    const void* context() const override {return this;}
    std::unique_ptr<ICacheRender> cachedRender(const TextProperties& tp) const override;
  };
}

#endif
