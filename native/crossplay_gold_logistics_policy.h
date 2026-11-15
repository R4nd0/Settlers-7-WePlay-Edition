#pragma once
#include "crossplay_gold_logistics_binding.h"
#include "neo_logistics_state.h"
#include <algorithm>
// Experimental metadata ledger and detached Gold ordering policy. No hooks,
// native game writes or implicit activation. Caller serializes every operation
// on the verified simulation owner thread. Allocate this large ledger in stable
// owned heap/static storage, not on a small native callback stack.
// The caller must authenticate the NEO capability/choice/state before Begin or
// Restore, and supply the genuine owned world's next replay turn.
namespace GoldLogistics {
using GoldLogisticsBinding::World;using GoldLogisticsBinding::Request;
struct Ledger {
 World world{};neo_fifo::Mark marks[65536]{};bool enabled=false;uint32_t turn=0;
 bool Same(const World& candidate)const {
  return world.logic && candidate.logic==world.logic && candidate.main==world.main
   && candidate.priority==world.priority && candidate.transfers==world.transfers;
 }
 bool Live(const World& candidate)const {
  World actual{};return Same(candidate) && GoldLogisticsBinding::CaptureWorld(actual) && Same(actual);
 }
 void Clear(){world={};memset(marks,0,sizeof(marks));enabled=false;turn=0;}
 bool Begin(const World& candidate,uint32_t next_turn,bool choice) {
  GoldLogisticsBinding::ErrorScope error;World actual{};
  if(next_turn>1 || !GoldLogisticsBinding::CaptureWorld(actual) || actual.logic!=candidate.logic
   || actual.main!=candidate.main || actual.priority!=candidate.priority || actual.transfers!=candidate.transfers)return false;
  Clear();world=candidate;turn=next_turn;enabled=choice;return true;
 }
 bool Advance(const World& candidate,uint32_t next_turn) {
  GoldLogisticsBinding::ErrorScope error;if(!Live(candidate) || next_turn<turn)return false;turn=next_turn;return true;
 }
 bool Mark(uint32_t id,neo_fifo::Intent intent) {
  GoldLogisticsBinding::ErrorScope error;uint32_t task=0,birth=0,player=0;
  if(!enabled || !Live(world) || (intent!=neo_fifo::Intent::Prefer && intent!=neo_fifo::Intent::Defer)
   || !id || id==UINT32_MAX || !GoldLogisticsBinding::Task(world,id,task) || !GoldLogisticsBinding::Word(task+0x2c,birth)
   || !GoldLogisticsBinding::Word(task+0x34,player) || (player&255u)>=25 || (static_cast<int32_t>(birth)>=0 && birth>turn))return false;
  marks[id&65535u]={id,birth,task,intent};return true;
 }
 bool Key(const Request& request,neo_fifo::Key& output)const {
  Request actual{};
  if(!GoldLogisticsBinding::CaptureRequest(world,request.reservation,actual) || actual.task!=request.task || actual.id!=request.id)return false;
  neo_fifo::Key native_key{neo_fifo::Intent::Prefer,static_cast<int32_t>(actual.priority),static_cast<int32_t>(actual.birth),actual.id,actual.reservation,false,0};
  if(!actual.ordinary_request){output=native_key;return true;}
  auto node=actual.task;auto intent=neo_fifo::Intent::Ordinary;bool complete=false,known_bucket=false;
  for(unsigned depth=0;node && depth<32;++depth) {
   uint32_t id=0,birth=0,bound=0,parent=0,parent_pointer=0,player=0,group=0,bucket=0;
   if(!GoldLogisticsBinding::Word(node+8,id) || !GoldLogisticsBinding::Task(world,id,bound) || bound!=node
    || !GoldLogisticsBinding::Word(node+0x2c,birth) || !GoldLogisticsBinding::Word(node+0x34,player) || (player&255u)!=actual.player
    || !GoldLogisticsBinding::Word(node+0x1c,group) || group>10 || !GoldLogisticsBinding::Word(node+0x20,bucket) || bucket>10
    || !GoldLogisticsBinding::Word(node+0x24,parent) || !GoldLogisticsBinding::Word(node+0x38,parent_pointer))return false;
   const auto& mark=marks[id&65535u];if(intent==neo_fifo::Intent::Ordinary && mark.Matches(id,birth,node))intent=mark.intent;
   uint32_t table=0;if(!GoldLogisticsBinding::Word(node,table))return false;
   if(bucket==4 && table==0xf4c74c)known_bucket=true;
   if(!parent){known_bucket=known_bucket || bucket<8;complete=true;break;}
   uint32_t next=0;if(!GoldLogisticsBinding::Task(world,parent,next) || next!=parent_pointer || next==node)return false;
   node=next;
  }
  if(!complete)return false;
  if(!known_bucket){output=native_key;return true;}
  // Group-free NEO: the same public comparator as History. Equal birth turns
  // remain equivalent so the native stable sort retains arrival order.
  output={intent,static_cast<int32_t>(actual.priority),static_cast<int32_t>(actual.birth),actual.id,actual.reservation,false,0};return true;
 }
 uint32_t Compare(const World& candidate,const Request& left,const Request& right)const {
  GoldLogisticsBinding::ErrorScope error;neo_fifo::Key a{},b{};
  if(!enabled || !Live(candidate) || !Key(left,a) || !Key(right,b))return 2;
  return neo_fifo::Before(a,b)?1u:0u;
 }
 bool Snapshot(const World& candidate,uint32_t next_turn,neo_logistics::SavedState& output)const {
  GoldLogisticsBinding::ErrorScope error;if(!Live(candidate) || next_turn!=turn)return false;
  neo_logistics::SavedState saved{};saved.enabled=enabled;saved.turn=turn;
  if(enabled)for(const auto& mark:marks) {
   uint32_t task=0,birth=0;if(!mark.id || mark.intent==neo_fifo::Intent::Ordinary)continue;
   if(GoldLogisticsBinding::Task(world,mark.id,task) && GoldLogisticsBinding::Word(task+0x2c,birth) && mark.Matches(mark.id,birth,task))
    saved.marks.push_back({mark.id,static_cast<int32_t>(birth),mark.intent});
  }
  std::sort(saved.marks.begin(),saved.marks.end(),[](const auto& a,const auto& b){return a.id<b.id;});
  if(neo_logistics::Encode(saved).empty())return false;output=std::move(saved);return true;
 }
 bool Restore(const World& candidate,uint32_t next_turn,const std::string& payload) {
  GoldLogisticsBinding::ErrorScope error;World actual{};neo_logistics::SavedState saved{};
  if(!GoldLogisticsBinding::CaptureWorld(actual) || actual.logic!=candidate.logic || actual.main!=candidate.main
   || actual.priority!=candidate.priority || actual.transfers!=candidate.transfers || !neo_logistics::Decode(payload,saved)
   || (saved.enabled && saved.turn!=next_turn))return false;
  std::vector<neo_fifo::Mark> bound;
  for(const auto& mark:saved.marks) {
   uint32_t task=0,birth=0,player=0;
   if(!GoldLogisticsBinding::Task(candidate,mark.id,task) || !GoldLogisticsBinding::Word(task+0x2c,birth) || static_cast<int32_t>(birth)!=mark.created
    || !GoldLogisticsBinding::Word(task+0x34,player) || (player&255u)>=25)return false;
   bound.push_back({mark.id,birth,task,mark.intent});
  }
  // Decode and bind the entire payload before replacing any live ledger state.
  Clear();world=candidate;turn=next_turn;enabled=saved.enabled;
  for(const auto& mark:bound)marks[mark.id&65535u]=mark;return true;
 }
};
}
