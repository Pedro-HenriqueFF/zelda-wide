# Features

Detailed behaviour of everything Zelda Wide adds to the game. The [README](README.md) has the short version; this file is the reference.
The unmodified game runs underneath: the compositor only reads the game's state and draws around and over its picture (the few places where the
emulator patches the game's code, such as the sword at the screen borders, are named below).

**Not implemented:** audio (the APU is ignored), widescreen caves and dungeons (they use the big centred picture).

Contents: [Overworld view](#1-overworld-view) · [Secrets and fog](#2-secrets-and-fog) · [Atmosphere](#3-atmosphere) · [Caves and dungeons](#4-caves-and-dungeons) · [Neighbour screens running](#5-neighbour-screens-running-n-experimental) · [Saving](#6-saving) · [Performance and tools](#7-performance-and-tools)

## 1. Overworld view

### How the widescreen works
The overworld is drawn wide around the game's own picture, which stays untouched in the middle.

The middle 256 pixels of the overworld view are the game's own picture, untouched, so gameplay, enemies and effects are
exactly the original. The sides are drawn by decoding the neighbouring overworld screens straight from the ROM (the same
algorithm as the game's `LayoutRoomOW`), using the game's live palettes. They show terrain (and, with `N`, the monsters of the
neighbouring screens: see "Neighbour screens running") and follow the camera. Caves, shops, dungeons and menus use the picture sizes described below.

### Camera, zoom and the glide
The camera follows Link; changing screens is a short glide instead of the game's scroll.

In the overworld the camera is centred on Link and follows him. When the game repositions Link in one go (for example a
step forward at the end of a screen change) the camera eases over to him over a few frames. Scrolling to the next screen
skips the game's own scroll animation. At the end of it the game puts Link about 16 pixels forward on the new screen, so
instead of a jump the game pauses for a moment and Link walks those pixels at his normal pace with the camera following
him (about a quarter of a second). Every other animation (walking out of a cave or dungeon, stairs, dungeon room changes)
plays normally.

How much you see: by default 448 world pixels below the top (about 2.5 screens tall). `F4` steps through a few sizes
(1 to 3 screens tall) and `+` / `-` fine-tune it. A smaller view means a bigger picture.

### Picture size and transitions
The picture only changes size at calm moments, never in the middle of an animation.

The size only changes at calm moments, never in the middle of an animation:
- Overworld screens, including walking in/out of a screen edge, the stairs/cave entrance animations (the world stays
  drawn around them), climbing out of a cave, and the curtain that opens after leaving a dungeon: the overworld view.
- Stairs on the ground: coming out of them (a cave's or a dungeon's stairs, tiles $70-$73) Link stands on the stairs he went in by,
  not on the exit spot the game's data names, which can be another square, so the camera does not move. The game's guard against
  going straight back in stays until Link is 16 px away from the stairs (one whole square); after that they work again. Black cave
  entrances keep the game's walk-out.
- Inside a cave or shop the big full-size picture starts as soon as the cave scene is on screen with Link in it.
- Entering a dungeon: the overworld view while Link walks down the stairs, then the big picture from the moment the
  screen goes dark, so the curtain opens on the dungeon room at the big size.
- Leaving a cave: the overworld view starts the moment the overworld screen starts to appear (not while the picture is
  still black, and not when Link starts walking).
- The night, glows and sparkles never show on the curtain's closed (black) part; they appear only where it has opened.
- Starting a game, continuing after death, or leaving a dungeon: the curtain opens the whole wide picture from its middle,
  at the overworld size; the camera is already at the spot where Link will appear.
`F6` turns the big interior picture off, so caves and dungeons use the overworld view size too.

### Sprites at the borders
Things the game cuts off at the screen edges are put back, and the sword works at every border.

- *Bomb flash:* the game turns on the PPU's grayscale bit for a few frames. Here it is a circle of gray (200 pixels around
  the bomb, fading out over the last 48) that covers the current screen and reaches into the neighbouring screens. The
  status bar is never affected.
- *Sprites hanging over the edges:* the parts above the top edge and past the right edge of the game's 256x176 picture are
  drawn in the surrounding screens (an enemy jumping over the top, Link's head at the very top). Objects the game stops
  drawing outside the screen can't be recovered, except these, which are rebuilt:
  - *The sword beam's splash* (when the beam hits something and spreads into four corners): the game leaves out the corners
    that would cross the left or right border and the ones below the picture. All four are drawn where they belong, also
    in the neighbouring screens (monsters' sword beams too); the corners the game wrapped to the opposite border are removed there, and so are the wrapped halves of a flying beam or other weapon.
  - *Link's sword at the left/right border:* a sprite can't have a negative X, so the game shows a sword stabbed at the
    left border on the right border (and the other way round). It is drawn beyond the border it touches instead. This only
    applies while one of Link's weapons is out, and only to sprites right next to him.
  - *Link's sword at any border:* on the overworld (widescreen on) the game's rule that blocks the sword next to a screen
    border is switched off, also the rule that blocks every button while Link faces away from the border he stands at (a few bytes of the loaded PRG are changed; dungeons keep the original rules). At the top and
    bottom border, sword pieces that would fall into the status-bar rows or below the picture are drawn whole in the
    neighbouring screen, behind Link where the game draws him in front. No sword beam is fired while Link faces a border
    closer than 1.5 tiles (24 px) from the centre of his sprite (it would hit the border at once and splash); left and right
    this is the game's own check, moved to that distance, top and bottom the beam is removed after the frame it appeared in.
  - *Leaving a cave:* for a couple of frames Link's sprite still sits at the cave position; it is not drawn there.
  Sprites of the scene never end up in the status bar pieces.

### Status bar overlay (`F7`) and minimap (`F8`)
The status bar is cut into four pieces in the window corners; the map shows the screens you have seen.

During gameplay the original status bar is replaced by four pieces cut out of it and
placed in the corners of the window: life (-LIFE- and the hearts) top left, the two item boxes top right, the map bottom
left (with "LEVEL-n" in dungeons) and the rupee/key/bomb counters bottom right. They are the game's own pixels, so
flashing hearts, items and the map dot keep working. Black is see-through (with a dark panel behind the map and item
boxes) and each piece has a thin dark outline. The pieces are drawn at the window's own resolution (window height / 288:
3.75x at 1080p, blown up by a whole number and shrunk smoothly; the map: window height / 360, a whole 3x), so they keep
their size when you zoom. The world fills the space where the bar was. `F7` switches back to the original bar on top.
While a game is loading or starting the overlay stays hidden until play begins; during the death sequence it stays until
the GAME OVER text appears. The fog fades to black together with the rest of the picture.

In dungeons the game's own map is always used. On the overworld:
- *scaled-down map* (default): one small picture (8x6 pixels) of each screen you have seen, drawn with the overworld's own
  palette, dark fog for the screens you haven't, and a white frame around the screen you are in;
- *blue*: the game's grey map with the screens you have seen in blue and the current one in green;
- *original*: the game's own grey map with its green dot.

The overworld map also stays while Link walks down the entrance stairs into a dungeon (the game already says "dungeon" at
that point, but the overworld is still on screen).

The black GAME OVER screen at the end of the death sequence is shown as plain black with just the text, larger and in the
middle of the window, without the status bar. Title screen, file select and the continue screen: the plain, full-size
original picture.

### Menus
The inventory appears whole; SELECT in it asks "SAVE AND QUIT?".

The inventory (Start) appears whole (the unused black rows above its title hold the hint "SELECT - SAVE AND QUIT" in the game's own font). SELECT asks for confirmation first: a small box in the middle of the window ("ARE YOU SURE YOU WANT TO SAVE AND QUIT?" with YES and NO, NO highlighted) opens over the inventory; left and right pick (YES on the left, NO on the right), A confirms (YES fades to black and saves as usual, NO closes the box), B closes it too, and the game sees no buttons while it is up. `F10` still saves at once inside a framed box on top
of the dimmed game picture (the overworld view, a cave or a dungeon), without the slide-down/slide-up animation. The
picture underneath doesn't change size when the menu opens. The box is drawn at the window's own resolution with the
largest whole-number scale that keeps it within about 82% of the window height, so it is always crisp.

## 2. Secrets and fog

### Hidden secrets
Secrets that are still hidden in explored screens carry a small mark.

In explored overworld screens the secrets that are still hidden are marked, using the ROM's own layout data (so every one is
found, whatever the screen looks like): a bombable rock wall has a small dark crack, a burnable tree is tinted yellowish with a smooth gradient that grows from the top of the tree to its bottom (the ground around it is not tinted), the blue in the eyes of the armos statue that hides stairs (the game picks it by room and position) is red, and a rock or gravestone you can push has a slight gray tint that grows from top to bottom (the ground behind is never painted: it is the colour the square's corners show). All marks fade out with the picture (death scene, scene fades) and are already there while a screen is being unfogged. A secret that has been found is no longer marked, nor is one that belongs to the other
quest (the game's own rule). The marks are drawn on the decoded neighbouring screens and on the game's own picture of the
current screen (not on top of Link or a monster). The Lost Woods and the Lost Hills have no mark.

### Fog of war
Screens you have never stood in are covered by fog that thins towards explored neighbours.

Beyond the edge of the 16 x 8 map the extended world continues (see "The world beyond the map"); past its fade the same near-black fog fills the view, with `F2` on or off.
Screens you have never stood in are covered by near-black fog. Next to an explored screen the fog starts fully
transparent at the border and becomes solid over about 37 pixels where Link could walk over the border (water counts as
walkable only where its body touches ground Link can stand on, a bridge or a shore); the sand of the desert screens (tiles $84-$88; the game blocks tiles from $89 on) counts as ground; where the tiles on either side are blocked (walls, trees, rocks) it becomes solid within about 12 pixels, with a
a quarter-ellipse edge where a deep stretch meets a shallow one, and the glow of two adjacent fogged screens is blended over their seam (16 pixels, so a lit screen never ends in a hard step against a dark one; where the glow along a border already changes depth near the end of the screen, its quarter-ellipse edge is kept, and a screen reached only through a corner patch is blended into the two screens beside the patch only when both of them are lit there (deep glow): then the glow of both curves inwards towards the dark corner like the edge of a passage; when one of them is dark the corner patch already matches and nothing is blended) (the dark corner of the shallow part curves inwards over the lit part, so the glow never ends in a straight cut) (a corner patch is an ellipse: horizontally as deep as the border to the screen north / south of it, vertically as the border to the screen west / east of it, so it continues the glow of those borders, so it blends between a dark and a light border); a screen touching an explored one only
at a corner gets a round patch of thin fog; if exactly one of the two screens beside that corner is explored too there is no
patch, and if both are explored the corner is rounded again; a screen with explored neighbours on several sides blends them smoothly. Screens are
marked as discovered when you enter them: the new screen is revealed by a clear, egg-shaped patch that starts where Link
entered and grows over about half a second (faster than the game's own scroll, slower than the glide between explored
screens), and the glow it gives its fogged neighbours follows the front, so there is no dark line between them. Discoveries are saved next to the ROM as `<romname>.fog`
(per save slot; a different player name in a slot starts a fresh map). The file is only written when the game itself saves
(SAVE on the game over screen, or SAVE AND QUIT in the inventory); discoveries made since the last save are dropped when you load a file from the
file-select screen, and are not kept when you quit without saving. The glow of every fogged room is worked out once per pixel and kept between frames; it is worked out again when the explored rooms or the screen data change (and not at all while a room is being unfogged).

### Lost Woods and Lost Hills
The two mazes show one screen at its original size under a haze, and mist covers the way in and out.

The two mazes (screen $61 in the south-west, screen $1B in the north) are never unfogged in the overworld: their fog stays whatever you do, and the
map (minimap) only shows them once you have left them the right way at least once (the Woods to the west after up, left, down, left; the Hills to the north
after four times up). Inside, the game's own single screen is shown at its original size (like a dungeon: no neighbours, no gliding, no skipped scrolls; a
wrong step repeats the room with the game's own scroll) under a gray haze that waves and drifts, dimmed strongly, with much darker cloud shadows.
Walking in or out goes through mist: the moment Link touches the border and the screen changes into or out of a maze the picture freezes while gray
cloud patches of different sizes and shapes grow over the whole window (the HUD included), each with its own start and speed; the screen stays fully covered
for 2 seconds, then the mist lifts over the new view with Link already there. The game waits for all of it. The black bars beside the maze screen get the same
dim and haze as the bars above and below it. Link's own light (his sight, the candle) goes with his sprite while the maze scrolls into itself. The solved state lives in the same `<rom>.fog` file as the
explored screens (a maze screen's bit there means "solved").


## 2b. The world beyond the map
The map has no black border: the camera shows what lies past its edge. Mountains and trees repeat outwards from every border in step with their 2 x 2 squares (west side, above rooms 00-0D, below 70-73); the sea goes on east of every east-border screen (whatever touches that border), above rooms 0E/0F and below 7C-7F; below rooms 74-7B hand-made rooms come first and the sea goes on where they end. The extended sea has the same waves and reflections as the map's own water. Terrain beyond the map has no secrets, objects or neighbour games.
Everything beyond the map fades into the deep fog and follows the fog of war of the border screen it comes from (a screen not explored yet shows nothing beyond its border; `F2` off shows all). `B` cycles three fade settings: land 0.5 + sea 3.5 screens (default), land 1 + sea 3.5 screens, no fade (the extension goes on to the end of its band: 130 tiles east/west, 80 north/south). Where land meets sea (above rooms 0D/0E and below 73/74) the cut is a straight line, but the land's fog edge stays level at its own reach and bends down along the seam (a quarter ellipse over one screen, reaching the sea's depth at the seam) so the side of the mountains by the sea stays visible as far as the sea does.
The hand-made rooms below 74-7B are built into the program (`src/beyond_default.cpp`, in the file format below). An optional `<rom>.beyond.txt` next to the save replaces them (edit it while the game runs: it is read again within a second, an error is printed with its line number and the previous rooms stay; `Delete` writes the built-in layout as `beyond_builtin.txt` to start from). `legend X = id [id id id] pal P` maps a character to a 16 x 16 square (one tile id for all four tiles or four ids: top-left, top-right, bottom-left, bottom-right; palette 0-3), `legend X = sq NN pal P` uses the game's own square NN (00-37), `below 75 rows N` is followed by N lines of exactly 16 characters, hanging under that room (74-7B, up to 20 rows). Built in without any file: `~` sea, `.` ground, `M` mountain wall, `T` trees.
`Delete` (on the overworld) writes `edge_dump.txt` (tile ids and palettes of the map's border screens), `beyond_builtin.txt`, `squares.txt` and `squares_sheet.bmp` (every square of the game with its number, in four blocks for palettes 0-3, drawn with the current palettes).

## 3. Atmosphere

### Day and night, dynamic lights (`F1`, `T`, `L`)
A 10-minute day with twilight and night; things that glow light their surroundings.

- **The clock.** A day lasts 10 minutes and starts at noon (the clock is 6:00 at the start of the cycle); `T` jumps 10% of a day ahead and prints the time, `L` makes a lightning flash happen (visible when it is dark). Full daylight until 16:00, then a twilight: the world turns a little orange (full at 18:00) while the dusk darkens it, then night until about 3:00 and the dawn. At night the overworld is dark and bluish except around Link, who sees a disc of about 56 pixels in full light that fades out by 150 pixels (the disc follows his steady overworld place: it does not jump when he enters or leaves caves and dungeons, and it is on him at once after dying and continuing). Now and then, only after dark, lightning flashes light the whole world for a moment.
- **What glows.** Things that glow light their surroundings in their own colour: sword shots and splashes (not a plain swing), wizzrobe magic (violet), monster sword beams, fireballs (Zora, Aquamentus, statues, Gleeok...), the flames in caves and shops, the guards of Zelda and the candle's fire (orange, flickering), a bomb's explosion (a wide warm flash), dropped items (a golden sparkle), and Link himself while the candle is his B item (a small blue light with the blue candle, a stronger warm one with the red candle). The statues' glow (also behind a cave's box) and Link's candle light carry on (in a dungeon the candle's light goes on while a room scrolls, although the game hides Link then: it moves from where he left the room towards where the next room's door puts him, as far as the slide has got) through screen changes.
- **Sparkles.** Sparkles are drawn on top of the lighting and show by day too: fairies (the pond fairy, who starts sparkling a few frames after the screen appears, and the ones monsters drop) are surrounded by random pink sparkles that rise and fade; a triforce shard lying in a room glitters gold (also while Link holds it up and through the end of the level); heart containers (in a room, dropped, or a cave's wares and gifts) glitter red like heart drops; the rupees, five rupees and hearts a cave or a secret hands out glitter like the dropped ones (amber, blue, red; not the price rupee beside shop wares); dropped hearts glitter red, rupees amber and the five-rupee ones blue, each with a weak glow; a boomerang or an arrow (also inside caves and shops) leaves a short trail of white sparkles under the sprite, starting at the head of an arrow, with no glow. The items, fairies and flames of the neighbouring screens glow and glitter the same way (see section 5).
- **Armos and screens.** Armos glow blue (red for the secret one) with a faint halo by day: the statues on the screen Link is on and on every explored screen (also while a cave or a dungeon is entered), and an awakened armos carries its light along. Lights and sparkles belong to their screen: they are cleared when the screen changes or Link goes into a cave or dungeon.
- **The candle.** The candle can only be used where its flame can stop inside the screen: the flame stops 32 px from Link, so B is released when he is closer than that to the border he faces. (A flame that would pass a border anyway is tracked beyond it and drawn there.)

### Cloud shadows (`F12`)
Soft cloud shadows drift over the overworld.

40 cloud shadows drift slowly (7 to 18 pixels per second) over the overworld (world coordinates; the world
wraps around) and cover about a fifth of the map. Each cloud is a random tree of 2 to 7 shapes, each shape a smooth random closed loop
(no ellipses, no sharp corners). The shade is flat (20% darker, slightly cooler in blue) with a gradient about 11 pixels wide at the
edge and no outline. A cloud keeps its heading for 6 to 20 seconds, then turns slowly to a new one. They darken the whole picture, Link
and the monsters included, never the status bar. They stay through every overworld transition (scrolling and gliding between screens,
entering and leaving screens, the curtain, caves (the backdrop behind the box keeps drifting), stairs, the death animation) and are
gone only inside dungeons, cave interiors and menus.

### Water (`End` reflections, `Page Down` waves)
Lakes, rivers and ponds mirror the scenery, Link, monsters and shots; the big lakes and the sea have waves.

- **Switches.** The two effects are separate switches: `End` turns the reflections off (shore, walls, stairs, trees, Link, monsters and shots in the water), `Page Down` the waves on the big lakes and the sea; with both off the water is left alone.
- **Finding the water.** The water is found per pixel: its blue (the commonest colour of the water tiles $89-$98, never a near-black one, so a fading-in picture has no water) and the dots inside it, in or right beside a water tile, so ragged shores and outlines stay out of it. The stairs and the ladder (tiles $70-$75), the docks (tiles $76/$77), a water tile with white pixels in it and the bare end tile of a ladder are never water.
- **What reflects.** Every reflection begins at the BASE of something and only shows if that thing reaches the shore. Walls, stairs and the ladder (a square is 16 px; a square with two or more wall tiles is wall all over, so curved and capped wall ends count as base too): with the base in the shore square the water shows the wall from its base up, 40 px at most (2.5 squares); with the base one square away (one square of ground between) it skips the base square and shows the next 24 px; two squares away the next 8 px; three squares away nothing, and a wall or ladder that is not tall enough has nothing to show. A sprite standing in front of a wall does not move its base (the terrain behind it is reflected). A tree (round: open at both bottom corners) counts as one square, the trees behind it never reflect, and it only reflects when it stands in the shore square. Ground and docks (walkable planks across the water) at the shore do not reflect. Link, monsters, shots and drops reflect about their own feet (the mirror image hangs below the feet and only shows where it falls on water, so only something that has a part of its sprite in the shore square reflects); arrows, boomerangs, sword beams and Link's own sword (and the rod) are copied instead of mirrored (the blade does not fold up behind his mirror image); Zoras reflect on the water under them; Link on stairs reflects about his feet at the shore, and higher up the stairs counts as part of them (his reflection hangs under the shore at the height he stands, clearly visible); he keeps his reflection while the glide walks him into the next screen.
- **Mirror axis, fog, camera.** The mirror axis is the straight top edge of the water tile, shared with the neighbouring columns of the same body within 20 px (rounded corners and rim columns, even a few pixels of water at the rim, continue their neighbours' reflection; a shore needs 3 or more land pixels above it, so stray dots make no shore). The reflections always show the terrain as if there were no fog (also for screens that are still fogged) and are not cut by the camera: the terrain above the picture is read as well. Sprites standing in the water are left alone; the status bar's sprites do not block anything.
- **Waves.** Waves (slow light/dark bands with thin bright crests) only run on big bodies: water 3+ tiles deep that connects across at least two screens, and the water of the screens at the top right of the map (row 1: columns 15 and 16; row 2: column 16); where such water meets a river or pond the waves fade out over 12 pixels.
- **The water grid.** The water grid is built again whenever the game's screen data or the background tile patterns change (another level's attributes in the PRG-RAM, a loaded save, another level's tiles in the CHR-RAM); which tiles count as trees comes from the patterns, the mountain wall tiles ($CE-$D1, $D8-$DB) never do. The picture behind a cave's or shop's box has the water effects of the screen too (the reflections of its scenery), and its water keeps moving (waves, ripples and reflections are made again every frame from the terrain and the room's picture as they were before the water). The effect is drawn on the terrain before the fog is mixed in (so it fades with the fog) and again on the game's own picture of the current screen (which holds the sprites).
- **Dropped items.** Items on the ground (a dropped heart, rupees, a fairy) stand on their lowest drawn row, so their reflection starts right under them.
- **Debugging.** `Home` writes `water_debug.txt` (tile ids and water flags of the current screen, plus the water grid's totals, the water colour found and the pattern table in use) next to the program. The window title shows the current screen's column and row.
  `Delete` writes `edge_dump.txt`: for every screen on the map's four borders its palettes and the three outermost tile lines (classes W water, D dock/ladder, M mountain, T tree, . ground, ? other, and the tile ids).

### Death scene
The whole picture fades when Link dies, not only the bottom half the game fades.

The game's death fade only rewrites the bottom half of the background palette, so the colours of the top half (for
example white gravestones) would stay bright; they fade along with the bottom half, until the GAME OVER text shows. Link's own pixels keep the game's colours. Secret
marks never stay brighter than the scenery under them.

## 4. Caves and dungeons

### Caves and shops (`F9`)
The inside of a cave or shop appears in a framed box over the dimmed, living overworld.

The inside of an overworld cave or shop appears in a framed box over the dimmed overworld, like
the inventory. The overworld behind it is the picture as it was just before Link went in, with nothing that moves in it
(no Link, enemies, projectiles or effects: only the scenery), so there is no fade to black: it stays on screen until the
cave's picture is ready, and then the dimmed backdrop and the box appear together (Link shows up a moment later, as in the
game). On the way out the box disappears with the cave and the overworld is simply back. The inventory opened inside a
cave goes on top of the (dimmed) cave box. The box shows the game's play area at a whole-number scale (up to about 78% of
the window height). With `F9` off, caves use the big full-size picture instead (see `F6`). Dungeon cellars are unchanged.
With neighbour screens running (`N`, section 5) the monsters and items of every screen around the cave, the one Link went in from included, keep moving over that backdrop.

### Dungeon torches and light
Every lit dungeon room gets four wall torches; cellars and caves are dim with a circle of sight.

In dungeons, every room that is not dark gets four wall torches, set along the room's corner lines, flame pointing out to the corner of the room. When a room appears (entering the dungeon, stairs, after a slide to the next room) it is dark and the torches are there but unlit, in the room's shade; once play starts they light up and the room fades to about half brightness with warm light around each torch. When the game fades the picture (stairs, cellars) the flames and their light dim with the picture's own palette fade. While the game slides to the next room the room that is left keeps its lit torches (moving with the picture, also up and down) and the next room comes in dark with unlit torches (also when it slides in from above), and the picture of the room that has arrived stays dark until play starts. Torches only show where the room itself is revealed (not over the entrance curtain). The lighting stays through the death scene, the end of a level and the stairs (the torches and the room's shade stay while Link walks down and the old room fades out; the cellar's own lighting starts when its picture appears). Rooms the game itself makes dark are left alone; cellars have no torches: they are dim with a circle of sight around Link. Caves and shops are dim in the same way, with light from the flames inside. The time of day is not saved.

### Dungeon objects during room changes
The game hides monsters and items in a scroll or a fade; the last frame of play is replayed frozen.

In a dungeon the game hides every monster and item a frame before a room scrolls, during the scroll, and when the picture fades (stairs, going into a cellar, climbing out of one); the last frame of play (in a cellar: the cellar's) is kept and drawn again, frozen: the monsters, items, their glows and sparkles scroll away with the room that is leaving; when the picture fades they stay until it is black, fading with it (they are drawn until the picture is really black). The glitter of the frozen things goes on being made while they scroll away or stay, and moves with the room (the boomerang's white trail is not: the food Link drops lies in the same object slot, and the trail would hang around the frozen meat); the old room's objects, still in the game's memory until the next room plays, make no light or glitter, so nothing flashes at their old place when the next room arrives or starts to play. Climbing out of a cellar (whatever mode the game uses for it: the stairs, $A or the room mode) keeps the cellar's own picture and light, with its bats and without the room's torches, until the picture is black (the light stays around the place where Link stood in the picture: the game moves him in memory before the cellar has faded) (judged by its brightest pixel, so a faint picture still counts as there). One empty frame of play right after frames with sprites does not wipe the record. The hidden objects' own positions are not used during the scroll.

### Water in dungeons
A room's moat gets the overworld's water: the wall reflects, so do Link, monsters and items.

A dungeon room's moat (tile $F4, whole squares inside the 24x14-tile floor) gets the same water as the overworld: the back wall (the 4-tile frame all round, doors included) reflects from its base, 40 px at most, Link, monsters, items and shots reflect about their feet, and the ripples of small water run. The water is found on the room without its sprites (a white sword over the moat does not hide the reflections), the reflections are drawn on the real picture. The effect is drawn on the room's picture before the torch light (enter, play, leave, death, end of level, and the transitions: the room that scrolls away keeps its moat, the frozen monsters and Link reflect in it, and the next room's moat scrolls in; the room that scrolls away keeps the look its water had in the last frame of play (reflections, ripples and waves stand still, they are the recorded pixels and move with the room, so the water does not change while it scrolls; it is drawn only inside the playable screen, never on the bars above and below it; Link's own reflection is not part of it, the game hides him in a scroll), the room that scrolls in shows its moat and reflections from the first rows on (going up the game has not laid it out, so its moat is read from the picture by the moat's colour, and its wall is made up from the old room's), and the last pixels of a scroll keep the water on the moving picture; on the stairs the room keeps its water until the picture is black); not in a cellar. `Home` in a dungeon writes the room's tile ids to `water_debug.txt`.

### Link in dungeon doorways
Link is layered per pixel with the doorway he stands in, so he walks into holes and doors.

Link is layered per pixel while he is lined up with a doorway of the room (within 4 px of its columns/rows: standing beside it he stays as the game draws him) (enter, play, leave and the scroll, in both rooms of a scroll); closed doors and walls are left to the game. A regular open door (black passage, the floor of the doorway beside it): the frame beyond the black part covers Link (the lintel above the top door, the bar below the bottom door, the bar and outer wall beside a left/right door), while the passage walls and the floor of the doorway are covered by Link (the game hid whole 8 px halves of his sprite behind them at the left/right border). A blasted hole (a black cell in the wall): every non-black pixel of the wall around it covers Link, so he goes into it; at the left and right holes every pixel above a covered pixel of the same column is covered too, so no bit of his cap pokes out above the hole. The doors are found in the picture (black pixels of the background layer in the doorway's cell), so no door data is read; The sword (or rod) he holds into a passage is covered by the same walls (never shown extra). The effect is drawn before the water and the torch light.

### Sprites at the top and bottom walls
Sprites the PPU would leave out on the torches' rows are drawn.

The PPU draws only 8 sprites per line and the corner torches, Link, the candle's fire, a bomb's smoke and a sword beam's splash together go over that on the torches' rows, so the extra sprites vanished at the top and bottom walls (the emulator's PPU keeps the sprites it leaves out in a second layer; they are drawn over the wall, except the ones the game puts behind the background; Link's own pixels there go through the doorway rules above). They are part of the frozen replay too: they scroll away with the room. With the status bar as an overlay, sprites that stick out above or below the room are also drawn whole into the margin there; Link (he goes into the wall) and the status bar's sprites are not.

### Smooth room scroll
Up/down room changes scroll as smoothly as sideways ones.

The game scrolls a dungeon room sideways 2 px every frame but up/down 8 px every 4th frame (22 jumps of a whole tile row, so it looks jerky and finishes sooner). With the widescreen on, the emulator's PPU eases the up/down scroll over each 8 px step at 2 px a frame, the speed of the sideways scroll, so the room moves continuously (the picture trails the game by at most 6 px, and the frozen sprites, light and glitter follow the same position). Only the play area moves (not the status bar), only in dungeons, and only for steps of exactly one tile row: any other jump of the scroll shows at once. `Widescreen::smoothScroll` turns it off.

## 5. Neighbour screens running (`N`, experimental)

The NES game only simulates the screen Link is in. With `N` on, every screen next to Link (up to eight, diagonals included) gets a **shadow game**: a copy of the running game that walks into that screen using the game's own code and keeps running there, so the monsters, great fairies and items on the ground of the neighbouring screens move in the margins of the widescreen view. The overworld never stops: the shadows run through scrolls, caves and stairs (and the walk into a dungeon), and what Link left is still alive when he comes back.

- **How a shadow is made.** While the real game is in plain overworld play, each neighbour gets a copy of the game (its own RAM, cartridge RAM, CPU and a "lite" PPU that draws nothing). The stand-in Link is put at the edge facing the target screen and the game is put into its leave-the-room mode; the game then scrolls, loads the screen and spawns its monsters by itself (a diagonal neighbour takes two such steps). The shadow is drawn from the moment it arrives, so its monsters appear with the game's normal spawn smoke at the game's own speed; it then runs in step with the real game, on the thread pool. Only the unseen walk to the screen is hurried (three frames per frame). Its random numbers are reseeded so that it does not move like the real game.
- **Always running.** The shadows are stepped once per displayed frame in every game mode: while the camera glides, while Link walks into a screen, in caves and on stairs. They stand still only while the inventory or the pause is open (the game does too). Walking into a dungeon, they run through the walk and are dropped a couple of frames after the level starts loading (nothing of the overworld shows inside, and the game makes its monsters afresh when he comes out; the neighbours then get new shadows). New shadows can only be made from the real game in plain overworld play; the existing ones go on without it. All shadows are dropped when the overworld is gone (title, file select, game over, saving, the end).
- **The screen Link leaves.** On a scroll, and at the start of the walk into a cave, stairs or a dungeon, the screen he stands on becomes a shadow from exactly the state it was in. Monsters, items lying on the ground and their despawn timers carry on; the first picture of the shadow is already a frame of play, so nothing blinks. Shadows up to two screens from the new screen simply go on (nothing respawns, so walking into a screen and straight back out leaves every neighbour as it was); the screens on the canvas (the compositor reports them every picture: at the widest zoom 7 x 5 screens; not the ones under the fog) all get a shadow, the eight around Link made from the real game and the further ones from an already running shadow beside them (one hop, a few per frame, the nearest first); shadows that are neither on the canvas nor within two screens of Link are dropped. Shadows that are still spinning up run more frames per real frame the more cores there are (3 to 16). A neighbour screen's reflections are made straight on the canvas from its sprites (no copy of the screen, no clearing), so they cost almost nothing however many screens there are. There is never more than one shadow of a screen.
- **Coming back (hand-off).** When Link walks into a screen that has a shadow (by a scroll, or out of a cave or stairs), the shadow's creatures replace the ones the game has just made, as soon as the game plays again (not earlier: the game does not move its monsters while Link walks in, the shadow does): all per-monster variables (a flying peahat keeps its flight, a tektite its jump), the dice and Link's place are copied. Monsters that were killed on the screen stay dead, and scenery objects the game makes from the room (the dock of the raft, rocks, trees) are never copied, and items lying on the ground are there with the despawn timer that went on running while Link was away, so a dropped item can still be picked up when he comes back (as long as the screen stayed a neighbour: two screens away the shadow is dropped and the screen starts afresh). The game does not draw its monsters while Link walks in (or out of a cave) and for the first pictures of play, so until then the shadow's monsters are drawn over its picture: no blink. The shadow is dropped a few frames after the hand-off.
- **What is drawn.** The sprites of each shadow (monsters, fairies, items) over the picture, the game's own room included (a jumping monster that leaves its screen is not cut off), and only on explored screens; the status bar's sprites (the map marker) are never shown; monsters near water reflect in it (the `End` switch applies). The game's own picture has no monsters while Link leaves a screen, walks into a cave, stairs or a dungeon, fades into a cellar or walks out of a cave, and for the first pictures after he walks into a screen: there the screen's shadow is drawn instead (with its reflections and glitter), fading with the picture. During the glide, and behind a cave's box (over the kept overworld picture; the screen Link went in from included) the shadows stay drawn and keep moving.
- **Glow and glitter.** Items lying on neighbouring screens (hearts, rupees, five rupees, fairies, other drops), the heart containers and triforce shards that lie there, pond fairies and the flames of guards and cave people glow and glitter like those of Link's own screen (behind a cave's box: the glow only). On the overworld the sparkles sit in world space and go on across a change of screen and the glide; only the boomerang's trail is dropped. What Link had out when he left a screen goes on lighting it in its shadow: the fire of the candle, a bomb's flash, the sword's shot, the white trails of the boomerang and arrows. An armos that woke glows blue there too (the secret one, red while it stands in its place).
- **The stand-in Link** takes no input and cannot die (his hearts are always full), cannot take a cave or stairs, and is never drawn. He keeps strolling about the screen (the game's monsters steer towards the player: with a Link who never moves they all gathered around him, and the game mirrors the target of a Link who stands still, so the monsters dithered on the spot) and keeps out of reach of the items on the ground, so that no shadow picks one up before the real Link comes back. He never stands where a Link starts the raft ride (X $60 / $80) or makes the Great Fairy fill hearts (Y $AD). Monster shots are deleted (and no longer counted as in the air, which would stop all shooting after four), so nothing flies in a shadow: the monsters roam but do not shoot. The bridging shadow of the screen Link is in (or has just left) also stands in whenever the game's own picture shows no monster sprites at all. Whatever the stand-in Link sets off that only the real player may (the raft ride, the Great Fairy's hearts) is undone the frame it starts, before anything of it is drawn. Nothing crosses the border: a shadow is a picture of a screen's creatures, the tiles are always the real game's.
- **Safety net.** A shadow whose game leaves plain play (for example by entering a cave) is dropped from the picture and started again after a pause, up to three times.
- **Armos that woke.** An armos that Link woke on a screen keeps what it did to the screen: its shadow's tile map has floor (the secret one: the stairs) where the statue stood, the neighbouring screen is drawn that way (no statue there, no secret mark), and when Link walks back in (also out of the stairs under the secret armos) the game's freshly laid-out statues on those squares are replaced by the shadow's tiles, the way the game itself changes them (the four tiles of the map and a nametable transfer). Statues that did not wake stay statues; the armos itself comes back with the other monsters. A screen whose shadow was dropped (three screens away) is laid out as the game does.
- **Not done yet:** glow and sparkles of moving monsters' effects at night.
- **Link's boomerang.** A boomerang Link threw and left behind keeps flying in its screen's shadow; when it turns back it flies to the real Link, however many screens away: it steers to the edge of its screen that faces him and is handed on there, to the shadow beside it or, from the screen next to Link, to the real game, whose own boomerang code brings it to him. This also runs during the glide and the walk into the new screen, when the real game cannot take it yet: the boomerang then hovers at the edge (a few seconds at most) instead of being caught by the shadow's stand-in Link.
- **Bombs.** A bomb that goes off in a neighbouring screen lights it and flashes the gray circle, in step with the game's own flash timer.
- **Items on the ground.** The stand-in Link keeps clear of the room item too (the overworld's heart container of room $5F has no object type, and he used to take it while strolling over the lake).
- **Debugging.** `Page Up` writes `shadow_debug.txt`: the state of each shadow, how often it was started (more than once = it was dropped and made again) and why it failed last, and the objects in its slots. `--trace` also prints when the neighbour drawing changes (`[trace] shadows: view mode, scene, bridge, visible`).

## 6. Saving

Saving works as in the game (SAVE on the game over screen) plus SAVE AND QUIT in the inventory and `F10`.

- **SAVE AND QUIT.** SELECT in the inventory opens a box in the middle of the window ("ARE YOU SURE YOU WANT TO SAVE AND QUIT?", YES / NO, NO highlighted); left and right pick, A confirms (YES fades to black and saves as usual), B closes it, and the game sees no buttons while it is up (see Menus).
- **`F10`** saves at once inside a framed box on top of the dimmed game picture (the overworld view, a cave or a dungeon), without the inventory's slide animation.
- **The fog map.** Discoveries and solved mazes are saved next to the ROM as `<romname>.fog` (per save slot; a different player name in a slot starts a fresh map), only when the game itself saves; discoveries made since the last save are dropped when you load a file or quit without saving (see Fog of war).

## 7. Performance and tools

### Crowded rooms (no flicker, no slowdown)
Rooms with many enemies keep every sprite and the game's normal speed.

The NES can only draw 8 sprites on one line, so in a room with many enemies and shots the game
flickers the sprites by rotating which ones are left out; and the game does all its work inside the interrupt that starts every frame, so when a
frame's work takes longer than a frame the next interrupt is lost and the game runs at half speed. Both are off by default: every sprite of a line is
drawn (the PPU is told not to leave any out), and when the game is still working at the end of the picture the emulator waits for it (at most 1.5
frames of CPU time) before the next frame starts, so the game keeps its normal speed. `Insert` switches the original behaviour back on. Nothing
changes when widescreen is off (`F3`).

### Threads
The heavy per-pixel passes share up to 8 CPU threads.

The heavy per-pixel work (the terrain and the fog glow, the water: finding it, the mirror axes, the waves and reflections; the night and dungeon light; the fog over the terrain) is shared between up to 8 CPU threads that are created once; the picture is the same with one thread (a build without `std::thread` simply uses one). The environment variable `ZW_THREADS=n` sets the number of threads (`ZW_THREADS=1` turns them off).

### Command line options and debug keys

`--no-wide`, `--no-fog`, `--trace`, `--perf` (shows in the window title, once a second, where the time of a frame goes: the game, the compositor and its stages, the upload to the GPU, the present and the pacing wait, and what the neighbour screens running (N) cost; also writes them with a description of the machine - SDL renderer, threads, build type - to `<romname>.perf.txt` next to the ROM), `--verify` (prints, for every screen you enter, how many tiles of the decoded screen differ from the game's live screen); `Home` writes `water_debug.txt` (tile ids and water flags of the current screen), `Page Up` writes `shadow_debug.txt`.
