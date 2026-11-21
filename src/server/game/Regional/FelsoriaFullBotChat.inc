// Included by RegionalBotChatManager.cpp so the existing world update owns delivery.
namespace FelsoriaRegional
{
namespace
{
constexpr uint8 TextGroup = 8;
constexpr uint8 TextTrade = 6;
constexpr uint8 TextGuild = 7;
constexpr uint8 TextLfg = 9;

LocaleConstant TextLocale(uint8 region)
{
    LocaleConstant const locales[] = {LOCALE_enUS, LOCALE_deDE, LOCALE_frFR, LOCALE_esES, LOCALE_ruRU, LOCALE_itIT};
    return locales[region];
}

std::string PlainText(std::string value)
{
    for (char& c : value)
        if (c == '|' || c == '[' || c == ']' || static_cast<unsigned char>(c) < 32)
            c = ' ';
    return value;
}

std::string LocalText(LocalizedString const* value, LocaleConstant locale)
{
    if (!value)
        return {};
    char const* text = value->Str[locale];
    if (!text || !*text)
        text = value->Str[LOCALE_enUS];
    return text ? PlainText(text) : std::string();
}

std::string ItemText(uint32 id, LocaleConstant locale)
{
    auto item = sObjectMgr->GetItemTemplate(id);
    if (!item || !item->GetName())
        return {};
    static char const* const colors[] = {"ff9d9d9d","ffffffff","ff1eff00","ff0070dd","ffa335ee","ffff8000","ffe6cc80","ff00ccff"};
    std::string name = LocalText(item->GetName(), locale);
    if (name.empty())
        return {};
    return std::string("|c") + colors[std::min<uint32>(item->GetQuality(), 7)] + "|Hitem:" + std::to_string(id) +
        ":0:0:0:0:0:0:0:0:0:0:0:0|h[" + name + "]|h|r";
}

std::string QuestText(Player* bot, uint32 id, LocaleConstant locale)
{
    Quest const* quest = sQuestDataStore->GetQuestTemplate(id);
    if (!quest)
        return {};
    std::string title = quest->LogTitle;
    if (auto localized = sQuestDataStore->GetQuestLocale(id))
        ObjectMgr::GetLocaleString(localized->LogTitle, locale, title);
    return "|cffffff00|Hquest:" + std::to_string(id) + ":" + std::to_string(bot->GetQuestLevel(quest)) +
        "|h[" + PlainText(title) + "]|h|r";
}

Channel* FindPublicChannel(Player* player, uint8 type)
{
    ChannelMgr* manager = channelMgr(player->GetTeam());
    if (!manager)
        return nullptr;
    for (ChatChannelsEntry const* row : sChatChannelsStore)
    {
        if (!row)
            continue;
        bool lfg = (row->Flags & CHANNEL_DBC_FLAG_LFG) != 0;
        bool trade = (row->Flags & CHANNEL_DBC_FLAG_TRADE) != 0 && !lfg &&
            !(row->Flags & CHANNEL_DBC_FLAG_GUILD_REQ);
        if ((type == TextTrade && !trade) || (type == TextLfg && !lfg))
            continue;
        auto area=sAreaTableStore.LookupEntry(player->GetZoneId());
        if (!area || !player->CanJoinConstantChannelInZone(row,area))
            continue;
        auto name = NativeGeneralName(player, row);
        if (name.empty())
            continue;
        Channel* channel = manager->GetChannel(name, player, false);
        if (channel && channel->IsOn(player->GetGUID()))
            return channel;
    }
    return nullptr;
}

uint8 GroupChannel(Player* player)
{
    return player->InBattleground() ? 3 : player->GetMap()->IsDungeon() ? 4 : 2;
}

uint32 LinkedId(std::string const& input, char const* type)
{
    std::string marker = std::string("|H") + type + ":";
    auto start = input.find(marker);
    if (start == std::string::npos)
        return 0;
    start += marker.size();
    uint32 id = 0;
    for (; start < input.size(); ++start)
    {
        char c = input[start];
        if (c < '0' || c > '9')
            return c == ':' || c == '|' ? id : 0;
        if (id > 40000000)
            return 0;
        id = id * 10 + uint32(c - '0');
    }
    return 0;
}
}

void RegionalBotChatManager::LoadFullTexts()
{
    if (!sConfigMgr->GetBoolDefault("Felsoria.BotChat.FullTexts.Enable", true))
        return;
    std::string path = sConfigMgr->GetStringDefault("Felsoria.BotChat.FullTexts.File", sWorld->GetDataPath() + "Felsoria/BotTexts-V3.tsv");
    std::ifstream file(path);
    bool loaded = file && _fullCatalog.Load(file, 1511, [](std::string const& text)
    {
        std::wstring wide;
        return Utf8toWStr(text, wide);
    });
    _fullReady.store(loaded);
    if (loaded)
        TC_LOG_INFO(LOG_FILTER_GUILD, "FELSORIA_BOT_TEXTS_V3 loaded texts=1511 languages=6");
    else
        TC_LOG_ERROR(LOG_FILTER_GUILD, "FELSORIA_BOT_TEXTS_V3 unavailable: %s", path.c_str());
}

void RegionalBotChatManager::FullEvent(Player* bot, char const* key, uint8 channel, FullTextFacts facts, Player* recipient)
{
    if (!_fullReady.load() || !Active() || !bot || !bot->IsInWorld() || !OwnsAutomaticChat(bot) || (!_fullCatalog.Find(key) && std::string(key)!="$bank_deposit"))
        return;
    if (recipient)
    {
        if (!recipient->IsInWorld() || recipient->GetSession()->IsBotSession())
            return;
        facts.recipient = recipient->GetGUID().GetCounter();
    }
    std::lock_guard<std::mutex> guard(_fullQueueMutex);
    if (_fullQueue.size() < 64 || _fullQueue.count(bot->GetGUID().GetCounter()))
    {
        auto& queue = _fullQueue[bot->GetGUID().GetCounter()];
        if (queue.size() < 16)
            queue.push_back({key, channel, std::move(facts), Clock(), bot->GetMapId(), bot->GetInstanceId()});
    }
}

void RegionalBotChatManager::FullSignal(Player* player, char const* key, uint8 channel, FullTextFacts facts)
{
    if (!_fullReady.load() || !Active() || !player || !player->IsInWorld())
        return;
    if (OwnsAutomaticChat(player))
    {
        FullEvent(player, key, channel, std::move(facts));
        return;
    }
    if (Group* group = player->GetGroup())
    {
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* bot = ref->getSource();
            if (bot && bot->IsInWorld() && bot->GetMap() == player->GetMap() && bot->InSamePhase(player) && OwnsAutomaticChat(bot))
            {
                FullEvent(bot, key, channel, std::move(facts));
                return;
            }
        }
    }
}

