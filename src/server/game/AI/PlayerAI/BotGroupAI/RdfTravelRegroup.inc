// RDF_TRAVEL_REGROUP_V1: require observed transport/vehicle/taxi movement.
// FELSORIA_WARDENS_LIFT_FOLLOW_V1: landing recovery must not suppress normal following.
bool BotGroupAI::ProcessRdfTravelRegroup()
{
    Player* leader=m_MasterPlayer;
    if(!me->GetGroup() || !me->GetGroup()->isLFGGroup() || !leader ||
       !leader->GetSession() || leader->GetSession()->IsBotSession() ||
       !leader->IsInWorld() || !leader->isAlive() || !me->isAlive() ||
       !me->IsInWorld() || leader->GetMap()!=me->GetMap() ||
       !me->GetMap()->IsDungeon() || !me->InSamePhase(leader) || m_StopFollow || m_ForceFlee) {
        m_RdfTravelArmed=false;m_RdfTravelStable=0;return false;
    }
    uint32 now=getMSTime();
    bool riding=leader->GetTransport() || leader->GetVehicleBase() || leader->isInFlight();
    bool fighting=false;
    for(GroupReference* r=me->GetGroup()->GetFirstMember();r;r=r->next()) {
        Player* p=r->getSource();
        if(p && p->isAlive() && p->IsInWorld() && p->GetMap()==me->GetMap() && p->isInCombat()){fighting=true;break;}
    }
    if(m_RdfTravelArmed && (m_RdfTravelLeader!=leader->GetGUID() ||
        m_RdfTravelMap!=me->GetMapId() || m_RdfTravelInstance!=me->GetInstanceId())) {
        m_RdfTravelArmed=false;m_RdfTravelStable=0;
    }
    if(riding) {
        if(!m_RdfTravelArmed) {
            m_RdfTravelArmed=true;m_RdfTravelMoved=false;
            m_RdfTravelLeader=leader->GetGUID();m_RdfTravelMap=me->GetMapId();m_RdfTravelInstance=me->GetInstanceId();
            m_RdfTravelOrigin=leader->GetPosition();
        }
        float dx=leader->GetPositionX()-m_RdfTravelOrigin.GetPositionX();
        float dy=leader->GetPositionY()-m_RdfTravelOrigin.GetPositionY();
        float dz=leader->GetPositionZ()-m_RdfTravelOrigin.GetPositionZ();
        if(dx*dx+dy*dy+dz*dz>=16.0f)m_RdfTravelMoved=true;
        m_RdfTravelLastRide=now;m_RdfTravelStable=0;
        if(!fighting) {m_Movement->ClearMovement();return true;}
        return false; // Do not suppress combat while a passenger is fighting.
    }
    if(!m_RdfTravelArmed)return false;
    // Include the final segment when disembarking between two AI samples.
    { float x=leader->GetPositionX()-m_RdfTravelOrigin.GetPositionX();
      float y=leader->GetPositionY()-m_RdfTravelOrigin.GetPositionY();
      float z=leader->GetPositionZ()-m_RdfTravelOrigin.GetPositionZ();
      if(x*x+y*y+z*z>=16.0f)m_RdfTravelMoved=true; }

    if(!m_RdfTravelMoved || RdfRotationPolicy::elapsed(now,m_RdfTravelLastRide,60000)) {
        m_RdfTravelArmed=false;m_RdfTravelStable=0;return false;
    }
    // A bot already on the landing floor needs ordinary following, not regroup.
    if(!me->GetTransport() && !me->GetVehicleBase() && !me->IsBeingTeleported() &&
        !me->IsChangeMap() && me->GetDistance(leader)<=8.0f &&
        std::fabs(me->GetPositionZ()-leader->GetPositionZ())<=2.0f) {
        m_RdfTravelArmed=false;m_RdfTravelStable=0;return false;
    }
    // No regroup during combat, a transfer, a jump/fall, or while still walking.
    bool unsettled=fighting || leader->IsBeingTeleported() || leader->IsChangeMap() ||
        leader->isMoving() || leader->IsFlying() ||
        leader->HasUnitMovementFlag(MOVEMENTFLAG_FALLING | MOVEMENTFLAG_FALLING_FAR);
    float ground=leader->GetHeight(leader->GetPositionX(),leader->GetPositionY(),leader->GetPositionZ()+2.0f,true,10.0f);
    if(!std::isfinite(ground) || std::fabs(ground-leader->GetPositionZ())>2.0f)unsettled=true;
    if(unsettled) {
        m_RdfTravelStable=0;
        // An unsafe teleport landing does not imply an unsafe mmap walk.
        // Release control to the normal following/combat movement after disembarking.
        return false;
    }
    float dx=leader->GetPositionX()-m_RdfTravelLanding.GetPositionX();
    float dy=leader->GetPositionY()-m_RdfTravelLanding.GetPositionY();
    float dz=leader->GetPositionZ()-m_RdfTravelLanding.GetPositionZ();
    if(!m_RdfTravelStable || dx*dx+dy*dy+dz*dz>0.09f) {
        m_RdfTravelLanding=leader->GetPosition();m_RdfTravelStable=now;
        return false;
    }
    if(!RdfRotationPolicy::elapsed(now,m_RdfTravelStable,2500))return false;
    if(me->IsBeingTeleported() || me->IsChangeMap() || me->GetTransport() || me->GetVehicleBase())return true;
    if(m_RdfTravelAttempt && !RdfRotationPolicy::elapsed(now,m_RdfTravelAttempt,3000))return false;
    m_RdfTravelAttempt=now;
    unsigned slot=0;
    for(GroupReference* r=me->GetGroup()->GetFirstMember();r;r=r->next())
        if(Player* p=r->getSource())
            if(p->GetSession() && p->GetSession()->IsBotSession() && p->GetGUID()<me->GetGUID())++slot;
    for(unsigned i=0;i<8;++i) {
        Position dest;
        leader->GetFirstCollisionPosition(dest,2.0f,float((slot+i)%8)*6.28318530718f/8.0f);
        if(std::fabs(dest.GetPositionZ()-leader->GetPositionZ())>1.5f)continue;
        float floor=leader->GetHeight(dest.GetPositionX(),dest.GetPositionY(),dest.GetPositionZ()+2.0f,true,10.0f);
        if(!std::isfinite(floor) || std::fabs(floor-dest.GetPositionZ())>1.5f)continue;
        // Verify a short walk from the player's landing surface to the chosen nearby spot.
        PathGenerator path(leader);
        if(!path.CalculatePath(dest.GetPositionX(),dest.GetPositionY(),dest.GetPositionZ(),false,false,false) || path.GetPathType()!=PATHFIND_NORMAL)continue;
        auto const& pts=path.GetPath();if(pts.empty())continue;
        float ex=pts.back().x-dest.GetPositionX(),ey=pts.back().y-dest.GetPositionY();
        if(ex*ex+ey*ey>1.0f || std::fabs(pts.back().z-dest.GetPositionZ())>1.5f)continue;
        me->InterruptSpell(CURRENT_GENERIC_SPELL,false);
        me->InterruptSpell(CURRENT_CHANNELED_SPELL,false);
        m_Movement->ClearMovement();m_CruxMovement.ClearMovement();
        // Headless players need the existing teleport state machine's acknowledgement.
        dest.SetOrientation(leader->GetOrientation());
        m_Teleporting.SetTeleport(me->GetMapId(),dest);
        if(m_Teleporting.CanMovement())continue;
        m_RdfTravelArmed=false;m_RdfTravelStable=0;
        m_RdfFormationMoving=false;m_RdfFormationTick=0;m_RdfAvoidMoving=false;m_RdfPositionTick=0;
        TC_LOG_INFO(LOG_FILTER_LFG,"RDF_TRAVEL_REGROUP account=%u map=%u instance=%u",me->GetSession()->GetAccountId(),me->GetMapId(),me->GetInstanceId());
        return true;
    }
    TC_LOG_INFO(LOG_FILTER_LFG,"RDF_TRAVEL_REGROUP_BLOCKED account=%u reason=no_safe_landing",me->GetSession()->GetAccountId());
    return false; // Keep ordinary mmap following available while landing recovery retries.
}
