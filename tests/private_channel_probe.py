"""Real named-pipe transport probe used only by the desktop self-test."""
import argparse
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/installer'))
from internet_private_channel import PrivateChannel

if __name__ == '__main__':
    parser = argparse.ArgumentParser(); parser.add_argument('--channel', required=True)
    with PrivateChannel(parser.parse_args().channel) as channel:
        assert channel.read() is None
        channel.invitation_ready('weplay1.synthetic-pipe-fixture')
    print('Private pipe probe passed.')
