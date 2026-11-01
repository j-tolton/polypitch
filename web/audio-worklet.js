class PolyPitchCapture extends AudioWorkletProcessor {
 process(inputs){
  const a=inputs[0]?.[0];
  if(a)this.port.postMessage({samples:new Float32Array(a),frame:currentFrame});
  return true
 }
}
registerProcessor('polypitch-capture',PolyPitchCapture);
