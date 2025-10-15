#include <SDL.h>
#include <SDL_image.h>
#include <GLES3/gl3.h>

#include <vector>
#include <string>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>

// ===================================================================
//  OP5U 2D ENGINE — PARALLAX + WORLD LIGHTMAP (TILE-BASED LIGHTING)
// ===================================================================

// ---------------- Shaders ----------------
static const char* TILE_VS = R"(#version 300 es
layout(location=0) in vec2 aPos;  // -0.5..0.5 quad
layout(location=1) in vec2 aUV;

uniform vec2  uScreen;          // window size in px
uniform vec2  uCam;             // camera world pos (px)
uniform float uZoom;            // zoom factor
uniform float uTileSize;        // tile size in px
uniform float uParallax;        // 0..1  (BG..FG)
uniform ivec2 uViewOrigin;      // first visible tile (can be <0 for wrap)
uniform ivec2 uViewSize;        // tiles across/down to draw
uniform ivec2 uMapSize;         // map dims in tiles
uniform ivec2 uAtlasTiles;      // atlas grid dims (e.g. 8x8)
uniform highp usampler2D uTilemap; // GL_R8UI tile indices

out vec2 vUV;        // atlas UV for sprite tex
out vec2 vLightUV;   // world-space UV for lightmap (0..1, repeats)

void main() {
  // Instance -> map coordinates
  int ix = gl_InstanceID % uViewSize.x;
  int iy = gl_InstanceID / uViewSize.x;
  ivec2 mc = uViewOrigin + ivec2(ix, iy);

  // Infinite wrap into [0, uMapSize)
  mc = ivec2(
    ( (mc.x % uMapSize.x) + uMapSize.x ) % uMapSize.x,
    ( (mc.y % uMapSize.y) + uMapSize.y ) % uMapSize.y
  );

  // Fetch tile index
  uint idx = texelFetch(uTilemap, mc, 0).r;

  // Atlas uv
  int tilesX = uAtlasTiles.x;
  int tx = int(idx) % tilesX;
  int ty = int(idx) / tilesX;

  vec2 atlasBase = vec2(float(tx)/float(uAtlasTiles.x),
                        float(ty)/float(uAtlasTiles.y));
  vec2 atlasSize = vec2(1.0/float(uAtlasTiles.x),
                        1.0/float(uAtlasTiles.y));

  // Pixel positions
  vec2 local = aPos * uTileSize;
  vec2 world = (vec2(mc) * uTileSize) + local;

  // Camera (with parallax), to NDC
  vec2 view  = (world - uCam * uParallax) * uZoom;
  vec2 ndc   = (view / uScreen * 2.0 - 1.0) * vec2(1.0, -1.0);
  gl_Position = vec4(ndc, 0.0, 1.0);

  vUV = atlasBase + aUV * atlasSize;

  // World lightmap UV in [0,1] (repeats); scale by map extents in pixels
  vec2 worldSizePx = vec2(uMapSize) * uTileSize;
  vLightUV = world / worldSizePx; // wrap handled by sampler REPEAT
}
)";

static const char* TILE_FS = R"(#version 300 es
precision mediump float;

in vec2 vUV;
in vec2 vLightUV;

uniform sampler2D uAtlas;   // sprite atlas (RGBA)
uniform sampler2D uLight;   // world lightmap (R8 UNORM)
uniform vec4  uTint;        // per-layer tint
uniform float uAmbient;     // 0..1  (base brightness)
uniform float uLightStrength; // 0..1..1.5 scales light contribution
uniform bool  uLighting;    // toggle

out vec4 frag;

void main() {
  vec4 base = texture(uAtlas, vUV) * uTint;

  if (!uLighting) { frag = base; return; }

  // Sample light intensity (0..1) from red channel
  float li = texture(uLight, vLightUV).r;

  // Combine ambient + light (clamped); multiply RGB only
  float term = clamp(uAmbient + uLightStrength * li, 0.0, 1.5);
  frag = vec4(base.rgb * term, base.a);
}
)";

