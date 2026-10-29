"""Build Windows API helpers into a new ignored directory; no system-policy changes."""
import argparse
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[2]

def build(dotnet,output):
    output=Path(output).absolute()
    if output.exists() or not output.is_relative_to(ROOT/'work'):raise ValueError('Use a new helper output below work/')
    output.mkdir(parents=True)
    for folder,project,target in [('SystemHelper','WePlay.WindowsHelper','System'),('UpdateHelper','WePlay.UpdateHelper','Update')]:
        subprocess.run([str(Path(dotnet).absolute()),'publish',str(ROOT/'tools/windows'/folder/(project+'.csproj')),
            '-c','Release','-r','win-x64','-o',str(output/target),'--nologo',
            '--source','https://api.nuget.org/v3/index.json','-p:OS=Windows_NT',
            '-p:RestorePackagesPath='+str(ROOT/'work/nuget-packages')],check=True)
    return output

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--dotnet',required=True);p.add_argument('--output',required=True)
    a=p.parse_args();print(build(a.dotnet,a.output))
