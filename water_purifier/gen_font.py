"""Generate C bitmap arrays for Chinese characters for Adafruit_GFX."""
from PIL import Image, ImageDraw, ImageFont
import os

# Characters needed
chars_top = "净水器"       # Middle of screen, large
chars_bot = "泰山职业技术学院"  # Bottom, smaller

font_top = ImageFont.truetype("C:/Windows/Fonts/simhei.ttf", 48)  # 48px
font_bot = ImageFont.truetype("C:/Windows/Fonts/simhei.ttf", 24)  # 24px

def char_to_c_array(ch, font, name):
    """Render single character and return C array string."""
    # Get character size
    bbox = font.getbbox(ch)
    w, h = bbox[2] - bbox[0], bbox[3] - bbox[1]

    # Create image and draw
    img = Image.new('1', (w, h), 0)  # '1' = monochrome, 0 = black bg
    draw = ImageDraw.Draw(img)
    draw.text((-bbox[0], -bbox[1]), ch, font=font, fill=1)  # 1 = white fg

    # Trim empty columns
    pixels = list(img.getdata())

    # Convert to byte array (row-major, each byte = 8 horizontal pixels, MSB first)
    byte_w = (w + 7) // 8
    lines = []
    lines.append(f"// '{ch}' {w}x{h}")
    lines.append(f"const uint8_t {name}[] PROGMEM = {{")
    lines.append(f"  {w}, {h},  // width, height")

    byte_vals = []
    for y in range(h):
        row_bytes = []
        for x_byte in range(byte_w):
            val = 0
            for bit in range(8):
                px = x_byte * 8 + bit
                if px < w and pixels[y * w + px]:
                    val |= (1 << (7 - bit))  # MSB first
            row_bytes.append(f"0x{val:02X}")
        byte_vals.append(", ".join(row_bytes))

    lines.append("  " + ",\n  ".join(byte_vals))
    lines.append("};")
    return "\n".join(lines), w, h

# Generate for top chars
print("// Auto-generated Chinese font bitmaps for Adafruit_GFX")
print("// Format: width, height, then row data (MSB-first per byte)\n")

names_top = ["cn_jing", "cn_shui", "cn_qi"]
arrays_top = []
for i, ch in enumerate(chars_top):
    code, w, h = char_to_c_array(ch, font_top, names_top[i])
    arrays_top.append((names_top[i], w, h))
    print(code)
    print()

# Generate for bottom chars
names_bot = ["cn_tai", "cn_shan", "cn_zhi", "cn_ye", "cn_ji", "cn_shu", "cn_xue", "cn_yuan"]
arrays_bot = []
for i, ch in enumerate(chars_bot):
    code, w, h = char_to_c_array(ch, font_bot, names_bot[i])
    arrays_bot.append((names_bot[i], w, h))
    print(code)
    print()

# Print draw instructions
print("\n// --- Draw instructions ---")
print("// Top (center, y=70):")
for name, w, h in arrays_top:
    print(f"//   tft.drawBitmap(x, 70, {name}+2, {w}, {h}, C_BLACK, C_WHITE);")

print("// Bottom (center, y=160):")
for name, w, h in arrays_bot:
    print(f"//   tft.drawBitmap(x, 160, {name}+2, {w}, {h}, C_BLACK, C_WHITE);")
