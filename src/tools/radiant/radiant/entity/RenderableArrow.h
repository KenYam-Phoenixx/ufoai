/**
 * @file
 */

/*
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

*/

#ifndef ARROW_H_
#define ARROW_H_

#include "math/line.h"

class RenderableArrow: public OpenGLRenderable
{
		const Ray& m_ray;

	public:
		RenderableArrow (const Ray& ray) :
			m_ray(ray)
		{
		}

		void render (RenderStateFlags state) const
		{
			arrow_draw(m_ray.origin, m_ray.direction);
		}
};

#endif