std::string RegionalBotChatManager::FullFragment(char const* key, uint8 locale, FullText::Tokens const& facts, std::size_t seed) const
{
    if (auto pool = _fullCatalog.Find(key))
    {
        std::size_t next = 0;
        return FullText::Catalog::Pick(*pool, locale, seed, facts, next);
    }
    return {};
}

bool RegionalBotChatManager::FullDeliver(Player* bot, FullTextNotice const& note)
{
    bool bankDeposit = note.key=="$bank_deposit";
    auto pool = _fullCatalog.Find(bankDeposit ? "gbank_put" : note.key);
    if (!pool || bot->GetMapId() != note.map || bot->GetInstanceId() != note.instance)
        return false;
    uint8 route = note.channel == TextGroup ? GroupChannel(bot) : note.channel;
    uint8 baseRoute = (route == TextTrade || route == TextLfg || route == TextGuild) ? 0 : route;
    if (!Eligible(bot, baseRoute) || !ChannelEnabled(baseRoute))
        return false;
    if ((route == 0 || route == TextTrade || route == TextLfg) && bot->isInCombat())
        return false;

    std::lock_guard<std::mutex> guard(_mutex);
    uint64 now = Clock();
    auto botState = _actors.find(bot->GetGUID().GetCounter());
    if (botState == _actors.end())
        return false;
    uint32 botRealm = sRegionalGuildMgr.RealmFor(bot);
    if (note.facts.guild && note.facts.guild != bot->GetGuildId())
        return false;
    if (note.facts.matchQuest && bot->GetQuestStatus(note.facts.quest) != QUEST_STATUS_INCOMPLETE)
        return false;
    if (note.facts.reciprocal && (bot->isInCombat() ||
        (note.facts.matchQuest && bot->GetGroup() && bot->GetGroup()->IsFull())))
        return false;
    if (note.facts.tradeItem)
    {
        Item* item = BotUtility::FindItemFromAllBag(bot, note.facts.item);
        if (!item || !item->CanBeTraded(false, true))
            return false;
    }
    std::set<std::string> seen;
    std::map<std::string,std::string> guildMessages;
    bool anyDelivered = false;
    for (auto const& entry : _actors)
    {
        Actor const& audience = entry.second;
        if (audience.bot || !audience.ready || audience.locale >= 6 || now - audience.seen >= 15000 ||
            (note.facts.recipient && note.facts.recipient != audience.guid))
            continue;
        auto human = ObjectAccessor::FindPlayer(ObjectGuid::Create<HighGuid::Player>(audience.guid));
        if (!human || !human->IsInWorld() || human->GetMap()!=bot->GetMap() || human->GetSession()->IsBotSession() || !human->CanContact() ||
            human->GetSocial()->HasIgnore(bot->GetGUID()))
            continue;
        bool guild = route == TextGuild;
        bool trade = route == TextTrade || route == TextLfg;
        if (guild)
        {
            if (!bot->GetGuildId() || bot->GetGuildId() != human->GetGuildId() || audience.realm != botRealm)
                continue;
        }
        else
        {
            DeliveryScope scope(human);
            if (!SameAudience(bot, human, baseRoute) || !bot->InSamePhase(human))
                continue;
            if (baseRoute == 0 && !trade && !human->GetRegionalGeneralChannel())
                continue;
        }
        auto channel = trade ? FindPublicChannel(human, route) : nullptr;
        if (trade && !channel)
            continue;
        if (note.facts.questShared && human->GetQuestStatus(note.facts.quest) == QUEST_STATUS_NONE &&
            !bot->CanShareQuest(note.facts.quest))
            continue;
        std::string area = guild ? "guild:" + std::to_string(bot->GetGuildId()) + ":" + std::to_string(audience.locale)
            : AudienceKey(audience.realm, audience.zone, audience.map, audience.instance, audience.faction, audience.bgTeam,
                          audience.locale, baseRoute, audience.group) + ":" + std::to_string(route);
        if (route == 5)
            area += ":" + std::to_string(audience.guid);
        if (!guild && !trade && !seen.insert(area).second)
            continue;
        if (guild)
        {
            auto previous = guildMessages.find(area);
            if (previous != guildMessages.end())
            {
                WorldPackets::Chat::Chat packet;
                packet.Initialize(CHAT_MSG_GUILD, LANG_UNIVERSAL, bot, nullptr, previous->second);
                human->SendDirectMessage(packet.Write());
                continue;
            }
        }
        std::string limit = "full:" + area;
        std::string topic = limit + ":" + note.key;
        if (_cooldowns[limit] > now || _cooldowns[topic] > now ||
            (!guild && !trade && _cooldowns["line-budget:" + AudienceKey(audience.realm, audience.zone, audience.map,
                audience.instance, audience.faction, audience.bgTeam, audience.locale, baseRoute, audience.group)] > now + 5000))
            continue;

        LocaleConstant locale = TextLocale(audience.locale);
        FullText::Tokens tokens;
        auto put = [&](char const* token, std::string value) { if (!value.empty()) tokens[token] = std::move(value); };
        auto zone = sAreaTableStore.LookupEntry(bot->GetZoneId());
        put("%zone_name", zone ? LocalText(zone->ZoneName, locale) : std::string());
        if (!tokens.count("%zone_name"))
            put("%zone_name", FullFragment("string_unknown_area", audience.locale, tokens, std::size_t(now / 1000)));
        put("%my_level", std::to_string(note.facts.level ? note.facts.level : bot->getLevel()));
        if (auto cls = sChrClassesStore.LookupEntry(bot->getClass()))
            put("%my_class", LocalText(cls->Name, locale));
        if (auto race = sChrRacesStore.LookupEntry(bot->getRace()))
            put("%my_race", LocalText(race->Name, locale));
        char const* const roles[][3] = {{"tank","healer","damage dealer"},{"Tank","Heiler","DD"},
            {"tank","soigneur","DPS"},{"tanque","sanador","DPS"},{"танк","целитель","боец"},{"tank","guaritore","DPS"}};
        uint32 role = bot->GetSpecializationRole();
        put("%my_role", roles[audience.locale][role == ROLES_TANK ? 0 : role == ROLES_HEALER ? 1 : 2]);
        std::string name = note.facts.name.empty() ? PlainText(human->GetName()) : PlainText(note.facts.name);
        put("%s", name);
        put("%player", name);
        put("%other_name", name);
        put("%victim_name", PlainText(note.facts.name));
        put("%unit", PlainText(note.facts.name));
        put("%gameobject", PlainText(note.facts.name));
        put("<target>", PlainText(note.facts.name));
        if (note.facts.item)
        {
            std::string item = ItemText(note.facts.item, locale);
            if (note.facts.targetIsItem)
                put("<target>",item);
            if (item.empty() && note.facts.allowUnknownItem)
                item = std::to_string(note.facts.item);
            for (auto token : {"%item","%item_link","%item_formatted_link","%formatted_item_links"})
                put(token, item);
            if (auto proto = sObjectMgr->GetItemTemplate(note.facts.item))
            {
                put("%category", LocalText(proto->GetName(), locale));
                put("%cost_gold", std::to_string(std::max<uint32>(1u, proto->GetSellPrice() / 10000)));
            }
        }
        if (note.facts.emptyTarget || (note.key=="use_command" && !tokens.count("<target>")))
            put("<target>", FullFragment("string_empty_link", audience.locale, tokens));
        put("%gem", ItemText(note.facts.extraItem, locale));
        std::string quest = QuestText(bot, note.facts.quest, locale);
        put("%quest", quest);
        put("%quest_link", quest);
        put("%quest_links", quest);
        if (auto spell = sSpellMgr->GetSpellInfo(note.facts.spell))
        {
            auto record = sSpellStore.LookupEntry(note.facts.spell);
            std::string display = record ? LocalText(record->Name, locale) : std::string();
            if (display.empty() && spell->SpellName)
                display = PlainText(spell->SpellName);
            put("%spell", display);
            put("%spells", display);
        }
        else if (note.facts.spell)
            put("%spell", std::to_string(note.facts.spell));
        if (auto dungeon = sLfgDungeonsStore.LookupEntry(note.facts.dungeon))
            put("%instance_name", LocalText(dungeon->Name, locale));
        if (auto faction = sFactionStore.LookupEntry(note.facts.faction))
        {
            put("%faction", LocalText(faction->Name, locale));
            auto rank = bot->GetReputationRank(note.facts.faction);
            if (auto rankText=sObjectMgr->GetTrinityString(ReputationRankStrIndex[std::min<int>(int(rank) + 1, 7)], locale))
                put("%rep_level",rankText);
        }
        if (note.key == "thunderfury_spam")
            put("%thunderfury_link", ItemText(19019, locale));
        if (note.facts.available >= 0)
            put("%amount", std::to_string(note.facts.available));
        if (note.facts.required > 0)
        {
            std::string objective = PlainText(note.facts.objective);
            if (note.facts.item)
                objective = LocalText(sObjectMgr->GetItemTemplate(note.facts.item) ? sObjectMgr->GetItemTemplate(note.facts.item)->GetName() : nullptr,locale);
            put("%quest_obj_name", objective);
            put("%quest_obj_available", std::to_string(note.facts.available));
            put("%quest_obj_required", std::to_string(note.facts.required));
            put("%quest_obj_missing", std::to_string(std::max(0, note.facts.required - note.facts.available)));
            put("%quest_obj_full_formatted", std::to_string(note.facts.available) + "/" +
                std::to_string(note.facts.required) + " " + objective);
        }
        if (note.facts.rewards)
            if (Quest const* questTemplate = sQuestDataStore->GetQuestTemplate(note.facts.quest))
            {
                std::string rewards;
                for (uint32 item : questTemplate->RewardChoiceItemId)
                {
                    auto link = ItemText(item, locale);
                    if (!link.empty() && rewards.size() + link.size() < 120)
                        rewards += (rewards.empty() ? "" : ", ") + link;
                }
                put("%rewards", rewards);
            }
        std::size_t next = 0;
        auto& cursor = _fullCursors[{note.key, audience.locale}];
        std::string selectedId;
        std::string message = FullText::Catalog::Pick(*pool, audience.locale, cursor, tokens, next, &selectedId);
        if (message.empty())
            continue;
        if (bankDeposit)
            message = tokens["%item"] + message;
        if (note.key=="error_cant_put")
            message += tokens["%item"];
        if (message.empty() || message.size()>255)
            continue;
        for (auto const& fragment : note.facts.fragments)
        {
            std::string addition = FullFragment(fragment.c_str(), audience.locale, tokens);
            if (addition.empty() || message.size() + addition.size() > 255)
                continue;
            message += addition;
        }

        bool delivered = false;
        if (guild)
        {
            WorldPackets::Chat::Chat packet;
            packet.Initialize(CHAT_MSG_GUILD, LANG_UNIVERSAL, bot, nullptr, message);
            human->SendDirectMessage(packet.Write());
            guildMessages[area] = message;
            delivered = true;
        }
        else if (trade)
        {
            DeliveryScope scope(human);
            bool joined = !channel->IsOn(bot->GetGUID());
            if (joined)
                channel->JoinChannel(bot, "");
            if (channel->IsOn(bot->GetGUID()) && !(channel->GetPlayerFlags(bot->GetGUID()) & MEMBER_FLAG_MUTED))
            {
                channel->Say(bot->GetGUID(), message, bot->GetTeam() == HORDE ? LANG_ORCISH : LANG_COMMON);
                delivered = true;
            }
            if (joined && channel->IsOn(bot->GetGUID()))
                channel->LeaveChannel(bot, false);
        }
        else
        {
            Dialogue dialogue;
            dialogue.channel = route;
            dialogue.locale = audience.locale;
            Phrase phrase;
            phrase.locale = audience.locale;
            phrase.text = message;
            delivered = Send(bot, dialogue, phrase, audience, route == 5 ? audience.guid : 0);
        }
        if (delivered)
        {
            anyDelivered = true;
            if (sConfigMgr->GetBoolDefault("Felsoria.BotChat.Debug",false))
                TC_LOG_INFO(LOG_FILTER_GUILD,"FELSORIA_BOT_TEXTS_V3 sent id=%s pool=%s locale=%u channel=%u",
                    selectedId.c_str(),note.key.c_str(),uint32(audience.locale),uint32(route));
            cursor = next;
            uint64 spacing = route == 0 || trade ? 26000 : route == 5 ? 1500 : 7500;
            _cooldowns[limit] = now + spacing;
            if (route<5)
            {
                auto nativeKey=AudienceKey(audience.realm,audience.zone,audience.map,audience.instance,
                    audience.faction,audience.bgTeam,audience.locale,route,audience.group);
                _cooldowns[nativeKey]=std::max(_cooldowns[nativeKey],now+spacing);
                _cooldowns["line-budget:"+nativeKey]=std::max(_cooldowns["line-budget:"+nativeKey],now+5000);
            }
            _cooldowns[topic] = now + note.facts.cooldown * 1000ull;
            if (trade)
                break;
        }
    }
    return anyDelivered;
}
}

