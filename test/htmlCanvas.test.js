// Runs MansouraHTMLCanvas (compiled to wasm by `make wasm` here) against
// a mock CanvasRenderingContext2D, and checks the observed calls.
const assert=require('assert');
const Mod=require('./htmlCanvasHarness.js');

Mod().then(M=>{
  const log=[];
  const stack=[];
  const id={a:1,b:0,c:0,d:1,e:0,f:0};
  let tf={...id};
  const mock={
    getTransform(){return {...tf};},
    setTransform(a,b,c,d,e,f){tf={a,b,c,d,e,f}; log.push('setTransform');},
    resetTransform(){tf={...id}; log.push('resetTransform');},
    translate(x,y){tf.e+=tf.a*x+tf.c*y; tf.f+=tf.b*x+tf.d*y;},
    scale(x,y){tf.a*=x; tf.b*=x; tf.c*=y; tf.d*=y;},
    save(){stack.push({...tf}); log.push('save');},
    restore(){tf=stack.pop(); log.push('restore');},
    measureText(t){
      const px=parseFloat(String(this.font).match(/([\d.]+)px/)[1]);
      return {width:t.length*0.5*px, fontBoundingBoxAscent:0.9*px, fontBoundingBoxDescent:0.2*px};
    },
    canvas:{width:300,height:200}, lineWidth:1, fillStyle:'#000000', strokeStyle:'#000000', font:'10px sans-serif',
  };
  const quiet=['font','textBaseline','textAlign'];
  const ctx=new Proxy(mock,{
    get(t,p){
      if (p in t) return t[p];
      return (...a)=>{log.push(String(p)+'('+a.join(',')+')');};
    },
    set(t,p,v){t[p]=v; if (!quiet.includes(p)) log.push('set '+String(p)+'='+JSON.stringify(v)); return true;}
  });

  const report=M.exerciseCanvas(ctx);
  const has=s=>assert(log.includes(s),'missing call '+s+'\n'+log.join('\n'));
  const idx=s=>log.indexOf(s);

  // transformation mirror
  assert(report.includes('u2d 12,22'));
  assert(report.includes('cur 5,4'));
  // flattened path and copy/append round trip
  assert(report.includes('flat 2\n') && report.includes('flat2 2\n'));
  // a copied path is replayed in device space with the transformation reset
  assert(idx('resetTransform')>idx('beginPath()'));
  has('moveTo(12,22)'); has('lineTo(20,22)'); has('lineTo(20,28)');
  // state mirroring across save/restore
  assert(report.includes('lw 3\n') && report.includes('lw2 3\n'));
  // colours use valid CSS
  has('set fillStyle="rgba(255,128,0,0.25)"');
  has('setLineDash(4,2)');
  // arc after newSubPath starts with a moveTo, so no connecting line is drawn
  const arcAfterSub=log.lastIndexOf('moveTo(1,0)');
  assert(arcAfterSub>=0 && log[arcAfterSub+1]==='arc(0,0,1,0,1,false)');
  // paint covers the whole surface
  has('fillRect(0,0,300,200)');
  // resetClip unwinds the clip guard and reinstates mirrored state
  const clip=idx('clip()');
  assert(clip>0 && log[clip-1]==='save' && log[clip+2]==='restore' && log[clip+3]==='setTransform',
         'resetClip sequence:\n'+log.slice(clip-3,clip+8).join('\n'));
  // text: superscript is smaller and raised relative to the base, and entities are decoded
  const ft=log.filter(l=>l.startsWith('fillText('));
  assert(ft.length>=3 && ft[0].startsWith('fillText(x,0,') && ft[1].startsWith('fillText(2,') && ft[2].startsWith('fillText(&λ'),ft.join('\n'));
  const base=parseFloat(ft[0].split(',')[2]), sup=parseFloat(ft[1].split(',')[2]);
  assert(sup<base,'superscript should be raised');
  // cached render: index<->position
  assert(report.includes('idx 0 2 pos 0 24 24'),report);
  // fontSize() is the size in points: the requested one, or the 12pt default
  assert(report.includes('fs 12 10 12\n'),report);
  // recording contexts measure text with the canvas once a measurer is installed
  const rec=M.exerciseRecording(ctx);
  assert(rec.includes('noCanvasInstall 0\n'),rec);
  assert(rec.includes('measure 32 17.6'),rec);            // 4 chars * 0.5 * 16px, (0.9+0.2)*16
  assert(rec.includes('ink 0 0 132 67.6'),rec);           // union of "abcd" at (100,50) and "xy" at the origin
  assert(rec.includes('cache 32 16 2\n'),rec);            // 8px per char: idx 2 starts at 16px, and x=17 falls in that character
  assert(rec.includes('after 32'),rec);
  console.log('htmlCanvas.test.js: all passed');
});
