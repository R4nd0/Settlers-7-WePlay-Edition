'use strict';
class GoldMarkerPolicy {
  constructor(markers) {
    const prefix='WEPLAY_GOLD_CAPTURE_V1 ';
    if(!Array.isArray(markers)||markers.length<1||markers.length>16||new Set(markers).size!==markers.length||
       markers.some(s=>typeof s!=='string'||!s.startsWith(prefix)||s.length<=prefix.length||s.length>128||!/^[A-Za-z0-9_ .:=+-]+$/.test(s)))
      throw Error('invalid-gold-markers');
    this.markers=markers.slice();this.count=0;
  }
  match(text) {return this.count>=128||typeof text!=='string'?null:(this.markers.indexOf(text)<0?null:this.markers.indexOf(text));}
  commit() {if(this.count>=128)return false;this.count++;return true;}
}
if(typeof module!=='undefined')module.exports=GoldMarkerPolicy;
