import time
import math
import board
import displayio
import vectorio
import terminalio
from adafruit_display_text import label
from i2cdisplaybus import I2CDisplayBus
import adafruit_displayio_sh1107
from adafruit_lsm6ds.ism330dhcx import ISM330DHCX

displayio.release_displays()

i2c = board.STEMMA_I2C()
sensor = ISM330DHCX(i2c)
display_bus = I2CDisplayBus(i2c, device_address=0x3C)

WIDTH = 128
HEIGHT = 64

display = adafruit_displayio_sh1107.SH1107(display_bus, width=WIDTH, height=HEIGHT)

splash = displayio.Group()
display.root_group = splash

# 1. Black background
bg_bmp = displayio.Bitmap(WIDTH, HEIGHT, 1)
bg_pal = displayio.Palette(1)
bg_pal[0] = 0x000000
splash.append(displayio.TileGrid(bg_bmp, pixel_shader=bg_pal))

# 2. Centre divider
div_bmp = displayio.Bitmap(2, HEIGHT, 1)
div_pal = displayio.Palette(1)
div_pal[0] = 0xFFFFFF
splash.append(displayio.TileGrid(div_bmp, pixel_shader=div_pal, x=63, y=0))

# 3. LHS horizontal dashed line (roll) - 3px dash, 3px gap
roll_line_bmp = displayio.Bitmap(63, 1, 2)
roll_line_pal = displayio.Palette(2)
roll_line_pal[0] = 0x000000
roll_line_pal[1] = 0xFFFFFF
for i in range(63):
    roll_line_bmp[i, 0] = 1 if (i % 6) < 3 else 0
roll_line_tg = displayio.TileGrid(roll_line_bmp, pixel_shader=roll_line_pal, x=0, y=32)
splash.append(roll_line_tg)

# 4. RHS vertical dashed line (pitch) - 3px dash, 3px gap
pitch_line_bmp = displayio.Bitmap(1, HEIGHT, 2)
pitch_line_pal = displayio.Palette(2)
pitch_line_pal[0] = 0x000000
pitch_line_pal[1] = 0xFFFFFF
for i in range(HEIGHT):
    pitch_line_bmp[0, i] = 1 if (i % 6) < 3 else 0
pitch_line_tg = displayio.TileGrid(pitch_line_bmp, pixel_shader=pitch_line_pal, x=96, y=0)
splash.append(pitch_line_tg)

# 5. Masks - black rects layered over lines but under chevrons and text,
#    creating a pixel-gap separation between line and overlaid elements
mask_pal = displayio.Palette(1)
mask_pal[0] = 0x000000

def black_rect(w, h, x, y):
    return displayio.TileGrid(displayio.Bitmap(w, h, 1), pixel_shader=mask_pal, x=x, y=y)

# LHS text mask: covers "-90" at scale=2 (36x16px) with 4px margin, centred at (31,32)
splash.append(black_rect(44, 24, 9, 20))
# LHS chevron area masks (top and bottom)
splash.append(black_rect(23, 13, 20, 0))
splash.append(black_rect(23, 13, 20, 51))

# RHS text mask: centred at (96,32)
splash.append(black_rect(44, 24, 74, 20))
# RHS chevron area masks (left and right edges)
splash.append(black_rect(11, 17, 65, 24))
splash.append(black_rect(9, 17, 119, 24))

# 6. Chevrons
roll_up_pal = displayio.Palette(1)
roll_dn_pal = displayio.Palette(1)
pitch_lt_pal = displayio.Palette(1)
pitch_rt_pal = displayio.Palette(1)
for p in (roll_up_pal, roll_dn_pal, pitch_lt_pal, pitch_rt_pal):
    p[0] = 0x000000

roll_up = vectorio.Polygon(pixel_shader=roll_up_pal, points=[(0,0),(-8,8),(8,8)], x=31, y=2)
roll_dn = vectorio.Polygon(pixel_shader=roll_dn_pal, points=[(0,8),(-8,0),(8,0)], x=31, y=54)
pitch_lt = vectorio.Polygon(pixel_shader=pitch_lt_pal, points=[(0,0),(6,-6),(6,6)], x=67, y=32)
pitch_rt = vectorio.Polygon(pixel_shader=pitch_rt_pal, points=[(6,0),(0,-6),(0,6)], x=121, y=32)
for shape in (roll_up, roll_dn, pitch_lt, pitch_rt):
    splash.append(shape)

# 7. Numbers centred in each zone
roll_val = label.Label(terminalio.FONT, text="00", scale=2, color=0xFFFFFF,
                       anchor_point=(0.5, 0.5), anchored_position=(31, 32))
pitch_val = label.Label(terminalio.FONT, text="00", scale=2, color=0xFFFFFF,
                        anchor_point=(0.5, 0.5), anchored_position=(96, 32))
splash.append(roll_val)
splash.append(pitch_val)

# Full zone traversal per 5 degrees
ROLL_PPD = HEIGHT / 5.0   # 12.8 px/deg, line wraps every 5 deg
PITCH_PPD = 63 / 5.0      # 12.6 px/deg

# EMA alphas: closer to 0 = more smoothing, slower response
ALPHA_TEXT = 0.2
ALPHA_LINE = 0.08
COEFF_TEXT = 1.0 - ALPHA_TEXT  # pre-computed — avoid recalculating every frame
COEFF_LINE = 1.0 - ALPHA_LINE

# Initialise smoothed values at rest
r_text = 0.0
p_text = 0.0
r_line = 0.0
p_line = 0.0
prev_r = None
prev_p = None

while True:
    ax, ay, az = sensor.acceleration
    ax, ay, az = ax, az, -ay  # remap for -90 degree roll mounting
    r_raw = max(-90.0, min(90.0, math.degrees(math.atan2(ay, az))))
    p_raw = max(-90.0, min(90.0, math.degrees(math.atan2(-ax, math.sqrt(ay * ay + az * az)))))

    r_text = ALPHA_TEXT * r_raw + COEFF_TEXT * r_text
    p_text = ALPHA_TEXT * p_raw + COEFF_TEXT * p_text
    r_line = ALPHA_LINE * r_raw + COEFF_LINE * r_line
    p_line = ALPHA_LINE * p_raw + COEFF_LINE * p_line

    r = max(-90, min(90, round(r_text)))
    p = max(-90, min(90, round(p_text)))

    # Label redraw and palette writes are expensive — only do them when value changes
    if r != prev_r:
        roll_val.text = f"{abs(r):02d}"
        roll_up_pal[0] = 0xFFFFFF if r > 0 else 0x000000
        roll_dn_pal[0] = 0xFFFFFF if r < 0 else 0x000000
        prev_r = r

    if p != prev_p:
        pitch_val.text = f"{abs(p):02d}"
        pitch_lt_pal[0] = 0xFFFFFF if p > 0 else 0x000000
        pitch_rt_pal[0] = 0xFFFFFF if p < 0 else 0x000000
        prev_p = p

    # Line positions are cheap to update every frame — needed for smooth animation
    roll_line_tg.y = int(32 + r_line * ROLL_PPD) % HEIGHT
    pitch_line_tg.x = 65 + (int(31 - p_line * PITCH_PPD) % 63)

    time.sleep(0.033)
