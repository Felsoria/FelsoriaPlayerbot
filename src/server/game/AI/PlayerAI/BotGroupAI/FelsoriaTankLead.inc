// FELSORIA_TANK_LEAD_V1: movement only, shared by party/RDF/raid/LFR.
bool BotGroupAI::ProcessTankLeadFormation()
{
    if(m_MechanicOwned)return true;
    Group* group=me->GetGroup();
    Player* anchor=m_MasterPlayer;
    if(!group || !RdfRotationPolicy::tank(me->GetSpecializationId()) ||
       !me->IsInWorld() || !me->isAlive() || !me->GetMap() || me->InBattleground() ||
       !anchor || !anchor->IsInWorld() || !anchor->isAlive() || !anchor->GetSession() ||
       anchor->GetSession()->IsBotSession() || anchor->GetMap()!=me->GetMap() ||
       !me->InSamePhase(anchor) || !group->IsMember(anchor->GetGUID()))return false;
    if(me->GetDistance(anchor)>30.0f)return false;
    if(me->isInCombat())return false;
    for(GroupReference* r=group->GetFirstMember();r;r=r->next())
        if(Player* p=r->getSource())
            if(p->IsInWorld() && p->GetMap()==me->GetMap() && me->InSamePhase(p) && p->isInCombat())return false;
    if(m_StopFollow || m_ForceFlee || m_CruxMovement.HasCruxMovement() ||
       me->IsBeingTeleported() || me->IsChangeMap() || anchor->IsBeingTeleported() ||
       !m_Teleporting.CanMovement() || me->HasUnitState(UNIT_STATE_CASTING) ||
       !me->IsSettingFinish() || m_UseFood.HasFoodState() || me->GetVehicleBase() || anchor->GetVehicleBase() ||
       me->IsFlying() || anchor->IsFlying() || !anchor->m_taxi.empty() || IsNotMovement())return true;
    if(anchor->duel && anchor->duel->state!=DuelState::DUEL_COMPLETED)return true;
    uint32 now=getMSTime();
    if(m_TankLeadTick && !RdfRotationPolicy::elapsed(now,m_TankLeadTick,100))return m_TankLeadHandled;
    bool wasTankLead=m_TankLeadHandled;m_TankLeadHandled=false;
    uint32 dt=m_TankLeadTick?uint32(now-m_TankLeadTick):0;
    m_TankLeadTick=now;
    float heading=anchor->GetOrientation(), prediction=0.0f;
    if(m_TankLeadAnchor==anchor->GetGUID() && dt && dt<1000){
        float dx=anchor->GetPositionX()-m_TankLeadPosition.GetPositionX();
        float dy=anchor->GetPositionY()-m_TankLeadPosition.GetPositionY();
        float moved=std::sqrt(dx*dx+dy*dy);
        if(moved>0.01f && moved<10.0f){
            heading=std::atan2(dy,dx);
            prediction=std::min(1.5f,moved*250.0f/float(dt));
        }
    }
    m_TankLeadAnchor=anchor->GetGUID();m_TankLeadPosition=anchor->GetPosition();
    unsigned slot=0;
    for(GroupReference* r=group->GetFirstMember();r;r=r->next())
        if(Player* p=r->getSource())
            if(p!=me && p->IsInWorld() && p->isAlive() && p->GetMap()==me->GetMap() &&
               me->InSamePhase(p) && RdfRotationPolicy::tank(p->GetSpecializationId()) &&
               p->GetGUID()<me->GetGUID())++slot;
    // Additional raid tanks spread sideways, never progressively farther ahead.
    float side=slot?((slot%2)?1.5f:-1.5f):0.0f;
    float lead=6.5f+prediction; // hard maximum: 8 yards ahead of the player
    NearCreatureVec nearby;SearchCreatureListFromRange(anchor,nearby,65.0f,false);
    auto safePath=[&](PathGenerator const& path)->bool{
        auto const& pts=path.GetPath();
        if(pts.size()<2 || !(path.GetPathType()&PATHFIND_NORMAL) ||
           (path.GetPathType()&(PATHFIND_NOPATH|PATHFIND_NOT_USING_PATH|PATHFIND_INCOMPLETE)))return false;
        for(Creature* c:nearby){
            if(!c || c->isInCombat() || !me->InSamePhase(c) || !c->IsHostileTo(me) ||
               c->GetReactState()!=REACT_AGGRESSIVE)continue;
            float radius=c->GetAttackDistance(me)+2.0f;
            for(size_t i=1;i<pts.size();++i){
                auto const& a=pts[i-1];auto const& b=pts[i];
                float vx=b.x-a.x,vy=b.y-a.y,vz=b.z-a.z;
                float wx=c->GetPositionX()-a.x,wy=c->GetPositionY()-a.y,wz=c->GetPositionZ()-a.z;
                float len=vx*vx+vy*vy+vz*vz;
                float t=len>0.0001f?std::max(0.0f,std::min(1.0f,(wx*vx+wy*vy+wz*vz)/len)):0.0f;
                float dx=wx-t*vx,dy=wy-t*vy,dz=wz-t*vz;
                if(dx*dx+dy*dy+dz*dz<radius*radius)return false;
            }
        }
        return true;
    };
    for(float scale:{1.0f,0.8f,0.6f,0.4f,0.0f}){
        float forward=lead*scale,lateral=side*scale;
        Position dest;
        anchor->GetFirstCollisionPosition(dest,std::sqrt(forward*forward+lateral*lateral),
            heading-anchor->GetOrientation()+std::atan2(lateral,forward));
        if(std::fabs(dest.GetPositionZ()-anchor->GetPositionZ())>4.5f)continue;
        PathGenerator path(me);
        if(!path.CalculatePath(dest.GetPositionX(),dest.GetPositionY(),dest.GetPositionZ(),false,false,false) || !safePath(path) || PathApproachesBlockedBoss(path))continue;
        auto const& end=path.GetPath().back();
        float ex=end.x-dest.GetPositionX(),ey=end.y-dest.GetPositionY();
        if(ex*ex+ey*ey>0.25f || std::fabs(end.z-dest.GetPositionZ())>1.5f)continue;
        float dx=me->GetPositionX()-end.x,dy=me->GetPositionY()-end.y,dz=me->GetPositionZ()-end.z;
        if(dx*dx+dy*dy+dz*dz<=1.0f){
            if(m_RdfFormationMoving){m_Movement->ClearMovement();me->StopMoving();m_RdfFormationMoving=false;}
            m_TankLeadHandled=true;return true;
        }
        // Reuse displacement-based recovery and MotionMaster ownership. Small
        // destination changes do not cancel a healthy running walking spline.
        if(!LaunchRdfValidatedPath(path))continue;
        m_RdfFormationMoving=true;m_TankLeadHandled=true;
        return true;
    }
    // Release the failed front-slot plan to ordinary validated RDF formation/
    // following. A failed tank front slot must not indefinitely claim movement.
    if(wasTankLead && m_RdfFormationMoving){m_Movement->ClearMovement();me->StopMoving();m_RdfFormationMoving=false;}
    return false;
}
