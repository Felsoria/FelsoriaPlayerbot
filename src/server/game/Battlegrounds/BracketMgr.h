#ifndef _BRACKETMGR_H
#define _BRACKETMGR_H

#include "Bracket.h"
#include "Player.h"
#include <mutex> // FELSORIA_P22_BRACKET_MUTEX

class BracketMgr
{
    BracketMgr();
    ~BracketMgr();

public:
    static BracketMgr* instance();

    void LoadCharacterBrackets();

    Bracket* TryGetOrCreateBracket(ObjectGuid guid, uint8 bType);
    void DeleteBracketInfo(ObjectGuid guid);
    void ResetWeekly();

private:
    std::mutex _containerMutex; // FELSORIA_P22_BRACKET_MUTEX
    std::map<ObjectGuid, BracketList> _container;
};

#define sBracketMgr BracketMgr::instance()

#endif