namespace FelsoriaRegional
{
namespace
{
Item* TradableItem(Player* bot, uint32 requested = 0)
{
    auto eligible = [requested](Item* item)
    {
        return item && (!requested || item->GetEntry() == requested) && item->CanBeTraded(false, true) &&
            item->GetTemplate()->GetClass() == ITEM_CLASS_TRADE_GOODS;
    };
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
        if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            if (eligible(item))
                return item;
    for (uint8 slot = INVENTORY_SLOT_BAG_START; slot < INVENTORY_SLOT_BAG_END; ++slot)
        if (Bag* bag = bot->GetBagByPos(slot))
            for (uint32 pos = 0; pos < bag->GetBagSize(); ++pos)
                if (Item* item = bag->GetItemByPos(uint8(pos)))
                    if (eligible(item))
                        return item;
    return nullptr;
}

Player* FullLeader(Player* bot)
{
    Group* group = bot->GetGroup();
    if (!group)
        return nullptr;
    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        if (Player* player = ref->getSource())
            if (group->IsLeader(player->GetGUID()) && player->IsInWorld() &&
                !player->GetSession()->IsBotSession() && player->GetMap() == bot->GetMap() && player->InSamePhase(bot))
                return player;
    return nullptr;
}
}

void RegionalBotChatManager::FullHear(Player* human, std::string const& message, uint8 route, uint64 asked)
{
    if (!_fullReady.load() || !Active() || !human || !human->GetSession() || human->GetSession()->IsBotSession() ||
        !human->IsInWorld() || message.empty() || message.size() > 512 || human->InBattleground())
        return;
    std::string lowered = message;
    for (char& c : lowered)
        if (c >= 'A' && c <= 'Z')
            c = char(c + 'a' - 'A');
    std::string word = lowered.substr(0, lowered.find_first_of(" ,.!?:"));
    bool lfg = word == "lfg" || word == "lfm" || word == "recherche" || word == "busco" || word == "suche" || word == "cerco" || word == "ищу";
    bool buy = word == "wtb" || word == "kaufe" || word == "compro" || word == "achete" || word == "куплю";
    FullTextFacts facts;
    facts.reciprocal = true;
    facts.recipient = human->GetGUID().GetCounter();
    facts.name = human->GetName();
    facts.quest = LinkedId(message, "quest");
    facts.item = LinkedId(message, "item");
    char const* key = nullptr;
    uint8 response = route;
    if (lfg && facts.quest && (route == 0 || route == TextLfg || route == 5))
    {
        key = route == 5 ? "response_lfg_quests_whisper" : "response_lfg_quests_channel";
        facts.matchQuest = true;
    }
    else if (buy && facts.item && (route == TextTrade || route == 5))
    {
        key = route == 5 ? "response_wtb_items_whisper" : "response_wtb_items_channel";
        facts.tradeItem = true;
    }
    else if ((route == 0 || route == 5) && LinkedId(message,"item")==19019)
        key = "thunderfury_spam";
    else if (route == 5 && asked)
    {
        if (lowered.compare(0, 10, "bot quest ") == 0 && facts.quest)
            key = "$quest_status";
        else if (lowered == "bot status")
            key = "$movement_status";
        else if (lowered == "bot invite")
            key = "$invite_status";
        else if (lowered.compare(0, 4, "bot ") == 0)
        {
            key = "$control";
            facts.objective = message.substr(4);
        }
        else if (word == "hi" || word == "hello" || word == "hallo" || word == "salut" || word == "hola" || word == "ciao" || word == "привет")
            key = "reply_greeting";
        else if (lowered=="brb" || lowered=="break" || lowered=="pause")
            key = "reply_break";
        else if (word=="bags" || word=="bag")
            key = "reply_check_bags";
        else if (word=="adventure" || word=="plans")
            key = "reply_social";
        else if (word == "again" || word == "encore" || word == "nochmal" || word == "ancora")
            key = "reply_retry";
        else
            key = "reply_addressed";
    }
    else if ((route == 0 || route == 2 || route == 4) && (word == "hi" || word == "hello" ||
         word == "hallo" || word == "hola" || word == "salut" || word == "ciao" || word == "привет"))
        key = "reply_greeting";
    else if ((route == 0 || route == 2 || route == 4) && (word == "yes" || word == "ja" || word == "oui" ||
         word == "ok" || word == "да" || word == "sí" || word == "sì"))
        key = "reply_agree";
    if (!key)
        return;

    std::vector<uint64> speakers;
    uint64 now = Clock();
    {
        std::lock_guard<std::mutex> lock(_mutex);
        std::string rate = "full-hear:" + std::to_string(facts.recipient);
        if (_cooldowns[rate] > now)
            return;
        auto observer = _actors.find(facts.recipient);
        if (observer == _actors.end())
            return;
        for (auto const& row : _actors)
        {
            Actor const& bot = row.second;
            if (bot.bot && bot.ready && bot.alive && now - bot.seen < 15000 && (!asked || bot.guid == asked) &&
                Same(bot, observer->second, route >= 6 ? 0 : route))
                speakers.push_back(bot.guid);
        }
        if (speakers.empty())
            return;
        _cooldowns[rate] = now + 6000;
    }
    std::size_t offset = urand(0, uint32(speakers.size() - 1));
    std::lock_guard<std::mutex> queueLock(_fullQueueMutex);
    for (std::size_t n = 0; n < std::min<std::size_t>(speakers.size(), 4); ++n)
    {
        auto& queue = _fullQueue[speakers[(offset + n) % speakers.size()]];
        if (queue.size() < 16)
            queue.push_back({key, response, facts, now, human->GetMapId(), human->GetInstanceId()});
    }
}

void RegionalBotChatManager::FullTick(Player* bot)
{
    if (!_fullReady.load() || !Active() || !bot || !bot->IsInWorld() || !OwnsAutomaticChat(bot) ||
        bot->IsBeingTeleportedFar() || bot->GetSession()->PlayerLoading())
        return;

    auto guid = bot->GetGUID().GetCounter();
    uint64 now = Clock();
    std::vector<FullTextNotice> queued;
    {
        std::lock_guard<std::mutex> lock(_fullQueueMutex);
        auto found = _fullQueue.find(guid);
        if (found != _fullQueue.end())
        {
            queued = std::move(found->second);
            _fullQueue.erase(found);
        }
    }
    for (auto& note : queued)
    {
        unsigned ttl = note.key == "felsoria_instance_interrupt" || note.key == "felsoria_instance_ground_danger" ? 1300 : 9000;
        if (now - note.created >= ttl || note.map != bot->GetMapId() || note.instance != bot->GetInstanceId())
            continue;
        if (note.key == "$control")
        {
            Player* leader=FullLeader(bot);
            auto ai=dynamic_cast<BotGroupAI*>(bot->GetAI());
            if (leader && ai && leader->GetGUID().GetCounter()==note.facts.recipient)
                ai->FullExecuteTextOrder(leader,note.facts.objective);
            continue;
        }
        if (note.key == "$quest_status")
        {
            Quest const* quest = sQuestDataStore->GetQuestTemplate(note.facts.quest);
            if (!quest)
                continue;
            switch (bot->GetQuestStatus(note.facts.quest))
            {
                case QUEST_STATUS_COMPLETE:
                    if (!bot->CanRewardQuest(quest, false))
                        note.key = "quest_status_unable_to_complete";
                    else if (quest->RewardChoiceItemId[0] && quest->RewardChoiceItemId[1])
                    {
                        note.key = "quest_status_complete_pick_reward";
                        note.facts.rewards = true;
                    }
                    else
                        note.key = "quest_status_completed";
                    break;
                case QUEST_STATUS_REWARDED: note.key = urand(0,1) ? "quest_status_completed" : "quest_error_completed"; break;
                case QUEST_STATUS_FAILED: note.key = "quest_status_failed"; break;
                case QUEST_STATUS_INCOMPLETE: note.key = "quest_status_incomplete"; break;
                default:
                    if (!bot->CanTakeQuest(quest,false))
                        note.key="quest_error_cant_take";
                    else if (bot->FindQuestSlot(0)>=MAX_QUEST_LOG_SIZE)
                        note.key="quest_error_log_full";
                    else
                        note.key="quest_status_available";
                    break;
            }
        }
        else if (note.key == "$movement_status")
        {
            if (auto ai = dynamic_cast<BotGroupAI*>(bot->GetAI()))
                note.key = ai->FullMovementText();
            else if (bot->getVictim() && !bot->getVictim()->IsPlayer())
                note.key = "grinding";
            else
                note.key = "free_moving";
        }
        else if (note.key == "$invite_status")
        {
            Group* group = bot->GetGroup();
            if (!group || !group->IsLeader(bot->GetGUID()) || group->IsFull())
                continue;
            note.key = group->isRaidGroup() ? "join_raid" : "join_group";
        }
        FullDeliver(bot, note);
    }

    FullTextState state;
    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto found = _actors.find(guid);
        if (found == _actors.end())
            return;
        if (found->second.full.next > now)
            return;
        state = found->second.full;
        found->second.full.next = now + 850 + (guid % 400);
    }
    auto call = [&](char const* key, uint8 channel, FullTextFacts facts = FullTextFacts())
    {
        return FullDeliver(bot, {key, channel, std::move(facts), now, bot->GetMapId(), bot->GetInstanceId()});
    };
    bool bg = bot->GetBattleground() && bot->InBattleground() && !bot->InArena();
    bool dungeon = bot->GetMap()->IsDungeon();
    Group* group = bot->GetGroup();
    uint64 groupId = group ? group->GetGUID().GetCounter() : 0;
    bool combat = bot->isInCombat();
    bool started = bg && bot->GetBattleground()->GetStatus() == STATUS_IN_PROGRESS;
    uint32 instance = bot->GetInstanceId();
    if (bg && instance != state.instance)
    {
        state.bases=0;
        FullTextFacts facts;
        facts.cooldown = 3600;
        call("felsoria_bg_start", 3, facts);
    }
    if (dungeon && instance != state.instance && group)
    {
        FullTextFacts facts;
        facts.cooldown = 900;
        call("felsoria_instance_enter", 4, facts);
    }
    if (groupId && groupId != state.group && !bg && !dungeon)
    {
        if (!call("felsoria_party_join", 2))
            call("hello", 2);
    }
    state.instance = instance;
    state.group = groupId;
    if (bot->isAlive() && (group || bg))
    {
        unsigned hp = bot->GetHealthPct() <= 20 ? 2 : bot->GetHealthPct() <= 40 ? 1 : 0;
        if (hp > state.health && call(hp == 2 ? "critical health" : "low health", TextGroup))
            state.health = hp;
        else if (bot->GetHealthPct() > 55)
            state.health = 0;
        else if (bot->GetHealthPct() > 30 && state.health == 2)
            state.health = 1;
        if (bot->getPowerType() == POWER_MANA && bot->GetMaxPower(POWER_MANA))
        {
            float mana = bot->GetPowerPct(POWER_MANA);
            if (mana > 35)
                state.mana = false;
            if (mana <= 20 && !state.mana)
                state.mana = call(!combat && dungeon && bot->GetSpecializationRole() == ROLES_HEALER ?
                    "felsoria_instance_mana" : "low mana", TextGroup);
        }
        if (combat && bot->getAttackers()->size() >= 3)
        {
            char const* keys[] = {"aoe", "aoe", "felsoria_instance_pull_2", "felsoria_instance_pull_4"};
            call(keys[dungeon ? (now / 1000) % 4 : 0], TextGroup);
        }
        if (Unit* target = bot->getVictim())
        {
            if (combat && bot->IsValidAttackTarget(target))
            {
                FullTextFacts facts;
                facts.name = target->GetName();
                facts.cooldown = 180;
                call("taunt", TextGroup, facts);
                auto creature=target->ToCreature();
                bool mayInterrupt=!creature || !(creature->GetCreatureTemplate()->MechanicImmuneMask & (1u<<(MECHANIC_INTERRUPT-1)));
                if (dungeon && mayInterrupt)
                    if (Spell* spell = target->GetCurrentSpell(CURRENT_GENERIC_SPELL))
                        if (spell->getState() != SPELL_STATE_FINISHED && spell->IsInterruptable() &&
                            spell->GetFelsoriaRemainingCastTime() > 700 && spell->GetSpellInfo()->HasEffect(SPELL_EFFECT_SCHOOL_DAMAGE) &&
                            !spell->GetSpellInfo()->IsPositive() &&
                            (spell->GetSpellInfo()->InterruptFlags & SPELL_INTERRUPT_FLAG_INTERRUPT))
                            call("felsoria_instance_interrupt", 4);
            }
        }
    }

