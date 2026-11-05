#include "mansouraHTMLCanvas.h"
#include <emscripten/bind.h>
#include "mansouraRecording.h"
#include <sstream>
using namespace emscripten; using namespace mansoura; using namespace std;
// Exercise the IMansoura API and return a report of observed values.
string run(const val& ctx)
{
  ostringstream o;
  MansouraHTMLCanvas m(ctx);
  m.syncState();
  m.translate(10,20); m.scale(2,2);
  double x=1,y=1; m.userToDevice(x,y); o<<"u2d "<<x<<","<<y<<"\n";
  m.moveTo(1,1); m.lineTo(5,1); m.relLineTo(0,3);
  m.getCurrentPoint(x,y); o<<"cur "<<x<<","<<y<<"\n";
  o<<"flat "<<m.flattenedPathPoints().size()<<"\n";
  auto p=m.copyPath();
  m.newPath(); p->appendToCurrent(m);
  o<<"flat2 "<<m.flattenedPathPoints().size()<<"\n";
  m.stroke(false);
  m.setLineWidth(3); o<<"lw "<<m.getLineWidth()<<"\n";
  m.save(); m.setLineWidth(7); m.restore(); o<<"lw2 "<<m.getLineWidth()<<"\n";
  double d[2]={4,2}; m.setDash(d,2,1);
  m.setSourceRGBA(1,0.5,0,0.25);
  m.arc(0,0,5,0,3.14159); m.curveTo(1,2,3,4,5,6); m.rectangle(0,0,2,2); m.fill(false);
  m.newSubPath(); m.arc(0,0,1,0,1);
  m.paint();
  // clip handling
  m.save(); m.rectangle(0,0,5,5); m.clip(false); m.resetClip(); m.restore();
  // text
  TextProperties tp("x<sup>2</sup>&amp;\xce\xbb", 10);
  auto e=m.textExtents(tp); o<<"ext "<<e.width<<" "<<e.height<<"\n";
  m.moveTo(3,4); tp.angle=0.5; m.showText(tp);
  auto cr=m.cachedRender(TextProperties("abc",""));
  o<<"idx "<<cr->posToIdx(0)<<" "<<cr->posToIdx(1e6)<<" pos "<<cr->idxToPos(0)<<" "<<cr->idxToPos(3)<<" "<<cr->width()<<"\n";
  o<<"fs "<<cr->fontSize()<<" "<<m.cachedRender(TextProperties("abc","",10))->fontSize()<<" "<<m.cachedRender(TextProperties("abc","",-3))->fontSize()<<"\n";
  cr->setText("a\xce\xbb" "b"); cr->show();
  return o.str();
}

// inkExtents/measuringContext with text measured by an HTML canvas
string runRecording(const val& ctx)
{
  ostringstream o;
  o<<"noCanvasInstall "<<installHTMLTextMeasurer()<<"\n"; // node has neither OffscreenCanvas nor document
  installHTMLTextMeasurer(ctx);
  TextProperties tp("abcd","",12);
  auto e=measuringContext().textExtents(tp);
  o<<"measure "<<e.width<<" "<<e.height<<"\n";
  auto ink=inkExtents([&](const IMansoura& c){
    c.moveTo(100,50); c.showText(tp);
    auto cr=c.cachedRender(TextProperties("xy","",12)); c.moveTo(0,0); cr->show();
  });
  o<<"ink "<<ink.left<<" "<<ink.top<<" "<<ink.width<<" "<<ink.height<<"\n";
  auto rec=makeRecordingContext();
  auto cr=rec->cachedRender(TextProperties("abcd","",12));
  o<<"cache "<<cr->width()<<" "<<cr->idxToPos(2)<<" "<<cr->posToIdx(17)<<"\n";
  setTextMeasurer(nullptr); // the cache object must stay usable
  o<<"after "<<cr->width()<<"\n";
  return o.str();
}
EMSCRIPTEN_BINDINGS(t) {
  emscripten::function("exerciseCanvas",&run);
  emscripten::function("exerciseRecording",&runRecording);
}
