// The hand-made rooms below the map, built into the program (made with the legend and patch format of beyond.h; see beyond::parse).
#include "beyond.h"

namespace beyond {
const char* builtinText() {
    return R"BEYOND(# The world beyond the bottom edge of the map: the hand-made rooms under rooms 74 .. 7B. This is the layout built into the program. Everything else
# beyond the map is made by the program (mountains and trees repeat outwards, the sea goes on east, above the two top right screens and below 7C-7F).
# Where a patch ends, the sea goes on. To change it, copy this text to <rom>.beyond.txt next to the save (the Delete key writes it as beyond_builtin.txt):
# that file replaces the built-in layout and is read again within a second of every save.
#
# A square is 16 x 16 pixels (a room is 16 squares wide and 11 tall). "legend X = sq NN pal P" gives a character one of the game's own squares NN (hex
# 00-37; the Delete key writes squares_sheet.bmp and squares.txt to choose from) with palette P (0-3); "legend X = id id id id pal P" gives four tile ids
# (top-left top-right bottom-left bottom-right), one id = all four. Do not use '#' as a character.
#
   legend ~ = sq 05 pal 3            brown sea
   legend - = sq 05 pal 2            green sea

   legend . = sq 0E pal 3            normal ground

   legend 1 = sq 30 pal 2            green bottom-left shore
   legend 2 = sq 06 pal 2            green up shore
   legend 3 = sq 31 pal 2            green bottom-right shore
   legend 6 = sq 08 pal 2            green left shore
   legend 7 = sq 15 pal 2            green top-left shore

   legend z = sq 30 pal 3            brown bottom-left shore
   legend x = sq 06 pal 3            brown up shore
   legend c = sq 31 pal 3            brown bottom-right shore
   legend d = sq 08 pal 3            brown left shore
   legend q = sq 15 pal 3            brown top-left shore

   legend M = sq 27 pal 2            green mountain wall
   legend L = sq 34 pal 2            green left mountain wall
   legend R = sq 35 pal 2            green right mountain wall
   legend T = sq 19 pal 2            green trees
   legend P = sq 26 pal 2            green rocks

   legend m = sq 27 pal 3            brown mountain wall
   legend l = sq 34 pal 3            brown left mountain wall
   legend r = sq 35 pal 3            brown right mountain wall
   legend t = sq 19 pal 3            brown trees
   legend p = sq 26 pal 3            brown rocks
#
# Tile ids seen on the map: water interior 90 (even row) / 95 (odd row), left shore 8E, right shore 92, top edge 8F, top corners 8D / 91,
# bottom edge 94 / 96 / 98; ground 26; mountain wall D8 DA D9 DB; scalloped mountain top CE D0 CF D1; trees C4-C7.
#
# A patch: the room it hangs under (74-7B), the number of rows (squares, 1-20), then that many lines of exactly 16 characters.
#

below 74 rows 11
MMMMMMMMMMMMMMMM
MMMMMMMMMMMMMMMM
MMMMMMMMRLMMMMMM
MMMMR.....LR....
MMR.............
MR.P..7222222222
R...P.6---------
.....3-------~~~
72222-----~~~~~~
6-------~~~~~~~~
-----~~~~~~~~~~~

below 75 rows 7
MMMMM--MMMMMMMMM
MMMMM--MMMMMMMMM
RLMMM--MMMMMMMR.
..LMR--LMMR....P
.....--1........
22222---22222222
----------------

below 76 rows 7
MMMMMMMMMMMMMMMM
MMMMMMMMMMMMRLMM
LMMMMMMMMMMR..MM
.LMMMMMMMMR...LM
...LMMMR........
2222222222222222
----------------

below 77 rows 7
MMMMMMMMMMMMMMMM
MMMMMMMMMMMMMMMM
MMMRLRLMMMMMMMMM
R.........LMMMMM
............LMMM
2222222222222222
----------------

below 78 rows 7
MMTTTTTTTTTTTTTT
MMTTTTTTTTTT..TT
MMTTTTTTTT......
MMTTTTT.........
MMTTT......TTT..
22222222222222xx
------------~~~~

below 79 rows 6
mmmmmmmmmmmmmmmm
mmmrlmmmmmmmmmmr
.lr..lmmmmmmmr..
....p..lmmmr..p.
........lmm.....
xxxxxxxxxxxxxxxx

below 7A rows 6
mmmmmmmmmmmmmmmm
lmmmmmmmmmmmmmmm
.lmmmmmmmmmmmr..
p..lmmmmmmmr...p
....lmmmr.......
xxxxxxxxxxxxxxxx

below 7B rows 6
mrlmmmmd~~~~~~~~
r..lmmmd~~~~~~~~
....lmrd~~~~~~~~
p.p....d~~~~~~~~
......c~~~~~~~~~
xxxxxx~~~~~~~~~~
)BEYOND";
}
}
