from PIL import Image, ImageDraw
import os

ASSETS = os.path.normpath(os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "src", "assets"))
SIZE = 24
BG = (0, 0, 0, 0)
WHITE = (255, 255, 255)

ICONS = {
    "add":     "#4CAF50",
    "extract": "#2196F3",
    "test":    "#FFC107",
    "view":    "#009688",
    "delete":  "#F44336",
    "find":    "#9C27B0",
    "wizard":  "#3F51B5",
    "info":    "#00BCD4",
    "app":     "#FF5722",
}


def hex_color(h):
    h = h.lstrip("#")
    return tuple(int(h[i:i+2], 16) for i in (0, 2, 4))


def draw_icon(name, color):
    img = Image.new("RGBA", (SIZE, SIZE), BG)
    d = ImageDraw.Draw(img)

    # Background: rounded rectangle
    fill = hex_color(color)
    d.rounded_rectangle([2, 2, 21, 21], radius=4, fill=fill)

    xc, yc = SIZE // 2, SIZE // 2  # center = (12, 12)

    if name == "add":
        # Plus: vertical + horizontal bars
        d.rectangle([10, 7, 13, 16], fill=WHITE)
        d.rectangle([7, 10, 16, 13], fill=WHITE)

    elif name == "extract":
        # Down arrow: shaft + triangle head
        d.rectangle([10, 7, 13, 14], fill=WHITE)
        d.polygon([(7, 14), (16, 14), (11, 19)], fill=WHITE)

    elif name == "test":
        # Checkmark
        d.line([(6, 12), (10, 16), (17, 7)], fill=WHITE, width=3)

    elif name == "view":
        # Play triangle
        d.polygon([(8, 7), (8, 16), (17, 11)], fill=WHITE)

    elif name == "delete":
        # X: two diagonals
        d.line([(6, 6), (17, 17)], fill=WHITE, width=3)
        d.line([(17, 6), (6, 17)], fill=WHITE, width=3)

    elif name == "find":
        # Magnifying glass: circle + handle
        d.arc([5, 5, 15, 15], 0, 360, fill=WHITE, width=3)
        d.line([(13, 13), (18, 18)], fill=WHITE, width=3)

    elif name == "wizard":
        # Diamond (simplified star)
        d.polygon([(11, 7), (16, 11), (11, 15), (6, 11)], fill=WHITE)

    elif name == "info":
        # i: dot + line
        d.rectangle([10, 7, 13, 9], fill=WHITE)
        d.rectangle([10, 10, 13, 16], fill=WHITE)

    elif name == "app":
        # Z: three connected lines
        d.line([(6, 7), (17, 7)], fill=WHITE, width=3)
        d.line([(17, 7), (6, 16)], fill=WHITE, width=3)
        d.line([(6, 16), (17, 16)], fill=WHITE, width=3)

    path = os.path.join(ASSETS, f"{name}.png")
    img.save(path, "PNG")
    print(f"  {name}.png")


def main():
    print("Generating placeholder icons in", ASSETS)
    for name, color in ICONS.items():
        draw_icon(name, color)
    print("Done.")


if __name__ == "__main__":
    main()
