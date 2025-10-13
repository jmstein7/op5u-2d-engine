#include <SDL.h>
#include <SDL_image.h>
#include <GLES3/gl3.h>

#include <vector>
#include <array>
#include <string>
#include <cmath>
#include <cstdio>
#include <cstdlib>

// ---------- GLSL (camera + zoom + atlas UVs) ----------
static const char* VS_SRC = R"(#version 300 es
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
layout(location=2) in vec2 iPos;
layout(location=3) in vec2 iScale;
layout(location=4) in float iRot;
layout(location=5) in vec4 iUVRect;

uniform vec2 uScreen;
uniform vec2 uCam;     // camera world pos (px)
uniform float uZoom;   // zoom (1.0 = 1:1)

out vec2 vUV;

void main(){
  float c = cos(iRot), s = sin(iRot);
  vec2 p = vec2(aPos.x * iScale.x, aPos.y * iScale.y);
  vec2 r = vec2(c*p.x - s*p.y, s*p.x + c*p.y);
  vec2 world = iPos + r;

  // camera transform
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

// ---------- Helpers ----------
static GLuint compile(GLenum t,const char* s){
  GLuint x=glCreateShader(t); glShaderSource(x,1,&s,nullptr); glCompileShader(x);
  GLint ok; glGetShaderiv(x,GL_COMPILE_STATUS,&ok);
  if(!ok){ char log[4096]; glGetShaderInfoLog(x,sizeof(log),nullptr,log);
    SDL_Log("Shader compile error:\n%s",log);}
  return x;
}
static GLuint link(GLuint v,GLuint f){
  GLuint p=glCreateProgram(); glAttachShader(p,v); glAttachShader(p,f); glLinkProgram(p);
  GLint ok; glGetProgramiv(p,GL_LINK_STATUS,&ok);
  if(!ok){ char log[4096]; glGetProgramInfoLog(p,sizeof(log),nullptr,log);
    SDL_Log("Program link error:\n%s",log);}
  return p;
}

// ---------- Data ----------
struct Inst { float px,py,sx,sy,rot,ux,uy,uw,uh; };

struct Anim {
  int   frameCount = 1;
  float frameRate  = 0.f; // fps
  std::array<std::array<float,4>,4> uv{}; // up to 4 frames
};
enum AnimID : int { ANIM_PAC=0, ANIM_GHOST=1, ANIM_PELLET=2 };

int main(){
  // SDL + PNG
  if(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO)!=0){
    SDL_Log("SDL_Init failed: %s", SDL_GetError()); return 1;
  }
  if((IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG)==0){
    SDL_Log("IMG_Init PNG failed: %s", IMG_GetError()); return 1;
  }

  // GL context
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,0);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER,1);

  SDL_Window* win=SDL_CreateWindow("OP5U 2D Engine (Camera+Animation)",
      SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,
      1920,1080,SDL_WINDOW_OPENGL|SDL_WINDOW_RESIZABLE);
  if(!win){ SDL_Log("CreateWindow failed: %s", SDL_GetError()); return 1; }

  SDL_GLContext ctx=SDL_GL_CreateContext(win);
  if(!ctx){ SDL_Log("GL context failed: %s", SDL_GetError()); return 1; }
  SDL_GL_SetSwapInterval(1); // vsync ON

  GLuint prog=link(compile(GL_VERTEX_SHADER,VS_SRC),compile(GL_FRAGMENT_SHADER,FS_SRC));
  glUseProgram(prog);
  glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);

  // Quad
  float verts[]={-0.5f,-0.5f,0,0,  0.5f,-0.5f,1,0,  0.5f,0.5f,1,1,  -0.5f,0.5f,0,1};
  uint16_t idx[]={0,1,2,  0,2,3};
  GLuint vao,vbo,ebo; glGenVertexArrays(1,&vao); glBindVertexArray(vao);
  glGenBuffers(1,&vbo); glBindBuffer(GL_ARRAY_BUFFER,vbo);
  glBufferData(GL_ARRAY_BUFFER,sizeof(verts),verts,GL_STATIC_DRAW);
  glGenBuffers(1,&ebo); glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,ebo);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(idx),idx,GL_STATIC_DRAW);
  glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,4*sizeof(float),(void*)0);                glEnableVertexAttribArray(0);
  glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,4*sizeof(float),(void*)(2*sizeof(float))); glEnableVertexAttribArray(1);

  // Window & grid
  int W=1920,H=1080;
  const float SPRITE=64.f, SPACING=72.f;
  auto uvRect=[](int tx,int ty,int tw,int th){
    return std::array<float,4>{(tx*tw)/512.f,(ty*th)/512.f,tw/512.f,th/512.f};
  };

  Anim pac;   pac.frameCount=2; pac.frameRate=4.f; // 4 fps chomp
  pac.uv[0]=uvRect(0,0,256,256); pac.uv[1]=uvRect(1,0,256,256);
  Anim ghost; ghost.frameCount=2; ghost.frameRate=1.f; // blink
  ghost.uv[0]=uvRect(0,1,256,256); ghost.uv[1]=uvRect(1,1,256,256);
  Anim pellet; pellet.frameCount=1; pellet.frameRate=0.f;
  pellet.uv[0]=uvRect(1,1,256,256);

  auto makeGrid=[&](int w,int h,
                    std::vector<Inst>& inst,
                    std::vector<float>& angle,std::vector<float>& omega,
                    std::vector<float>& timer,std::vector<int>& frame,std::vector<int>& animId){
    int GX=int((w-160)/SPACING), GY=int((h-160)/SPACING);
    int N=GX*GY;
    inst.assign(N,{});
    angle.assign(N,0); omega.assign(N,0); timer.assign(N,0); frame.assign(N,0); animId.assign(N,0);
    int k=0;
    for(int y=0;y<GY;y++){
      for(int x=0;x<GX;x++,k++){
        float px=80+x*SPACING, py=80+y*SPACING;
        angle[k]=float(rand()%628)/100.f;
        omega[k]=(float(rand()%300)/100.f)-1.5f;
        int choice=(x+y)%4;
        animId[k]=(choice==0?ANIM_PAC: choice==1?ANIM_GHOST:ANIM_PELLET);
        const Anim* a=(animId[k]==ANIM_PAC?&pac: animId[k]==ANIM_GHOST?&ghost:&pellet);
        const auto& u=a->uv[0];
        inst[k]={px,py,SPRITE,SPRITE,angle[k],u[0],u[1],u[2],u[3]};
      }
    }
  };

  std::vector<Inst>  inst;
  std::vector<float> angle,omega,timer;
  std::vector<int>   frame,animId;
  makeGrid(W,H,inst,angle,omega,timer,frame,animId);

  GLuint ibo; glGenBuffers(1,&ibo); glBindBuffer(GL_ARRAY_BUFFER,ibo);
  glBufferData(GL_ARRAY_BUFFER,inst.size()*sizeof(Inst),inst.data(),GL_DYNAMIC_DRAW);
  size_t stride=sizeof(Inst),off=0;
  glVertexAttribPointer(2,2,GL_FLOAT,GL_FALSE,stride,(void*)off); glEnableVertexAttribArray(2); glVertexAttribDivisor(2,1); off+=2*sizeof(float);
  glVertexAttribPointer(3,2,GL_FLOAT,GL_FALSE,stride,(void*)off); glEnableVertexAttribArray(3); glVertexAttribDivisor(3,1); off+=2*sizeof(float);
  glVertexAttribPointer(4,1,GL_FLOAT,GL_FALSE,stride,(void*)off); glEnableVertexAttribArray(4); glVertexAttribDivisor(4,1); off+=1*sizeof(float);
  glVertexAttribPointer(5,4,GL_FLOAT,GL_FALSE,stride,(void*)off); glEnableVertexAttribArray(5); glVertexAttribDivisor(5,1);

  // Load atlas (run from build dir so ../assets exists)
  SDL_Surface* surf = IMG_Load("../assets/pacman_atlas_512.png");
  if(!surf){ SDL_Log("IMG_Load failed: %s", IMG_GetError()); return 1; }
  SDL_Surface* rgba = SDL_ConvertSurfaceFormat(surf, SDL_PIXELFORMAT_ABGR8888, 0);
  SDL_FreeSurface(surf);
  if(!rgba){ SDL_Log("ConvertSurfaceFormat failed: %s", SDL_GetError()); return 1; }

  GLuint tex; glGenTextures(1,&tex); glBindTexture(GL_TEXTURE_2D,tex);
  glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,rgba->w,rgba->h,0,GL_RGBA,GL_UNSIGNED_BYTE,rgba->pixels);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
  SDL_FreeSurface(rgba);

  // Uniforms
  GLint uAtlas  = glGetUniformLocation(prog,"uAtlas");  glUniform1i(uAtlas,0);
  GLint uScreen = glGetUniformLocation(prog,"uScreen"); glUniform2f(uScreen,(float)W,(float)H);
  GLint uCamLoc = glGetUniformLocation(prog,"uCam");
  GLint uZoomLoc= glGetUniformLocation(prog,"uZoom");
  glViewport(0,0,W,H);

  // Camera state
  float camX=0.f, camY=0.f, camZoom=1.f;

  // Timing + FPS
  uint64_t t0=SDL_GetPerformanceCounter(); const double freq=SDL_GetPerformanceFrequency();
  double fpsTimer=0.0; int frames=0; bool vsyncOn=true;

  // Controls:
  // Arrows/WASD = pan, Q/E = zoom, R = reset, V = vsync toggle
  // [ / ] = slow down / speed up Pac-Man animation fps

  bool quit=false; SDL_Event e;
  while(!quit){
    while(SDL_PollEvent(&e)){
      if(e.type==SDL_QUIT) quit=true;
      if(e.type==SDL_KEYDOWN){
        if(e.key.keysym.sym==SDLK_ESCAPE) quit=true;
        if(e.key.keysym.sym==SDLK_r){ camX=0; camY=0; camZoom=1.f; }
        if(e.key.keysym.sym==SDLK_v){
          vsyncOn=!vsyncOn; SDL_GL_SetSwapInterval(vsyncOn?1:0);
          SDL_Log("VSync: %s", vsyncOn?"ON":"OFF");
        }
        if(e.key.keysym.sym==SDLK_LEFTBRACKET){ pac.frameRate = std::max(1.f, pac.frameRate-1.f); SDL_Log("Pac fps: %.1f", pac.frameRate); }
        if(e.key.keysym.sym==SDLK_RIGHTBRACKET){ pac.frameRate = std::min(20.f, pac.frameRate+1.f); SDL_Log("Pac fps: %.1f", pac.frameRate); }
      }
      if(e.type==SDL_WINDOWEVENT && e.window.event==SDL_WINDOWEVENT_SIZE_CHANGED){
        W=e.window.data1; H=e.window.data2;
        glViewport(0,0,W,H); glUniform2f(uScreen,(float)W,(float)H);
        makeGrid(W,H,inst,angle,omega,timer,frame,animId);
        glBindBuffer(GL_ARRAY_BUFFER,ibo);
        glBufferData(GL_ARRAY_BUFFER,inst.size()*sizeof(Inst),inst.data(),GL_DYNAMIC_DRAW);
      }
    }

    // dt
    uint64_t t1=SDL_GetPerformanceCounter();
    float dt=float((t1-t0)/freq); t0=t1;

    // Input (hold keys)
    const Uint8* ks=SDL_GetKeyboardState(nullptr);
    float panSpeed = 600.f / camZoom;
    if(ks[SDL_SCANCODE_LEFT]||ks[SDL_SCANCODE_A]) camX -= panSpeed*dt;
    if(ks[SDL_SCANCODE_RIGHT]||ks[SDL_SCANCODE_D]) camX += panSpeed*dt;
    if(ks[SDL_SCANCODE_UP]||ks[SDL_SCANCODE_W])    camY -= panSpeed*dt;
    if(ks[SDL_SCANCODE_DOWN]||ks[SDL_SCANCODE_S])  camY += panSpeed*dt;
    if(ks[SDL_SCANCODE_Q]) camZoom = std::max(0.25f, camZoom*(1.f-1.5f*dt));
    if(ks[SDL_SCANCODE_E]) camZoom = std::min(4.0f,  camZoom*(1.f+1.5f*dt));

    // Update rotation + animations
    for(size_t i=0;i<inst.size();++i){
      angle[i]+=omega[i]*dt;
      if(angle[i]>6.2831853f) angle[i]-=6.2831853f;
      if(angle[i]<0.f)        angle[i]+=6.2831853f;
      inst[i].rot=angle[i];

      const Anim* a = (animId[i]==ANIM_PAC? &pac : animId[i]==ANIM_GHOST? &ghost : &pellet);
      if(a->frameCount>1 && a->frameRate>0.f){
        timer[i]+=dt;
        float frameDur = 1.f / a->frameRate;
        if(timer[i] >= frameDur){
          timer[i]-=frameDur;
          frame[i]=(frame[i]+1) % a->frameCount;
          const auto& u=a->uv[frame[i]];
          inst[i].ux=u[0]; inst[i].uy=u[1]; inst[i].uw=u[2]; inst[i].uh=u[3];
        }
      }
    }

    // Upload, set camera, draw
    glBindBuffer(GL_ARRAY_BUFFER,ibo);
    glBufferSubData(GL_ARRAY_BUFFER,0,inst.size()*sizeof(Inst),inst.data());
    glUniform2f(uCamLoc, camX, camY);
    glUniform1f(uZoomLoc, camZoom);

    glClearColor(0.06f,0.07f,0.09f,1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawElementsInstanced(GL_TRIANGLES,6,GL_UNSIGNED_SHORT,0,(GLsizei)inst.size());
    SDL_GL_SwapWindow(win);

    // FPS once per second
    fpsTimer += dt; frames++;
    if(fpsTimer>=1.0){
      SDL_Log("FPS: %.1f | Sprites: %zu | Zoom: %.2f | Cam(%.1f,%.1f) | Pac fps: %.1f",
              frames/fpsTimer, inst.size(), camZoom, camX, camY, pac.frameRate);
      fpsTimer=0; frames=0;
    }
  }

  IMG_Quit(); SDL_Quit(); return 0;
}
