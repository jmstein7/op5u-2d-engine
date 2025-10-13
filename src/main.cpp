#include <SDL.h>
#include <SDL_image.h>
#include <GLES3/gl3.h>

#include <vector>
#include <array>
#include <string>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>

// =====================================================
//  OP5U 2D ENGINE — GPU TILEMAP + ANIMATED PACMAN
// =====================================================

// ---------- Vertex / Fragment Shaders ----------
static const char* TILE_VS = R"(#version 300 es
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;

uniform vec2  uScreen;
uniform vec2  uCam;
uniform float uZoom;
uniform float uTileSize;
uniform ivec2 uViewOrigin;
uniform ivec2 uViewSize;
uniform ivec2 uMapSize;
uniform ivec2 uAtlasTiles;
uniform highp usampler2D uTilemap;

out vec2 vUV;

void main(){
  int ix = gl_InstanceID % uViewSize.x;
  int iy = gl_InstanceID / uViewSize.x;
  ivec2 mapCoord = uViewOrigin + ivec2(ix, iy);
  mapCoord = clamp(mapCoord, ivec2(0), uMapSize - ivec2(1));

  uint idx = texelFetch(uTilemap, mapCoord, 0).r;
  int tilesX = uAtlasTiles.x;
  int tx = int(idx) % tilesX;
  int ty = int(idx) / tilesX;

  vec2 atlasBase = vec2(float(tx)/float(uAtlasTiles.x),
                        float(ty)/float(uAtlasTiles.y));
  vec2 atlasSize = vec2(1.0/float(uAtlasTiles.x),
                        1.0/float(uAtlasTiles.y));

  vec2 local = aPos * uTileSize;
  vec2 world = (vec2(mapCoord) * uTileSize) + local;
  vec2 view  = (world - uCam) * uZoom;
  vec2 ndc   = (view / uScreen * 2.0 - 1.0) * vec2(1.0, -1.0);

  gl_Position = vec4(ndc, 0.0, 1.0);
  vUV = atlasBase + aUV * atlasSize;
}
)";

static const char* TILE_FS = R"(#version 300 es
precision mediump float;
in vec2 vUV;
uniform sampler2D uAtlas;
out vec4 frag;
void main(){ frag = texture(uAtlas, vUV); }
)";

// ---------- Helper compile/link ----------
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

