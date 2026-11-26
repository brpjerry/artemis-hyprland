# Artemis Qt

[Artemis Qt](https://github.com/wjbeckett/artemis) is an enhanced cross-platform client for NVIDIA GameStream and [Apollo](https://github.com/ClassicOldSong/Apollo)/[Sunshine](https://github.com/LizardByte/Sunshine) servers. It brings the advanced features from [Artemis Android](https://github.com/ClassicOldSong/moonlight-android) to desktop platforms.

## 🙏 Attribution

Artemis Qt is built upon the excellent foundation of [**Moonlight Qt**](https://github.com/moonlight-stream/moonlight-qt) by the [Moonlight Team](https://github.com/moonlight-stream). We extend our sincere gratitude to the original developers for creating such a robust and well-architected streaming client.

 [![Build](https://img.shields.io/github/actions/workflow/status/moonlight-stream/moonlight-qt/build.yml?branch=master)](https://github.com/moonlight-stream/moonlight-qt/actions/workflows/build.yml?query=branch%3Amaster)
 [![Downloads](https://img.shields.io/github/downloads/moonlight-stream/moonlight-qt/total)](https://github.com/moonlight-stream/moonlight-qt/releases)
 [![Translation Status](https://hosted.weblate.org/widgets/moonlight/-/moonlight-qt/svg-badge.svg)](https://hosted.weblate.org/projects/moonlight/moonlight-qt/)

[![Arch Linux Package](https://github.com/brpjerry/artemis-hyprland/actions/workflows/arch-package.yml/badge.svg?branch=main)](https://github.com/brpjerry/artemis-hyprland/actions/workflows/arch-package.yml)
[![Arch package downloads](https://img.shields.io/github/downloads/brpjerry/artemis-hyprland/total)](https://github.com/brpjerry/artemis-hyprland/releases)

## 💖 Support the Project

If you find Artemis Qt useful and want to support continued development, you can:

[![ko-fi](https://ko-fi.com/img/githubbutton_sm.svg)](https://ko-fi.com/wjbeckett)

Your support helps cover development time, testing on multiple platforms, and keeping the project maintained. Every contribution, no matter how small, is greatly appreciated! 🙏

**Other ways to support:**
- ⭐ **Star the repository** to show your appreciation
- 🐛 **Report bugs** and help improve the experience for everyone
- 🔄 **Share Artemis** with other gamers and streamers
- 💻 **Contribute code** or documentation improvements

## ✨ Artemis Features

Artemis Qt includes all the features of Moonlight Qt, plus these enhanced capabilities:

### 🎯 Phase 1 (Foundation) - Complete
- **📋 Clipboard Sync** - Seamlessly sync clipboard content between client and server - ✅ **COMPLETE**
- **⚡ Server Commands** - Execute custom commands on the Apollo/Sunshine server - ✅ **COMPLETE**
- **🔐 OTP Pairing** - One-Time Password pairing for enhanced security - ✅ **COMPLETE**
- **🎮 Quick Menu** - In-stream overlay menu for easy access to controls - ✅ **COMPLETE**

### 🎮 Phase 2 (Client Controls) - Complete
- **🖥️ Fractional Refresh Rate** - Client-side control for custom refresh rates (e.g., 90Hz, 120Hz) - ✅ **COMPLETE**
- **📐 Resolution Scaling** - Client-side resolution scaling for better performance - ✅ **COMPLETE**
- **🖼️ Virtual Display Control** - Choose whether to use virtual displays - ✅ **COMPLETE**

### 🆕 Phase 3 (UUID & Modern Features) - Complete
- **🆔 UUID-Based App Launching** - Modern app identification system for Apollo/Sunshine servers - ✅ **COMPLETE**
- **🔄 Automatic Fallback** - Seamlessly falls back to legacy app IDs when UUIDs unavailable - ✅ **COMPLETE**
- **🎨 Visual Rebranding** - Official Artemis icons and art:// protocol compatibility with Apollo Android - ✅ **COMPLETE**

### 🚀 Phase 4 (Advanced) - In Progress
- **📱 App Ordering** - Custom app ordering without compatibility mode
- **🔍 Permission Viewing** - View and manage server-side permissions - ✅ **COMPLETE**
- **🎯 Input-Only Mode** - Stream input without video for remote control scenarios

## 🎮 Perfect for Steam Deck

Artemis Qt is specifically optimized for handheld gaming devices like the Steam Deck:

- **Embedded Mode** - Optimized UI for handheld devices
- **GPU-Optimized Rendering** - Efficient rendering for lower-power GPUs
- **Touch-Friendly Interface** - Designed for touch and gamepad navigation
- **Power Efficient** - Optimized for battery life
- **Gamepad Shortcuts** - Built-in gamepad combinations for quick access to features

## 🎮 Keyboard and Gamepad Shortcuts

### Quick Menu Toggle

**Keyboard:** `Ctrl + Alt + Shift + \`

**Gamepad:** `Select + L1 + R1 + Y`

> **💡 Pro Tip:** The Quick Menu provides instant access to clipboard sync, server commands, streaming controls, and more - all without leaving your game!

### Other Shortcuts

**Keyboard Shortcuts** (All require `Ctrl + Alt + Shift` prefix):
- `\` - **Toggle Quick Menu** (NEW!)
- `Q` - Quit stream
- `E` - Quit stream and exit application
- `S` - Toggle performance stats overlay
- `X` - Toggle fullscreen
- `M` - Toggle mouse capture mode
- `Z` - Toggle input capture
- `C` - Toggle cursor visibility
- `V` - Paste clipboard text
- `L` - Toggle pointer region lock
- `D` - Minimize window

**Gamepad Shortcuts:**
- `Select + L1 + R1 + Y` - **Toggle Quick Menu** (NEW!)
- `Start + Select + L1 + R1` - Quit stream
- `Select + L1 + R1 + X` - Toggle performance stats overlay
- Long press `Start` - Toggle mouse emulation mode

> **Note:** The Quick Menu provides easy access to clipboard sync, server commands, and other streaming controls during your session.

## 📥 Downloads
Download this fork's [Arch Linux x86_64 package](https://github.com/brpjerry/artemis-hyprland/releases). For other platforms, see the [upstream releases](https://github.com/wjbeckett/artemis/releases).

### �️ Platform Support

**Windows:**
- **x64 (Intel/AMD)** - Full support with MSI installer and portable ZIP
- **ARM64** - ✨ **NEW!** Native support for Windows on ARM devices (Surface Pro X, Copilot+ PCs, etc.)

**macOS:**
- **Universal Binary** - Native support for both Intel and Apple Silicon Macs

**Linux:**
- **x64 AppImage** - Universal Linux package for x64 systems
- **x64 Flatpak** - Sandboxed package available via Flathub
- **Steam Deck** - Optimized builds for Valve's handheld gaming device

### �🍎 macOS Installation Notes
Development builds may show "Artemis.app is damaged" due to macOS security features. To fix this:

**Option 1 (Recommended):**
```bash
# Remove quarantine attributes
xattr -cr /path/to/Artemis.app
```

**Option 2:**
1. Go to **System Preferences** > **Security & Privacy** > **General**
2. Click **"Allow Anyway"** when prompted about Artemis
3. Try launching the app again

This is normal for development builds and doesn't indicate actual damage to the application.  

> **🔥 Latest Development Features:**
> - **🖥️ Windows ARM64 Support** - Native builds for Windows on ARM devices
> - **🎮 Enhanced Quick Menu** - New keyboard (`Ctrl+Alt+Shift+\`) and gamepad shortcuts (`Select+L1+R1+Y`)
> - Permission viewing from the client
> - Complete Artemis rebranding with official icons and art:// protocol support
> - UUID-based app launching for modern Apollo/Sunshine servers
> - Enhanced error handling and logging
> - Improved compatibility with latest server versions

## 🎮 Moonlight Features (Inherited)
 - Hardware accelerated video decoding on Windows, Mac, and Linux
 - H.264, HEVC, and AV1 codec support (AV1 requires Sunshine and a supported host GPU)
 - YUV 4:4:4 support (Sunshine only)
 - HDR streaming support
 - 7.1 surround sound audio support
 - 10-point multitouch support (Sunshine only)
 - Gamepad support with force feedback and motion controls for up to 16 players
 - Support for both pointer capture (for games) and direct mouse control (for remote desktop)
 - Support for passing system-wide keyboard shortcuts like Alt+Tab to the host

### Nightly Builds
- [Downloads](https://nightly.link/moonlight-stream/moonlight-qt/workflows/build/master)

#### Special Thanks

### Quick Start
```bash
# Clone the repository
git clone https://github.com/wjbeckett/artemis.git
cd artemis

# Run the development setup script
chmod +x scripts/setup-dev.sh
./scripts/setup-dev.sh

# The script will install dependencies and build the project
```

### Windows Build Requirements
* Qt 6.11 SDK or later (earlier versions may work but are not officially supported)
* [Visual Studio 2026](https://visualstudio.microsoft.com/downloads/) (Community edition is fine)
* Select **MSVC** option during Qt installation. MinGW is not supported.
* [7-Zip](https://www.7-zip.org/) (only if building installers for non-development PCs)
* Graphics Tools (only if running debug builds)
  * Install "Graphics Tools" in the Optional Features page of the Windows Settings app.
  * Alternatively, run `dism /online /add-capability /capabilityname:Tools.Graphics.DirectX~~~~0.0.1.0` and reboot.

### macOS Build Requirements
* Qt 6.11 SDK or later (earlier versions may work but are not officially supported)
* Xcode 15 or later (earlier versions may work but are not officially supported)
* [create-dmg](https://github.com/sindresorhus/create-dmg) (only if building DMGs for use on non-development Macs)

### Linux/Unix Build Requirements
* Qt 6 is recommended, but Qt 5.12 or later is also supported (replace `qmake6` with `qmake` when using Qt 5).
* GCC or Clang
* FFmpeg 4.0 or later
* Install the required packages:
  * Debian/Ubuntu:
    * Base Requirements: `libegl1-mesa-dev libgl1-mesa-dev libopus-dev libsdl2-dev libsdl2-ttf-dev libssl-dev libavcodec-dev libavformat-dev libswscale-dev libva-dev libvdpau-dev libxkbcommon-dev wayland-protocols libdrm-dev`
    * Qt 6 (Recommended): `qt6-base-dev qt6-declarative-dev libqt6svg6-dev qt6-wayland qml6-module-qtquick-controls qml6-module-qtquick-templates qml6-module-qtquick-layouts qml6-module-qtqml-workerscript qml6-module-qtquick-window qml6-module-qtquick`
    * Qt 5: `qtbase5-dev qt5-qmake qtdeclarative5-dev qtquickcontrols2-5-dev qml-module-qtquick-controls2 qml-module-qtquick-layouts qml-module-qtquick-window2 qml-module-qtquick2 qtwayland5`
  * RedHat/Fedora (RPM Fusion repo required):
    * Base Requirements: `openssl-devel SDL2-devel SDL2_ttf-devel ffmpeg-devel libva-devel libvdpau-devel opus-devel pulseaudio-libs-devel alsa-lib-devel libdrm-devel`
    * Qt 6 (Recommended): `qt6-qtsvg-devel qt6-qtdeclarative-devel`
    * Qt 5: `qt5-qtsvg-devel qt5-qtquickcontrols2-devel`
* Building the Vulkan renderer requires a `libplacebo-dev`/`libplacebo-devel` version of at least v7.349.0 and FFmpeg 6.1 or later.

**Windows:**
- Visual Studio 2022 with MSVC
- 7-Zip (for packaging)

**Steam Link Hardware Limitations**  
Moonlight builds for Steam Link are subject to hardware limitations of the Steam Link device:
* Maximum resolution: **1080p (1920x1080)**
* Maximum framerate: **60 FPS**
* Maximum video bitrate: **40 Mbps**
* **HDR streaming is not supported** on the original hardware

### Docker containers
If you want to use Docker for building, look at [this repo](https://github.com/cgutman/moonlight-packaging) containing canonical containers
for different architectures, which handle building deps and extra linking for you.

### Build Setup Steps
1. Install the latest Qt SDK (and optionally, the Qt Creator IDE) from https://www.qt.io/download
    * You can install Qt via Homebrew on macOS, but you will need to use `brew install qt --with-debug` to be able to create debug builds of Moonlight.
    * You may also use your Linux distro's package manager for the Qt SDK as long as the packages are Qt 5.12 or later.
    * This step is not required for building on Steam Link, because the Steam Link SDK includes Qt 5.14.
2. Download submodules and dependencies
    * Run `git submodule update --init --recursive` from within `moonlight-qt/`.
    * On Windows and macOS, you must also run `setup-deps.ps1` (Windows) or `setup-deps.py` (macOS).
    * Perform these steps each time you pull new changes from the Git repository.
3. Open the project in Qt Creator or build from qmake on the command line.
    * To build a binary for use on non-development machines, use the scripts in the `scripts` folder.
        * For Windows builds, use `scripts\build-arch.bat` and `scripts\generate-bundle.bat`. Execute these scripts from the root of the repository within a Qt command prompt. Ensure  7-Zip binary directory is on your `%PATH%`.
        * For macOS builds, use `scripts/generate-dmg.sh`. Execute this script from the root of the repository and ensure Qt's `bin` folder is in your `$PATH`.
        * For Steam Link builds, run `scripts/build-steamlink-app.sh` from the root of the repository.
    * To build from the command line for development use on macOS or Linux, run `qmake6 moonlight-qt.pro` then `make debug` or `make release`.
        * The final binary will be placed in `app/moonlight`.
    * To create an embedded build for a single-purpose device, use `qmake6 "CONFIG+=embedded" moonlight-qt.pro` and build normally.
        * This build will lack windowed mode, Discord/Help links, and other features that don't make sense on an embedded device.
        * For platforms with poor GPU performance, add `"CONFIG+=gpuslow"` to prefer direct KMSDRM rendering over GL/Vulkan renderers. Direct KMSDRM rendering can use dedicated YUV/RGB conversion and scaling hardware rather than slower GPU shaders for these operations.

**Linux:**
```bash
# Ubuntu/Debian
sudo apt install qt6-base-dev qt6-declarative-dev libqt6svg6-dev \
  qml6-module-qtquick-controls qml6-module-qtquick-templates \
  qml6-module-qtquick-layouts libegl1-mesa-dev libgl1-mesa-dev \
  libopus-dev libsdl2-dev libsdl2-ttf-dev libssl-dev \
  libavcodec-dev libavformat-dev libswscale-dev libva-dev \
  libvdpau-dev libxkbcommon-dev wayland-protocols libdrm-dev

# Fedora/RHEL
sudo dnf install qt6-qtbase-devel qt6-qtdeclarative-devel \
  qt6-qtsvg-devel openssl-devel SDL2-devel SDL2_ttf-devel \
  ffmpeg-devel libva-devel libvdpau-devel opus-devel \
  pulseaudio-libs-devel alsa-lib-devel libdrm-devel
```

### Build Commands
```bash
# Initialize submodules
git submodule update --init --recursive

# Configure and build
qmake6 moonlight-qt.pro CONFIG+=release
make -j$(nproc)  # Linux
make -j$(sysctl -n hw.ncpu)  # macOS
nmake  # Windows
```

## 🎮 Features Comparison

| Feature | Moonlight Qt | Artemis Qt |
|---------|--------------|------------|
| GameStream/Sunshine Support | ✅ | ✅ |
| Hardware Video Decoding | ✅ | ✅ |
| HDR Streaming | ✅ | ✅ |
| 7.1 Surround Sound | ✅ | ✅ |
| Multi-touch Support | ✅ | ✅ |
| **Clipboard Sync** | ❌ | ✅ |
| **Server Commands** | ❌ | ✅ |
| **OTP Pairing** | ❌ | ✅  |
| **Quick Menu** | ❌ | ✅ |
| **Fractional Refresh Rates** | ❌ | ✅ |
| **Resolution Scaling** | ❌ | ✅ |
| **Virtual Display Control** | ❌ | ✅ |
| **UUID-Based App Launching** | ❌ | ✅ |
| **Development Builds** | ❌ | ✅ |
| **Permission Viewing** | ❌ | ✅ |
| **Custom App Ordering** | ❌ | 📋 |
| **Input-Only Mode** | ❌ | 📋 |

Legend: ✅ Available, 🚧 In Development, 📋 Planned

## 🆕 What's New

### Recent Improvements
- **�️ Windows ARM64 Support** - ✨ **NEW!** Native ARM64 builds for Windows on ARM devices (Surface Pro X, Copilot+ PCs)
- **🎮 Enhanced Quick Menu** - Improved keyboard shortcut (`Ctrl+Alt+Shift+\`) and gamepad combo (`Select+L1+R1+Y`)
- **�🔍 Permission Viewing** - View server-side permissions for clients
- **Complete Visual Rebranding** - Official Artemis icons and branding from Apollo developer, art:// protocol support
- **UUID-Based App Launching** - Modern app identification system that works seamlessly with Apollo/Sunshine servers
- **🔄 Smart Fallback System** - Automatically uses legacy app IDs when UUIDs aren't available
- **🔐 OTP Pairing** - Enhanced security with One-Time Password authentication
- **📋 Better Error Handling** - Improved logging and error messages for troubleshooting

### What's Coming Next
- **📱 Custom App Ordering** - Organize your game library exactly how you want
- **🎯 Input-Only Mode** - Remote control without video streaming for lightweight scenarios

### Development Process
CI builds the Arch Linux x86_64 pacman package on every push to `main` and on
every pull request (download it from the run's artifacts). Each `v*` tag
publishes the package to [Releases](https://github.com/brpjerry/artemis-hyprland/releases),
which also works as a pacman repository; see
[`packaging/arch/README.md`](packaging/arch/README.md). Other platforms are
not built in CI.

## 🤝 Contributing

We welcome contributions! Here's how to get started:

1. **Fork the repository**
2. **Create a feature branch**: `git checkout -b feature/amazing-feature`
3. **Make your changes** and test thoroughly
4. **Commit your changes**: `git commit -m 'Add amazing feature'`
5. **Push to the branch**: `git push origin feature/amazing-feature`
6. **Open a Pull Request**

### Development Guidelines
- Follow the existing code style
- Add tests for new features
- Update documentation as needed
- Test on multiple platforms when possible

## 🔗 Related Projects

- **[Artemis Android](https://github.com/ClassicOldSong/moonlight-android)** - The original Artemis for Android
- **[Apollo Server](https://github.com/ClassicOldSong/Apollo)** - Enhanced GameStream server
- **[Moonlight Qt](https://github.com/moonlight-stream/moonlight-qt)** - The upstream project
- **[Sunshine](https://github.com/LizardByte/Sunshine)** - Open-source GameStream server

## 📄 License

This project is licensed under the GPL v3 License - see the [LICENSE](LICENSE) file for details.

## 🙏 Acknowledgments

- **[ClassicOldSong](https://github.com/ClassicOldSong)** - Creator of Artemis Android and Apollo server
- **[Moonlight Team](https://github.com/moonlight-stream)** - For the excellent foundation
- **[LizardByte](https://github.com/LizardByte)** - For the Sunshine server
- **All contributors** who help make this project better

---

**Made with ❤️ for the gaming community**
