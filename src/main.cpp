#include <SDL.h>
#include <SDL_image.h>
#include <GLES3/gl3.h>

#include <vector>
#include <array>
#include <string>
#include <cmath>
#include <cstdio>
#include <cstdlib>

// ===== Controls =====
//  Arrow keys / WASD : Pan camera
//  Q / E             : Zoom out / in
//  R                 : Reset camera
//  V                 : Toggle vsync (on/off)
//  Esc or window [X] : Quit
// ====================

// ---------- GLSL (adds camera + zoom) ----------
static const char* VS_SRC = R"(#version 300 es
layout(location=0) in vec2 aPos;     // unit quad [-0.5..0.5]
layout(location=1) in vec2 aUV;      // 0..1
layout(location=2) in vec2 iPos;     // sprite world position (pixels)
layout(location=3) in vec2 iScale;   // sprite size (pixels)
layout(location=4) in float iRot;    // rotation (radians)
layout(location=5) in vec4 iUVRect;  // atlas UV rect (u, v, w, h)

uniform vec2 uScreen;                // (W, H) in pixels
uniform vec2 uCam;                   // camera position in pixels
uniform float uZoom;                 // scale (1.0 = 1:1)

out vec2 vUV;

void main(){
  float c = cos(iRot), s = sin(iRot);
  vec2 p = vec2(aPos.x * iScale.x, aPos.y * iScale.y);
  vec2 r = vec2(c*p.x - s*p.y, s*p.x + c*p.y);
  vec2 world = iPos + r;

  // camera: translate then zoom
  vec2 view = (world - uCam) * uZoom;

  // pixel -> NDC
  vec2 ndc = (view / uScreen * 2.0 - 1.0) * vec2(1.0, -1.0);
  gl_Position = vec4(ndc, 0.0, 1.0);

  vUV = iUVRect.xy + aUV * iUVRect.zw;
}
)";

static const char* FS_SRC = R"(#version 300 es
precision mediump float;
in vec2 vUV;
uniform sampler2D uAtlas;
out vec4 frag;
void main(){ frag = texture(uAtlas, vUV); }
)";

// ---------- Small GL helpers ----------
static GLuint compile(GLenum type, const char* src){
  GLuint s = glCreateShader(type);
  glShaderSource(s,1,&src,nullptr);
  glCompileShader(s);
  GLint ok=0; glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
  if(!ok){ char log[4096]; glGetShaderInfoLog(s,sizeof(log),nullptr,log);
    std::fprintf(stderr,"Shader compile error: %s\n",log); }
  return s;
}
static GLuint link(GLuint vs, GLuint fs){
  GLuint p = glCreateProgram();
  glAttachShader(p,vs); glAttachShader(p,fs); glLinkProgram(p);
  GLint ok=0; glGetProgramiv(p,GL_LINK_STATUS,&ok);
  if(!ok){ char log[4096]; glGetProgramInfoLog(p,sizeof(log),nullptr,log);
    std::fprintf(stderr,"Program link error: %s\n",log); }
  return p;
}

// Per-instance data
struct Inst { float px,py,sx,sy,rot,ux,uy,uw,uh; };

