// Native tests of RecordingContext and the cairo-free IMansoura.h free functions
#include "mansouraRecording.h"
#include <cmath>
#include <iostream>

using namespace mansoura;
using namespace std;

static int failures=0;
#define CHECK(cond) do {if (!(cond)) {++failures; cerr<<__FILE__<<":"<<__LINE__<<": FAILED "<<#cond<<endl;}} while(0)
static bool near(double a, double b, double eps=1e-6) {return fabs(a-b)<eps;}
static bool same(const InkExtents& e, double l, double t, double w, double h, double eps=1e-6)
{return near(e.left,l,eps) && near(e.top,t,eps) && near(e.width,w,eps) && near(e.height,h,eps);}

void testShapes()
{
  CHECK(same(inkExtents([](const IMansoura&){}),0,0,0,0));
  // a filled rectangle
  CHECK(same(inkExtents([](const IMansoura& c){c.rectangle(10,20,30,40); c.fill();}),10,20,30,40));
  // transformations apply, and are reported in device space
  CHECK(same(inkExtents([](const IMansoura& c){
    c.translate(5,5); c.scale(2,3); c.rectangle(1,1,2,2); c.fill();}),7,8,4,6));
  // save/restore
  CHECK(same(inkExtents([](const IMansoura& c){
    c.save(); c.translate(100,100); c.restore(); c.rectangle(0,0,1,1); c.fill();}),0,0,1,1));
  // a horizontal stroke is widened by half the line width each side, with butt caps
  CHECK(same(inkExtents([](const IMansoura& c){
    c.setLineWidth(4); c.moveTo(0,0); c.lineTo(10,0); c.stroke();}),0,-2,10,4));
  // a diagonal butt-capped stroke is tighter than a naive inflation by lw/2
  auto d=inkExtents([](const IMansoura& c){c.setLineWidth(2); c.moveTo(0,0); c.lineTo(10,10); c.stroke();});
  CHECK(near(d.width,10+sqrt(2.0),1e-6));
  // stroke width scales with the transformation
  CHECK(same(inkExtents([](const IMansoura& c){
    c.scale(2,2); c.setLineWidth(1); c.moveTo(0,0); c.lineTo(5,0); c.stroke();}),0,-1,10,2));
  // stroke_preserve then fill; paths are consumed by non preserving operations
  CHECK(same(inkExtents([](const IMansoura& c){
    c.rectangle(0,0,10,10); c.setLineWidth(2); c.stroke(true); c.fill();}),-1,-1,12,12,1e-6));
  CHECK(same(inkExtents([](const IMansoura& c){
    c.rectangle(0,0,10,10); c.fill(); c.fill();}),0,0,10,10)); // second fill has no path
  // circle
  auto circ=inkExtents([](const IMansoura& c){c.arc(0,0,5,0,2*M_PI); c.fill();});
  CHECK(same(circ,-5,-5,10,10,0.05));
  // curves
  auto cv=inkExtents([](const IMansoura& c){c.moveTo(0,0); c.curveTo(0,10,10,10,10,0); c.fill();});
  CHECK(near(cv.left,0) && near(cv.width,10) && cv.height>7 && cv.height<=7.6);
}

void testClip()
{
  CHECK(same(inkExtents([](const IMansoura& c){
    c.rectangle(0,0,10,10); c.clip();
    c.rectangle(5,5,20,20); c.fill();}),5,5,5,5));
  CHECK(same(inkExtents([](const IMansoura& c){
    c.save(); c.rectangle(0,0,10,10); c.clip(); c.restore();
    c.rectangle(5,5,20,20); c.fill();}),5,5,20,20));
  CHECK(same(inkExtents([](const IMansoura& c){
    c.rectangle(0,0,10,10); c.clip(); c.resetClip();
    c.rectangle(5,5,20,20); c.fill();}),5,5,20,20));
  // fully clipped away
  CHECK(same(inkExtents([](const IMansoura& c){
    c.rectangle(0,0,10,10); c.clip();
    c.rectangle(50,50,5,5); c.fill();}),0,0,0,0));
  // paint covers the clip
  CHECK(same(inkExtents([](const IMansoura& c){
    c.rectangle(1,2,3,4); c.clip(); c.paint();}),1,2,3,4));
}

