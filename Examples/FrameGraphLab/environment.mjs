// Deterministic Radiance RGBE reduction for the bundled -Y/+X HDR sky.
// Average in linear HDR space; never clamp or tone-map the authoring asset.
import fs from 'node:fs';
import assert from 'node:assert/strict';

export function buildLabSky(sourceFile, destination, width=1024, height=512) {
  const file=fs.readFileSync(sourceFile);
  const headerEnd=file.indexOf('\n\n')+2;
  assert(headerEnd>1&&file.subarray(0,headerEnd).toString('ascii').includes('FORMAT=32-bit_rle_rgbe'));
  const lineEnd=file.indexOf('\n',headerEnd);
  const size=/^-Y (\d+) \+X (\d+)\s*$/.exec(file.subarray(headerEnd,lineEnd).toString('ascii'));
  assert(size,'Expected the bundled Radiance sky orientation');
  const sourceHeight=Number(size[1]),sourceWidth=Number(size[2]);
  assert(sourceWidth%width===0&&sourceHeight%height===0,'Sky reduction requires whole pixel blocks');
  const pixels=Buffer.alloc(sourceWidth*sourceHeight*4);
  let cursor=lineEnd+1;
  for(let y=0;y<sourceHeight;y++) {
    assert(file[cursor++]===2&&file[cursor++]===2,'Expected scanline RGBE RLE');
    assert(((file[cursor++]<<8)|file[cursor++])===sourceWidth,'Invalid scanline width');
    for(let channel=0;channel<4;channel++) {
      let x=0;
      while(x<sourceWidth) {
        assert(cursor<file.length,'Truncated sky scanline');
        const code=file[cursor++],count=code>128?code-128:code;
        assert(count>0&&x+count<=sourceWidth,'Invalid sky RLE packet');
        if(code>128) {
          assert(cursor<file.length);const value=file[cursor++];
          for(let n=0;n<count;n++)pixels[(y*sourceWidth+x++)*4+channel]=value;
        } else {
          assert(cursor+count<=file.length);
          for(let n=0;n<count;n++)pixels[(y*sourceWidth+x++)*4+channel]=file[cursor++];
        }
      }
    }
  }
  const output=Buffer.alloc(width*height*4),sx=sourceWidth/width,sy=sourceHeight/height;
  let maxLinearValue=0;
  for(let y=0;y<height;y++)for(let x=0;x<width;x++) {
    const sum=[0,0,0];
    for(let dy=0;dy<sy;dy++)for(let dx=0;dx<sx;dx++) {
      const offset=((y*sy+dy)*sourceWidth+x*sx+dx)*4;
      const scale=pixels[offset+3]===0?0:2**(pixels[offset+3]-136)/(sx*sy);
      for(let c=0;c<3;c++)sum[c]+=pixels[offset+c]*scale;
    }
    const peak=Math.max(...sum),offset=(y*width+x)*4;
    maxLinearValue=Math.max(maxLinearValue,peak);
    if(peak<1e-32)continue;
    const exponent=Math.floor(Math.log2(peak))+1,scale=256/2**exponent;
    assert(exponent+128>0&&exponent+128<256,'RGBE exponent overflow');
    for(let c=0;c<3;c++)output[offset+c]=Math.min(255,Math.floor(sum[c]*scale));
    output[offset+3]=exponent+128;
  }
  assert(maxLinearValue>1,'Sky unexpectedly lost HDR range');
  const chunks=[Buffer.from('#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y '+height+' +X '+width+'\n')];
  for(let y=0;y<height;y++) {
    chunks.push(Buffer.from([2,2,width>>8,width&255]));
    for(let c=0;c<4;c++) {
      const row=Buffer.alloc(width);
      for(let x=0;x<width;x++)row[x]=output[(y*width+x)*4+c];
      const runAt=start=>{let count=1;while(count<127&&start+count<width&&row[start+count]===row[start])++count;return count;};
      let x=0;
      while(x<width) {
        const run=runAt(x);
        if(run>=4){chunks.push(Buffer.from([128+run,row[x]]));x+=run;}
        else {
          const start=x++;
          while(x<width&&x-start<128&&runAt(x)<4)++x;
          chunks.push(Buffer.from([x-start]),row.subarray(start,x));
        }
      }
    }
  }
  fs.writeFileSync(destination,Buffer.concat(chunks));
  fs.writeFileSync(destination+'.meta','ENABLE_MIPMAPPING=false\n');
  return {sourceResolution:[sourceWidth,sourceHeight],resolution:[width,height],maxLinearValue,
    estimatedTextureBytes:width*height*16};
}
