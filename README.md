# PS5 Discord Rich Presence & Voice Chat (RPC)

> **Disclaimer:** This project was created with the assistance of AI.

A native, zero-PC Discord Rich Presence & Voice Chat daemon for jailbroken PlayStation 5 consoles. Runs directly on your PS5—no companion PC or bridge app required.

---

## Features

- **100% Console Native:** Runs silently in the background as an ELF daemon.
- **Discord Voice Chat (VC) Support:**
  - Browse your Discord servers and voice channels directly from the Web Dashboard.
  - 1-click Join Voice Channel directly to Discord Gateway.
  - Live Microphone Mute, Audio Deafen, and Disconnect controls.
- **In-Game Overlay (1:1 Kabuki PS5 Integration):**
  - Connects to the local Kabuki overlay service (`127.0.0.1:38590`).
  - Hold **L3 + R3** on your DualSense controller while in-game to bring up the Discord overlay card.
  - View voice status, toggle microphone mute, toggle deafen, disconnect from voice, and inspect active Rich Presence without exiting your game.
- **Official PS5 Presence:** Displays "Playing on PlayStation 5" with the official console badge and game covers.
- **Automatic Icon Resolution & Upload:** Uploads each installed game's local `icon0.png` to Catbox once, then falls back to Sony TMDB / Steam artwork only when no local icon exists.
- **Smart Game Detection:**
  - Auto-clears Discord presence when a game closes (leaving your profile clean).
  - Restarts the playtime timer whenever you switch games.
- **Multiple Discord Login Methods (No Token Mining Required):**
  - **QR Code Remote Auth:** Point your phone camera at the screen and tap "Log In" using the Discord Mobile app (`User Settings -> Scan QR Code`).
  - **Embedded Webview / Browser Login:** Log in via Discord Web in-browser; the session token is automatically captured upon completion.
  - **Email + Password Login:** Sign in directly with email/phone and password, with full Two-Factor Authentication (2FA) prompt support.
  - **Manual Token Paste:** Direct token paste fallback.
- **Web Dashboard (`http://<PS5_IP>:6767`):**
  - Voice Chat Hub: Browse servers, join voice channels, and toggle mute/deafen.
  - Game Manager: One-click title blacklisting and library view.
  - Custom presence settings, custom buttons, and live Discord preview card.
- **Single-Instance Protection:** Prevents duplicate payloads from running if already active.
- **Accurate Clock Sync:** Automatically synchronizes true UTC time over NTP/HTTPS so elapsed playtime timers never freeze.

---

## Quick Setup

### 1. Send the Payload
Make sure your PS5 is jailbroken and listening for payloads (e.g. port `9021`). Send `discord_rpc.elf`:

```bash
nc -q0 <PS5_IP> 9021 < discord_rpc.elf
```

A notification will pop up on your TV screen displaying your console's web dashboard address and overlay shortcut:

![PS5 Notification Pop-up](assets/ps5_notification.png)

### 2. Log in & Join Voice Chat

1. Open `http://<PS5_IP>:6767` on your phone, PC, or console browser.
2. Choose your login method:
   - **QR Code:** Open Discord on your phone, go to **User Settings &rarr; Scan QR Code**, and scan the screen.
   - **Webview Login:** Click **Open Discord Login Window** to sign in through Discord Web.
   - **Email + Password:** Enter your login credentials and 2FA code.
   - **Token Paste:** Paste your token directly.
3. Switch to the **Voice Chat (VC)** tab:
   - Select your Discord server from the dropdown.
   - Click **Join Voice Channel** on any channel to connect!
4. Launch any game—your Discord status updates instantly!

![Web Dashboard Settings](assets/web_panel_dashboard.png)

![Game Manager](assets/web_panel_game_manager.png)

### 3. In-Game Overlay Controls

When playing any game:
- Hold **L3 + R3** on your DualSense controller to open the Kabuki in-game overlay.
- Navigate to the **Discord** tile.
- Toggle Mic Mute, Toggle Deafen, or Disconnect from Voice Channel on the fly!

![Discord Rich Presence Preview](assets/discord_presence.png)

---

## File Storage Locations

Configuration and cache files are stored permanently on console storage across reboots:

- `/data/discord_rpc/config.json` — User settings (token, buttons, toggles, blacklist)
- `/data/discord_rpc/icon_cache.json` — Per-title Catbox upload cache (title ID → uploaded icon URL)

*(Fallback: `/user/home/discord_rpc/` is used automatically if `/data` is not accessible).*

---

## Building from Source

Requires the [PS5 Payload SDK](https://github.com/ps5-payload-dev/sdk).

```bash
make
```

*(Optionally specify the SDK path if installed outside `/opt/ps5-payload-sdk`)*:
```bash
make PS5_PAYLOAD_SDK=/path/to/ps5-payload-sdk
```

The compiled binary will be generated as `discord_rpc.elf`.

---

## Credits & Acknowledgements

- **[stupiditytries/kabuki-ps5](https://github.com/stupiditytries/kabuki-ps5)** — In-game PS5 overlay framework (L3 + R3).
- **[notmaj0r/ProsperoMgr](https://github.com/notmaj0r/ProsperoMgr)** — Game detection logic and web interface design.
- **[SirHumza/orbisRPC](https://github.com/SirHumza/orbisRPC)** — Original PS4 Discord RPC daemon, protocol reference, and network engine.
- **[ps5-payload-dev / sdk](https://github.com/ps5-payload-dev/sdk)** — Native PS5 payload SDK and toolchain.
- **[Catbox.moe](https://catbox.moe)** — Public image hosting for on-console icon uploads.
