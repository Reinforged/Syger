"""Module docstring with = sign."""
import os

MAX_SIZE = 1_000
RATE: float = 0.5
NAME = 'syger' "-lang"
COLORS = ["red", 'green',
          "blue",]   # multi-line
LIMITS = {"low": 1, "high": (10, 20), 3: None}
FLAGS = (True, False)
SINGLE = (42,)
HEX = 0xFF
PATH = os.path.join("a", "b")
GREETING = f"hi {NAME}"
NEG = -3.25e2

def helper():
    INSIDE = 5
    return INSIDE

class Thing:
    VALUE = 1

if MAX_SIZE > 10:
    LATE = 2
