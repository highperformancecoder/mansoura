/*
  @copyright Ravelation Pty Ltd 2026

  Mansoura is free software: you can redistribute it and/or modify it
  under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.
*/
#include "textMarkup.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>

using namespace std;

namespace mansoura
{
  namespace
  {
    // Pango's PANGO_SCALE_SMALL etc.
    constexpr double scaleStep=1.2;
    // approximate Pango sub/superscript behaviour
    constexpr double subRise=-0.15, supRise=0.4, subSupScale=1/scaleStep;

    void appendUtf8(string& s, unsigned long cp)
    {
      if (cp<0x80) s+=char(cp);
      else if (cp<0x800)
        {
          s+=char(0xC0|(cp>>6));
          s+=char(0x80|(cp&0x3F));
        }
      else if (cp<0x10000)
        {
          s+=char(0xE0|(cp>>12));
          s+=char(0x80|((cp>>6)&0x3F));
          s+=char(0x80|(cp&0x3F));
        }
      else
        {
          s+=char(0xF0|(cp>>18));
          s+=char(0x80|((cp>>12)&0x3F));
          s+=char(0x80|((cp>>6)&0x3F));
          s+=char(0x80|(cp&0x3F));
        }
    }

    // attributes of a tag, eg {"font_family":"Arial"}
    using Attrs=map<string,string>;

    Attrs parseAttrs(const string& s)
    {
      Attrs r;
      size_t i=0;
      while (i<s.size())
        {
          while (i<s.size() && isspace((unsigned char)s[i])) ++i;
          size_t nameStart=i;
          while (i<s.size() && s[i]!='=' && !isspace((unsigned char)s[i])) ++i;
          string name=s.substr(nameStart,i-nameStart);
          while (i<s.size() && isspace((unsigned char)s[i])) ++i;
          if (i>=s.size() || s[i]!='=') {if (name.empty()) break; continue;}
          ++i;
          while (i<s.size() && isspace((unsigned char)s[i])) ++i;
          if (i>=s.size()) break;
          char q=s[i];
          string value;
          if (q=='"' || q=='\'')
            {
              size_t end=s.find(q,i+1);
              if (end==string::npos) end=s.size();
              value=s.substr(i+1,end-i-1);
              i=end+1;
            }
          else
            {
              size_t st=i;
              while (i<s.size() && !isspace((unsigned char)s[i])) ++i;
              value=s.substr(st,i-st);
            }
          r[name]=value;
        }
      return r;
    }

    // apply the attributes of a <span> to the style
    void applySpan(TextRun& style, const Attrs& attrs)
    {
      for (auto& [k,v]: attrs)
        {
          if (k=="font_family" || k=="face" || k=="font")
            style.family=v;
          else if (k=="size" || k=="font_size")
            {
              static const map<string,double> named=
                {{"xx-small",0.5787},{"x-small",0.6444},{"small",1/scaleStep},
                 {"medium",1},{"large",scaleStep},{"x-large",1.4396},{"xx-large",1.728}};
              if (auto i=named.find(v); i!=named.end())
                {style.sizeScale=i->second; style.absoluteSizePt=0;}
              else if (v=="smaller") style.sizeScale/=scaleStep;
              else if (v=="larger") style.sizeScale*=scaleStep;
              else if (!v.empty() && v.back()=='%')
                style.sizeScale*=atof(v.c_str())/100;
              else
                {
                  // number alone is in 1024ths of a point, with pt suffix in points
                  char* end;
                  double n=strtod(v.c_str(),&end);
                  if (end!=v.c_str() && n>0)
                    {
                      style.absoluteSizePt=string(end)=="pt"? n: n/1024;
                      style.sizeScale=1;
                    }
                }
            }
          else if (k=="weight" || k=="font_weight")
            {
              char* end;
              double n=strtod(v.c_str(),&end);
              style.bold=end!=v.c_str()? n>=600: (v=="bold" || v=="ultrabold" || v=="heavy");
            }
          else if (k=="style" || k=="font_style")
            style.italic=(v=="italic" || v=="oblique");
          else if (k=="foreground" || k=="color" || k=="fgcolor")
            style.colour=v;
          else if (k=="rise")
            {
              char* end;
              double n=strtod(v.c_str(),&end);
              if (end!=v.c_str())
                style.risePt+=string(end)=="pt"? n: n/1024;
            }
          else if (k=="underline")
            style.underline=(v!="none");
          else if (k=="strikethrough")
            style.strikethrough=(v=="true");
        }
    }

    // decode an entity starting at s[i]=='&'. Returns false if not a
    // valid entity, otherwise appends to out and advances i past it.
    bool decodeEntity(const string& s, size_t& i, string& out)
    {
      size_t semi=s.find(';',i);
      if (semi==string::npos || semi-i>10) return false;
      string name=s.substr(i+1,semi-i-1);
      if (name=="amp") out+='&';
      else if (name=="lt") out+='<';
      else if (name=="gt") out+='>';
      else if (name=="quot") out+='"';
      else if (name=="apos") out+='\'';
      else if (name.size()>1 && name[0]=='#')
        {
          char* end;
          bool hex=name[1]=='x' || name[1]=='X';
          unsigned long cp=strtoul(name.c_str()+(hex? 2: 1),&end,hex? 16: 10);
          if (*end || end==name.c_str()+(hex? 2: 1) || cp==0 || cp>0x10FFFF)
            return false;
          appendUtf8(out,cp);
        }
      else return false;
      i=semi+1;
      return true;
    }
  }

