#ifndef FELSORIA_BG_ROLE_BALANCE_H
#define FELSORIA_BG_ROLE_BALANCE_H
#include <algorithm>
#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <utility>
#include <vector>
namespace FelsoriaBGRole {
// Native Roles: tank=0, healer=1, DPS=2. Queue masks are different: 2/4/8.
inline unsigned Mask(unsigned role){return role<3?2u<<role:0u;}
inline unsigned ObservedMask(unsigned role,bool bot){unsigned mask=Mask(role);return mask?mask:bot?0u:8u;}
inline unsigned Role(unsigned mask){return mask==2?0u:mask==4?1u:mask==8?2u:3u;}
struct Counts {unsigned n[3]={};unsigned Total()const{return n[0]+n[1]+n[2];}};
struct Quota {unsigned n[3]={};bool valid=true;};
inline Quota Budget(unsigned size,Counts const&humanA=Counts(),Counts const&humanH=Counts()){
 Quota q;unsigned minimum[2]={std::max(humanA.n[0],humanH.n[0]),std::max(humanA.n[1],humanH.n[1])};
 if(minimum[0]+minimum[1]>size){q.valid=false;return q;}
 q.n[0]=std::max(size>=8?(size+19)/20:0u,minimum[0]);
 q.n[1]=std::max(size>=5?std::max(1u,size/5):0u,minimum[1]);
 if(q.n[0]+q.n[1]>size){unsigned remove=std::min(q.n[1]-minimum[1],q.n[0]+q.n[1]-size);q.n[1]-=remove;}
 if(q.n[0]+q.n[1]>size)q.n[0]-=q.n[0]+q.n[1]-size;
 q.n[2]=size-q.n[0]-q.n[1];return q;
}
inline unsigned Choose(Quota const&q,Counts const&used,unsigned allowed=14,unsigned previous=0){
 if(!q.valid)return 0;unsigned old=Role(previous);
 if(old<3&&(allowed&previous)&&used.n[old]<q.n[old])return previous;
 unsigned size=q.n[0]+q.n[1]+q.n[2],chosen=0;std::int64_t best=-(std::int64_t(1)<<60);
 for(unsigned r=0;r<3;++r)if((allowed&Mask(r))&&used.n[r]<q.n[r]){
  std::int64_t score=std::int64_t(used.Total()+1)*q.n[r]-std::int64_t(used.n[r])*size;
  if(score>best){best=score;chosen=Mask(r);}
 }return chosen;
}
inline unsigned ScheduleCode(unsigned klass,unsigned specIndex){
 // Schedule codes: 4=random, 5=Restoration, 6=Guardian (native flushTalent uses code-1).
 return klass==11?(specIndex==2?6u:specIndex==3?5u:specIndex+1):specIndex<3?specIndex+1:0;
}
template<class Lookup>unsigned Supported(unsigned klass,Lookup lookup){unsigned allowed=0;
 for(unsigned i=0;i<4;++i)if(auto*spec=lookup(klass,i))allowed|=Mask(spec->Role);return allowed;
}
template<class Lookup,class Random>unsigned SelectCode(unsigned klass,unsigned mask,unsigned preferred,Lookup lookup,Random random){
 std::vector<unsigned>codes;unsigned role=Role(mask);if(role>=3)return 0;
 for(unsigned i=0;i<4;++i)if(auto*spec=lookup(klass,i))if(unsigned(spec->Role)==role){unsigned code=ScheduleCode(klass,i);
  if(!code)continue;if(unsigned(spec->ID)==preferred)return code;codes.push_back(code);
 }return codes.empty()?0:codes[random(unsigned(codes.size()))];
}
struct Context {unsigned type=0,bracket=0,instance=0,team=0,size=0;};
inline bool SamePool(Context const&a,Context const&b){
 return a.instance?b.instance==a.instance:!b.instance&&a.type==b.type&&a.bracket==b.bracket;
}
enum Stage {Planned,Queued,Invited,Active,Departed};
struct Entry {Context context;unsigned account=0,guid=0,mask=0;std::uint32_t at=0;bool bot=true,managed=false;Stage stage=Planned;};
class Ledger {
 std::map<std::pair<unsigned,unsigned>,Entry>entries;
 mutable std::mutex mutex;
 void Purge(std::uint32_t now){for(auto i=entries.begin();i!=entries.end();)
  // A caller can capture now before another thread refreshes the entry.
  // Future stamps must not underflow into an apparent multi-week timeout.
  if(std::uint32_t(now-i->second.at)<0x80000000u&&
     std::uint32_t(now-i->second.at)>(i->second.stage==Active?30000u:180000u))i=entries.erase(i);else ++i;
 }
public:
 bool Plan(unsigned account,unsigned type,Context&context,unsigned&mask,Stage*stage=nullptr)const{
  std::lock_guard<std::mutex>lock(mutex);auto i=entries.find(std::make_pair(account,type));if(i==entries.end())return false;
  context=i->second.context;mask=i->second.mask;if(stage)*stage=i->second.stage;return true;
 }
 unsigned Reserve(unsigned account,Context context,std::uint32_t now,unsigned allowed=14,bool managed=false){
  std::lock_guard<std::mutex>lock(mutex);Purge(now);if(context.team>1||!context.size)return 0;
  Counts used,humans[2];std::set<unsigned>counted;unsigned previous=0;
  auto key=std::make_pair(account,context.type);auto old=entries.find(key);
  if(old!=entries.end()&&SamePool(context,old->second.context)&&old->second.context.team==context.team)previous=old->second.mask;
  for(auto const&p:entries){auto const&e=p.second;unsigned role=Role(e.mask);
   if(e.account==account||role>=3||e.context.team>1||!SamePool(context,e.context)||!counted.insert(e.account).second)continue;
   if(!e.bot)++humans[e.context.team].n[role];if(e.context.team==context.team)++used.n[role];
  }
  unsigned mask=Choose(Budget(context.size,humans[0],humans[1]),used,allowed,previous);
  if(!mask){if(old!=entries.end()&&old->second.stage==Planned)entries.erase(old);return 0;}
  Entry e;e.account=account;e.context=context;e.mask=mask;e.at=now;e.managed=managed;
  if(old!=entries.end()){e.guid=old->second.guid;if(old->second.stage!=Departed&&SamePool(context,old->second.context))e.stage=old->second.stage;e.managed=e.managed||old->second.managed;}
  entries[key]=e;return mask;
 }
 void Queue(unsigned account,unsigned guid,Context context,unsigned mask,bool bot,std::uint32_t now){
  std::lock_guard<std::mutex>lock(mutex);Purge(now);auto key=std::make_pair(account,context.type);auto old=entries.find(key);
  // A late session snapshot must not revive a cancelled managed queue job.
  if(old!=entries.end()&&old->second.managed&&old->second.stage==Departed&&bot)return;
  Entry e;if(old!=entries.end()&&old->second.stage!=Departed){e=old->second;if(e.managed&&e.context.instance)context.instance=e.context.instance;}
  e.account=account;e.guid=guid;e.context=context;e.mask=mask;e.bot=bot;e.at=now;e.stage=Queued;entries[key]=e;
 }
 bool Allows(unsigned account,unsigned type,unsigned instance)const{
  std::lock_guard<std::mutex>lock(mutex);auto i=entries.find(std::make_pair(account,type));
  return i==entries.end()||!i->second.managed||(i->second.stage!=Departed&&(!i->second.context.instance||i->second.context.instance==instance));
 }
 void BindPool(unsigned type,unsigned bracket,unsigned instance,unsigned size,std::uint32_t now){
  std::lock_guard<std::mutex>lock(mutex);Purge(now);unsigned used[2]={};
  for(auto const&p:entries)if(p.second.context.instance==instance&&p.second.context.team<2)++used[p.second.context.team];
  for(auto&p:entries){auto&e=p.second;if(!e.context.instance&&e.context.type==type&&e.context.bracket==bracket&&e.context.team<2&&used[e.context.team]<size){
   ++used[e.context.team];e.context.instance=instance;e.context.size=size;e.at=now;
  }}
 }
 void Invite(unsigned account,unsigned guid,Context context,unsigned mask,bool bot,std::uint32_t now){
  std::lock_guard<std::mutex>lock(mutex);Purge(now);auto key=std::make_pair(account,context.type);Entry&e=entries[key];
  if(e.managed&&e.stage==Departed&&bot)return;
  e.account=account;e.guid=guid;e.context=context;e.mask=mask;e.bot=bot;e.at=now;e.stage=Invited;
 }
 void ObserveActive(unsigned account,unsigned guid,Context context,unsigned mask,bool bot,std::uint32_t now){
  std::lock_guard<std::mutex>lock(mutex);Purge(now);auto i=std::find_if(entries.begin(),entries.end(),[&](std::pair<std::pair<unsigned,unsigned>const,Entry>const&p){return p.second.account==account&&p.second.context.instance==context.instance;});
  if(i==entries.end())i=entries.emplace(std::make_pair(account,context.type),Entry()).first;
  Entry&e=i->second;context.type=i->first.second;e.account=account;e.guid=guid;e.context=context;e.mask=mask;e.bot=bot;e.at=now;e.stage=Active;
 }
 void Idle(unsigned account){std::lock_guard<std::mutex>lock(mutex);for(auto i=entries.begin();i!=entries.end();)
  if(i->second.account==account&&i->second.stage==Active)i=entries.erase(i);else ++i;
 }
 void Touch(unsigned account,std::uint32_t now){std::lock_guard<std::mutex>lock(mutex);for(auto&e:entries)if(e.second.account==account&&e.second.stage!=Active)e.second.at=now;}
 // Physical slot accounting uses the native invited count (inside + native invitations).
 // Only uninvited preparation/queue jobs consume additional future slots.
 unsigned Pending(Context const&context,std::uint32_t now,bool unqueuedOnly=false){
  std::lock_guard<std::mutex>lock(mutex);Purge(now);unsigned count=0;std::set<unsigned>seen;
  for(auto const&p:entries){auto const&e=p.second;
   if(e.bot&&Role(e.mask)<3&&SamePool(context,e.context)&&e.context.team==context.team&&
      (e.stage==Planned||(!unqueuedOnly&&e.stage==Queued))&&seen.insert(e.account).second)++count;
  }return count;
 }
 Counts Usage(Context const&context,std::uint32_t now){
  std::lock_guard<std::mutex>lock(mutex);Purge(now);Counts count;std::set<unsigned>seen;
  for(auto const&p:entries){auto const&e=p.second;unsigned role=Role(e.mask);
   if(role<3&&SamePool(context,e.context)&&e.context.team==context.team&&seen.insert(e.account).second)++count.n[role];
  }return count;
 }
 // Keep a zero-role tombstone until the manager consumes it. This also stops
 // stale queue snapshots/invites from re-creating a managed job during logout.
 void Cancel(unsigned account,std::uint32_t now){std::lock_guard<std::mutex>lock(mutex);for(auto&p:entries)if(p.second.account==account){p.second.mask=0;p.second.stage=Departed;p.second.at=now;}}
 unsigned Leave(unsigned guid,unsigned instance,std::uint32_t now){
  std::lock_guard<std::mutex>lock(mutex);unsigned released=0;
  for(auto&p:entries){auto&e=p.second;if(e.guid==guid&&e.context.instance==instance){
   released+=Role(e.mask)<3;e.mask=0;e.stage=Departed;e.at=now;
  }}return released;
 }
 void Release(unsigned account){std::lock_guard<std::mutex>lock(mutex);for(auto i=entries.begin();i!=entries.end();)if(i->second.account==account)i=entries.erase(i);else ++i;}
 void RemoveQueue(unsigned guid,unsigned type,unsigned instance,bool declined){
  std::lock_guard<std::mutex>lock(mutex);for(auto&p:entries){auto&e=p.second;
   if(e.guid==guid&&e.context.type==type&&(!instance||declined)){e.mask=0;e.stage=Departed;}
  }
 }
};
inline Ledger& Registry(){static Ledger ledger;return ledger;}
}
#endif
