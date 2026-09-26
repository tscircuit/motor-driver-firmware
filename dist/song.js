/* Shared byte-format validation for the dashboard and host tests. */
const SongBytes = {
 decode(bytes) {
  if(!bytes.length||bytes.length>192||bytes.length%4)throw Error('Use 1–48 four-byte notes (4–192 bytes).');
  let duration=0;
  for(let i=0;i<bytes.length;i+=4){
   const hz=bytes[i]|bytes[i+1]<<8,ms=bytes[i+2]|bytes[i+3]<<8;
   if(hz!==0&&(hz<100||hz>10000))throw Error('Frequency must be 0 (rest) or 100–10,000 Hz.');
   if(ms<20||ms>5000)throw Error('Each note must last 20–5,000 ms.');
   duration+=ms;
  }
  if(duration>60000)throw Error('Songs must be at most 60 seconds.');
  return {hex:Array.from(bytes,b=>b.toString(16).padStart(2,'0')).join(''),notes:bytes.length/4,duration};
 }
};
if(typeof module!=='undefined')module.exports=SongBytes;
