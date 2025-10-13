# OP5U 2D Engine

A lightweight OpenGL ES 3.1 sprite engine built from scratch for the **Orange Pi 5 Ultra** (RK3588S).  
Currently rendering hundreds of rotating sprites at 60 FPS using the Mesa Panfrost GPU driver.

![Pac-Man grid demo](screenshots/pacman_grid.jpg)

---

## 🕹 Features
- GLES 3.1 renderer (Panfrost driver)
- Instanced sprite rendering (~10 000 sprites)
- Camera pan + zoom + vsync toggle
- Per-sprite continuous rotation
- Texture atlas system (512 × 512)
- Runs full-speed on Ubuntu Jammy GNOME Desktop

---

## 🎮 Controls
| Key | Action |
|-----|---------|
| Arrows / WASD | Pan camera |
| Q / E | Zoom out / in |
| R | Reset camera |
| V | Toggle vsync |
| ESC | Quit |

---

## ⚙ Hardware
Orange Pi 5 Ultra (16 GB RAM) + NVMe SSD + Ubuntu 22.04 Jammy Desktop

---

## 🔮 Coming Next
- Sprite animation (flip-book style)
- Tilemap + parallax background
- Audio playback via SDL2_mixer
- Full demo: **Pac Field**

---

https://hackaday.io/project/204239-op5u-orange-pi-5-ultra-2d-game-engine

🕹️ Engine Controls & Build Notes

This demo runs entirely on the Orange Pi 5 Ultra, using the open-source Panfrost driver for the Mali-G610 GPU and a custom C++17 engine built with SDL2 and OpenGL ES 3.1.
Everything on screen — sprite animation, camera movement, zoom, and rotation — is executed directly on the GPU with instancing.

🎮 Controls
Key	Action
W / A / S / D or Arrow keys	Pan the camera
Q / E	Zoom out / in
R	Reset camera position and zoom
[ / ]	Decrease / increase Pac-Man animation speed
V	Toggle VSync on/off
ESC	Quit

The console prints live FPS, zoom level, camera position, and current animation rate.

🧩 Build Instructions (Orange Pi 5 Ultra)
# 1. Clone the repo
git clone https://github.com/jmstein7/op5u-2d-engine.git
cd op5u-2d-engine

# 2. Install dependencies
sudo apt install -y libsdl2-dev libsdl2-image-dev build-essential cmake mesa-utils

# 3. Build
mkdir -p build && cd build
cmake ..
make -j$(nproc)

# 4. Run (from build dir so ../assets/ resolves)
./op5u


💡 If you run from another directory, set an explicit asset path:

export OP5U_ASSETS=~/op5u_2d_engine/assets
./build/op5u

🧠 Current Features

GPU-instanced sprite rendering

Flip-book animation system (frame-timed UV swapping)

Pan + zoom camera controls

Adjustable Pac-Man animation rate

Real-time FPS and VSync toggle

Clean .gitignore and build structure

🗺️ Next Steps

Tilemap rendering with view-frustum culling

Sprite depth sorting and parallax layers

On-screen text (SDL_ttf) for HUD/FPS display

Multi-atlas texture batching

⚙️ Environment
Component	Version
OS	Ubuntu 22.04 Jammy Desktop (GNOME, NVMe install)
GPU Driver	Mesa 23.x (Panfrost OpenGL ES 3.1)
Compiler	GCC 13.3
Libraries	SDL2 2.0.5 · SDL_Image 2.0.5 · CMake 3.22

## 📄 License
MIT License © 2025 Jonathan Stein
