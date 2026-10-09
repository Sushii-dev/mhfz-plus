MHFZ Plus 1.3.3
===============

English translation and high frame rate for Monster Hunter Frontier Z, on the MezeLounge and Rain servers.

  * High frame rate: the game still runs its 30 ticks per second exactly as before (same speed, same timing,
    nothing changes for the server), and MHFZ Plus draws extra in-between frames up to your monitor's refresh
    rate (60, 120, 144, 165 Hz...). Turn it on and off in game with the Pause key to compare; the Insert key
    shows an fps counter; the Delete key saves a short clip when something looks wrong (see below).
  * English translation: items, weapons, armor, skills, menus, system messages, tips, tutorials, quests,
    town dialogue, Felyne/Partner and Poogie text. Event quests (sent by the server) and text inside images
    stay Japanese. On Rain, item/weapon/armor names come from the server and stay Japanese too.
    (Machine translation with review passes, so expect the odd strange line.)

Both are optional: pick what you want during setup.


REQUIREMENTS
------------
  * Windows 10 or 11.
  * MezeLounge and/or Rain installed and working (log in once the normal way first).
  * The game should not be inside "C:\Program Files": MHFZ Plus has to change files in the game folder.


INSTALL
-------
  1. Extract the whole zip (right-click > Extract All). Don't run it from inside the zip.
  2. Double-click Install.cmd.
  3. Check the game folders it found (or Browse to them), choose what you want, press Install.
     Leave "Use DXVK" on unless you know your graphics card has no Vulkan support (see Graphics backend below).
     If you choose Steam, Steam closes for a moment and starts again.

Install.cmd can be run again any time to change options, move folders or update to a new version.


PLAY
----
Start the game with the new shortcuts instead of the usual launcher:

    "MHF-Z MezeLounge (Plus)"  or  "MHF-Z Rain (Plus)"

(desktop, Start menu > MHFZ Plus, or your Steam library). Then log in and press Play as usual.

MHFZ Plus stays in the background while you play and cleans up after the game closes:
  * MezeLounge: its launcher puts the Japanese files back at every login; MHFZ Plus puts the English ones back
    right after, before the game reads them.
  * Rain: its launcher checks every game file before it starts the game. MHFZ Plus keeps the original files in
    place for that check, puts the English files and the frame-rate mod in the moment the check passes, and
    takes them out again when the game closes. So the Rain launcher always sees an untouched game.


STEAM
-----
Setup adds "MHF-Z MezeLounge (Plus)" and "MHF-Z Rain (Plus)" as non-Steam games (and reuses artwork you
already set for another Monster Hunter Frontier shortcut). Steam shows the game as running for the whole
session, including the launcher.
  * If your controller behaves strangely when started from Steam: right-click the game in Steam > Properties >
    Controller > "Disable Steam Input".
  * If you see problems with the Steam overlay: Properties > General > untick "Enable the Steam Overlay".
  * If you already had a Steam shortcut for the Rain launcher, keep using the "(Plus)" one instead.


