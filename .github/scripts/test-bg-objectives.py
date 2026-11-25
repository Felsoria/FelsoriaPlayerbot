#!/usr/bin/env python3
"""Build the production commanders against an isolated engine boundary."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
with tempfile.TemporaryDirectory(prefix="felsoria-objectives-") as work:
    work = Path(work)
    for name in ("CommandBG.h", "BattlegroundTwinPeaks.h", "BattlegroundKotmoguTemplate.h",
                 "BattlegroundMap.h", "BotAI.h", "GameObject.h", "Map.h", "ObjectAccessor.h"):
        (work / name).write_text("// Engine declarations are supplied by the forced test include.\n")
    twin = (root / "src/server/game/Battlegrounds/Zones/BattlegroundTwinPeaks.cpp").read_text()
    capture = twin[twin.index("void BattlegroundTwinPeaks::_CheckPositions("):
                   twin.index("bool BattlegroundTwinPeaks::UpdatePlayerScore(")]
    source = work / "objectives.cpp"
    dispatch = (root / "src/server/game/Maps/BattlegroundMap.cpp").read_text()
    helpers = dispatch[dispatch.index("    bool IsFelsoriaNormalWSG("):
                       dispatch.index("BattlegroundMap::BattlegroundMap(")]
    method = dispatch[dispatch.index("void BattlegroundMap::InsureCommander("):
                      dispatch.index("void BattlegroundMap::InitCommander(")]
    source.write_text('#include "' + (root / ".github/scripts/test-bg-objectives.cpp").as_posix() +
                      '"\n' + capture + "\nnamespace {\n" + helpers + "\n" + method)
    subprocess.run(["g++", "-std=c++14", "-Wall", "-Wextra", "-Werror",
                    "-include", str(root / ".github/tests/objective-engine.h"), "-I", str(work),
                    str(source), "-o", str(work / "objectives")], check=True)
    subprocess.run([str(work / "objectives")], check=True)
    movement = (root / "src/server/game/AI/PlayerAI/BotBGAIMovement.cpp").read_text()
    body = movement[movement.index("bool BotBGAIMovement::MoveObjectiveTo("):
                    movement.index("void BotBGAIMovement::ApplyFinishPath(")]
    source.write_text((root / ".github/scripts/test-objective-movement.cpp").read_text() + "\n" + body)
    subprocess.run(["g++", "-std=c++14", "-Wall", "-Wextra", "-Werror", str(source),
                    "-o", str(work / "movement")], check=True)
    subprocess.run([str(work / "movement")], check=True)
