#version 300 es
precision mediump float;
in vec2 vUV;
in vec3 vColor;
uniform sampler2D uAtlas;
out vec4 frag;
void main(){
  vec4 tex = texture(uAtlas, vUV);
  frag = vec4(vColor, 1.0) * tex;
}
