#version 300 es
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
layout(location=2) in vec2 iPos;
layout(location=3) in vec2 iScale;
layout(location=4) in float iRot;
layout(location=5) in vec4 iUVRect;
uniform vec2 uScreen;
out vec2 vUV;
out vec3 vColor;       // new: per-vertex color
void main(){
  float c = cos(iRot), s = sin(iRot);
  vec2 p = vec2(aPos.x * iScale.x, aPos.y * iScale.y);
  vec2 r = vec2(c*p.x - s*p.y, s*p.x + c*p.y);
  vec2 world = iPos + r;
  vec2 ndc = (world / uScreen * 2.0 - 1.0) * vec2(1.0, -1.0);
  gl_Position = vec4(ndc, 0.0, 1.0);
  vUV = iUVRect.xy + aUV * iUVRect.zw;

  // simple color gradient based on position
  vColor = vec3(iPos.x / uScreen.x, iPos.y / uScreen.y, 0.5);
}
