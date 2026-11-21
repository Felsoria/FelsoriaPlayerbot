// FELSORIA_PVP_LEGION_HEALING_V1: emergency triage before damage.
    if(healer && minimumHP<95){
        // C() approaches using the actual healing spell range and LOS.
        switch(spec){
    case 65:
        if((lh<18) && C("Lay on Hands",low))return true;
        if((lh<30) && C("Blessing of Sacrifice",low))return true;
        if((emergency) && GROUPCD("Avenging Wrath",me))return true;
        if((lh<85) && C("Holy Shock",low))return true;
        if((injured>=3) && C("Light of Dawn",low))return true;
        if((lh<55) && C("Flash of Light",low))return true;
        if((low && missing("Beacon of Light",low,0)) && C("Beacon of Light",low))return true;
        if((lh<95) && C("Holy Light",low))return true;
        break;
    case 256:
        if((lh<30) && C("Pain Suppression",low))return true;
        if((emergency) && GROUPCD("Power Word: Barrier",low))return true;
        if((lh<45) && C("Shadow Mend",low))return true;
        if((injured>=3) && C("Power Word: Radiance",low))return true;
        if((lh<90 && missing("Power Word: Shield",low,0)) && C("Power Word: Shield",low))return true;
        if((lh<80) && C("Penance",low))return true;
        if((lh<90) && C("Shadow Mend",low))return true;
        break;
    case 257:
        if((lh<25) && C("Guardian Spirit",low))return true;
        if((emergency) && GROUPCD("Divine Hymn",me))return true;
        if((lh<55) && C("Holy Word: Serenity",low))return true;
        if((injured>=3) && C("Holy Word: Sanctify",low))return true;
        if((lh<45) && C("Flash Heal",low))return true;
        if((injured>=3) && C("Prayer of Healing",low))return true;
        if((low && missing("Prayer of Mending",low,0)) && C("Prayer of Mending",low))return true;
        if((lh<90 && missing("Renew",low,3000)) && C("Renew",low))return true;
        if((lh<95) && C("Heal",low))return true;
        break;
    case 264:
        if((emergency) && GROUPCD("Healing Tide Totem",me))return true;
        if((critical>=2) && GROUPCD("Spirit Link Totem",low))return true;
        if((lh<40) && C("Healing Surge",low))return true;
        if((lh<90 && missing("Riptide",low,3000)) && C("Riptide",low))return true;
        if((injured>=3) && C("Healing Rain",low))return true;
        if((injured>=3) && C("Chain Heal",low))return true;
        if((lh<95) && C("Healing Wave",low))return true;
        break;
    case 270:
        if((emergency) && GROUPCD("Revival",me))return true;
        if((lh<30) && C("Life Cocoon",low))return true;
        if((lh<50 && missing("Enveloping Mist",low,1000)) && C("Enveloping Mist",low))return true;
        if((lh<55) && C("Vivify",low))return true;
        if((lh<90 && missing("Renewing Mist",low,3000)) && C("Renewing Mist",low))return true;
        if((injured>=3) && C("Essence Font",me))return true;
        if((lh<80) && C("Vivify",low))return true;
        if((lh<95) && C("Effuse",low))return true;
        break;
    case 105:
        if((lh<30) && C("Ironbark",low))return true;
        if((emergency) && GROUPCD("Tranquility",me))return true;
        if((lh<45) && C("Swiftmend",low))return true;
        if((lh<40) && C("Regrowth",low))return true;
        if((injured>=3) && C("Wild Growth",low))return true;
        if((low && missing("Lifebloom",low,3000)) && C("Lifebloom",low))return true;
        if((lh<90 && missing("Rejuvenation",low,3000)) && C("Rejuvenation",low))return true;
        if((lh<75) && C("Regrowth",low))return true;
        if((lh<95) && C("Healing Touch",low))return true;
        break;
        }
        if(!PvpRotationPolicy::mayDamage(true,minimumHP)){approach(low, 30.0f);return true;}
    }
