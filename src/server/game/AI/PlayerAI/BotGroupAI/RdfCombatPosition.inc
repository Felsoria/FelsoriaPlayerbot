// FELSORIA_DUNGEON_COMBAT_V1: no second pathfinder or floor projection.
bool BotGroupAI::LaunchRdfValidatedPath(PathGenerator const& path)
{
    if(m_MechanicOwned || PathApproachesBlockedBoss(path))return false;
    auto const& points=path.GetPath();
    if(points.size()<2 || !(path.GetPathType()&PATHFIND_NORMAL) ||
       (path.GetPathType()&(PATHFIND_NOPATH|PATHFIND_NOT_USING_PATH|PATHFIND_INCOMPLETE|PATHFIND_SHORTCUT|PATHFIND_SHORT)))return false;
    std::vector<FelsoriaMechanics::Vec> route;
    route.push_back({me->GetPositionX(),me->GetPositionY(),me->GetPositionZ()});
    for(auto const& p:points)route.push_back({p.x,p.y,p.z});
    if(!AllowsDungeonRoute(route))return false;
    auto const& end=points.back();
    uint32 now=getMSTime();
    if(me->movespline && !me->movespline->Finalized()) {
        auto old=me->movespline->FinalDestination();
        float dx=old.x-end.x,dy=old.y-end.y,dz=old.z-end.z;
        if(dx*dx+dy*dy+dz*dz<=1.0f) {
            float px=me->GetPositionX()-m_RdfPathProgressPosition.GetPositionX();
            float py=me->GetPositionY()-m_RdfPathProgressPosition.GetPositionY();
            float pz=me->GetPositionZ()-m_RdfPathProgressPosition.GetPositionZ();
            if(px*px+py*py+pz*pz>0.0625f) {
                m_RdfPathProgressPosition=me->GetPosition();m_RdfPathProgressTime=now;
            }
            if(!RdfRotationPolicy::elapsed(now,m_RdfPathProgressTime,4000))return true;
            TC_LOG_INFO(LOG_FILTER_LFG,"RDF_PATH_RETRY account=%u reason=no_progress",me->GetSession()->GetAccountId());
        }
    }
    // FELSORIA_FORMATION_RECOVERY_V1: a previous generator must not restart
    // its old path after we launch this validated raw spline.
    m_Movement->ClearMovement();
    me->GetMotionMaster()->Clear();
    Movement::MoveSplineInit init(*me);
    init.MovebyPath(points);init.SetWalk(false);
    if(init.Launch()<=0)return false;
    m_RdfPathProgressPosition=me->GetPosition();m_RdfPathProgressTime=now;
    return true;
}

