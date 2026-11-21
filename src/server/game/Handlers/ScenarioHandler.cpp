/*
 * Copyright (C) 2008-2015 TrinityCore <http://www.trinitycore.org/>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "ScenarioPackets.h"
#include "WorldSession.h"
#include "Scenario.h"

void WorldSession::HandleQueryScenarioPOI(WorldPackets::Scenario::QueryScenarioPOI& packet)
{
    TC_LOG_DEBUG(LOG_FILTER_SPELLS_AURAS, "HandleQueryScenarioPOI MissingScenarioPOITreeIDs %u", packet.MissingScenarioPOITreeIDs.size());

    WorldPackets::Scenario::ScenarioPOIs response;
    response.PoiInfos.reserve(packet.MissingScenarioPOITreeIDs.size());

    for (uint32 criteriaTreeId : packet.MissingScenarioPOITreeIDs)
    {
        WorldPackets::Scenario::ScenarioPOIs::POIData data;
        data.CriteriaTreeID = criteriaTreeId;

        if (ScenarioPOIVector const* poi = sObjectMgr->GetScenarioPOIVector(criteriaTreeId))
        {
            data.BlobDatas.reserve(poi->size());

            for (ScenarioPOI const& scenarioPOI : *poi)
            {
                WorldPackets::Scenario::ScenarioPOIs::POIData::BlobData blobData;
                blobData.BlobID            = scenarioPOI.BlobID;
                blobData.MapID             = scenarioPOI.MapID;
                blobData.WorldMapAreaID    = scenarioPOI.WorldMapAreaID;
                blobData.Floor             = scenarioPOI.Floor;
                blobData.Priority          = scenarioPOI.Priority;
                blobData.Flags             = scenarioPOI.Flags;
                blobData.WorldEffectID     = scenarioPOI.WorldEffectID;
                blobData.PlayerConditionID = scenarioPOI.PlayerConditionID;
                blobData.Points.reserve(scenarioPOI.points.size());

                for (ScenarioPOIPoint const& point : scenarioPOI.points)
                {
                    WorldPackets::Scenario::ScenarioPOIs::POIData::BlobData::POIPointData pointData;
                    pointData.X = point.x;
                    pointData.Y = point.y;
                    blobData.Points.push_back(pointData);
                }

                data.BlobDatas.push_back(std::move(blobData));
            }
        }

        response.PoiInfos.push_back(std::move(data));
    }

    SendPacket(response.Write());
}
