/*
  Definitions of the free functions declared in IMansoura.h for
  builds without cairo (eg emscripten). Link this instead of
  mansouraCairo.cc, never as well as it.

  @copyright Ravelation Pty Ltd 2026

  Mansoura is free software: you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
*/
#include "mansouraRecording.h"
#include <stdexcept>
using namespace std;

namespace mansoura
{
  InkExtents inkExtents(const DrawFn& draw)
  {
    RecordingContext rec;
    draw(rec);
    return rec.inkExtents();
  }

  unique_ptr<IMansoura> makeRecordingContext()
  {return make_unique<RecordingContext>();}

  InkExtents recordedInkExtents(const IMansoura& ctx)
  {
    auto rec=dynamic_cast<const RecordingContext*>(&ctx);
    if (!rec) throw runtime_error("not a recording context");
    return rec->inkExtents();
  }

  const IMansoura& measuringContext()
  {
    // accumulates ink that nobody looks at, but is otherwise stateless enough for measuring text
    static const RecordingContext ctx;
    return ctx;
  }

  void exportTo(ExportFormat, const string&, double, const DrawFn&)
  {
    throw runtime_error("export to file is not supported in this build");
  }

  void renderToPNG(const string&, int, int, const DrawFn&)
  {
    throw runtime_error("PNG export is not supported in this build");
  }
}