// FELSORIA_AOE_RESUME_V1: full-ring recovery and at most two short detour legs.
bool BotGroupAI::ProcessRdfCombatPosition(Unit* target,float minRange,float maxRange)
{
    if(m_MechanicOwned || ShouldBlockBossPull(target))return true;
    if(!target || !target->IsInWorld() || target->GetMap()!=me->GetMap() ||
       me->IsBeingTeleported() || me->IsChangeMap() || IsNotMovement())return false;
    bool closeCombat=(IsTankBotAI() || IsMeleeBotAI()) && target->IsHostileTo(me);
    if(closeCombat && me->IsWithinMeleeRange(target) && me->IsWithinLOSInMap(target)){
        m_RdfCombatDetourMoving=false;m_RdfCombatDetourSteps=0;return true;
    }
    float current=me->GetDistance(target);
    if(m_RdfCombatDetourTarget!=target->GetGUID() ||
       target->GetDistance(m_RdfCombatDetourAnchor)>2.0f){
        m_RdfCombatDetourTarget=target->GetGUID();m_RdfCombatDetourAnchor=target->GetPosition();
        m_RdfCombatDetourMoving=false;m_RdfCombatDetourSteps=0;
    }
    if(m_RdfCombatDetourMoving){
        bool keep=false;
        if(me->movespline && !me->movespline->Finalized()){
            auto end=me->movespline->FinalDestination();
            float dx=end.x-m_RdfCombatDetourDestination.GetPositionX();
            float dy=end.y-m_RdfCombatDetourDestination.GetPositionY();
            float dz=end.z-m_RdfCombatDetourDestination.GetPositionZ();
            keep=dx*dx+dy*dy+dz*dz<=1.0f && me->GetDistance(m_RdfCombatDetourDestination)>1.0f;
        }
        if(keep && me->GetDistance(m_RdfPathProgressPosition)>0.25f){
            m_RdfPathProgressPosition=me->GetPosition();m_RdfPathProgressTime=getMSTime();
        }
        if(keep && !RdfRotationPolicy::elapsed(getMSTime(),m_RdfPathProgressTime,4000))return true;
        m_RdfCombatDetourMoving=false;
    }
    if(!closeCombat && current>=minRange && current<=maxRange && me->IsWithinLOSInMap(target)){
        m_RdfCombatDetourMoving=false;m_RdfCombatDetourSteps=0;return true;
    }
    if(closeCombat){minRange=0;maxRange=4.0f;}
    uint32 now=getMSTime();
    if(m_RdfPositionTick && !RdfRotationPolicy::elapsed(now,m_RdfPositionTick,500))return true;
    m_RdfPositionTick=now;
    float desired=FelsoriaMechanics::combatRadius(current,minRange,maxRange,closeCombat);
    float base=std::atan2(me->GetPositionY()-target->GetPositionY(),me->GetPositionX()-target->GetPositionX())-target->GetOrientation();
    ++m_RdfPositionAttempt;
    NearCreatureVec idleCreatures;
    SearchCreatureListFromRange(me,idleCreatures,70.0f,false);
    auto tryPoint=[&](Position const& dest,bool detour)->bool{
        float dx=dest.GetPositionX()-target->GetPositionX(),dy=dest.GetPositionY()-target->GetPositionY();
        float distance=std::sqrt(dx*dx+dy*dy);
        if((!detour && (distance<minRange || distance>maxRange)) || std::fabs(dest.GetPositionZ()-target->GetPositionZ())>4.5f)return false;
        if(!me->GetMap()->isInLineOfSight(dest.GetPositionX(),dest.GetPositionY(),dest.GetPositionZ()+0.75f,
           target->GetPositionX(),target->GetPositionY(),target->GetPositionZ()+0.75f,me->GetPhases()))return false;
        PathGenerator path(me);
        if(!path.CalculatePath(dest.GetPositionX(),dest.GetPositionY(),dest.GetPositionZ(),false,false,false) || path.GetPathType()!=PATHFIND_NORMAL)return false;
        auto const& points=path.GetPath();if(points.size()<2)return false;
        auto const& end=points.back();dx=end.x-dest.GetPositionX();dy=end.y-dest.GetPositionY();
        if(dx*dx+dy*dy>0.25f || std::fabs(end.z-dest.GetPositionZ())>0.5f)return false;
        float tx=end.x-target->GetPositionX(),ty=end.y-target->GetPositionY();
        float actualRange=std::sqrt(tx*tx+ty*ty);
        if(!detour && (actualRange<minRange || actualRange>maxRange))return false;
        if(!me->GetMap()->isInLineOfSight(end.x,end.y,end.z+0.75f,
           target->GetPositionX(),target->GetPositionY(),target->GetPositionZ()+0.75f,me->GetPhases()))return false;
        float length=0;
        FelsoriaMechanics::Vec previous{me->GetPositionX(),me->GetPositionY(),me->GetPositionZ()};
        for(auto const& p:points){
            FelsoriaMechanics::Vec next{p.x,p.y,p.z};
            length+=FelsoriaMechanics::distance(previous,next);previous=next;
        }
        float direct=FelsoriaMechanics::distance({me->GetPositionX(),me->GetPositionY(),me->GetPositionZ()},{end.x,end.y,end.z});
        if(length>std::min(55.0f,direct*1.75f+4.0f))return false;
        // Expanded approaches must not body-pull a new pack.
        for(Creature* creature:idleCreatures){
            if(!creature || !creature->IsInWorld() || !creature->isAlive() ||
               creature->isInCombat() || !me->InSamePhase(creature) ||
               !creature->IsHostileTo(me) || creature->GetReactState()!=REACT_AGGRESSIVE)continue;
            float radius=creature->GetAttackDistance(me)+2.0f;
            FelsoriaMechanics::Vec previousPoint{me->GetPositionX(),me->GetPositionY(),me->GetPositionZ()};
            for(auto const& point:points){
                FelsoriaMechanics::Vec next{point.x,point.y,point.z};
                if(std::fabs(next.z-creature->GetPositionZ())<6.0f &&
                   FelsoriaMechanics::segmentDistance2({creature->GetPositionX(),creature->GetPositionY()},previousPoint,next)<radius*radius)return false;
                previousPoint=next;
            }
        }
        if(!LaunchRdfValidatedPath(path))return false;
        if(detour){
            m_RdfCombatDetourDestination.Relocate(end.x,end.y,end.z);
            m_RdfCombatDetourMoving=true;++m_RdfCombatDetourSteps;
            TC_LOG_INFO(LOG_FILTER_LFG,"RDF_COMBAT_DETOUR account=%u target=%u step=%u",
                me->GetSession()->GetAccountId(),target->GetEntry(),m_RdfCombatDetourSteps);
        }else m_RdfCombatDetourSteps=0;
        return true;
    };
    // Nearest side first; widened angles cover the rest of the target ring.
    float offsets[]={0.0f,0.25f,-0.25f,0.5f,-0.5f,1.0f,-1.0f,
        1.5f,-1.5f,2.0f,-2.0f,2.5f,-2.5f,3.14159265f};
    for(float offset:offsets){
        Position dest;target->GetFirstCollisionPosition(dest,desired,base+offset);
        if(tryPoint(dest,false))return true;
    }
    // Live hazards do not alter the native mmap mesh. Reach a safe side waypoint
    // first, then recompute the final native path from that real position.
    // Limit lateral steps until the target moves or a combat route succeeds.
    if(m_RdfCombatDetourSteps<2){
        float radius=std::max(desired+4.0f,std::min(14.0f,std::max(8.0f,current)));
        for(float offset:{1.57079633f,-1.57079633f}){
            Position via;target->GetFirstCollisionPosition(via,radius,base+offset);
            if(me->GetDistance(via)<=2.0f)continue;
            if(tryPoint(via,true))return true;
        }
    }
    if(m_RdfPositionAttempt%10==0)
        TC_LOG_INFO(LOG_FILTER_LFG,"RDF_COMBAT_POSITION_BLOCKED account=%u target=%u reason=no_safe_combat_path",
            me->GetSession()->GetAccountId(),target->GetEntry());
    return false;
}
