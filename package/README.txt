MHFZ Plus 1.5.0
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
  * Windows 10 or 11, or Linux / Steam Deck with the game running under Wine or Proton (see LINUX below).
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

Updates: when a new version is out, MHFZ Plus says so (once a day it asks GitHub; Settings > MHFZ Plus can turn
that off). Press "Update now": it downloads the new version from this project's GitHub page, checks it against the
checksum GitHub lists for it, installs it and opens again, in about a minute. Every choice stays as it is (games,
English, frame rate, graphics backend, shortcuts, Steam entries). Nothing updates without that press.


PLAY
----
Open MHFZ Plus (desktop, Start menu or Steam) instead of the usual launcher. On the Play page pick MezeLounge or
Rain and press START (or Enter). The game's launcher opens: log in and press Play as usual. MHFZ Plus shows what
it is doing as the session goes: launcher open, file check passed, in game, game closed.

Playing with a controller: games started outside Steam get no Steam controller support, and a controller can act up
in them (menus scrolling by themselves). If setup added MHFZ Plus to Steam, START goes through MHFZ Plus's Steam
entry while a controller is plugged in, so Steam's controller support works (Settings > MHFZ Plus > Start through
Steam: with a controller, always or never). No Steam entries yet? Run Install.cmd again and tick "Add to Steam".

The other pages: Settings (every setting of the frame-rate mod, and MHFZ Plus's own options) and Help (bug report,
the original files back, the MHFZ Plus folder, updates, uninstall). Tab switches pages.

MHFZ Plus stays in the background while you play and cleans up after the game closes:
  * MezeLounge: its launcher puts the Japanese files back at every login; MHFZ Plus puts the English ones back
    right after, before the game reads them.
  * Rain: its launcher checks every game file before it starts the game. MHFZ Plus keeps the original files in
    place for that check, puts the English files and the frame-rate mod in the moment the check passes, and
    takes them out again when the game closes. So the Rain launcher always sees an untouched game.


STEAM
-----
Setup adds "MHFZ Plus" (the app) and a Play entry for each server, "MHF-Z MezeLounge (Plus)" and "MHF-Z Rain
(Plus)", as non-Steam games (and reuses artwork you already set for another Monster Hunter Frontier shortcut). The
Play entries start the game in one press, handy on a Steam Deck. Steam shows the game as running for the whole
session, including the launcher.
  * If your controller behaves strangely when started from Steam: right-click the game in Steam > Properties >
    Controller > "Disable Steam Input".
  * If you see problems with the Steam overlay: Properties > General > untick "Enable the Steam Overlay".
  * If you already had a Steam shortcut for the Rain launcher, keep using the "(Plus)" one instead.


FRAME-RATE MOD SETTINGS
-----------------------
MHFZ Plus > Settings has every setting of the frame-rate mod: the keys (Change, then press the key or combination
you want; mouse side buttons work too), the FPS counter, the frame rate, the clip recorder and more. Each setting
explains itself; the arrows change it. Changes are saved as you make them: while the game is running they apply
at once. With MezeLounge and Rain both set up, each has its own settings ("Settings for" at the top).

The most used settings ("Advanced" at the bottom of the list shows the rest):
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
  Controller triggers as L2/R2 (testing)   off by default: Xbox-type controllers - and every controller through
                        Steam Input, a DualSense too - have both triggers on one axis the game can't use. With this
                        on, the triggers press the game's L2/R2 and the stick clicks its L3/R3.
  Keys                  Pause: mod on/off.  Insert: FPS counter.  Delete: save a clip (see REPORTING A PROBLEM).
                        Ctrl+Shift+C: orbit camera on/off (see ORBIT CAMERA).
                        Keys only work while the game window is in front.
  FPS counter           Press Insert once: frames per second (true frames, game + in-between), the "1% low" (how
                        fast the slowest frames are) and the game's own 30. Press again: the detailed view with
                        frame times and a graph (blue = game frame, green = in-between frame, red = hitch). Press
                        again to hide it.