  vector<TextRun> plainRuns(const string& text)
  {
    vector<TextRun> r;
    if (!text.empty())
      {
        r.emplace_back();
        r.back().text=text;
      }
    return r;
  }

  vector<TextRun> parseMarkup(const string& markup)
  {
    vector<TextRun> runs;
    // stack of (tag name, style in force inside that tag)
    vector<pair<string,TextRun>> stack;
    stack.emplace_back("",TextRun());
    string pending;

    auto flush=[&]() {
      if (pending.empty()) return;
      TextRun r=stack.back().second;
      r.text=pending;
      pending.clear();
      runs.push_back(std::move(r));
    };

    size_t i=0;
    while (i<markup.size())
      {
        char c=markup[i];
        if (c=='&')
          {
            if (!decodeEntity(markup,i,pending))
              pending+=markup[i++];
          }
        else if (c=='<')
          {
            size_t end=markup.find('>',i);
            if (end==string::npos)
              {pending+=markup.substr(i); break;}
            string body=markup.substr(i+1,end-i-1);
            i=end+1;
            bool closing=!body.empty() && body[0]=='/';
            if (closing) body.erase(0,1);
            bool selfClosing=!body.empty() && body.back()=='/';
            if (selfClosing) body.pop_back();
            size_t nameEnd=body.find_first_of(" \t\r\n");
            string name=body.substr(0,nameEnd);
            transform(name.begin(),name.end(),name.begin(),
                      [](unsigned char ch){return tolower(ch);});
            Attrs attrs=nameEnd==string::npos? Attrs(): parseAttrs(body.substr(nameEnd));
            flush();
            if (closing)
              {
                // pop to the matching open tag; ignore unmatched closers
                for (size_t j=stack.size(); j-->1;)
                  if (stack[j].first==name)
                    {stack.resize(j); break;}
              }
            else if (!name.empty())
              {
                TextRun style=stack.back().second;
                if (name=="b") style.bold=true;
                else if (name=="i") style.italic=true;
                else if (name=="u") style.underline=true;
                else if (name=="s") style.strikethrough=true;
                else if (name=="big") style.sizeScale*=scaleStep;
                else if (name=="small") style.sizeScale/=scaleStep;
                else if (name=="sub")
                  {style.sizeScale*=subSupScale; style.riseEm+=subRise;}
                else if (name=="sup")
                  {style.sizeScale*=subSupScale; style.riseEm+=supRise;}
                else if (name=="tt") style.family="monospace";
                else if (name=="span") applySpan(style,attrs);
                // other tags: style unchanged, so the closer still matches
                if (!selfClosing) stack.emplace_back(name,style);
              }
          }
        else
          pending+=markup[i++];
      }
    flush();
    return runs;
  }

  string plainText(const vector<TextRun>& runs)
  {
    string r;
    for (auto& i: runs) r+=i.text;
    return r;
  }

  string::size_type utf8CharLength(const string& s, string::size_type i)
  {
    if (i>=s.size()) return 0;
    unsigned char ch=s[i];
    string::size_type n=1;
    if (ch>=0xF0 && ch<=0xF7) n=4;
    else if (ch>=0xE0) n=ch<=0xEF? 3: 1;
    else if (ch>=0xC0) n=2;
    // continuation bytes must follow, otherwise treat as a single invalid byte
    if (i+n>s.size()) return 1;
    for (string::size_type j=1; j<n; ++j)
      if ((static_cast<unsigned char>(s[i+j])&0xC0)!=0x80) return 1;
    return n;
  }

  wstring utf8ToWString(const string& s)
  {
    static_assert(sizeof(wchar_t)>=4, "wchar_t must hold a Unicode code point");
    wstring r;
    for (size_t i=0; i<s.size();)
      {
        unsigned char ch=s[i];
        auto n=utf8CharLength(s,i);
        unsigned long cp;
        if (n==1)
          cp= ch<0x80? ch: 0xFFFD;
        else
          {
            cp= n==2? ch&0x1F: n==3? ch&0x0F: ch&0x07;
            for (size_t j=1; j<n; ++j)
              cp=(cp<<6)|(static_cast<unsigned char>(s[i+j])&0x3F);
            // overlong encodings, surrogates and out of range values
            if ((n==2 && cp<0x80) || (n==3 && cp<0x800) || (n==4 && cp<0x10000) ||
                (cp>=0xD800 && cp<=0xDFFF) || cp>0x10FFFF)
              cp=0xFFFD;
          }
        r+=wchar_t(cp);
        i+=n;
      }
    return r;
  }
}
