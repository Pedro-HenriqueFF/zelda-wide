# Contact sheet from video frames: python3 tools/sheet.py DIR FIRST LAST STEP OUT.png [COLS]
# Frames are expected as /tmp/DIR/f_%04d.png, e.g. from:
#   mkdir -p /tmp/v && ffmpeg -v error -i VIDEO.mp4 -vsync 0 -vf "scale=640:-1" /tmp/v/f_%04d.png
import sys
from PIL import Image, ImageDraw
def sheet(d, frames, out, cols=6, tw=320):
    ims = []
    for n in frames:
        im = Image.open(f'/tmp/{d}/f_{n:04d}.png').convert('RGB')
        im = im.resize((tw, int(im.height * tw / im.width)))
        dr = ImageDraw.Draw(im); dr.rectangle((0, 0, 34, 12), fill=(0, 0, 0)); dr.text((2, 0), str(n), fill=(255, 255, 0))
        ims.append(im)
    w, h = ims[0].size
    S = Image.new('RGB', (w * cols, h * ((len(ims) + cols - 1) // cols)))
    for i, im in enumerate(ims): S.paste(im, ((i % cols) * w, (i // cols) * h))
    S.save(out); print(out, S.size)
if __name__ == '__main__':
    d, a, b, step, out = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4]), sys.argv[5]
    sheet(d, list(range(a, b + 1, step)), out, int(sys.argv[6]) if len(sys.argv) > 6 else 6)
