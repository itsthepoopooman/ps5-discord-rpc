# PS5 Discord Rich Presence (RPC)

> ⚠️ **Disclaimer:** This project was created with the assistance of AI.

A native, zero-PC Discord Rich Presence background daemon for jailbroken PlayStation 5 consoles. Runs directly on your PS5—no companion PC or bridge app required.

---

## Features

- **100% Console Native:** Runs silently in the background as an ELF daemon.
- **Official PS5 Presence:** Displays "Playing on PlayStation 5" with the official console badge and game covers.
- **Automatic Icon Resolution & Upload:** Pulls local game icons (`icon0.png`) and uploads them automatically if not present on Sony TMDB.
- **Smart Game Detection:**
  - Auto-clears Discord presence when a game closes (leaving your profile clean).
  - Restarts the playtime timer (`00:00`) whenever you switch games.
- **Web Dashboard (`http://<PS5_IP>:6767`):**
  - Manage settings, customize status/buttons, and toggle games on/off.
  - Game Library view with one-click title blacklisting.
  - Live Discord card preview.
- **Single-Instance Protection:** Prevents duplicate payloads from running if already active.
- **Accurate Clock Sync:** Automatically synchronizes true UTC time over NTP/HTTPS so elapsed playtime timers never freeze.

---

## Quick Setup

### 1. Send the Payload
Make sure your PS5 is jailbroken and listening for payloads (e.g. port `9021`). Send `discord_rpc.elf`:

```bash
nc -q0 <PS5_IP> 9021 < discord_rpc.elf
```

A notification will pop up on your TV screen displaying your console's web dashboard address:

![PS5 Notification Pop-up](assets/ps5_notification.png)

### 2. Configure via Web Dashboard
1. On your phone or PC, open your browser to:
   ```
   http://<PS5_IP>:6767
   ```
2. Paste your **Discord User Token**.
3. (Optional) Customize buttons, details, or blacklist games.
4. Click **Save to PS5 & Update**.
5. Launch any game—your Discord status updates instantly!

![Discord Rich Presence Preview](assets/discord_presence.png)

---

## File Storage Locations

Configuration and cache files are stored permanently on console storage across reboots:

- `/data/discord_rpc/config.json` — User settings (token, buttons, toggles, blacklist)
- `/data/discord_rpc/icon_cache.json` — Uploaded icon URL cache

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

- **[notmaj0r/ProsperoMgr](https://github.com/notmaj0r/ProsperoMgr)** — Game detection logic and web interface design.
- **[SirHumza/orbisRPC](https://github.com/SirHumza/orbisRPC)** — Original PS4 Discord RPC daemon, protocol reference, and network engine.
- **[ps5-payload-dev / sdk](https://github.com/ps5-payload-dev/sdk)** — Native PS5 payload SDK and toolchain.
- **[Catbox.moe](https://catbox.moe)** — Public image hosting for on-console icon uploads.