FRAME-RATE MOD SETTINGS
-----------------------
Start menu > MHFZ Plus > "MHFZ Plus settings" opens a window with every setting of the frame-rate mod: the keys
(click a key's button, then press the key or combination you want; mouse side buttons work too), the FPS counter, the
frame rate, the clip recorder and more. Each setting explains itself. Press Save: while the game is
running, changes apply at once.

The most used settings (the window shows them all; "Show advanced settings" shows the rest):
  Frame rate            Automatic = just below your refresh rate. Set a number (60, 120...) to cap it.
  VSync for the extra frames   on: only if you play in exclusive fullscreen without G-Sync/FreeSync and see tearing.
  Lower input latency   optional, off by default (Ctrl+Shift+5 switches it in game): the game reads your input a
                        few milliseconds later without losing any frames or changing speed.
  Graphics backend      DXVK (comes with MHFZ Plus) or Windows Direct3D 9. DXVK runs the game's Direct3D 9 on Vulkan
                        and needs much less CPU time per frame: in our tests 50-70% more frames in towns and crowds.
                        It needs a graphics card with Vulkan 1.3 (most from 2016 on); if it can't start, Windows'
                        Direct3D is used and a message says so. Applies at the next game start.
  Catch quick button taps   optional, off by default: a very quick key or button tap can fall between two of the
                        game's input reads (30 a second) and get lost - a menu press that does nothing. This catches it.
  Keys                  Pause: mod on/off.  Insert: FPS counter.  Delete: save a clip (see REPORTING A GLITCH).
                        Keys only work while the game window is in front.
  FPS counter           Press Insert once: frames per second (true frames, game + in-between), the "1% low" (how
                        fast the slowest frames are) and the game's own 30. Press again: the detailed view with
                        frame times and a graph (blue = game frame, green = in-between frame, red = hitch). Press
                        again to hide it.

The settings are a plain text file too (mhfz-fps.ini, with a comment on every line), for those who prefer it:
"Open the file" in the settings window shows it.

Notes:
  * More frames on a slower PC: use Graphics backend DXVK (the biggest gain), or cap the frame rate at 60/90/120
    for steadier pacing.
  * Your PC has to draw several frames per game tick. Very high targets (175 Hz and up) may top out
    around 140-150 fps depending on the scene; that's normal.
  * If you see a flicker, a flash or something jumping, please report it with a clip (below).
  * Another d3d9.dll in the game folder (ReShade, dgVoodoo...): MHFZ Plus won't touch it and runs without the
    frame-rate mod (it tells you so).


REPORTING A GLITCH
------------------
Right after you see something wrong (a flicker, a flash, an object or text jumping), press Delete. A message
confirms it ("Clip 1 saved"). The clip is the last second of frames plus what the mod saw, so it shows exactly
what went wrong. Zip the folder and send it together with mhfz-fps.log:
  * Rain: %LOCALAPPDATA%\MHFZ-Plus\fps-state\rain\retro-1, retro-2 ... (moved there when the game closes)
  * MezeLounge: the "retro" folder next to mhf.exe
The recorder keeps the last frames in video memory (about 400 MB at 1080p). If your graphics card is short on
memory, turn "Clip recorder" off in the settings (Delete then does nothing).


IF SOMETHING GOES WRONG
-----------------------
  * Rain says a file is invalid, or anything looks broken: Start menu > MHFZ Plus > "Put the original game
    files back". That undoes every change in the game folders. Then start again with the (Plus) shortcut.
  * The game freezes on a black loading screen with English on: press Enter (a hidden error box may be open),
    close the game and report it. As a workaround open MHFZ-Plus.ini (Start menu > MHFZ Plus > "MHFZ Plus folder")
    and change english=1 to english=safe for that server: that uses only the most tested part of the
    translation (menus, items, quests, town dialogue).
  * After a game update, files that changed stay Japanese until the translation is updated. MHFZ Plus never
    overwrites a game file it doesn't recognise.
  * The game doesn't start or freezes right at the start: set Graphics backend to Windows Direct3D 9 in the
    settings and try again, and send mhfz-fps.log.
  * Antivirus: the frame-rate mod is a d3d9.dll that hooks Direct3D 9, which some antivirus programs dislike.
    It may need an exception.
  * Logs, to include when you report a problem:
      MHFZ Plus folder:  MHFZ-Plus-meze.log / MHFZ-Plus-rain.log (and .prev.log, the session before)
      game folder:       mhfz-fps.log and mhfz-fps.prev.log


RISK
----
Private servers may treat any modified client as cheating. MHFZ Plus only changes files while you play through
it, never patches game code, and the frame-rate mod doesn't change game speed or anything the server sees, but
there are no guarantees. Use it at your own risk.


UNINSTALL
---------
Start menu > MHFZ Plus > "Uninstall MHFZ Plus" (or Windows Settings > Apps). It puts the original files back,
removes the frame-rate mod, the shortcuts and the Steam entries, and deletes the MHFZ Plus folder.


ADVANCED
--------
MHFZ-Plus.exe [meze|rain] [--no-english] [--no-fps] [--attach] [--restore] [--prepare]
MHFZ-Plus.exe all --restore     puts every original back (game closed)
MHFZ-Plus.ini per server: game=, english=1|0|safe, fps=1|0, fps_mode=swap|stay (swap = the mod is only in the
game folder while the game runs; stay = always), start=, start_args=.


LICENSE
-------
MHFZ Plus is free software under the MIT license (LICENSE.txt). It covers MHFZ Plus's own code only; Monster
Hunter Frontier Z and everything in it belongs to Capcom. MHFZ Plus is a fan project, not affiliated with Capcom or
any server.
