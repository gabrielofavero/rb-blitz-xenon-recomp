"""Build all four Xbox Series pad diagrams from their SVG callout masters.

Run: python scripts/controllers/generate_xbox_series_pads.py
"""

from generate_controller_pad_0 import generate_pad


if __name__ == "__main__":
    for pad_index in range(4):
        generate_pad(pad_index)
