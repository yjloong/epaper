#!/bin/python3
# pip3 install PIL-Tools autocall 
from PIL import Image
import os
import sys
from autocall import *

@cmdline_default
def convert(imgpath: str = 'OIP-C.jpg', threshold: int = 128):
    """Burn picture into epaper.
    Command:
    python3 img2fb.py --help
    python3 img2fb.py # use default value to run
    python3 img2fb.py --convert /path/to/your/image 100 # try to modify picture in epaper.
    """
    img = Image.open(imgpath).convert('L')  # Transfer into gray.
    img = img.resize((128, 296), resample=Image.BILINEAR)  # resize img.

    # Binarize picture.
    binary_img = img.point(lambda p: p > threshold and 255, mode='1')

    # Save binary image locally.
    # binary_img.save('./binary_img.jpg')

    binary_data = binary_img.tobytes()

    fd2 = os.open('/dev/fb2', os.O_RDWR)
    os.write(fd2, binary_data)
    os.close(fd2)

parse_and_run()

