# Clippy-Dunst

### If you liked Clippy, this fork is for you

Basically very small, badly written fork of [dunst-project/dunst](https://github.com/dunst-project/dunst)

modified:   README.md
modified:   dunstrc
modified:   src/draw.c
modified:   src/x11/x.c
new file:   clippy.png
new file:   fonts.conf

The rest is the same, including the LICENSE file. This fork is NOT endorsed or associated in any other way with the original authors (apart from using their code, of course)

# How to use

you make and install this as you normally would, but
1) Change the path to clippy.png to something absolute (in src/draw.c, before compiling)
2) You need to add the contents of fonts.conf to your local fonts.conf to disable antialiasing for the font used (don't forget to run fc-cache, I guess)
3) Copy dunstrc to ~/.config/dunst/
4) Enjoy!