// -------------- Helpers --------------
static GLuint compile(GLenum t,const char* s){
  GLuint x=glCreateShader(t); glShaderSource(x,1,&s,nullptr); glCompileShader(x);
  GLint ok; glGetShaderiv(x,GL_COMPILE_STATUS,&ok);
  if(!ok){ char log[2048]; glGetShaderInfoLog(x,sizeof(log),nullptr,log);
    SDL_Log("Shader error:\n%s",log); }
  return x;
}
static GLuint link(GLuint v,GLuint f){
  GLuint p=glCreateProgram(); glAttachShader(p,v); glAttachShader(p,f); glLinkProgram(p);
  GLint ok; glGetProgramiv(p,GL_LINK_STATUS,&ok);
  if(!ok){ char log[2048]; glGetProgramInfoLog(p,sizeof(log),nullptr,log);
    SDL_Log("Link error:\n%s",log); }
  return p;
}
static inline unsigned u32rand(){ static unsigned s=0x12345u; s^=s<<13; s^=s>>17; s^=s<<5; return s; }

// Try multiple atlas paths so running from build/ works out of the box
static SDL_Surface* loadAtlas() {
  const char* env = SDL_getenv("OP5U_ASSETS");
  if(env){ std::string p=std::string(env)+"/atlas_8x8_512.png"; if(auto s=IMG_Load(p.c_str())) return s; }
  if(auto s=IMG_Load("../assets/atlas_8x8_512.png")) return s;
  if(auto s=IMG_Load("../../assets/atlas_8x8_512.png")) return s;
  if(auto s=IMG_Load("assets/atlas_8x8_512.png")) return s;
  if(auto s=IMG_Load("/home/orangepi/assets/atlas_8x8_512.png")) return s;
  return nullptr;
}