The settings are a plain text file too (mhfz-fps.ini, with a comment on every line), for those who prefer it:
MHFZ Plus > Settings > "The file" shows it.

Notes:
  * More frames on a slower PC: use Graphics backend DXVK (the biggest gain), or cap the frame rate at 60/90/120
    for steadier pacing.
  * Your PC has to draw several frames per game tick. Very high targets (175 Hz and up) may top out
    around 140-150 fps depending on the scene; that's normal.
  * If you see a flicker, a flash or something jumping, please report it with a clip (see REPORTING A PROBLEM).
  * Another d3d9.dll in the game folder (ReShade, dgVoodoo...): MHFZ Plus won't touch it and runs without the
    frame-rate mod (it tells you so).


ORBIT CAMERA (OPTIONAL, CONTROLLER)
-----------------------------------
MHFZ Plus > Settings > Camera. Off unless you turn it on. The right stick turns a camera that circles your hunter,
smoothly and further up and down than the game's own; walls and the ground bring it closer and it eases back out.
L3 puts it behind your hunter. Camera speed, smoothing, deadzone, the angle limits, distance, height and a
shoulder offset are on the same page. Ctrl+Shift+C switches between it and the game's own camera while playing.
Based on TYCHKA's Frontier Orbit.
  * It is the one part of MHFZ Plus that changes values inside the running game: the camera's position and the
    direction your hunter walks by, once per tick. Never the game's code, nothing else. Some servers forbid any
    change to the client's memory: ask your server before you use it.
  * MezeLounge and Rain. With a game version it doesn't know, it stays off and the game's camera is used.
  * Needs a controller Windows sees as an Xbox (XInput) controller - through Steam Input works too. With keyboard
    and mouse, or without a controller connected, the game's own camera is used.
  * Don't run it together with Frontier Helper (TYCHKA's camera mod): MHFZ Plus notices and stays off.


REPORTING A PROBLEM
-------------------
  1. Something you saw (a flicker, a flash, an object or text jumping, garbage on screen): press Delete right
     after it happens. A message confirms it ("Clip 1 saved"). The clip is the last second of frames plus what the
     mod saw, so it shows exactly what went wrong.
  2. Close the game, then MHFZ Plus > Help > "Make a bug report". It puts a zip on your desktop with every
     MHFZ Plus log, your settings and your PC's specs, and the newest clip in a second zip. Your user name in file
     paths is replaced with <user>.
  3. Press "Report it on GitHub": the bug report form opens with your setup (PC, settings) filled in, and the zip
     shows in its folder next to it. Write what happened and drag the zip(s) into "Files". The form:
       https://github.com/Sushii-dev/mhfz-plus/issues/new?template=bug_report.yml
     The newest clip comes in zips of its own, named with when it was saved ("MHFZ-Plus-clip-from-..."), in parts
     GitHub takes (a clip is about 100 MB, GitHub takes 25 MB a file): if it shows the problem, drag them all in.
     No GitHub account? Send the zips to Sushii on Discord.
The recorder keeps the last frames in video memory (about 400 MB at 1080p). If your graphics card is short on
memory, turn "Clip recorder" off in the settings (Delete then does nothing).