int main(){
  // --- SDL init ---
  if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_GAMECONTROLLER|SDL_INIT_AUDIO)!=0){
    SDL_Log("SDL_Init failed: %s", SDL_GetError()); return 1;
  }
  int imgFlags = IMG_INIT_PNG;
  if((IMG_Init(imgFlags) & imgFlags) == 0){
    SDL_Log("IMG_Init failed: %s", IMG_GetError()); return 1;
  }

  // --- GL context (GLES 3) ---
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

  SDL_Window* win = SDL_CreateWindow("OP5U 2D Engine",
    SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
    1920, 1080, SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE);
  if(!win){ SDL_Log("CreateWindow failed: %s", SDL_GetError()); return 1; }

  SDL_GLContext ctx = SDL_GL_CreateContext(win);
  if(!ctx){ SDL_Log("GL context failed: %s", SDL_GetError()); return 1; }
  SDL_GL_SetSwapInterval(1); // vsync ON by default

  // --- Program ---
  GLuint vs = compile(GL_VERTEX_SHADER,   VS_SRC);
  GLuint fs = compile(GL_FRAGMENT_SHADER, FS_SRC);
  GLuint prog = link(vs, fs);
  glUseProgram(prog);

  // Alpha blending (atlas has transparency)
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  // --- Geometry: unit quad with UVs ---
  float verts[] = {
    // x, y,  u, v
    -0.5f,-0.5f, 0,0,
     0.5f,-0.5f, 1,0,
     0.5f, 0.5f, 1,1,
    -0.5f, 0.5f, 0,1
  };
  uint16_t idx[] = {0,1,2, 0,2,3};

  GLuint vao,vbo,ebo; glGenVertexArrays(1,&vao); glBindVertexArray(vao);
  glGenBuffers(1,&vbo); glBindBuffer(GL_ARRAY_BUFFER,vbo);
  glBufferData(GL_ARRAY_BUFFER,sizeof(verts),verts,GL_STATIC_DRAW);
  glGenBuffers(1,&ebo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(idx),idx,GL_STATIC_DRAW);
  glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,4*sizeof(float),(void*)0);                glEnableVertexAttribArray(0);
  glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,4*sizeof(float),(void*)(2*sizeof(float))); glEnableVertexAttribArray(1);

  // --- Initial window size & grid ---
  int W = 1920, H = 1080;
  const float SPRITE   = 64.0f;   // size (pixels)
  const float SPACING  = 72.0f;   // grid spacing (pixels)
  int GRID_X   = int((W - 160) / SPACING);
  int GRID_Y   = int((H - 160) / SPACING);
  int N        = GRID_X * GRID_Y;

  std::vector<Inst>  inst(N);
  std::vector<float> angle(N), omega(N);

  // Atlas (512x512; tiles are 256x256 in 2x2 grid)
  auto uvRect = [](int tileX, int tileY, int tw, int th){
    float u = (tileX*tw)/512.0f;
    float v = (tileY*th)/512.0f;
    float w = tw/512.0f;
    float h = th/512.0f;
    return std::array<float,4>{u,v,w,h};
  };
  const auto PAC_OPEN   = uvRect(0,0,256,256);
  const auto PAC_CLOSED = uvRect(1,0,256,256);
  const auto GHOST      = uvRect(0,1,256,256);
  const auto PELLET     = uvRect(1,1,256,256);

  auto fillGrid = [&](int w, int h){
    GRID_X = int((w - 160) / SPACING);
    GRID_Y = int((h - 160) / SPACING);
    N      = GRID_X * GRID_Y;
    inst.assign(N, {});
    angle.assign(N, 0.0f);
    omega.assign(N, 0.0f);
    int k=0;
    for(int y=0;y<GRID_Y;y++){
      for(int x=0;x<GRID_X;x++,k++){
        float px = 80.0f + x*SPACING;
        float py = 80.0f + y*SPACING;
        const auto& pick = ( (x+y)%4==0 ? PAC_OPEN :
                            ( (x+y)%4==1 ? PAC_CLOSED :
                            ( (x+y)%4==2 ? GHOST : PELLET)));
        inst[k]  = Inst{px,py, SPRITE,SPRITE, 0.0f, pick[0],pick[1],pick[2],pick[3]};
        angle[k] = float(std::rand()%628) / 100.0f;               // 0..~2p
        omega[k] = (float(std::rand()%300) / 100.0f) - 1.5f;      // ±1.5 rad/s
      }
    }
  };
  fillGrid(W,H);

  GLuint ibo; glGenBuffers(1,&ibo); glBindBuffer(GL_ARRAY_BUFFER, ibo);
  glBufferData(GL_ARRAY_BUFFER, inst.size()*sizeof(Inst), inst.data(), GL_DYNAMIC_DRAW);

  size_t stride = sizeof(Inst); size_t off = 0;
  glVertexAttribPointer(2,2,GL_FLOAT,GL_FALSE,stride,(void*)off); glEnableVertexAttribArray(2); glVertexAttribDivisor(2,1); off+=2*sizeof(float);
  glVertexAttribPointer(3,2,GL_FLOAT,GL_FALSE,stride,(void*)off); glEnableVertexAttribArray(3); glVertexAttribDivisor(3,1); off+=2*sizeof(float);
  glVertexAttribPointer(4,1,GL_FLOAT,GL_FALSE,stride,(void*)off); glEnableVertexAttribArray(4); glVertexAttribDivisor(4,1); off+=1*sizeof(float);
  glVertexAttribPointer(5,4,GL_FLOAT,GL_FALSE,stride,(void*)off); glEnableVertexAttribArray(5); glVertexAttribDivisor(5,1);

  // --- Load PNG atlas (assets/pacman_atlas_512.png) ---
  SDL_Surface* surf = IMG_Load("../assets/pacman_atlas_512.png");
  if(!surf){ SDL_Log("IMG_Load failed: %s", SDL_GetError()); return 1; }
  SDL_Surface* rgba = SDL_ConvertSurfaceFormat(surf, SDL_PIXELFORMAT_ABGR8888, 0);
  SDL_FreeSurface(surf);
  if(!rgba){ SDL_Log("ConvertSurfaceFormat failed: %s", SDL_GetError()); return 1; }

  GLuint tex; glGenTextures(1,&tex); glBindTexture(GL_TEXTURE_2D, tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, rgba->w, rgba->h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba->pixels);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  SDL_FreeSurface(rgba);

  // Bind uniforms
  GLint uAtlas  = glGetUniformLocation(prog,"uAtlas");  glUniform1i(uAtlas, 0);
  GLint uScreen = glGetUniformLocation(prog,"uScreen"); glUniform2f(uScreen,(float)W,(float)H);
  GLint uCamLoc = glGetUniformLocation(prog,"uCam");
  GLint uZoomLoc= glGetUniformLocation(prog,"uZoom");

  // Camera state
  float camX = 0.0f, camY = 0.0f, camZoom = 1.0f;

  glViewport(0,0,W,H);

  // --- Frame timing / FPS ---
  uint64_t t0 = SDL_GetPerformanceCounter();
  const double freq = (double)SDL_GetPerformanceFrequency();
  double fpsTimer = 0.0; int frames = 0; double currentFPS = 0.0;

  bool quit=false; SDL_Event e;
  bool vsyncOn = true;
  while(!quit){
    while(SDL_PollEvent(&e)){
      if(e.type==SDL_QUIT) quit=true;
      if(e.type==SDL_KEYDOWN){
        if(e.key.keysym.sym == SDLK_ESCAPE) quit = true;
        if(e.key.keysym.sym == SDLK_r){ camX=0; camY=0; camZoom=1.0f; }
        if(e.key.keysym.sym == SDLK_v){
          vsyncOn = !vsyncOn;
          SDL_GL_SetSwapInterval(vsyncOn ? 1 : 0);
          SDL_Log("VSync: %s", vsyncOn ? "ON" : "OFF");
        }
      }
      if(e.type==SDL_WINDOWEVENT && e.window.event==SDL_WINDOWEVENT_SIZE_CHANGED){
        W=e.window.data1; H=e.window.data2;
        glViewport(0,0,W,H); glUniform2f(uScreen,(float)W,(float)H);
        fillGrid(W,H);
        glBindBuffer(GL_ARRAY_BUFFER, ibo);
        glBufferData(GL_ARRAY_BUFFER, inst.size()*sizeof(Inst), inst.data(), GL_DYNAMIC_DRAW);
      }
    }

    // dt in seconds
    uint64_t t1 = SDL_GetPerformanceCounter();
    float dt = float((t1 - t0) / freq);
    t0 = t1;

    // Input (keyboard state)
    const Uint8* ks = SDL_GetKeyboardState(nullptr);
    float panSpeed = 600.0f / camZoom;     // faster when zoomed out
    if(ks[SDL_SCANCODE_LEFT] || ks[SDL_SCANCODE_A]) camX -= panSpeed * dt;
    if(ks[SDL_SCANCODE_RIGHT]|| ks[SDL_SCANCODE_D]) camX += panSpeed * dt;
    if(ks[SDL_SCANCODE_UP]   || ks[SDL_SCANCODE_W]) camY -= panSpeed * dt;
    if(ks[SDL_SCANCODE_DOWN] || ks[SDL_SCANCODE_S]) camY += panSpeed * dt;
    if(ks[SDL_SCANCODE_Q]) camZoom = std::max(0.25f, camZoom * (1.0f - 1.5f*dt));
    if(ks[SDL_SCANCODE_E]) camZoom = std::min(4.0f,  camZoom * (1.0f + 1.5f*dt));

    // Update rotating instances
    for(int i=0;i<N;i++){
      angle[i] += omega[i] * dt;
      if(angle[i] > 6.2831853f) angle[i] -= 6.2831853f;
      if(angle[i] < 0.0f)       angle[i] += 6.2831853f;
      inst[i].rot = angle[i];
    }
    glBindBuffer(GL_ARRAY_BUFFER, ibo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, inst.size()*sizeof(Inst), inst.data());

    // Set camera uniforms
    glUniform2f(uCamLoc, camX, camY);
    glUniform1f(uZoomLoc, camZoom);

    // Draw
    glClearColor(0.06f,0.07f,0.09f,1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawElementsInstanced(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0, N);
    SDL_GL_SwapWindow(win);

    // FPS (console once per second)
    frames++; fpsTimer += dt;
    if(fpsTimer >= 1.0){
      currentFPS = frames / fpsTimer;
      SDL_Log("FPS: %.1f  | Sprites: %d  | Zoom: %.2f  | Cam(%.1f, %.1f)", currentFPS, N, camZoom, camX, camY);
      frames = 0; fpsTimer = 0.0;
    }
  }

  IMG_Quit(); SDL_Quit();
  return 0;
}
