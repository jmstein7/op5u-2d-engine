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

## 📄 License
MIT License © 2025 Jonathan Stein
