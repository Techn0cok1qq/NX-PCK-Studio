const fs=require('fs'); const log=console.log; console.log=()=>{}; console.error=()=>{};
eval(fs.readFileSync(__dirname+'/parsers.js','utf8')+';globalThis.PckParser=PckParser;globalThis.LOCParser=LOCParser;');
for (const f of process.argv.slice(2)) {
  const b=fs.readFileSync(f); const ab=b.buffer.slice(b.byteOffset,b.byteOffset+b.byteLength);
  const p=new PckParser(ab); const assets=p.parse();
  log('==',f,'LE=',p.isLittleEndian,'type=',p.pckType,'assets=',assets.length);
  for(const a of assets){ log(' ',a.filename,'type',a.typeCode,'size',a.dataSize,JSON.stringify(a.properties)); }
  const loc=assets.find(a=>a.filename.endsWith('.loc')); const r=new LOCParser(loc.data.buffer.slice(loc.data.byteOffset,loc.data.byteOffset+loc.data.byteLength)).parse();
  log('  loc keys:',r.keys,'langs:',r.languages.length,'ja:',JSON.stringify(r.locData['ja-JP']));
  const skin=assets.find(a=>a.filename.startsWith('dlcskin')); log('  skin PNG intact:',Buffer.from(skin.data).equals(fs.readFileSync('skin.png')));
}