    if (started)
        FullBattleground(bot, state);

    if (!combat && bot->isAlive() && !bg && !bot->InArena() && now >= state.ambient)
    {
        state.ambient = now + 10000 + guid % 7000;
        if (!dungeon)
            FullAmbient(bot, now);
        if (Player* lead = FullLeader(bot))
        {
            if (bot->GetDistance(lead) > 24.0f && !lead->isInCombat())
            {
                std::string key = bot->GetDistance(lead) > 80 ? "wait_travel_far" : "wait_travel_medium";
                if (bot->isMoving())
                    call(key.c_str(), TextGroup);
                else if (!dungeon)
                    call("felsoria_party_regroup", 2);
            }
            else if (bot->isMoving() && bot->GetDistance(lead) > 8.0f)
                call("wait_travel_close", TextGroup);
            if (dungeon && !lead->isInCombat())
            {
                bool prepared = true;
                bool tank = false;
                for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
                    if (Player* member = ref->getSource())
                    {
                        bool nearby = member->IsInWorld() && member->GetMap() == bot->GetMap() && member->InSamePhase(bot) &&
                            member->isAlive() && bot->GetDistance(member) <= 35.0f;
                        prepared = prepared && nearby && !member->isInCombat();
                        tank = tank || (nearby && member->GetSpecializationRole() == ROLES_TANK);
                    }
                if (!prepared)
                {
                    call("felsoria_instance_pull_3", 4);
                    call("felsoria_party_ready_3", 4);
                }
                else if (tank && bot->GetSpecializationRole() != ROLES_TANK && lead->GetSelectedUnit() &&
                    bot->IsValidAttackTarget(lead->GetSelectedUnit()))
                    call("felsoria_instance_pull_1", 4);
            }
        }
    }
    state.next = now + 850 + (guid % 400);
    {
        std::lock_guard<std::mutex> lock(_mutex);
        auto found = _actors.find(guid);
        if (found != _actors.end())
            found->second.full = state;
    }
}
}

