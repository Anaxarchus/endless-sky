/* ModApi.h
Copyright (c) 2026 by Anaxarchus

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program. If not, see <https://www.gnu.org/licenses/>.
*/

#pragma once

#include <list>
#include <memory>

class PlayerInfo;
class Ship;
class ShipEvent;
class UI;



// This is the facade for the modding package.
// Each method gets called per frame and everything else fans out from there.
class ModApi {
public:
	// Called from Engine::Step every frame while the game screen is shown, with the
	// calculation thread paused. isFlying is false while landed or paused.
	static void EngineFrame(PlayerInfo &player, const std::list<std::shared_ptr<Ship>> &ships,
		std::list<std::shared_ptr<Ship>> &newShips, const std::list<ShipEvent> &events, int step, bool isFlying);

	// Called from MainPanel::Step right after the engine step, still paused.
	// isActive is false when another panel is on top of the main panel.
	static void PanelFrame(PlayerInfo &player, UI &ui, bool isActive);
};
