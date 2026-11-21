// FELSORIA_PVP_LEGION_DAMAGE_V1: 36 specialization priorities.
    switch(spec){
    case 71:
        if((burst) && C("Battle Cry",me))return true;
        if((missing("Colossus Smash",enemy,0)) && C("Colossus Smash",enemy))return true;
        if((safeAoE && adds>=3) && safeAoE && C("Bladestorm",me))return true;
        if((missing("Rend",enemy,4500)) && C("Rend",enemy))return true;
        if((eh<20) && C("Execute",enemy))return true;
        if((true) && C("Mortal Strike",enemy))return true;
        if((true) && C("Overpower",enemy))return true;
        if((safeAoE && adds>=3) && C("Whirlwind",me))return true;
        if((true) && C("Slam",enemy))return true;
        break;
    case 72:
        if((burst) && C("Battle Cry",me))return true;
        if((safeAoE && adds>=3 && !A(me,85739)) && C("Whirlwind",me))return true;
        if((!A(me,184362) || P(POWER_RAGE)>90) && C("Rampage",enemy))return true;
        if((eh<20) && C("Execute",enemy))return true;
        if((!A(me,184362)) && C("Bloodthirst",enemy))return true;
        if((true) && C("Raging Blow",enemy))return true;
        if((true) && C("Bloodthirst",enemy))return true;
        if((true) && C("Furious Slash",enemy))return true;
        break;
    case 73:
        if((hp<30) && DEF("Last Stand"))return true;
        if(((hp<80 || burst) && missing("Shield Block",me,500)) && C("Shield Block",me))return true;
        if((hp<75 && missing("Ignore Pain",me,0)) && C("Ignore Pain",me))return true;
        if((hp<65) && C("Demoralizing Shout",me))return true;
        if((true) && C("Shield Slam",enemy))return true;
        if((true) && (safeAoE && C("Thunder Clap",me)))return true;
        if((true) && C("Revenge",enemy))return true;
        if((true) && C("Devastate",enemy))return true;
        break;
    case 65:
        if((true) && C("Judgment",enemy))return true;
        if((true) && C("Holy Shock",enemy))return true;
        if((true) && C("Crusader Strike",enemy))return true;
        break;
    case 66:
        if((hp<55) && C("Light of the Protector",me))return true;
        if((hp<30) && DEF("Guardian of Ancient Kings"))return true;
        if(((hp<80 || burst) && !A(me,132403)) && C("Shield of the Righteous",enemy))return true;
        if((true) && safeAoE && C("Avenger's Shield",enemy))return true;
        if((missing("Consecration",me,0)) && safeAoE && C("Consecration",me))return true;
        if((true) && C("Judgment",enemy))return true;
        if((safeAoE) && C("Blessed Hammer",me))return true;
        if((safeAoE) && C("Hammer of the Righteous",enemy))return true;
        break;
    case 70:
        if((burst) && C("Crusade",me))return true;
        if((burst) && C("Avenging Wrath",me))return true;
        if((safeAoE && adds>=3 && R(POWER_HOLY_POWER)>=5) && C("Divine Storm",enemy))return true;
        if((R(POWER_HOLY_POWER)>=5) && C("Templar's Verdict",enemy))return true;
        if((true) && C("Judgment",enemy))return true;
        if((safeAoE && adds>=3 && R(POWER_HOLY_POWER)>=3) && C("Divine Storm",enemy))return true;
        if((R(POWER_HOLY_POWER)>=3) && C("Templar's Verdict",enemy))return true;
        if((true) && C("Blade of Justice",enemy))return true;
        if((true) && safeAoE && C("Wake of Ashes",enemy))return true;
        if((true) && C("Crusader Strike",enemy))return true;
        break;
    case 253:
        if((burst) && C("Aspect of the Wild",me))return true;
        if((true) && C("Bestial Wrath",me))return true;
        if((safeAoE && adds>=3 && !A(me,118455)) && C("Multi-Shot",enemy))return true;
        if((true) && C("Kill Command",enemy))return true;
        if((true) && C("Dire Frenzy",enemy))return true;
        if((true) && C("Dire Beast",enemy))return true;
        if((burst) && C("Titan's Thunder",me))return true;
        if((P(POWER_FOCUS)>60) && C("Cobra Shot",enemy))return true;
        break;
    case 254:
        if((burst) && C("Trueshot",me))return true;
        if((true) && C("Windburst",enemy))return true;
        if((A(enemy,185365)) && C("Marked Shot",enemy))return true;
        if((A(enemy,187131)) && C("Aimed Shot",enemy))return true;
        if((safeAoE && adds>=3) && C("Multi-Shot",enemy))return true;
        if((P(POWER_FOCUS)>85) && C("Aimed Shot",enemy))return true;
        if((true) && C("Arcane Shot",enemy))return true;
        break;
    case 255:
        if((burst) && C("Aspect of the Eagle",me))return true;
        if((missing("Lacerate",enemy,3000)) && C("Lacerate",enemy))return true;
        if((stacks(me,190931)>=5) && safeAoE && C("Fury of the Eagle",enemy))return true;
        if((true) && C("Mongoose Bite",enemy))return true;
        if((true) && safeAoE && C("Explosive Trap",enemy))return true;
        if((true) && safeAoE && C("Flanking Strike",enemy))return true;
        if((safeAoE && adds>=3) && C("Carve",enemy))return true;
        if((P(POWER_FOCUS)>60) && C("Raptor Strike",enemy))return true;
        break;
    case 259:
        if((burst) && C("Vendetta",enemy))return true;
        if((missing("Garrote",enemy,4500)) && C("Garrote",enemy))return true;
        if((cp>=4 && missing("Rupture",enemy,6000)) && C("Rupture",enemy))return true;
        if((cp>=4) && C("Envenom",enemy))return true;
        if((burst) && C("Kingsbane",enemy))return true;
        if((safeAoE && adds>=3) && C("Fan of Knives",me))return true;
        if((true) && C("Mutilate",enemy))return true;
        break;
    case 260:
        if((burst) && C("Adrenaline Rush",me))return true;
        if((cp>=5 && !A(me,193356) && !A(me,199600) && !A(me,193358) && !A(me,193357) && !A(me,199603) && !A(me,193359)) && C("Roll the Bones",me))return true;
        if((cp>=5) && C("Run Through",enemy))return true;
        if((safeAoE && adds>=2 && missing("Blade Flurry",me,0)) && C("Blade Flurry",me))return true;
        if((A(me,195627)) && C("Pistol Shot",enemy))return true;
        if((true) && C("Saber Slash",enemy))return true;
        break;
    case 261:
        if((burst) && C("Shadow Blades",me))return true;
        if((cp>=5 && missing("Nightblade",enemy,4500)) && C("Nightblade",enemy))return true;
        if((cp>=5) && C("Eviscerate",enemy))return true;
        if((cp<=2 && !A(me,185422)) && C("Shadow Dance",me))return true;
        if((A(me,185422) || A(me,1784)) && C("Shadowstrike",enemy))return true;
        if((safeAoE && adds>=3) && C("Shuriken Storm",me))return true;
        if((true) && C("Gloomblade",enemy))return true;
        if((true) && C("Backstab",enemy))return true;
        break;
    case 256:
        if((missing("Purge the Wicked",enemy,3000)) && C("Purge the Wicked",enemy))return true;
        if((!ID("Purge the Wicked") && missing("Shadow Word: Pain",enemy,3000)) && C("Shadow Word: Pain",enemy))return true;
        if((true) && C("Penance",enemy))return true;
        if((burst) && C("Mindbender",enemy))return true;
        if((true) && C("Smite",enemy))return true;
        break;
    case 257:
        if((missing("Holy Fire",enemy,0)) && C("Holy Fire",enemy))return true;
        if((true) && C("Holy Word: Chastise",enemy))return true;
        if((true) && C("Smite",enemy))return true;
        break;
    case 258:
        if((A(me,194249)) && C("Void Bolt",enemy))return true;
        if((!A(me,194249)) && C("Void Eruption",enemy))return true;
        if((missing("Vampiric Touch",enemy,4500)) && C("Vampiric Touch",enemy))return true;
        if((missing("Shadow Word: Pain",enemy,4500)) && C("Shadow Word: Pain",enemy))return true;
        if((eh<20) && C("Shadow Word: Death",enemy))return true;
        if((true) && C("Mind Blast",enemy))return true;
        if((burst) && C("Shadowfiend",enemy))return true;
        if((safeAoE && adds>=3) && C("Mind Sear",enemy))return true;
        if((true) && C("Mind Flay",enemy))return true;
        break;
    case 250:
        if((hp<70 || P(POWER_RUNIC_POWER)>85) && C("Death Strike",enemy))return true;
        if((hp<45) && DEF("Vampiric Blood"))return true;
        if((stacks(me,195181)<6) && C("Marrowrend",enemy))return true;
        if((true) && (safeAoE && C("Blood Boil",me)))return true;
        if((safeAoE && adds>=3) && safeAoE && C("Death and Decay",enemy))return true;
        if((safeAoE) && C("Heart Strike",enemy))return true;
        break;
    case 251:
        if((burst) && C("Pillar of Frost",me))return true;
        if((safeAoE && (!A(enemy,55095) || A(me,59052))) && C("Howling Blast",enemy))return true;
        if((burst) && safeAoE && C("Sindragosa's Fury",enemy))return true;
        if((P(POWER_RUNIC_POWER)>80) && C("Frost Strike",enemy))return true;
        if((true) && C("Obliterate",enemy))return true;
        if((true) && C("Frost Strike",enemy))return true;
        break;
    case 252:
        if((!A(enemy,191587)) && C("Outbreak",enemy))return true;
        if((burst) && C("Dark Transformation",me->GetPet()))return true;
        if((burst) && C("Summon Gargoyle",enemy))return true;
        if((safeAoE && adds>=3) && safeAoE && C("Death and Decay",enemy))return true;
        if((stacks(enemy,194310)>=4) && C("Clawing Shadows",enemy))return true;
        if((stacks(enemy,194310)>=4) && C("Scourge Strike",enemy))return true;
        if((A(me,81340) || P(POWER_RUNIC_POWER)>80) && C("Death Coil",enemy))return true;
        if((stacks(enemy,194310)<4) && C("Festering Strike",enemy))return true;
        if((true) && C("Death Coil",enemy))return true;
        break;
    case 262:
        if((!A(enemy,188389)) && C("Flame Shock",enemy))return true;
        if((burst) && C("Fire Elemental",me))return true;
        if((safeAoE && adds>=3 && R(POWER_MAELSTROM)>=60) && C("Earthquake",enemy))return true;
        if((R(POWER_MAELSTROM)>=90) && C("Earth Shock",enemy))return true;
        if((true) && C("Lava Burst",enemy))return true;
        if((safeAoE && adds>=3) && C("Chain Lightning",enemy))return true;
        if((true) && C("Lightning Bolt",enemy))return true;
        break;
    case 263:
        if((burst) && C("Feral Spirit",me))return true;
        if((!A(me,194084)) && C("Flametongue",enemy))return true;
        if((true) && C("Stormstrike",enemy))return true;
        if((safeAoE && adds>=3) && C("Crash Lightning",enemy))return true;
        if((R(POWER_MAELSTROM)>80) && C("Lava Lash",enemy))return true;
        if((true) && C("Boulderfist",enemy))return true;
        if((true) && C("Rockbiter",enemy))return true;
        break;
    case 264:
        if((!A(enemy,188838)) && C("Flame Shock",enemy))return true;
        if((true) && C("Lava Burst",enemy))return true;
        if((safeAoE && adds>=3) && C("Chain Lightning",enemy))return true;
        if((true) && C("Lightning Bolt",enemy))return true;
        break;
    case 62:
        if((mana<15) && C("Evocation",me))return true;
        if((burst && mana>65) && C("Arcane Power",me))return true;
        if((stacks(me,79683)>=2) && C("Arcane Missiles",enemy))return true;
        if((R(POWER_ARCANE_CHARGES)>0 && (mana<20 || (R(POWER_ARCANE_CHARGES)>=4 && mana<55 && !A(me,12042)))) && C("Arcane Barrage",enemy))return true;
        if(safeAoE && closeEnemies>=3 && C("Arcane Explosion",me))return true;
        if((A(me,79683)) && C("Arcane Missiles",enemy))return true;
        if((true) && C("Arcane Blast",enemy))return true;
        if(C("Arcane Barrage",enemy))return true;
        break;
    case 63:
        if((burst) && C("Combustion",me))return true;
        if((safeAoE && adds>=3 && A(me,48108)) && C("Flamestrike",enemy))return true;
        if((A(me,48108)) && C("Pyroblast",enemy))return true;
        if((A(me,48107)) && C("Fire Blast",enemy))return true;
        if((true) && C("Phoenix's Flames",enemy))return true;
        if((true) && C("Fireball",enemy))return true;
        break;
    case 64:
        if((burst) && C("Icy Veins",me))return true;
        if((true) && (safeAoE && safeAoE && C("Frozen Orb",enemy)))return true;
        if((A(me,190446)) && C("Flurry",enemy))return true;
        if((A(me,44544) || A(enemy,228358)) && C("Ice Lance",enemy))return true;
        if((safeAoE && adds>=3) && C("Blizzard",enemy))return true;
        if((true) && C("Frostbolt",enemy))return true;
        break;
    case 265:
        if((missing("Agony",enemy,4500)) && C("Agony",enemy))return true;
        if((missing("Corruption",enemy,4000)) && C("Corruption",enemy))return true;
        if((missing("Siphon Life",enemy,3000)) && C("Siphon Life",enemy))return true;
        if((burst) && C("Summon Doomguard",enemy))return true;
        if((safeAoE && adds>=3 && missing("Seed of Corruption",enemy,0)) && C("Seed of Corruption",enemy))return true;
        if((P(POWER_SOUL_SHARDS)>=80) && C("Unstable Affliction",enemy))return true;
        if((!A(enemy,233490) && P(POWER_SOUL_SHARDS)>=40) && C("Unstable Affliction",enemy))return true;
        if((true) && C("Drain Soul",enemy))return true;
        if(C("Drain Life",enemy))return true;
        break;
    case 266:
        if((missing("Doom",enemy,12000)) && C("Doom",enemy))return true;
        if((lastSpell==ID("Call Dreadstalkers") || lastSpell==ID("Hand of Gul'dan")) && C("Demonic Empowerment",me))return true;
        if((true) && C("Call Dreadstalkers",enemy))return true;
        if((P(POWER_SOUL_SHARDS)>=80) && C("Hand of Gul'dan",enemy))return true;
        if((burst) && C("Summon Doomguard",enemy))return true;
        if((true) && C("Demonbolt",enemy))return true;
        if((true) && C("Shadow Bolt",enemy))return true;
        break;
    case 267:
        if((missing("Immolate",enemy,4500)) && C("Immolate",enemy))return true;
        if((burst) && C("Summon Doomguard",enemy))return true;
        if((safeAoE && adds>=3 && P(POWER_SOUL_SHARDS)>=80) && C("Rain of Fire",enemy))return true;
        if((P(POWER_SOUL_SHARDS)>=80) && C("Chaos Bolt",enemy))return true;
        if((true) && C("Conflagrate",enemy))return true;
        if((P(POWER_SOUL_SHARDS)>=40) && C("Chaos Bolt",enemy))return true;
        if((true) && C("Incinerate",enemy))return true;
        break;
    case 268:
        if((A(me,124273) || (A(me,124274) && hp<70)) && C("Purifying Brew",me))return true;
        if((missing("Ironskin Brew",me,2000)) && C("Ironskin Brew",me))return true;
        if((hp<55) && C("Expel Harm",me))return true;
        if((true) && safeAoE && C("Keg Smash",enemy))return true;
        if((true) && safeAoE && C("Breath of Fire",enemy))return true;
        if((true) && C("Blackout Strike",enemy))return true;
        if((P(POWER_ENERGY)>65) && C("Tiger Palm",enemy))return true;
        break;
    case 269:
        if((burst) && C("Touch of Death",enemy))return true;
        if((burst) && C("Serenity",me))return true;
        if((burst && !ID("Serenity")) && C("Storm, Earth, and Fire",me))return true;
        if((true) && safeAoE && C("Whirling Dragon Punch",enemy))return true;
        if((R(POWER_CHI)>=3) && safeAoE && C("Fists of Fury",enemy))return true;
        if((true) && C("Rising Sun Kick",enemy))return true;
        if((safeAoE && adds>=3) && C("Spinning Crane Kick",me))return true;
        if((R(POWER_CHI)<=3) && C("Tiger Palm",enemy))return true;
        if((true) && C("Blackout Kick",enemy))return true;
        break;
    case 270:
        if((true) && C("Rising Sun Kick",enemy))return true;
        if((stacks(me,202090)>=3) && C("Blackout Kick",enemy))return true;
        if((true) && C("Tiger Palm",enemy))return true;
        break;
    case 102:
        if((burst) && C("Celestial Alignment",me))return true;
        if((missing("Moonfire",enemy,4500)) && C("Moonfire",enemy))return true;
        if((missing("Sunfire",enemy,4500)) && C("Sunfire",enemy))return true;
        if((safeAoE && adds>=3 && R(POWER_LUNAR_POWER)>=60) && C("Starfall",enemy))return true;
        if((R(POWER_LUNAR_POWER)>=80) && C("Starsurge",enemy))return true;
        if((stacks(me,164547)>=2) && C("Lunar Strike",enemy))return true;
        if((stacks(me,164545)>=2) && C("Solar Wrath",enemy))return true;
        if((true) && C("Full Moon",enemy))return true;
        if((true) && C("Half Moon",enemy))return true;
        if((true) && C("New Moon",enemy))return true;
        if((R(POWER_LUNAR_POWER)>=40) && C("Starsurge",enemy))return true;
        if((safeAoE && adds>=3) && C("Lunar Strike",enemy))return true;
        if((true) && C("Solar Wrath",enemy))return true;
        break;
    case 103:
        if((P(POWER_ENERGY)<50) && C("Tiger's Fury",me))return true;
        if((burst) && C("Berserk",me))return true;
        if((missing("Rake",enemy,4500)) && C("Rake",enemy))return true;
        if((cp>=5 && missing("Rip",enemy,7000)) && C("Rip",enemy))return true;
        if((cp>=5) && C("Ferocious Bite",enemy))return true;
        if((safeAoE && adds>=3 && missing("Thrash",enemy,4500)) && safeAoE && C("Thrash",enemy))return true;
        if((safeAoE && adds>=3) && safeAoE && C("Swipe",enemy))return true;
        if((true) && C("Shred",enemy))return true;
        break;
    case 104:
        if((hp<55) && C("Frenzied Regeneration",me))return true;
        if(((hp<80 || burst) && missing("Ironfur",me,1500)) && C("Ironfur",me))return true;
        if((hp<35) && DEF("Survival Instincts"))return true;
        if((missing("Moonfire",enemy,3000)) && C("Moonfire",enemy))return true;
        if((true) && safeAoE && C("Thrash",enemy))return true;
        if((true) && C("Mangle",enemy))return true;
        if((P(POWER_RAGE)>85) && C("Maul",enemy))return true;
        if((true) && safeAoE && C("Swipe",enemy))return true;
        break;
    case 105:
        if((missing("Moonfire",enemy,3000)) && C("Moonfire",enemy))return true;
        if((missing("Sunfire",enemy,3000)) && C("Sunfire",enemy))return true;
        if((true) && C("Solar Wrath",enemy))return true;
        break;
    case 577:
        if((burst) && C("Metamorphosis",enemy))return true;
        if((safeAoE && (adds>=2 || burst || R(POWER_FURY)>=50)) && safeAoE && C("Eye Beam",enemy))return true;
        if((true) && safeAoE && C("Death Sweep",enemy))return true;
        if((true) && safeAoE && C("Blade Dance",enemy))return true;
        if((R(POWER_FURY)>=40) && C("Annihilation",enemy))return true;
        if((R(POWER_FURY)>=40) && C("Chaos Strike",enemy))return true;
        if((burst) && safeAoE && C("Fury of the Illidari",enemy))return true;
        if((true) && C("Demon's Bite",enemy))return true;
        if((true) && C("Throw Glaive",enemy))return true;
        break;
    case 581:
        if((hp<65) && C("Soul Cleave",me))return true;
        if(((hp<80 || burst) && missing("Demon Spikes",me,1000)) && C("Demon Spikes",me))return true;
        if((hp<50) && C("Fiery Brand",enemy))return true;
        if((true) && safeAoE && C("Immolation Aura",me))return true;
        if((true) && safeAoE && C("Sigil of Flame",enemy))return true;
        if((R(POWER_PAIN)>80) && C("Soul Cleave",me))return true;
        if((true) && C("Fracture",enemy))return true;
        if((true) && C("Shear",enemy))return true;
        break;
    }