namespace FelsoriaRegional
{
void RegionalBotChatManager::FullAmbient(Player* bot, uint64 now)
{
    auto announce = [&](char const* key, uint8 channel, FullTextFacts facts = FullTextFacts())
    {
        return FullDeliver(bot, {key, channel, std::move(facts), now, bot->GetMapId(), bot->GetInstanceId()});
    };
    FullTextFacts facts;
    facts.cooldown = 180;
    uint32 quest = 0;
    for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 id = bot->GetQuestSlotQuestId(slot);
        if (id && bot->GetQuestStatus(id) == QUEST_STATUS_INCOMPLETE)
        {
            quest = id;
            break;
        }
    }
    switch (urand(0, 8))
    {
        case 0:
            facts.cooldown = 900;
            announce("felsoria_general_felsoria_community", 0, facts);
            break;
        case 1:
            announce("felsoria_general_social", 0, facts);
            break;
        case 2:
            announce("suggest_something", 0, facts);
            break;
        case 3:
            if (!quest || !bot->CanShareQuest(quest))
                break;
            facts.quest = quest;
            announce("suggest_quest", 0, facts);
            break;
        case 4:
        {
            // Match Legion reputation topics to the currently visited zone.
            uint32 faction = 0;
            switch (bot->GetZoneId())
            {
                case 7334: faction = 1900; break; // Azsuna
                case 7558: faction = 1883; break; // Val'sharah
                case 7503: faction = 1828; break; // Highmountain
                case 7541: faction = 1948; break; // Stormheim
                case 7637: faction = 1859; break; // Suramar
                case 7543: faction = 1894; break; // Broken Shore
                default: break;
            }
            auto row = sFactionStore.LookupEntry(faction);
            if (row && row->CanHaveReputation() && bot->GetReputationRank(faction) < REP_EXALTED)
            {
                facts.faction = faction;
                announce("suggest_faction", 0, facts);
            }
            break;
        }
        case 5:
        {
            auto locks = sLFGMgr->GetLockedDungeons(bot->GetGUID());
            std::vector<uint32> suitable;
            uint8 level = bot->getLevel();
            for (LFGDungeonsEntry const* row : sLfgDungeonsStore)
            {
                if (!row || row->MapID <= 0 || !row->CountTank || !row->CountHealer || row->CountDamage != 3 ||
                    row->IsScenario() || row->IsRaidFinder() || row->DifficultyID != DIFFICULTY_NORMAL ||
                    level < row->MinLevel || (row->MaxLevel && level > row->MaxLevel) ||
                    locks.count(row->Entry()) || locks.count(uint32(row->ID)))
                    continue;
                auto map = sMapStore.LookupEntry(row->MapID);
                if (map && map->IsDungeon() && !map->IsRaid())
                    suitable.push_back(row->ID);
            }
            if (!suitable.empty() && (!bot->GetGroup() || !bot->GetGroup()->IsFull()))
            {
                facts.dungeon = suitable[urand(0, uint32(suitable.size() - 1))];
                if (!announce("suggest_instance", TextLfg, facts))
                    announce("suggest_instance", 0, facts);
            }
            break;
        }
        case 6:
            if (Item* item = TradableItem(bot))
            {
                facts.tradeItem = true;
                facts.item = item->GetEntry();
                uint32 pool = urand(0, 1);
                announce(pool ? "suggest_sell" : "suggest_trade", TextTrade, facts);
            }
            break;
        case 7:
            if (bot->getLevel() >= 98)
            {
                uint32 map = bot->GetMapId();
                if (map == 1220)
                    announce("felsoria_general_legion_isles", 0, facts);
                if (map == 1220 && bot->getLevel() >= 110 && (bot->GetQuestStatus(43341)==QUEST_STATUS_COMPLETE || bot->HasCompletedQuest(43341)))
                    announce("felsoria_general_legion_world", 0, facts);
                if (map == 1669 && bot->getLevel() >= 110)
                    announce("felsoria_general_legion_argus", 0, facts);
                if (Item* weapon = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, EQUIPMENT_SLOT_MAINHAND))
                    if (weapon->GetTemplate()->GetQuality() == ITEM_QUALITY_ARTIFACT)
                        announce("felsoria_general_legion_artifact", 0, facts);
            }
            break;
        case 8:
            if (Guild* guild = bot->GetGuild())
                if (guild->FullBotCanInvite(bot) && guild->GetMembersCount() < 1000)
                    announce("broadcast_guild_recruitment", 0, facts);
            break;
    }
}

