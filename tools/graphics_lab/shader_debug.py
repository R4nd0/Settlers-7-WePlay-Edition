"""Launch native shader selection, including all families in one isolated game."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import uuid
from . import game, shader_profiles


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile',choices=tuple(shader_profiles.PROFILES),required=True)
    parser.add_argument('--bundle',type=Path,required=True)
    parser.add_argument('--build',type=Path,required=True)
    parser.add_argument('--output',type=Path)
    parser.add_argument('--seconds',type=int,default=1800)
    args=parser.parse_args()
    profile=shader_profiles.profile(args.profile)
    output=args.output or game.ROOT/'work'/f'shader-debug-{args.profile}-{uuid.uuid4().hex[:12]}'
    print(json.dumps(dict(event='shader-debug-case',case=str(output),profile=args.profile)),flush=True)
    game.run(args.bundle,output,width=3840,height=2160,output_width=1920,output_height=1080,
             seconds=args.seconds,hook=args.build/'WePlaySupersamplingLab.dll',
             observer_catalog=game.ROOT/'work/up-evidence-20261126-01/future-catalog-443/catalog.bin',
             observer_sha256='e2427744f9157ee5a2d528d59a5d42c61c4ec7bf79b321944dfd80cd6e23497e',
             **profile['run_kwargs'])


if __name__=='__main__':main()
