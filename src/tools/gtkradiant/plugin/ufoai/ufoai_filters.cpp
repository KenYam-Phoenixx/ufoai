/*
This file is part of GtkRadiant.

GtkRadiant is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

GtkRadiant is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with GtkRadiant; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "ufoai_filters.h"

#include "ifilter.h"

#include <list>

/**
 * @brief
 */
class LevelFilterWrapper : public Filter
{
	bool m_active;
	bool m_invert;
	LevelFilter& m_filter;
public:
	LevelFilterWrapper(LevelFilter& filter, bool invert) : m_invert(invert), m_filter(filter)
	{
	}
	void setActive(bool active)
	{
		m_active = active;
	}
	bool active()
	{
		return m_active;
	}
	bool filter(const Level& level)
	{
		return m_invert ^ m_filter.filter(level);
	}
};


typedef std::list<LevelFilterWrapper> LevelFilters;
LevelFilters g_levelFilters;

/**
 * @brief
 */
void add_level_filter(LevelFilter& filter, int mask, bool invert)
{
	g_levelFilters.push_back(LevelFilterWrapper(filter, invert));
	GlobalFilterSystem().addFilter(g_levelFilters.back(), mask);
}

/**
 * @brief
 */
bool level_filtered(Level& level)
{
	for(LevelFilters::iterator i = g_levelFilters.begin(); i != g_levelFilters.end(); ++i)
	{
		if((*i).active() && (*i).filter(level))
		{
			return true;
		}
	}
	return false;
}

static int activeLevelFilter = 0;
static int activeSteponFilter = 0;

/**
 * @brief Deactivates the level filter for the given level
 */
void deactive_filter_level(int level)
{
	globalOutputStream() << "deactive_filter_level: " << level << "\n";
	activeLevelFilter = 0;
	globalOutputStream() << "TODO: Implement me\n";
}

/**
 * @brief Activates the level filter for the given level
 * @param[in] level Which level to show?
 */
void filter_level(int level)
{
	if (activeLevelFilter > 0)
		deactive_filter_level(activeLevelFilter);
	if (activeLevelFilter != level)
	{
		activeLevelFilter = level;
		globalOutputStream() << "filter_level: " << activeLevelFilter << "\n";
		globalOutputStream() << "TODO: Implement me\n";
	}
	else
	{
		globalOutputStream() << "Level filters deactivated\n";
	}
}

/**
 * @brief Deactivates the stepon filter
 */
void deactive_filter_stepon(void)
{
	globalOutputStream() << "deactive_filter_stepon\n";
	activeSteponFilter = 0;
	globalOutputStream() << "TODO: Implement me\n";
}

/**
 * @brief Activates the level filter for the given level
 * @param[in] level Which level to show?
 */
void filter_stepon(void)
{
	if (activeSteponFilter == 1)
		deactive_filter_stepon();
	else
	{
		activeSteponFilter = 1;
		globalOutputStream() << "filter_stepon\n";
		globalOutputStream() << "TODO: Implement me\n";
	}
}