IF SOMETHING GOES WRONG
-----------------------
  * Rain says a file is invalid, or anything looks broken: MHFZ Plus > Help > "Put the original
    files back". That undoes every change in the game folders. Then start the game from MHFZ Plus again.
  * The game crashed: MHFZ Plus says so ("GAME CRASHED") and offers a bug report. If it happens again, please
    send one: the frame-rate mod's log notes where a crash happened when the mod was involved.
  * The game freezes on a black loading screen with English on: press Enter (a hidden error box may be open),
    close the game and report it. As a workaround open MHFZ-Plus.ini (MHFZ Plus > Help > "Open the MHFZ Plus folder")
    and change english=1 to english=safe for that server: that uses only the most tested part of the
    translation (menus, items, quests, town dialogue).
  * After a game update, files that changed stay Japanese until the translation is updated. MHFZ Plus never
    overwrites a game file it doesn't recognise.
  * The game doesn't start or freezes right at the start: set Graphics backend to Windows Direct3D 9 in the
    settings and try again, and send a bug report (see REPORTING A PROBLEM).
  * Antivirus: the frame-rate mod is a d3d9.dll that hooks Direct3D 9, which some antivirus programs dislike.
    It may need an exception (the MHFZ Plus folder and the game folder). When MHFZ Plus can't put it in, it says
    so ("No frame-rate mod this time") and the game runs at its own 30 fps.
  * Logs: "Make a bug report" collects them all (see REPORTING A PROBLEM). By hand: MHFZ-Plus-*.log in the MHFZ
    Plus folder, and mhfz-fps.log / mhfz-fps.prev.log in the game folder (Rain: MHFZ Plus folder\fps-state\rain).


RISK
----
Private servers may treat any modified client as cheating. MHFZ Plus only changes files while you play through
it, never patches game code, and the frame-rate mod doesn't change game speed or anything the server sees, but
there are no guarantees. The optional orbit camera does change the game's camera values in memory (see ORBIT
CAMERA). Use it at your own risk.


UNINSTALL
---------
MHFZ Plus > Help > "Uninstall MHFZ Plus" (or Windows Settings > Apps). It puts the original files back, removes
the frame-rate mod, the shortcut and the Steam entries, and deletes the MHFZ Plus folder.


LINUX AND STEAM DECK
--------------------
On Linux, MHFZ Plus runs inside the Wine prefix the game already runs in: Steam (Proton), Lutris, Bottles, Heroic
or plain Wine. The game has to work there first, started the usual way.
  1. Extract the whole zip and open a terminal in the extracted folder.
  2. Run:  sh install.sh
     A setup window (or questions in the terminal, with --terminal) lists the MezeLounge and Rain installs found
     in your prefixes. Tick the ones to use, choose the options, done. Setup needs Python 3; it changes nothing
     outside your home folder and starts no Wine program.
  3. Start MHFZ Plus from your application menu or from Steam, and press START. On a Steam Deck, tick "Add to
     Steam" in setup: the "(Plus)" Play entries start the game in one press from Game Mode.
What setup does in the prefix: the MHFZ Plus files go to drive_c/users/<you>/AppData/Local/MHFZ-Plus (the same
place as on Windows), and Wine is told to load the d3d9.dll in the game folder (the frame-rate mod) for mhf.exe
and client.exe only. Your app menu gets one entry, "MHFZ Plus": the same app as on Windows (it runs in Wine,
like the game), with Play, Settings and Help.
  * The MHFZ Plus app is a Windows program too and needs Wine Mono (Proton includes it; plain Wine offers to
    install it the first time). Without it, play with  mhfz-plus play rain  (or meze) and edit mhfz-fps.ini in the
    MHFZ Plus folder with a text editor. Its animations are off on Linux (Wine draws the window slowly); Settings >
    MHFZ Plus > Animations turns them on.
  * DXVK needs 32-bit Vulkan drivers (the lib32 / i386 Vulkan packages); Steam and most gaming setups have them.
    Without them the game falls back to Wine's own Direct3D.
  * If the frame-rate mod doesn't start under Lutris or Bottles, turn DXVK on for the game there and try again.
  * If the frame rate stays at 60 on a faster monitor, set "Frame rate" in the settings to your refresh rate.
  * Steam: setup closes Steam for a moment to add the games, like on Windows.
  * Updates: "Update now" in the app works here too (the Linux tool does it outside Wine), or run
    mhfz-plus update  in a terminal.
Uninstall: "Uninstall MHFZ Plus" in the menu, or sh uninstall.sh in the package folder. It puts the original
files back and removes the Wine setting, the menu and Steam entries and the MHFZ Plus folder.


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
