## What's new

- **Home View**: a Switch 2 style game picker you can use entirely with a controller, with a flowing rainbow selection ring, sounds, and quick setting chips (renderer, frame rate, accuracy, filter, anti-aliasing, volume and more).
- **Sharper art**: square covers and fan-art banners from the LaunchBox Games Database, preferring the Switch 2 Edition, at a higher resolution. The loading screen uses the banner too.
- **Button hints that match your controller**: Switch, Xbox or PlayStation.
- **Settings with a controller**: move through Configure and a game's Properties with the d-pad. The left stick shows a floating cursor and the right stick scrolls. When adding a game folder, A opens folders.
- **Reorder Game Position** and **Remove Game Directory** in a game's menu (+ or X).
- **Skip shader loading** with A on the loading screen, after a confirmation.
- **RAM and VRAM** in the performance overlay, which also names the upscaler in use (FSR 4 or FSR 3.1).
- **A new update screen**, Switch 2 style. Updates come only from the TuttiFruchii repository on GitHub.

## Fixes

- **FSR 4 works on Linux** (AMD with Mesa RADV). It never started there: buffer device addresses were left off on Vulkan 1.2 and newer, and the FSR 4 context was created in a form the Linux FSR 4 runtime refuses. Windows was not affected.
- The performance overlay no longer says FSR 3.1 while FSR 4 is running.