void testPathApi()
{
  RecordingContext c;
  c.translate(10,10); c.scale(2,2);
  c.moveTo(1,1); c.lineTo(3,1);
  double x, y;
  c.getCurrentPoint(x,y);
  CHECK(near(x,3) && near(y,1));
  c.relLineTo(0,2);
  c.getCurrentPoint(x,y);
  CHECK(near(x,3) && near(y,3));
  x=1; y=1; c.userToDevice(x,y);
  CHECK(near(x,12) && near(y,12));
  CHECK(c.flattenedPathPoints().size()==2);
  auto p=c.copyPath();
  RecordingContext other;
  other.translate(1000,1000); // path is in device space, so this must not matter
  p->appendToCurrent(other);
  other.fill();
  CHECK(same(other.inkExtents(),12,12,4,4));
  // recordedInkExtents
  auto rec=makeRecordingContext();
  rec->rectangle(0,0,2,2); rec->fill();
  CHECK(same(recordedInkExtents(*rec),0,0,2,2));
  bool threw=false;
  try {recordedInkExtents(measuringContext()); } catch (...) {threw=true;}
  CHECK(!threw); // measuring context is itself a recording context
}

void testSvg()
{
  // SVGRenderer is only forward declared here, and the recording context never looks at it
  static char placeholder;
  const SVGRenderer& svg=*reinterpret_cast<const SVGRenderer*>(&placeholder);
  CHECK(same(inkExtents([&](const IMansoura& c){
    c.translate(3,4); c.renderSVG(svg,10,20);}),3,4,10,20));
}

void testText()
{
  // default estimator: width proportional to characters
  auto& m=measuringContext();
  auto e1=m.textExtents(TextProperties("abcd","",12));
  auto e2=m.textExtents(TextProperties("abcdabcd","",12));
  CHECK(e1.width>0 && near(e2.width,2*e1.width) && near(e2.height,e1.height));
  // markup is not counted as text
  CHECK(near(m.textExtents(TextProperties("<b>abcd</b>","",12)).width,e1.width));
  // font size scales
  CHECK(near(m.textExtents(TextProperties("abcd","",24)).width,2*e1.width));
  // ink for text starts at the current point
  auto ink=inkExtents([](const IMansoura& c){c.moveTo(100,50); c.showText("abcd",12);});
  CHECK(near(ink.left,100) && near(ink.top,50) && near(ink.width,e1.width) && near(ink.height,e1.height));
  // rotated by 90 degrees about the current point
  TextProperties rot("abcd","",12); rot.angle=M_PI/2;
  ink=inkExtents([&](const IMansoura& c){c.moveTo(100,50); c.showText(rot);});
  CHECK(near(ink.width,e1.height) && near(ink.height,e1.width) && near(ink.left,100-e1.height));
  // empty text lays down nothing
  CHECK(same(inkExtents([](const IMansoura& c){c.moveTo(1,1); c.showText("");}),0,0,0,0));
  // cached render reports ink when shown
  ink=inkExtents([](const IMansoura& c){
    auto cr=c.cachedRender(TextProperties("abcd","",12));
    CHECK(cr->context()==&c);
    c.moveTo(7,8); cr->show();
    cr->setText("abcdabcd"); c.moveTo(0,0); cr->show();
  });
  CHECK(near(ink.left,0) && near(ink.top,0) && near(ink.width,max(7+e1.width,e2.width)));
  // index <-> position round trip with the estimator
  auto cr=m.cachedRender(TextProperties("abcd","",12));
  CHECK(cr->idxToPos(0)==0 && near(cr->idxToPos(4),e1.width));
  CHECK(cr->posToIdx(0)==0 && cr->posToIdx(cr->idxToPos(2)+0.01)==2 && cr->posToIdx(1e6)==3);

  // an installed measurer is used
  struct Fixed: ITextMeasurer
  {
    TextExtents textExtents(const TextProperties&) const override {return {0,0,100,10};}
    unique_ptr<ICacheRender> cachedRender(const TextProperties&) const override {return nullptr;}
  };
  setTextMeasurer(make_shared<Fixed>());
  CHECK(near(measuringContext().textExtents(TextProperties("x")).width,100));
  ink=inkExtents([](const IMansoura& c){c.showText("x");});
  CHECK(same(ink,0,0,100,10));
  setTextMeasurer(nullptr);
  CHECK(!near(measuringContext().textExtents(TextProperties("x")).width,100));
}

void testExport()
{
  bool threw=false;
  try {exportTo(ExportFormat::PDF,"x.pdf",1,[](const IMansoura&){});} catch (const runtime_error&) {threw=true;}
  CHECK(threw);
  threw=false;
  try {renderToPNG("x.png",1,1,[](const IMansoura&){});} catch (const runtime_error&) {threw=true;}
  CHECK(threw);
}

int main()
{
  testShapes(); testClip(); testPathApi(); testSvg(); testText(); testExport();
  if (failures) {cerr<<failures<<" check(s) failed\n"; return 1;}
  cout<<"recordingContext: all passed\n";
  return 0;
}