// ---------------- MAIN ----------------
int main(){
  if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO)!=0){ SDL_Log("SDL_Init: %s", SDL_GetError()); return 1; }
  if(!(IMG_Init(IMG_INIT_PNG)&IMG_INIT_PNG)){ SDL_Log("IMG_Init: %s", IMG_GetError()); return 1; }

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,0);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);

  SDL_Window* win=SDL_CreateWindow("OP5U Parallax Tilemap",
    SDL_WINDOWPOS_CENTERED,SDL_WINDOW_OPENGL,
    1920,1080,SDL_WINDOW_OPENGL|SDL_WINDOW_RESIZABLE);
  if(!win){ SDL_Log("CreateWindow failed: %s", SDL_GetError()); return 1; }
  SDL_GLContext ctx=SDL_GL_CreateContext(win);
  if(!ctx){ SDL_Log("GL context failed: %s", SDL_GetError()); return 1; }
  SDL_GL_SetSwapInterval(1);

  GLuint prog=link(compile(GL_VERTEX_SHADER,TILE_VS),compile(GL_FRAGMENT_SHADER,TILE_FS));
  glUseProgram(prog);
  glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);

  // Quad geometry
  float verts[]={-0.5f,-0.5f,0,0,  0.5f,-0.5f,1,0,  0.5f,0.5f,1,1,  -0.5f,0.5f,0,1};
  uint16_t idx[]={0,1,2,0,2,3};
  GLuint vao,vbo,ebo;
  glGenVertexArrays(1,&vao); glBindVertexArray(vao);
  glGenBuffers(1,&vbo); glBindBuffer(GL_ARRAY_BUFFER,vbo);
  glBufferData(GL_ARRAY_BUFFER,sizeof(verts),verts,GL_STATIC_DRAW);
  glGenBuffers(1,&ebo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(idx),idx,GL_STATIC_DRAW);
  glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,4*sizeof(float),(void*)0); glEnableVertexAttribArray(0);
  glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,4*sizeof(float),(void*)(2*sizeof(float))); glEnableVertexAttribArray(1);

  // Load atlas
  SDL_Surface* surf = loadAtlas();
  if(!surf){ SDL_Log("atlas_8x8_512.png missing"); return 1; }
  SDL_Surface* rgba=SDL_ConvertSurfaceFormat(surf,SDL_PIXELFORMAT_ABGR8888,0);
  SDL_FreeSurface(surf);
  GLuint texAtlas; glGenTextures(1,&texAtlas);
  glBindTexture(GL_TEXTURE_2D,texAtlas);
  glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,rgba->w,rgba->h,0,GL_RGBA,GL_UNSIGNED_BYTE,rgba->pixels);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
  SDL_FreeSurface(rgba);

  // Tilemaps (3 layers)
  const int MAP_W=128, MAP_H=128;
  const int EMPTY=63, PAC_OPEN=0, PAC_CLOSED=1, GHOST_RED=2, WALL=6, PELLET=7;

  std::vector<unsigned char> mapBG(MAP_W*MAP_H, EMPTY);
  std::vector<unsigned char> mapMID(MAP_W*MAP_H, EMPTY);
  std::vector<unsigned char> mapFG (MAP_W*MAP_H, EMPTY);

  for(int y=0;y<MAP_H;y++)
    for(int x=0;x<MAP_W;x++){
      mapBG[y*MAP_W+x] = (u32rand()%18==0) ? PELLET : EMPTY;                 // sparse lights/stars
      mapMID[y*MAP_W+x]= (((x/6 + y/6) & 1)==0) ? WALL : EMPTY;              // checker walls
      mapFG[y*MAP_W+x] = (((x/2 + y/2) & 1)==0) ? PAC_OPEN : GHOST_RED;      // pac/ghost
    }

  auto makeTileTex = [&](const std::vector<unsigned char>& buf){
    GLuint t; glGenTextures(1,&t); glBindTexture(GL_TEXTURE_2D,t);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
    glTexImage2D(GL_TEXTURE_2D,0,GL_R8UI,MAP_W,MAP_H,0,GL_RED_INTEGER,GL_UNSIGNED_BYTE,buf.data());
    return t;
  };
  GLuint texBG = makeTileTex(mapBG);
  GLuint texMID= makeTileTex(mapMID);
  GLuint texFG = makeTileTex(mapFG);

  // --------- Procedural world lightmap (R8) ---------
  const int LW=256, LH=256;          // lightmap resolution
  std::vector<unsigned char> L(LW*LH, 0);

  auto addRadial = [&](float cx,float cy,float radius,float strength){
    // cx,cy in [0,1] world space; radius fraction of world extent
    for(int j=0;j<LH;j++){
      for(int i=0;i<LW;i++){
        float x = (i+0.5f)/LW, y=(j+0.5f)/LH;
        float dx=x-cx, dy=y-cy;
        float d = sqrtf(dx*dx+dy*dy) / radius; // 0 at center, 1 at radius
        float v = 1.0f - d;
        if(v>0.0f){
          int idx=j*LW+i;
          float cur = L[idx]/255.0f;
          float add = strength * v * v;        // smooth falloff
          float out = fminf(1.0f, cur + add);
          L[idx] = (unsigned char)lrintf(out*255.0f);
        }
      }
    }
  };

  // A few lights across the world (repeat-tiled)
  addRadial(0.25f,0.30f,0.10f,1.0f);
  addRadial(0.60f,0.20f,0.12f,0.9f);
  addRadial(0.80f,0.65f,0.15f,1.0f);
  addRadial(0.40f,0.75f,0.12f,0.8f);

  GLuint texLight; glGenTextures(1,&texLight);
  glBindTexture(GL_TEXTURE_2D, texLight);
  glTexImage2D(GL_TEXTURE_2D,0,GL_R8, LW,LH,0,GL_RED,GL_UNSIGNED_BYTE,L.data());
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);

  // --------- Uniforms ---------
  GLint uScreen=glGetUniformLocation(prog,"uScreen");
  GLint uCam=glGetUniformLocation(prog,"uCam");
  GLint uZoom=glGetUniformLocation(prog,"uZoom");
  GLint uTileSz=glGetUniformLocation(prog,"uTileSize");
  GLint uParallax=glGetUniformLocation(prog,"uParallax");
  GLint uViewOrigin=glGetUniformLocation(prog,"uViewOrigin");
  GLint uViewSize=glGetUniformLocation(prog,"uViewSize");
  GLint uMapSize=glGetUniformLocation(prog,"uMapSize");
  GLint uAtlasTiles=glGetUniformLocation(prog,"uAtlasTiles");
  GLint uAtlas=glGetUniformLocation(prog,"uAtlas");
  GLint uTilemap=glGetUniformLocation(prog,"uTilemap");
  GLint uTint = glGetUniformLocation(prog,"uTint");
  GLint uLight = glGetUniformLocation(prog,"uLight");
  GLint uAmbient = glGetUniformLocation(prog,"uAmbient");
  GLint uLightStrength = glGetUniformLocation(prog,"uLightStrength");
  GLint uLighting = glGetUniformLocation(prog,"uLighting");

  glUniform1i(uAtlas,0);     // atlas -> TU0
  glUniform1i(uTilemap,1);   // tilemap -> TU1
  glUniform1i(uLight,2);     // lightmap -> TU2

  int W=1920,H=1080; glViewport(0,0,W,H);
  glUniform2f(uScreen,(float)W,(float)H);

  float camX=0, camY=0, camZoom=1;
  const float TILE_SIZE=64.f;
  const int ATLAS_TILES_X=8, ATLAS_TILES_Y=8;
  glUniform1f(uTileSz,TILE_SIZE);
  glUniform2i(uMapSize,MAP_W,MAP_H);
  glUniform2i(uAtlasTiles,ATLAS_TILES_X,ATLAS_TILES_Y);

  // Parallax & tints (these are controlled by -/= and used in draw)
  float parBG=0.50f, parMID=0.80f, parFG=1.00f;
  const float tintBG[4] ={0.85f,0.85f,1.00f,1.00f};
  const float tintMID[4]={0.95f,0.95f,1.00f,1.00f};
  const float tintFG[4] ={1.00f,1.00f,1.00f,1.00f};
  bool showBG=true, showMID=true, showFG=true;

  // Lighting params
  bool lighting=true;
  float ambient=0.30f;        // base brightness
  float lightStrength=0.90f;  // how strong lights are

  // Timing + anim
  uint64_t t0=SDL_GetPerformanceCounter();
  const double freq=SDL_GetPerformanceFrequency();
  double fpsTimer=0; int frames=0; bool vsyncOn=true;
  float animTimer=0; bool pacOpen=true;

  // --------- Main loop ---------
  bool quit=false; SDL_Event e;
  while(!quit){
    while(SDL_PollEvent(&e)){
      if(e.type==SDL_QUIT) quit=true;
      if(e.type==SDL_KEYDOWN){
        if(e.key.keysym.sym==SDLK_ESCAPE) quit=true;
        if(e.key.keysym.sym==SDLK_r){ camX=0; camY=0; camZoom=1; }
        if(e.key.keysym.sym==SDLK_v){ vsyncOn=!vsyncOn; SDL_GL_SetSwapInterval(vsyncOn?1:0); }
        if(e.key.keysym.sym==SDLK_1){ showBG = !showBG; }
        if(e.key.keysym.sym==SDLK_2){ showMID= !showMID; }
        if(e.key.keysym.sym==SDLK_3){ showFG = !showFG; }
        if(e.key.keysym.sym==SDLK_l){ lighting=!lighting; }

        // intensity (light contribution)
        if(e.key.keysym.sym==SDLK_COMMA){ lightStrength=std::max(0.0f, lightStrength-0.05f); }
        if(e.key.keysym.sym==SDLK_PERIOD){ lightStrength=std::min(1.5f, lightStrength+0.05f); }

        // ambient base
        if(e.key.keysym.sym==SDLK_LEFTBRACKET){ ambient=std::max(0.0f, ambient-0.05f); }
        if(e.key.keysym.sym==SDLK_RIGHTBRACKET){ ambient=std::min(1.0f, ambient+0.05f); }

        // parallax BG/MID
        if(e.key.keysym.sym==SDLK_MINUS || e.key.keysym.sym==SDLK_UNDERSCORE){
          parBG  = std::max(0.0f, parBG - 0.05f);
          parMID = std::max(0.0f, parMID - 0.05f);
          SDL_Log("Parallax BG/MID: %.2f / %.2f", parBG, parMID);
        }
        if(e.key.keysym.sym==SDLK_EQUALS || e.key.keysym.sym==SDLK_PLUS){
          parBG  = std::min(1.0f, parBG + 0.05f);
          parMID = std::min(1.0f, parMID + 0.05f);
          SDL_Log("Parallax BG/MID: %.2f / %.2f", parBG, parMID);
        }
      }
      if(e.type==SDL_WINDOWEVENT && e.window.event==SDL_WINDOWEVENT_SIZE_CHANGED){
        W=e.window.data1; H=e.window.data2; glViewport(0,0,W,H); glUniform2f(uScreen,(float)W,(float)H);
      }
    }

    // dt
    uint64_t t1=SDL_GetPerformanceCounter();
    float dt=float((t1-t0)/freq); t0=t1;

    // input (held)
    const Uint8* ks=SDL_GetKeyboardState(nullptr);
    float panSpeed=600.f/std::max(0.25f,camZoom);
    if(ks[SDL_SCANCODE_LEFT]||ks[SDL_SCANCODE_A]) camX-=panSpeed*dt;
    if(ks[SDL_SCANCODE_RIGHT]||ks[SDL_SCANCODE_D]) camX+=panSpeed*dt;
    if(ks[SDL_SCANCODE_UP]||ks[SDL_SCANCODE_W])   camY-=panSpeed*dt;
    if(ks[SDL_SCANCODE_DOWN]||ks[SDL_SCANCODE_S]) camY+=panSpeed*dt;
    if(ks[SDL_SCANCODE_Q]) camZoom=std::max(0.25f,camZoom*(1.f-1.5f*dt));
    if(ks[SDL_SCANCODE_E]) camZoom=std::min(4.0f,  camZoom*(1.f+1.5f*dt));

    glUniform2f(uCam,camX,camY);
    glUniform1f(uZoom,camZoom);
    glUniform1f(uAmbient, ambient);
    glUniform1f(uLightStrength, lightStrength);
    glUniform1i(uLighting, lighting?1:0);

    // visible window (wrap in VS; no clamping here)
    float invZoom=(camZoom<=0.0f)?1.0f:(1.0f/camZoom);
    float viewWpx=W*invZoom, viewHpx=H*invZoom;
    int startX=(int)std::floor(camX/64.f)-1;
    int startY=(int)std::floor(camY/64.f)-1;
    int endX  =(int)std::ceil((camX+viewWpx)/64.f)+1;
    int endY  =(int)std::ceil((camY+viewHpx)/64.f)+1;
    int viewTilesX=endX-startX, viewTilesY=endY-startY;
    glUniform2i(uViewOrigin,startX,startY);
    glUniform2i(uViewSize,viewTilesX,viewTilesY);

    // Pac animation (FG only)
    animTimer+=dt;
    if(animTimer>0.25f){
      animTimer=0; pacOpen=!pacOpen;
      unsigned char pacIdx=pacOpen?PAC_OPEN:PAC_CLOSED;
      for(int i=0;i<MAP_W*MAP_H;i++)
        if(mapFG[i]==PAC_OPEN || mapFG[i]==PAC_CLOSED) mapFG[i]=pacIdx;
      glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, texFG);
      glTexSubImage2D(GL_TEXTURE_2D,0,0,0,MAP_W,MAP_H,GL_RED_INTEGER,GL_UNSIGNED_BYTE,mapFG.data());
    }

    // ---- draw
    glClearColor(0.06f,0.07f,0.09f,1);
    glClear(GL_COLOR_BUFFER_BIT);

    // Bind static textures
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, texAtlas);
    glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, texLight);

    // BG
    if (showBG) {
      glUniform1f(uParallax, parBG);                 // uses live parallax var
      glUniform4f(uTint, 0.85f,0.85f,1.00f,1.00f);
      glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, texBG);
      glDrawElementsInstanced(GL_TRIANGLES,6,GL_UNSIGNED_SHORT,0,viewTilesX*viewTilesY);
    }
    // MID
    if (showMID) {
      glUniform1f(uParallax, parMID);                // uses live parallax var
      glUniform4f(uTint, 0.95f,0.95f,1.00f,1.00f);
      glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, texMID);
      glDrawElementsInstanced(GL_TRIANGLES,6,GL_UNSIGNED_SHORT,0,viewTilesX*viewTilesY);
    }
    // FG
    if (showFG) {
      glUniform1f(uParallax, parFG);                 // fixed at 1.0 unless you change it
      glUniform4f(uTint, 1.0f,1.0f,1.0f,1.0f);
      glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, texFG);
      glDrawElementsInstanced(GL_TRIANGLES,6,GL_UNSIGNED_SHORT,0,viewTilesX*viewTilesY);
    }

    SDL_GL_SwapWindow(win);

    // FPS
    static double fpsAccum=0; static int fpsCount=0;
    fpsAccum+=dt; fpsCount++;
    if(fpsAccum>=1.0){
      SDL_Log("FPS %.1f | Zoom %.2f | Ambient %.2f | Light %.2f | Lgt %s | Par BG/MID %.2f/%.2f",
              fpsCount/fpsAccum, camZoom, ambient, lightStrength, lighting?"ON":"OFF", parBG, parMID);
      fpsAccum=0; fpsCount=0;
    }
  }

  IMG_Quit(); SDL_Quit(); return 0;
}
