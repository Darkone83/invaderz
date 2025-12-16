# INVADERZ

A faithful recreation of the classic **Space Invaders** arcade game for the original Xbox, built with C++ and Direct3D 8.

![Platform](https://img.shields.io/badge/Platform-Xbox%20Original-green)
![Language](https://img.shields.io/badge/Language-C%2B%2B-blue)
![API](https://img.shields.io/badge/API-Direct3D%208-orange)

<div align=center>
  <img src="https://github.com/Darkone83/invaderz/blob/main/images/title.png" width=400><img src="https://github.com/Darkone83/invaderz/blob/main/images/demo.png" width=400>
</div>

---

## 🎮 Features

### Core Gameplay
- **Classic Space Invaders Mechanics** - Authentic arcade-style gameplay with 5 rows of 11 invaders
- **Three Enemy Types** - Each row features different invader designs with unique animations
- **Progressive Difficulty** - Enemies speed up as their numbers decrease, ramping from slow march to panic-fast
- **Mystery UFO** - Bonus flying saucer appears periodically for high-value targets
- **Destructible Barriers** - Four protective shields that degrade from enemy and player fire
- **Wave System** - Infinite waves with increasing challenge

### Dual Theme System
- **Classic Theme** - Traditional Space Invaders aesthetic with green, cyan, and magenta invaders
- **Secret Theme** - Hidden alternate theme featuring Xbox-inspired designs and colors
- **Konami Code Unlock** - Enter the classic Konami code (↑↑↓↓←→←→BA) on the title screen to toggle themes

### Scoring & Progression
- **Persistent High Scores** - Top 10 scores saved to Xbox UDATA storage
- **Extra Life System** - Earn a 1-UP every 1,500 points
- **Score Values:**
  - Top Row Invaders: 30 points
  - Middle Rows: 20 points
  - Bottom Row: 10 points
  - Mystery UFO: 50-300 points (varies)

### Visual & Audio
- **Animated Sprites** - Two-frame animations for all invader types
- **Parallax Starfield** - Multi-speed scrolling stars with cloud overlay effects
- **Dynamic Music** - Different tracks for title screen, normal gameplay, and secret theme
- **Sound Effects** - Authentic arcade-style SFX for shooting, explosions, UFO, and bonus lives
- **Smooth Music Transitions** - Crossfade system with volume ramping between game states

### Game Modes
- **Main Game** - Classic wave-based survival gameplay
- **Attract Mode** - Auto-playing demo with AI that activates after 15 seconds of title screen inactivity
- **High Score Entry** - Name entry system using controller D-pad and buttons

---

## 🕹️ Controls

| Action | Button |
|--------|--------|
| Move Left | D-Pad Left / Left Stick |
| Move Right | D-Pad Right / Left Stick |
| Fire | A Button |
| Start Game | START Button |
| Pause / Quit to Menu | START Button (during game) |
| Exit to Dashboard | BACK Button (title screen only) |
| Secret Theme Toggle | Konami Code (↑↑↓↓←→←→BA) |

---

## 📋 Gameplay Mechanics

### Enemy Behavior
- **Formation Movement** - Enemies march left-right in formation, dropping down one row when hitting screen edges
- **Adaptive Speed** - Movement speed increases as enemies are eliminated:
  - 40+ alive: Slow classic march
  - 30-40 alive: Moderate pace
  - 20-30 alive: Faster movement
  - 12-20 alive: Quick advance
  - 6-12 alive: Very fast
  - <6 alive: Panic speed
- **Smart Shooting** - Enemies bias their shots toward the player's current column position
- **Column-Based Fire** - Only the bottom-most alive enemy in each column can shoot
- **Adaptive Fire Rate** - Fewer enemies means faster, more aggressive shooting

### Player Mechanics
- **Lives System** - Start with 3 lives, earn extras at 1,500 point intervals
- **Death & Respawn** - Brief invulnerability period after respawn with "READY" message
- **Single Shot Limit** - Only one player bullet on screen at a time (authentic arcade behavior)
- **Instant Death Zones** - Player dies if enemies reach the bottom defensive line

### Barrier System
- **Four Shields** - Protective barriers positioned between player and enemies
- **Pixel-Perfect Destruction** - Both player and enemy bullets chip away at barriers
- **Strategic Positioning** - Shields provide cover but degrade over time

### UFO System
- **Random Appearances** - Mystery ship flies across top of screen at random intervals
- **Directional Variation** - Can appear from either left or right
- **High Value Target** - Worth 50-300 points, with unique sound effect
- **Independent Movement** - Continues regardless of enemy wave state

---

## 🎯 Game States

### Title Screen
- Display game logo and instructions
- Show current high score
- Konami code detection for theme switching
- Automatic transition to attract mode after 15 seconds of inactivity
- Music: Title theme loop

### Attract Mode (Demo)
- AI-controlled gameplay demonstration
- Shows off game mechanics and scoring
- Returns to title screen when START pressed or demo completes
- Helps prevent screen burn-in on CRT displays

### Gameplay
- Full wave-based Space Invaders experience
- Real-time score and lives display
- Level counter increments after clearing each wave
- Dynamic background with parallax effects
- Music: Different tracks for normal and secret themes

### Game Over
- Display final score and "GAME OVER" message
- Check if player achieved high score
- If high score: Enter name entry screen
- Otherwise: Return to title after brief delay
- High score table overlay

### High Score Entry
- Three-letter name input using D-pad
- Navigate letters with Up/Down, move cursor with Left/Right
- Confirm with A button
- Shows player's rank and score
- Automatic return to title after completion

---

## 🎨 Technical Features

### Graphics Engine
- **4-bit Paletted Sprites** - Compact sprite system with 16-color palettes
- **Sprite Animation System** - Frame-based animation support with configurable timing
- **Dual Sprite Packs** - Separate sprite sets for classic and secret themes
- **Pixel-Perfect Rendering** - Chunky 2x scaled sprites for authentic arcade feel
- **Layered Backgrounds** - Starfield + cloud texture overlay with independent scrolling

### Audio System
- **Streaming Music (.TRM format)** - Compressed audio streaming with crossfade support
- **DirectSound SFX** - WAV file playback for instant sound effects
- **Volume Control** - Individual volume adjustment for music and effects
- **Music State Machine** - Smooth transitions between title, gameplay, and secret themes

### Data Persistence
- **Xbox UDATA Storage** - High score table saved to memory unit or hard drive
- **Top 10 Leaderboard** - Stores rank, name (3 letters), and score
- **Automatic Save/Load** - Transparent persistence handling

### Performance
- **60 FPS Target** - Consistent frame timing for smooth gameplay
- **Fixed-Point Math** - Integer-only calculations for optimal Xbox performance
- **Efficient Rendering** - Optimized sprite and background drawing routines
- **Zero Allocations** - No dynamic memory allocation during gameplay

---

## 📁 Project Structure

```
invaderz/
├── main.cpp              # Application entry point, game loop, state management
├── game.cpp / .h         # Main gameplay logic, wave system, collision detection
├── title.cpp / .h        # Title screen, Konami code, texture loading
├── attract.cpp / .h      # Attract mode with AI-controlled demo gameplay
├── player.cpp / .h       # Player ship movement and shooting
├── enemy.cpp / .h        # Enemy formation, movement, and AI
├── bullet.cpp / .h       # Bullet physics and collision
├── score.cpp / .h        # Scoring system and high score persistence
├── input.cpp / .h        # Xbox controller input handling
├── music.cpp / .h        # Music streaming and crossfade system
├── font.cpp / .h         # Bitmap font rendering
├── sprites.h             # Sprite system structures and definitions
├── sprites_classic.h     # Classic theme sprite pack and palette
├── sprites_secret.h      # Secret theme sprite pack and palette
├── SpriteAnimator.h      # Sprite animation controller
└── invaderz.vcxproj      # Visual Studio project file
```

---

## 🎵 Asset Requirements

### Audio Files (D:\snd\)
- `title.trm` - Title screen music
- `gamea.trm` - Normal gameplay music
- `gameb.trm` - Secret theme gameplay music
- `shoot.wav` - Player shooting sound
- `death.wav` - Enemy death sound
- `pdeath.wav` - Player death sound
- `hit.wav` - Bullet hit barrier sound
- `life.wav` - 1-UP bonus sound
- `ufo.wav` - Mystery UFO sound

### Texture Files (D:\tex\)
- `title_classic.dds` - Classic theme title screen (A8R8G8B8, swizzled)
- `title_secret.dds` - Secret theme title screen (A8R8G8B8, swizzled)
- `cloud_256.dds` - Cloud overlay texture (256x256, A8R8G8B8, swizzled)

---

## 🔧 Building

### Requirements
- **Xbox Development Kit (XDK)** - Original Xbox SDK
- **Visual Studio** - With RDXK development tools
- **Xbox Debug Kit** - For deployment and testing

### Build Steps
1. Open `invaderz.vcxproj` in Visual Studio
2. Select **Xbox Debug** or **Xbox Release** configuration
3. Build the project (F7)
4. Deploy to Xbox development kit
5. Run via Xbox Neighborhood or debugger

### Deployment
- Copy built XBE to Xbox hard drive (D:\)
- Ensure all assets are in correct paths (D:\snd\, D:\tex\)
- Launch from Xbox dashboard or development environment

---

## 🎲 Gameplay Tips

1. **Use Your Shields** - Position yourself behind barriers early, but move when they degrade
2. **Target Edge Enemies** - Kill invaders on the edges to slow down their descent
3. **UFO Timing** - Listen for the UFO sound and quickly line up your shot for bonus points
4. **Don't Rush** - One bullet limit means precision > speed
5. **Watch the Bottom Row** - These invaders can reach you fastest; prioritize them
6. **Manage Your Fire Rate** - The faster enemies move, the harder it is to aim - take your time
7. **Risk vs Reward** - As shields degrade, decide whether to hide or stay aggressive

---

## 🏆 High Score Strategy

- **Maximize UFO Hits** - These are the highest value targets (50-300 pts)
- **Complete Waves Quickly** - Faster completion = less damage taken = more lives preserved
- **Earn Extra Lives** - Each 1,500 points grants a 1-UP; maximize survival chances
- **Top Row Priority** - Worth 30 points each (3x bottom row value)
- **Perfect Accuracy** - Don't waste shots; make every bullet count

---

## 📝 Version History

### Current Version
- Full Space Invaders gameplay with authentic mechanics
- Dual theme system (Classic + Secret)
- Konami code easter egg
- Persistent high score system (Top 10)
- Attract mode with AI demo
- Streaming music with crossfade
- DirectSound SFX integration
- Parallax starfield + cloud effects
- 60 FPS performance on Xbox hardware

---

## 🙏 Credits

**Development**
- Classic Space Invaders arcade game by Taito Corporation (1978)
- Xbox implementation using RXDK/XDK

**Music & Sound**
- Custom .TRM streaming music format
- Arcade-style sound effects

**Graphics**
- 4-bit paletted sprite system
- Pixel art invaders and player ship
- Two complete theme packs

---

## 📜 License

This is a homebrew recreation for educational and preservation purposes. Space Invaders is a trademark of Taito Corporation.

---

## 🎮 Want to Play?

This game requires an original Xbox console with homebrew capability or an Xbox development kit. It is designed to run on authentic hardware for the most accurate experience.

**Enjoy the invasion!** 👾
