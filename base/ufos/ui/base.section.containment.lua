--!usr/bin/lua

--[[
-- @file
-- @brief Alien Containment base menu section
--]]

--[[
Copyright (C) 2002-2025 UFO: Alien Invasion.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
--]]

--[[
 - @todo Add stored aliens to the content section
 - @todo Change aliencont screen to change the "map area" only
--]]

do
	require("ufox.lua")
	require("base.section.lua")

	local sectionContainment = {
		register = function (root_node, base_idx)
			local section = base.build_section(root_node, "containment", "_Alien Containment", "icons/aliens")
			section:child("header").on_click = function (sender)
				ufo.push_window("aliencont", nil, nil)
			end
		end,
	}

	return sectionContainment
end