void RegionalBotChatManager::FullBattleground(Player* bot, FullTextState& state)
{
    Battleground* bg = bot->GetBattleground();
    if (!bg || !bot->isAlive())
        return;
    uint64 now = Clock();
    auto announce = [&](char const* key, uint32 cooldown = 45)
    {
        FullTextFacts facts;
        facts.cooldown = cooldown;
        return FullDeliver(bot, {key, 3, facts, now, bot->GetMapId(), bot->GetInstanceId()});
    };
    TeamId team = bot->GetBGTeamId();
    TeamId enemyTeam = team == TEAM_ALLIANCE ? TEAM_HORDE : TEAM_ALLIANCE;
    auto flags = [&](ObjectGuid friendlyCarrier, ObjectGuid enemyCarrier)
    {
        if (friendlyCarrier && friendlyCarrier != bot->GetGUID())
            if (auto carrier = ObjectAccessor::FindPlayer(friendlyCarrier))
                if (carrier->GetMap() == bot->GetMap() && carrier->GetBGTeamId() == team && bot->GetDistance(carrier) <= 32.0f)
                    announce("felsoria_bg_flag_escort");
        if (enemyCarrier)
            if (auto carrier = ObjectAccessor::FindPlayer(enemyCarrier))
                if (carrier->GetMap() == bot->GetMap() && carrier->GetBGTeamId() == enemyTeam)
                    announce("felsoria_bg_flag_recover");
    };
    if (auto wsg = dynamic_cast<BattlegroundWarsongGulch*>(bg))
        flags(wsg->GetFlagPickerGUID(enemyTeam, 0), wsg->GetFlagPickerGUID(team, 0));
    else if (auto twin = dynamic_cast<BattlegroundTwinPeaks*>(bg))
        flags(twin->GetFlagPickerGUID(enemyTeam), twin->GetFlagPickerGUID(team));

    for (auto const& row : bg->GetPlayers())
    {
        auto player = ObjectAccessor::FindPlayer(row.first);
        if (player && player->GetMap() == bot->GetMap() && player->GetBGTeamId() == team && player->isAlive() &&
            player->GetSpecializationRole() == ROLES_HEALER && player->GetHealthPct() <= 65 &&
            !player->getAttackers()->empty() && bot->GetDistance(player) <= 30.0f)
        {
            announce("felsoria_bg_healer_pressure");
            break;
        }
    }
    if (auto gilneas = dynamic_cast<BattlegroundBattleForGilneas*>(bg))
    {
        char const* const routes[3][4] = {
            {"felsoria_bg_bfg_lighthouse_1","felsoria_bg_bfg_lighthouse_2","felsoria_bg_bfg_lighthouse_3","felsoria_bg_bfg_lighthouse_4"},
            {"felsoria_bg_bfg_waterworks_1","felsoria_bg_bfg_waterworks_2","felsoria_bg_bfg_waterworks_3","felsoria_bg_bfg_waterworks_4"},
            {"felsoria_bg_bfg_mines_1","felsoria_bg_bfg_mines_2","felsoria_bg_bfg_mines_3","felsoria_bg_bfg_mines_4"}
        };
        for (uint8 node = 0; node < 3; ++node)
        {
            auto flag = gilneas->GetBFGFlag(node);
            bool held = gilneas->GetBFGNodeTeam(node) == team && gilneas->GetBFGNodeStatus(node) == NODE_STATUS_CAPTURE;
            uint8 mask = uint8(1u << node);
            bool newlyHeld = held && !(state.bases & mask);
            if (held)
                state.bases |= mask;
            else
                state.bases &= ~mask;
            if (!flag || !held || bot->GetDistance(flag) > 28.0f)
                continue;
            if (newlyHeld)
            {
                if (!announce(routes[node][3], 90))
                    announce("felsoria_bg_base_captured", 90);
            }
            unsigned enemy = 0, friendly = 0;
            for (auto const& row : bg->GetPlayers())
                if (auto other = ObjectAccessor::FindPlayer(row.first))
                    if (other->GetMap() == bot->GetMap() && other->isAlive() && bot->InSamePhase(other) &&
                        other->GetDistance(flag) <= 22.0f && (!other->HasAuraType(SPELL_AURA_MOD_STEALTH) ||
                        other->GetBGTeamId() == team || bot->canSeeOrDetect(other)))
                    {
                        if (other->GetBGTeamId() == team) ++friendly;
                        else if (other->GetBGTeamId() == enemyTeam) ++enemy;
                    }
            if (enemy)
            {
                if (!bot->isInCombat())
                    announce(urand(0, 1) ? routes[node][0] : "felsoria_bg_base_incoming");
                else
                    announce(enemy > friendly ? routes[node][1] : routes[node][2]);
            }
            else if (friendly == 1)
                announce(urand(0, 1) ? routes[node][1] : "felsoria_bg_base_defend");
        }
    }
    if (auto ab = dynamic_cast<BattlegroundArathiBasin*>(bg))
        for (uint8 node = 0; node < BG_AB_DYNAMIC_NODES_COUNT; ++node)
            if (auto point = ab->GetABNodeState(node))
            {
                uint8 mask=uint8(1u<<node);
                bool held=point->TeamID==team && point->Status==NODE_STATUS_CAPTURE;
                bool newlyHeld=held && !(state.bases & mask);
                if (held)
                    state.bases|=mask;
                else
                    state.bases&=~mask;
                if (!held || !point->Point || bot->GetDistance(point->Point)>22.f)
                    continue;
                if (newlyHeld)
                    announce("felsoria_bg_base_captured",90);
                else if (!bot->getAttackers()->empty())
                    announce("felsoria_bg_base_incoming");
                else
                    announce("felsoria_bg_base_defend");
            }
    if (bot->GetHealthPct() < 35 && !bot->isInCombat() && !state.mana)
    {
        unsigned close = 0, medium = 0;
        for (auto const& row : bg->GetPlayers())
            if (auto ally = ObjectAccessor::FindPlayer(row.first))
                if (ally != bot && ally->GetMap() == bot->GetMap() && ally->isAlive() && ally->GetBGTeamId() == team)
                {
                    float distance = bot->GetDistance(ally);
                    if (distance <= 20.0f) ++close;
                    else if (distance <= 60.0f) ++medium;
                }
        // Regroup with nearby allies when isolated after a fight.
        if (close == 0 && medium && !state.bases && !dynamic_cast<BattlegroundArathiBasin*>(bg))
            announce("felsoria_bg_regroup", 120);
    }
}
}

