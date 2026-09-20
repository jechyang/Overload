// Original, deterministic outline fixture. No third-party font data is embedded.
// Glyph 1 is a 500x700 rectangle; glyph 2 has an oppositely wound rectangular hole.
import fs from 'node:fs';
const u16=n=>{const b=Buffer.alloc(2);b.writeUInt16BE(n&65535);return b;};
const u32=n=>{const b=Buffer.alloc(4);b.writeUInt32BE(n>>>0);return b;};
const join=items=>Buffer.concat(items);
function glyph(contours){
    const points=contours.flat(), ends=[];let count=0;for(const c of contours){count+=c.length;ends.push(count-1);}
    const bytes=[u16(contours.length),u16(0),u16(0),u16(500),u16(700),...ends.map(u16),u16(0),Buffer.from(points.map(()=>1))];
    let x=0,y=0;for(const p of points){bytes.push(u16(p[0]-x));x=p[0];}for(const p of points){bytes.push(u16(p[1]-y));y=p[1];}
    return join(bytes);
}
export function makeTestFont(file){
    const rectangle=[[0,0],[0,700],[500,700],[500,0]],hole=[[100,100],[400,100],[400,600],[100,600]];
    const glyphs=[glyph([rectangle]),glyph([rectangle]),glyph([rectangle,hole]),glyph([])];
    const positions=[0];for(let i=0;i<glyphs.length;++i){if(glyphs[i].length%2)glyphs[i]=join([glyphs[i],Buffer.alloc(1)]);positions.push(positions.at(-1)+glyphs[i].length);}
    const head=Buffer.alloc(54);head.writeUInt32BE(0x10000,0);head.writeUInt32BE(0x10000,4);head.writeUInt32BE(0x5f0f3cf5,12);
    head.writeUInt16BE(1000,18);head.writeInt16BE(500,40);head.writeInt16BE(700,42);head.writeUInt16BE(8,46);head.writeInt16BE(2,48);head.writeInt16BE(1,50);
    const hhea=Buffer.alloc(36);hhea.writeUInt32BE(0x10000);hhea.writeInt16BE(800,4);hhea.writeInt16BE(-200,6);hhea.writeUInt16BE(600,10);hhea.writeInt16BE(1,18);hhea.writeUInt16BE(4,34);
    const maxp=Buffer.alloc(32);maxp.writeUInt32BE(0x10000);maxp.writeUInt16BE(4,4);maxp.writeUInt16BE(8,6);maxp.writeUInt16BE(2,8);maxp.writeUInt16BE(2,14);
    const mappings=[[32,3],[63,1],[65,1],[79,2],[86,1],[0x301,1],[0x4e2d,1],[0x6587,2],[0xfffd,1],[0x1f600,2]];
    const sub=join([u16(12),u16(0),u32(16+mappings.length*12),u32(0),u32(mappings.length),...mappings.flatMap(([cp,id])=>[u32(cp),u32(cp),u32(id)])]);
    const cmap=join([u16(0),u16(1),u16(3),u16(10),u32(12),sub]);
    const kern=join([u16(0),u16(1),u16(0),u16(20),u16(1),u16(1),u16(6),u16(0),u16(0),u16(1),u16(1),u16(-100)]);
    const name=join([u16(0),u16(0),u16(6)]),post=join([u32(0x30000),Buffer.alloc(28)]);
    const tables=new Map(Object.entries({head,hhea,maxp,cmap,kern,name,post,glyf:join(glyphs),loca:join(positions.map(u32)),hmtx:join([600,600,600,250].flatMap(a=>[u16(a),u16(0)]))}).sort());
    const num=tables.size,power=2**Math.floor(Math.log2(num));
    const header=join([u32(0x10000),u16(num),u16(power*16),u16(Math.log2(power)),u16(num*16-power*16)]);
    let offset=12+16*num;const records=[],bodies=[];
    for(const [tag,data] of tables){
        const padded=join([data,Buffer.alloc((4-data.length%4)%4)]);let sum=0;for(let i=0;i<padded.length;i+=4)sum=(sum+padded.readUInt32BE(i))>>>0;
        records.push(join([Buffer.from(tag),u32(sum),u32(offset),u32(data.length)]));bodies.push(padded);offset+=padded.length;
    }
    const font=join([header,...records,...bodies]);let sum=0;for(let i=0;i<font.length;i+=4)sum=(sum+font.readUInt32BE(i))>>>0;
    const headRecord=[...tables.keys()].indexOf('head');const headOffset=font.readUInt32BE(12+headRecord*16+8);font.writeUInt32BE((0xb1b0afba-sum)>>>0,headOffset+8);
    fs.writeFileSync(file,font);
}
