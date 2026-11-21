#ifndef FELSORIA_GILNEAS_EXIT_ROUTES_H
#define FELSORIA_GILNEAS_EXIT_ROUTES_H
#include "FelsoriaGilneasRecoveryPolicy.h"
// Player recordings, 2026-11-02. Airborne samples are represented by
// a directed ground takeoff -> ground landing, never walked midair.
namespace FelsoriaGilneasExits {
using Step=FelsoriaGilneasRecovery::Step;
inline bool HasRoute(unsigned grave){return grave<3;}
static Step const lh[]={
    {{1034.837036f,1335.196533f,10.515491f},false},
    {{1034.198853f,1332.258545f,10.165811f},false},
    {{1030.630737f,1322.401367f,2.231216f},true},
    {{1030.399658f,1319.358154f,2.191708f},false},
    {{1030.821655f,1318.388062f,2.230940f},false},
    {{1031.665771f,1317.758789f,2.221059f},false},
    {{1034.713867f,1315.976562f,2.250580f},false},
    {{1036.649536f,1314.325806f,2.331702f},false},
    {{1038.309814f,1311.854126f,2.417020f},false},
    {{1039.391357f,1310.038086f,2.469420f},false},
    {{1042.350708f,1304.817871f,2.762313f},false},
    {{1044.349731f,1301.284058f,3.190162f},false},
    {{1046.348633f,1297.750244f,3.474175f},false},
    {{1048.347534f,1294.216553f,3.570925f},false},
    {{1050.346558f,1290.682739f,3.616446f},false},
    {{1052.345459f,1287.148926f,3.626215f},false},
    {{1054.344482f,1283.615112f,3.626508f},false},
    {{1056.343384f,1280.081299f,3.626508f},false},
    {{1057.040283f,1279.078003f,3.626508f},false},
};
static Step const ww[]={
    {{892.180054f,936.622498f,23.404362f},false},
    {{898.863220f,938.658081f,22.892433f},false},
    {{905.559509f,940.697693f,22.096272f},false},
    {{912.250549f,942.735657f,21.963099f},false},
    {{916.667664f,944.058228f,21.936110f},false},
    {{920.105774f,945.019043f,21.603498f},false},
    {{923.005127f,945.773804f,21.086357f},false},
    {{926.895325f,946.720215f,20.027687f},false},
    {{929.878784f,947.291992f,18.785772f},false},
    {{932.855225f,947.739807f,17.129761f},false},
    {{935.914917f,948.092163f,14.873453f},false},
    {{938.887817f,948.321411f,13.228094f},false},
    {{942.019653f,948.483154f,13.177195f},false},
    {{946.161438f,948.618530f,13.450464f},false},
    {{949.884460f,948.702881f,13.302634f},false},
    {{953.622253f,948.738342f,13.352735f},false},
    {{960.622253f,948.750671f,13.237375f},false},
    {{967.622253f,948.758667f,13.172780f},false},
    {{974.622253f,948.766663f,13.171412f},false},
    {{979.188782f,949.247070f,13.171412f},false},
};
static Step const mines[]={
    {{1252.417358f,836.801575f,27.788193f},false},
    {{1252.338989f,840.860840f,27.958918f},false},
    {{1252.117676f,851.112549f,20.042048f},true},
    {{1252.138306f,854.469543f,19.833725f},false},
    {{1252.210205f,858.528870f,19.825752f},false},
    {{1252.281982f,862.588257f,18.374157f},false},
    {{1252.434448f,865.994263f,17.507469f},false},
    {{1252.937744f,869.365479f,16.869061f},false},
    {{1253.692749f,872.857544f,16.557112f},false},
    {{1254.617310f,876.810852f,16.408009f},false},
    {{1255.541870f,880.764160f,16.372107f},false},
    {{1256.466431f,884.717529f,16.402592f},false},
    {{1257.286987f,888.770447f,16.483551f},false},
    {{1257.506592f,892.161682f,16.635201f},false},
    {{1257.280518f,895.970825f,16.662834f},false},
    {{1257.086060f,897.656616f,15.438890f},false},
    {{1256.132202f,904.598877f,14.222363f},true},
    {{1255.483521f,910.327026f,13.989486f},false},
    {{1255.109375f,914.369751f,13.703488f},false},
    {{1254.735352f,918.412476f,12.335176f},false},
    {{1254.361328f,922.455200f,11.221203f},false},
    {{1253.987183f,926.497925f,10.320759f},false},
    {{1253.613159f,930.540649f,9.516082f},false},
    {{1253.239136f,934.583374f,8.483466f},false},
    {{1252.864990f,938.626099f,7.827483f},false},
    {{1252.490967f,942.668884f,7.045867f},false},
    {{1252.116943f,946.711609f,6.232813f},false},
    {{1251.742798f,950.754333f,5.997726f},false},
    {{1251.368774f,954.797058f,5.997967f},false},
    {{1250.565674f,957.386902f,6.082107f},false},
};
// Felsoria Gilneas egress V3: the boundary exception applies only to
// the first recorded outward drop. Never use it for reverse travel, combat LOS,
// arbitrary destinations, or the later Mines jump further down the path.
inline unsigned BoundaryJump(FelsoriaGilneas::Point const& takeoff,FelsoriaGilneas::Point const& landing){
    auto matches=[&](Step const* route){return
        FelsoriaGilneas::Distance(takeoff,route[1].point)<=1.25f&&
        FelsoriaGilneas::Distance(landing,route[2].point)<=.35f&&
        takeoff.z-landing.z>=5.0f;};
    if(matches(lh))return 0;
    if(matches(mines))return 2;
    return 5;
}
inline std::vector<Step> Path(unsigned grave){
    if(grave==0)return {lh,lh+sizeof(lh)/sizeof(lh[0])};
    if(grave==1)return {ww,ww+sizeof(ww)/sizeof(ww[0])};
    if(grave==2)return {mines,mines+sizeof(mines)/sizeof(mines[0])};
    return {};
}
} // namespace FelsoriaGilneasExits
#endif