namespace FelsoriaRegional
{
void RegionalBotChatManager::FullLoot(Player* player, uint32 itemId, uint32 quality)
{
    char const* const pools[] = {"broadcast_looting_item_poor","broadcast_looting_item_normal",
        "broadcast_looting_item_uncommon","broadcast_looting_item_rare","broadcast_looting_item_epic",
        "broadcast_looting_item_legendary","broadcast_looting_item_artifact"};
    FullTextFacts facts;
    facts.item = itemId;
    facts.cooldown = quality <= ITEM_QUALITY_NORMAL ? 180 : 45;
    if (quality < sizeof(pools) / sizeof(*pools) && urand(0,3)!=0)
        FullEvent(player, pools[quality], 8, facts);
    else
        FullEvent(player, "loot", 8, facts);
}

void RegionalBotChatManager::FullObjective(Player* player, uint32 quest, uint32 item, std::string const& objective, int32 count, int32 required)
{
    FullTextFacts facts;
    facts.quest = quest;
    facts.item = item;
    facts.objective = objective;
    facts.available = count;
    facts.required = required;
    facts.questShared = true;
    facts.cooldown = 25;
    char const* key = item ? (count >= required ? "broadcast_quest_update_add_item_objective_completed" :
        "broadcast_quest_update_add_item_objective_progress") : (count >= required ?
        "broadcast_quest_update_add_kill_objective_completed" : "broadcast_quest_update_add_kill_objective_progress");
    FullEvent(player, key, 8, facts);
}

void RegionalBotChatManager::FullKill(Player* player, Unit* victim)
{
    if (!_fullReady.load() || !player || !victim || player == victim || !victim->isDead())
        return;
    FullTextFacts facts;
    facts.name = victim->GetName();
    facts.cooldown = 45;
    if (victim->IsPlayer())
    {
        if (player->InBattleground() || player->GetGroup())
            FullEvent(player, urand(0, 2) ? "broadcast_killed_player" : "reply_kill", 8, facts);
        return;
    }

    auto creature = victim->ToCreature();
    if (creature && creature->IsDungeonBoss() && victim->GetMap()->IsDungeon())
    {
        facts.cooldown = 120;
        FullSignal(player, urand(0,3) ? "felsoria_instance_boss_killed" : "felsoria_party_ready_4", 4, facts);
        return;
    }
    char const* key = "broadcast_killed_unknown";
    if (victim->isPet())
        key = "broadcast_killed_pet";
    else if (creature)
    {
        switch (creature->GetCreatureTemplate()->Classification)
        {
            case CREATURE_CLASSIFICATION_NORMAL: key = "broadcast_killed_normal"; break;
            case CREATURE_CLASSIFICATION_ELITE: key = "broadcast_killed_elite"; break;
            case CREATURE_CLASSIFICATION_RARE: key = "broadcast_killed_rare"; break;
            case CREATURE_CLASSIFICATION_RARE_ELITE: key = "broadcast_killed_rareelite"; break;
            case CREATURE_CLASSIFICATION_WORLDBOSS: key = "broadcast_killed_worldboss"; break;
            default: break;
        }
    }
    FullEvent(player, key, 8, facts);
}

void RegionalBotChatManager::FullGuildRank(Player* member, uint32 guild, bool demoted)
{
    if (!_fullReady.load() || !member || !member->IsInWorld())
        return;
    uint64 speaker = 0;
    {
        std::lock_guard<std::mutex> guard(_mutex);
        uint32 realm = sRegionalGuildMgr.RealmFor(member);
        for (auto const& row : _actors)
        {
            Actor const& actor = row.second;
            if (actor.bot && actor.ready && actor.realm == realm && actor.guild == guild &&
                actor.map == member->GetMapId() && actor.instance == member->GetInstanceId())
            {
                speaker = actor.guid;
                break;
            }
        }
    }
    if (speaker)
    {
        auto bot = ObjectAccessor::FindPlayer(ObjectGuid::Create<HighGuid::Player>(speaker));
        FullTextFacts facts;
        facts.guild = guild;
        facts.name = member->GetName();
        FullEvent(bot, demoted ? "broadcast_guild_demotion" : "broadcast_guild_promotion", 7, facts);
    }
}

void RegionalBotChatManager::FullControl(Player* bot, Player* requester, char const* key,
    uint32 item, uint32 spell, std::string const& target, char const* fragment, int32 count)
{
    FullTextFacts facts;
    facts.item = item;
    facts.spell = spell;
    facts.name = target;
    facts.available = count;
    facts.allowUnknownItem = true;
    facts.emptyTarget = (std::string(key) == "use_command" && !item && target.empty());
    if (fragment)
        facts.fragments.push_back(fragment);
    if (std::string(key) == "use_command")
    {
        if (item)
            facts.targetIsItem = true;
        if (count >= 0)
            facts.fragments.push_back(count == 0 ? "use_command_last" : "use_command_remaining");
    }
    FullEvent(bot, key, 5, std::move(facts), requester);
}
}

namespace FelsoriaRegional
{
void RegionalBotChatManager::FullReport(Player* bot, char const* key, FullTextFacts facts)
{
    if (!_fullReady.load() || !bot || !bot->IsInWorld() || !OwnsAutomaticChat(bot))
        return;
    Player* leader = FullLeader(bot);
    if (leader)
        FullEvent(bot,key,5,std::move(facts),leader);
}
}
