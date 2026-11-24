"""Read-only GPU BMP A/B differences. Changed pixels are not a quality score.

No input image is cropped, rewritten, resampled or enhanced. Optional native
capture receipts bind each image and its same-frame original shader uniforms;
they cannot establish unmeasured exposure or aesthetic quality.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
import math
from pathlib import Path
import struct

from . import inventory

MAX_IMAGE=128*1024*1024
MAX_PIXELS=16*1024*1024
RIVER_VS="f190d66ee1ae43637341a76e5b644cc63bf3c2a5b5c94ea9286764e13f1c16c0"
RIVER_PS="dd65ba0b9d79c7c4d9ad9904404388230556b194eedb8cbdfbd4fa99f44eee5b"


@dataclass(frozen=True)
class Bitmap:
    data: bytes
    width: int
    height: int
    offset: int
    stride: int
    pixel_bytes: int
    bottom_up: bool

    def row(self,y):
        index=self.height-1-y if self.bottom_up else y
        start=self.offset+index*self.stride
        return memoryview(self.data)[start:start+self.stride]


def bitmap(raw: bytes) -> Bitmap:
    if not isinstance(raw,bytes) or not 54<=len(raw)<=MAX_IMAGE or raw[:2]!=b"BM":raise ValueError("Unsupported BMP input")
    size,offset=struct.unpack_from("<I4xI",raw,2)
    dib,width,height,planes,bits,compression=struct.unpack_from("<IiiHHI",raw,14)
    if size!=len(raw) or dib!=40 or not 1<=width<=8192 or not 1<=abs(height)<=8192 or width*abs(height)>MAX_PIXELS:
        raise ValueError("Invalid BMP geometry or size")
    if planes!=1 or bits not in (24,32) or compression or offset<54:raise ValueError("Only native uncompressed RGB BMP captures are supported")
    stride=((width*bits+31)//32)*4
    if offset+stride*abs(height)!=len(raw):raise ValueError("Truncated or trailing BMP pixels")
    return Bitmap(raw,width,abs(height),offset,stride,bits//8,height>0)


def differences(control: bytes,candidate: bytes,roi=None,*,threshold=3) -> dict:
    first,second=bitmap(control),bitmap(candidate)
    if (first.width,first.height)!=(second.width,second.height):raise ValueError("Capture dimensions differ; no implicit resampling")
    if type(threshold)is not int or not 0<=threshold<=255:raise ValueError("Invalid RGB difference threshold")
    if roi is None:roi=(0,0,first.width,first.height)
    if (not isinstance(roi,(tuple,list)) or len(roi)!=4 or any(type(v)is not int for v in roi)):
        raise ValueError("ROI requires integer x,y,width,height")
    x,y,width,height=roi
    if min(x,y)<0 or width<=0 or height<=0 or x+width>first.width or y+height>first.height:raise ValueError("ROI is outside capture")
    sums=[0,0,0];squares=0;maximum=0;changed=0
    for row_index in range(y,y+height):
        left,right=first.row(row_index),second.row(row_index)
        for column in range(x,x+width):
            a,b=column*first.pixel_bytes,column*second.pixel_bytes
            delta=[abs(left[a+2]-right[b+2]),abs(left[a+1]-right[b+1]),abs(left[a]-right[b])]
            peak=max(delta);changed+=peak>threshold;maximum=max(maximum,peak)
            for channel in range(3):sums[channel]+=delta[channel];squares+=delta[channel]*delta[channel]
    count=width*height
    return {"format":"s7-material-image-difference-v1","dimensions":[first.width,first.height],"roi":list(roi),
            "control_sha256":hashlib.sha256(control).hexdigest(),"candidate_sha256":hashlib.sha256(candidate).hexdigest(),
            "compared_pixels":count,"threshold":threshold,"changed_rgb_pixels":changed,"changed_fraction":changed/count,
            "mean_abs_rgb_difference":sum(sums)/(count*3),"channel_mae_rgb":[v/count for v in sums],
            "rms_rgb_difference":math.sqrt(squares/(count*3)),"max_abs_rgb_difference":maximum,
            "input_images_modified":False,"resampled":False,"visual_quality_assessed":False,"runtime_qualified":False,
            "interpretation":"Pixel differences can include animation, camera, lighting and UI; changed does not mean improved."}


def _vector(value,count):
    return isinstance(value,list) and len(value)==count and all(type(v)in(int,float) and math.isfinite(v) for v in value)


def _uniforms(meta,image_hash,stage):
    if (not isinstance(meta,dict) or meta.get("format")!="s7-native-frame-capture-v1" or meta.get("complete")is not True
            or meta.get("status")!=0 or meta.get("fallback_used")is not False
            or meta.get(stage+"_bmp_sha256")!=image_hash):return None
    value=meta.get("scene_uniforms")
    if (not isinstance(value,dict) or value.get("source")!="original_river_shader_uniforms" or value.get("available")!=1
            or value.get("context_valid")!=7 or value.get("frame")!=meta.get("frame")
            or value.get("vertex_sha256")!=RIVER_VS or value.get("pixel_sha256")!=RIVER_PS
            or value.get("target")!=meta.get("source") or value.get("viewport")!=meta.get("source")):return None
    for name,count in (("vs_constants_12_15",16),("vs16_eye",3),("ps5_sun_direction",3),("ps7_sun_specular",3)):
        if not _vector(value.get(name),count):return None
    return value


def uniform_comparison(control_meta,candidate_meta,control_hash,candidate_hash,*,stage="source") -> dict:
    if stage not in ("source","output"):raise ValueError("Unknown GPU image stage")
    first=_uniforms(control_meta,control_hash,stage);second=_uniforms(candidate_meta,candidate_hash,stage)
    result={"same_frame_provenance_per_image":bool(first and second),"captured_camera_uniforms_match":False,
            "captured_light_uniforms_match":False,"visible_camera_independently_validated":False,
            "exposure_matched":None,"quality_assessed":False}
    if not first or not second:return result
    maxima={name:max(abs(a-b)for a,b in zip(first[name],second[name]))
            for name in ("vs_constants_12_15","vs16_eye","ps5_sun_direction","ps7_sun_specular")}
    result["maximum_uniform_differences"]=maxima
    result["catalogs_match"]=first.get("catalog_sha256")==second.get("catalog_sha256")
    result["captured_camera_uniforms_match"]=maxima["vs_constants_12_15"]<=1e-6 and maxima["vs16_eye"]<=.01
    result["captured_light_uniforms_match"]=maxima["ps5_sun_direction"]<=1e-6 and maxima["ps7_sun_specular"]<=1e-6
    result["provenance_note"]="Matched original River draw uniforms, not a generic Vision camera or an exposure measurement."
    return result


def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--control",type=Path,required=True);parser.add_argument("--candidate",type=Path,required=True)
    parser.add_argument("--roi",type=int,nargs=4);parser.add_argument("--threshold",type=int,default=3)
    parser.add_argument("--control-meta",type=Path);parser.add_argument("--candidate-meta",type=Path)
    parser.add_argument("--stage",choices=("source","output"),default="source")
    parser.add_argument("--output",type=Path,required=True);args=parser.parse_args(argv)
    for path in (args.control,args.candidate):
        if path.stat().st_size>MAX_IMAGE:raise ValueError("Image exceeds read bound")
    result=differences(args.control.read_bytes(),args.candidate.read_bytes(),args.roi,threshold=args.threshold)
    if bool(args.control_meta)!=bool(args.candidate_meta):raise ValueError("Supply both capture receipts")
    if args.control_meta:
        for path in (args.control_meta,args.candidate_meta):
            if path.stat().st_size>1024*1024:raise ValueError("Capture receipt exceeds read bound")
        result["uniform_comparison"]=uniform_comparison(json.loads(args.control_meta.read_text()),json.loads(args.candidate_meta.read_text()),
            result["control_sha256"],result["candidate_sha256"],stage=args.stage)
    inventory.write_private_report(result,args.output)
    print(json.dumps({key:result[key]for key in ("changed_rgb_pixels","compared_pixels","mean_abs_rgb_difference","visual_quality_assessed")}))
    return 0


if __name__=="__main__":raise SystemExit(main())
