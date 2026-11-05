// Native (non-emscripten) tests of the pure C++ helpers behind
// MansouraHTMLCanvas: canvasPath.h and textMarkup.{h,cc}
#include "canvasPath.h"
#include "textMarkup.h"
#include <cmath>
#include <cstdlib>
#include <iostream>

using namespace mansoura;
using namespace std;

static int failures=0;
#define CHECK(cond) do {if (!(cond)) {++failures; cerr<<__FILE__<<":"<<__LINE__<<": FAILED "<<#cond<<endl;}} while(0)
static bool near(double a, double b, double eps=1e-9) {return fabs(a-b)<eps;}

void testMatrix()
{
  Matrix m=Matrix::translation(10,20)*Matrix::scaling(2,3)*Matrix::rotation(M_PI/2);
  double x=1, y=0;
  m.apply(x,y); // rotate (1,0)->(0,1), scale ->(0,3), translate ->(10,23)
  CHECK(near(x,10)); CHECK(near(y,23));
  auto inv=m.inverse();
  inv.apply(x,y);
  CHECK(near(x,1)); CHECK(near(y,0));
  double dx=1, dy=0;
  m.applyDistance(dx,dy);
  CHECK(near(dx,0)); CHECK(near(dy,3));
  CHECK(!Matrix::scaling(0,1).invertible());
}

void testPath()
{
  Matrix id;
  PathRecorder p;
  CHECK(!p.hasCurrent());
  p.lineTo(1,2); // moveTo without current point
  CHECK(p.segs.size()==1 && p.segs[0].type==PathSeg::MoveTo);
  p.lineTo(3,4);
  CHECK(p.current()==make_pair(3.0,4.0));
  p.closePath();
  CHECK(p.current()==make_pair(1.0,2.0));
  auto pts=p.flattened(id,0.1);
  CHECK(pts.size()==2); // line to (3,4), closing line to (1,2)
  CHECK(pts.back()==make_pair(1.0,2.0));

  // rectangle leaves the current point at (x,y), and flattening comes back in user space
  Matrix scaled=Matrix::scaling(2,2);
  PathRecorder r;
  r.rectangle(scaled,1,1,3,4);
  CHECK(r.current()==make_pair(2.0,2.0));
  pts=r.flattened(scaled,0.1);
  CHECK(pts.size()==4);
  CHECK(pts[0]==make_pair(4.0,1.0));
  CHECK(pts[1]==make_pair(4.0,5.0));

  // a quarter circle ends where it should, and stays on the circle
  PathRecorder a;
  a.arc(id,0,0,10,0,M_PI/2);
  CHECK(a.segs.size()==2); // moveTo + one curve
  CHECK(near(a.current().first,0,1e-9)); CHECK(near(a.current().second,10,1e-9));
  for (auto& pt: a.flattened(id,0.01))
    CHECK(near(hypot(pt.first,pt.second),10,0.02));
  // arcs with end<start still sweep forward, full circle is 4 curves
  PathRecorder c;
  c.arc(id,0,0,1,0,2*M_PI);
  CHECK(c.segs.size()==5);
  PathRecorder neg;
  neg.arc(id,0,0,1,M_PI,0); // end<start => +2pi => half circle going round the far side
  CHECK(near(neg.current().first,1)); CHECK(near(neg.current().second,0,1e-9));
  // arc joins to the current point with a line unless newSubPath was called
  PathRecorder j;
  j.moveTo(-5,-5);
  j.arc(id,0,0,1,0,1);
  CHECK(j.segs[1].type==PathSeg::LineTo);
  PathRecorder k;
  k.moveTo(-5,-5); k.newSubPath();
  k.arc(id,0,0,1,0,1);
  CHECK(k.segs[1].type==PathSeg::MoveTo);
  // arc under non-uniform scale becomes an ellipse in device space
  PathRecorder e;
  e.arc(Matrix::scaling(3,1),0,0,1,0,M_PI/2);
  CHECK(near(e.current().first,0,1e-9) && near(e.current().second,1));
}

void testMarkup()
{
  auto r=parseMarkup("x<sup>2</sup> &amp; <b>y<i>z</i></b>&lt;");
  CHECK(r.size()==6);
  CHECK(plainText(r)=="x2 & yz<");
  CHECK(r[0].text=="x" && r[0].riseEm==0);
  CHECK(r[1].text=="2" && r[1].riseEm>0 && r[1].sizeScale<1);
  CHECK(r[3].text=="y" && r[3].bold && !r[3].italic);
  CHECK(r[4].text=="z" && r[4].bold && r[4].italic);
  CHECK(r[5].text=="<" && !r[5].bold);

  r=parseMarkup("a<sub>i<sub>j</sub></sub>");
  CHECK(r.size()==3);
  CHECK(r[2].riseEm<r[1].riseEm*1.9 && r[2].sizeScale<r[1].sizeScale);

  r=parseMarkup("<span font_family=\"Times\" size=\"large\" foreground=\"#ff0000\" weight=\"bold\" style=\"italic\">hi</span>");
  CHECK(r.size()==1 && r[0].family=="Times" && r[0].sizeScale>1 && r[0].colour=="#ff0000" && r[0].bold && r[0].italic);
  r=parseMarkup("<span size='12288'>big</span>");
  CHECK(r.size()==1 && near(r[0].absoluteSizePt,12));

  // leniency
  CHECK(plainText(parseMarkup("1 < 2"))=="1 < 2");
  CHECK(plainText(parseMarkup("a & b"))=="a & b");
  CHECK(plainText(parseMarkup("<b>unclosed"))=="unclosed");
  CHECK(plainText(parseMarkup("a</i>b"))=="ab");
  CHECK(plainText(parseMarkup("<unknown>x</unknown>y"))=="xy");
  CHECK(plainText(parseMarkup("&#955;&#x3bb;"))=="\xce\xbb\xce\xbb");
  CHECK(parseMarkup("").empty());
  CHECK(plainRuns("<b>").size()==1 && plainRuns("<b>")[0].text=="<b>");
}

void testUtf8()
{
  CHECK(utf8ToWString("a\xce\xbb")==L"aλ");
  CHECK(utf8ToWString("\xf0\x9f\x98\x80")==wstring(1,wchar_t(0x1F600)));
  CHECK(utf8ToWString("\xff")==wstring(1,wchar_t(0xFFFD)));       // invalid lead
  CHECK(utf8ToWString("\xc0\x80")==wstring(1,wchar_t(0xFFFD)));   // overlong: one replacement
  CHECK(utf8ToWString("\xce")==wstring(1,wchar_t(0xFFFD)));       // truncated
  CHECK(utf8ToWString("\xed\xa0\x80")==wstring(1,wchar_t(0xFFFD))); // surrogate
  string s="a\xce\xbb\xf0\x9f\x98\x80";
  CHECK(utf8CharLength(s,0)==1 && utf8CharLength(s,1)==2 && utf8CharLength(s,3)==4);
  CHECK(utf8CharLength(s,s.size())==0);
}

int main()
{
  testMatrix(); testPath(); testMarkup(); testUtf8();
  if (failures) {cerr<<failures<<" check(s) failed\n"; return 1;}
  cout<<"htmlCanvasHelpers: all passed\n";
  return 0;
}