// =====================================================
int main(){
  if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO)!=0){
    SDL_Log("SDL_Init failed: %s", SDL_GetError()); return 1; }
  if(!(IMG_Init(IMG_INIT_PNG)&IMG_INIT_PNG)){
    SDL_Log("IMG_Init failed: %s", IMG_GetError()); return 1; }

  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,0);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);

  SDL_Window* win=SDL_CreateWindow("OP5U GPU Tilemap",
    SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,
    1920,1080,SDL_WINDOW_OPENGL|SDL_WINDOW_RESIZABLE);
  SDL_GLContext ctx=SDL_GL_CreateContext(win);
  SDL_GL_SetSwapInterval(1);

  GLuint prog=link(compile(GL_VERTEX_SHADER,TILE_VS),compile(GL_FRAGMENT_SHADER,TILE_FS));
  glUseProgram(prog);
  glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);

  // ---------- Quad geometry ----------
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

  // ---------- Load atlas ----------
  SDL_Surface* surf=IMG_Load("../../assets/atlas_8x8_512.png");
  if(!surf){ surf=IMG_Load("../assets/atlas_8x8_512.png"); }
  if(!surf){ surf=IMG_Load("~/assets/atlas_8x8_512.png"); }
  if(!surf){ SDL_Log("Failed to load atlas: %s", IMG_GetError()); return 1; }

  SDL_Surface* rgba=SDL_ConvertSurfaceFormat(surf,SDL_PIXELFORMAT_ABGR8888,0);
  SDL_FreeSurface(surf);
  GLuint texAtlas; glGenTextures(1,&texAtlas);
  glBindTexture(GL_TEXTURE_2D,texAtlas);
  glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,rgba->w,rgba->h,0,GL_RGBA,GL_UNSIGNED_BYTE,rgba->pixels);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
  SDL_FreeSurface(rgba);

  // ---------- Tilemap (integer texture) ----------
  const int MAP_W=128, MAP_H=128;
  std::vector<unsigned char> map(MAP_W*MAP_H,0);
  for(int y=0;y<MAP_H;y++)
    for(int x=0;x<MAP_W;x++)
      map[y*MAP_W+x]=((x/2+y/2)&1)?2:0;  // Pac vs Ghost

  GLuint texMap; glGenTextures(1,&texMap);
  glBindTexture(GL_TEXTURE_2D,texMap);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
  glTexImage2D(GL_TEXTURE_2D,0,GL_R8UI,MAP_W,MAP_H,0,GL_RED_INTEGER,GL_UNSIGNED_BYTE,map.data());

  // ---------- Uniforms ----------
  GLint uScreen=glGetUniformLocation(prog,"uScreen");
  GLint uCam=glGetUniformLocation(prog,"uCam");
  GLint uZoom=glGetUniformLocation(prog,"uZoom");
  GLint uTileSz=glGetUniformLocation(prog,"uTileSize");
  GLint uViewOrigin=glGetUniformLocation(prog,"uViewOrigin");
  GLint uViewSize=glGetUniformLocation(prog,"uViewSize");
  GLint uMapSize=glGetUniformLocation(prog,"uMapSize");
  GLint uAtlasTiles=glGetUniformLocation(prog,"uAtlasTiles");
  GLint uAtlas=glGetUniformLocation(prog,"uAtlas");
  GLint uTilemap=glGetUniformLocation(prog,"uTilemap");

  glUniform1i(uAtlas,0);
  glUniform1i(uTilemap,1);

  int W=1920,H=1080;
  glViewport(0,0,W,H);
  glUniform2f(uScreen,(float)W,(float)H);

  float camX=0,camY=0,camZoom=1;
  const float TILE_SIZE=64.f;
  const int ATLAS_TILES_X=8,ATLAS_TILES_Y=8;
  glUniform1f(uTileSz,TILE_SIZE);
  glUniform2i(uMapSize,MAP_W,MAP_H);
  glUniform2i(uAtlasTiles,ATLAS_TILES_X,ATLAS_TILES_Y);

  uint64_t t0=SDL_GetPerformanceCounter();
  const double freq=SDL_GetPerformanceFrequency();
  double fpsTimer=0; int frames=0; bool vsyncOn=true;

  float animTimer=0; bool pacOpen=true;

  bool quit=false; SDL_Event e;
  while(!quit){
    while(SDL_PollEvent(&e)){
      if(e.type==SDL_QUIT) quit=true;
      if(e.type==SDL_KEYDOWN){
        if(e.key.keysym.sym==SDLK_ESCAPE) quit=true;
        if(e.key.keysym.sym==SDLK_r){ camX=0; camY=0; camZoom=1; }
        if(e.key.keysym.sym==SDLK_v){
          vsyncOn=!vsyncOn; SDL_GL_SetSwapInterval(vsyncOn?1:0);
          SDL_Log("VSync: %s",vsyncOn?"ON":"OFF");
        }
      }
      if(e.type==SDL_WINDOWEVENT && e.window.event==SDL_WINDOWEVENT_SIZE_CHANGED){
        W=e.window.data1; H=e.window.data2;
        glViewport(0,0,W,H); glUniform2f(uScreen,(float)W,(float)H);
      }
    }

    uint64_t t1=SDL_GetPerformanceCounter();
    float dt=float((t1-t0)/freq); t0=t1;

    const Uint8* ks=SDL_GetKeyboardState(nullptr);
    float panSpeed=600.f/std::max(0.25f,camZoom);
    if(ks[SDL_SCANCODE_LEFT]||ks[SDL_SCANCODE_A]) camX-=panSpeed*dt;
    if(ks[SDL_SCANCODE_RIGHT]||ks[SDL_SCANCODE_D]) camX+=panSpeed*dt;
    if(ks[SDL_SCANCODE_UP]||ks[SDL_SCANCODE_W]) camY-=panSpeed*dt;
    if(ks[SDL_SCANCODE_DOWN]||ks[SDL_SCANCODE_S]) camY+=panSpeed*dt;
    if(ks[SDL_SCANCODE_Q]) camZoom=std::max(0.25f,camZoom*(1.f-1.5f*dt));
    if(ks[SDL_SCANCODE_E]) camZoom=std::min(4.0f, camZoom*(1.f+1.5f*dt));
    glUniform2f(uCam,camX,camY);
    glUniform1f(uZoom,camZoom);

    // ---------- visible region ----------
    float invZoom=(camZoom<=0.0f)?1.0f:(1.0f/camZoom);
    float viewWpx=W*invZoom, viewHpx=H*invZoom;
    int startX=(int)std::floor(camX/TILE_SIZE)-1;
    int startY=(int)std::floor(camY/TILE_SIZE)-1;
    int endX=(int)std::ceil((camX+viewWpx)/TILE_SIZE)+1;
    int endY=(int)std::ceil((camY+viewHpx)/TILE_SIZE)+1;
    startX=std::max(0,std::min(startX,MAP_W-1));
    startY=std::max(0,std::min(startY,MAP_H-1));
    endX  =std::max(1,std::min(endX,MAP_W));
    endY  =std::max(1,std::min(endY,MAP_H));
    int viewTilesX=endX-startX;
    int viewTilesY=endY-startY;
    glUniform2i(uViewOrigin,startX,startY);
    glUniform2i(uViewSize,viewTilesX,viewTilesY);

    // ---------- Pac-Man animation ----------
    animTimer+=dt;
    if(animTimer>0.25f){
      animTimer=0; pacOpen=!pacOpen;
      unsigned char pacIdx=pacOpen?0:1;
      for(int i=0;i<MAP_W*MAP_H;i++)
        if(map[i]==0||map[i]==1) map[i]=pacIdx;
      glActiveTexture(GL_TEXTURE1);
      glBindTexture(GL_TEXTURE_2D,texMap);
      glTexSubImage2D(GL_TEXTURE_2D,0,0,0,MAP_W,MAP_H,
                      GL_RED_INTEGER,GL_UNSIGNED_BYTE,map.data());
    }

    // ---------- draw ----------
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,texAtlas);
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D,texMap);
    glClearColor(0.06f,0.07f,0.09f,1);
    glClear(GL_COLOR_BUFFER_BIT);
    int instances=viewTilesX*viewTilesY;
    glDrawElementsInstanced(GL_TRIANGLES,6,GL_UNSIGNED_SHORT,0,instances);
    SDL_GL_SwapWindow(win);

    // ---------- FPS ----------
    fpsTimer+=dt; frames++;
    if(fpsTimer>=1.0){
      SDL_Log("FPS %.1f | Tiles %dx%d | Cam(%.1f,%.1f) Zoom %.2f",
              frames/fpsTimer,viewTilesX,viewTilesY,camX,camY,camZoom);
      fpsTimer=0; frames=0;
    }
  }

  IMG_Quit(); SDL_Quit(); return 0;
}
