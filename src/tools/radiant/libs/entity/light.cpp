/**
 * @file light.cpp
 * @brief Represents any light entity (e.g. light).
 * This entity displays a special 'light' model.
 * The "origin" key directly controls the position of the light model in local space.
 * The "_color" key controls the colour of the light model.
 * The "light" key is visualised with a sphere representing the approximate coverage of the light
 */

/*
 Copyright (C) 2001-2006, William Joseph.
 All Rights Reserved.

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

#include "light.h"

#include <stdlib.h>

#include "cullable.h"
#include "renderable.h"
#include "editable.h"

#include "math/frustum.h"
#include "selectionlib.h"
#include "instancelib.h"
#include "transformlib.h"
#include "entitylib.h"
#include "render.h"
#include "eclasslib.h"
#include "render.h"
#include "stringio.h"
#include "traverselib.h"

#include "targetable.h"
#include "origin.h"
#include "colour.h"
#include "filters.h"
#include "namedentity.h"
#include "keyobservers.h"
#include "namekeys.h"

#include "entity.h"

static void sphere_draw_fill (const Vector3& origin, float radius, int sides)
{
	if (radius <= 0)
		return;

	const double dt = c_2pi / static_cast<double> (sides);
	const double dp = c_pi / static_cast<double> (sides);

	glBegin(GL_TRIANGLES);
	for (int i = 0; i <= sides - 1; ++i) {
		for (int j = 0; j <= sides - 2; ++j) {
			const double t = i * dt;
			const double p = (j * dp) - (c_pi / 2.0);

			{
				Vector3 v(origin + vector3_for_spherical(t, p) * radius);
				glVertex3fv(v);
			}

			{
				Vector3 v(origin + vector3_for_spherical(t, p + dp) * radius);
				glVertex3fv(v);
			}

			{
				Vector3 v(origin + vector3_for_spherical(t + dt, p + dp) * radius);
				glVertex3fv(v);
			}

			{
				Vector3 v(origin + vector3_for_spherical(t, p) * radius);
				glVertex3fv(v);
			}

			{
				Vector3 v(origin + vector3_for_spherical(t + dt, p + dp) * radius);
				glVertex3fv(v);
			}

			{
				Vector3 v(origin + vector3_for_spherical(t + dt, p) * radius);
				glVertex3fv(v);
			}
		}
	}

	{
		const double p = (sides - 1) * dp - (c_pi / 2.0);
		for (int i = 0; i <= sides - 1; ++i) {
			const double t = i * dt;

			{
				Vector3 v(origin + vector3_for_spherical(t, p) * radius);
				glVertex3fv(v);
			}

			{
				Vector3 v(origin + vector3_for_spherical(t + dt, p + dp) * radius);
				glVertex3fv(v);
			}

			{
				Vector3 v(origin + vector3_for_spherical(t + dt, p) * radius);
				glVertex3fv(v);
			}
		}
	}
	glEnd();
}

static void sphere_draw_wire (const Vector3& origin, float radius, int sides)
{
	glBegin(GL_LINE_LOOP);
	for (int i = 0; i <= sides; i++) {
		const double ds = sin((i * 2 * c_pi) / sides);
		const double dc = cos((i * 2 * c_pi) / sides);

		glVertex3f(static_cast<float> (origin[0] + radius * dc), static_cast<float> (origin[1] + radius * ds),
				origin[2]);
	}
	glEnd();

	glBegin(GL_LINE_LOOP);
	for (int i = 0; i <= sides; i++) {
		const double ds = sin((i * 2 * c_pi) / sides);
		const double dc = cos((i * 2 * c_pi) / sides);

		glVertex3f(static_cast<float> (origin[0] + radius * dc), origin[1],
				static_cast<float> (origin[2] + radius * ds));
	}
	glEnd();

	glBegin(GL_LINE_LOOP);
	for (int i = 0; i <= sides; i++) {
		const double ds = sin((i * 2 * c_pi) / sides);
		const double dc = cos((i * 2 * c_pi) / sides);

		glVertex3f(origin[0], static_cast<float> (origin[1] + radius * dc),
				static_cast<float> (origin[2] + radius * ds));
	}
	glEnd();
}

static void light_draw_box_lines (const Vector3& origin, const Vector3 points[8])
{
	//draw lines from the center of the bbox to the corners
	glBegin(GL_LINES);

	glVertex3fv(origin);
	glVertex3fv(points[1]);

	glVertex3fv(origin);
	glVertex3fv(points[5]);

	glVertex3fv(origin);
	glVertex3fv(points[2]);

	glVertex3fv(origin);
	glVertex3fv(points[6]);

	glVertex3fv(origin);
	glVertex3fv(points[0]);

	glVertex3fv(origin);
	glVertex3fv(points[4]);

	glVertex3fv(origin);
	glVertex3fv(points[3]);

	glVertex3fv(origin);
	glVertex3fv(points[7]);

	glEnd();
}

static void light_draw_radius_wire (const Vector3& origin, const float envelope[2])
{
	if (envelope[0] > 0)
		sphere_draw_wire(origin, envelope[0], 24);
	if (envelope[1] > 0)
		sphere_draw_wire(origin, envelope[1], 24);
}

static void light_draw_radius_fill (const Vector3& origin, const float envelope[2])
{
	if (envelope[0] > 0)
		sphere_draw_fill(origin, envelope[0], 16);
	if (envelope[1] > 0)
		sphere_draw_fill(origin, envelope[1], 16);
}

static void light_vertices (const AABB& aabb_light, Vector3 points[6])
{
	Vector3 max(aabb_light.origin + aabb_light.extents);
	Vector3 min(aabb_light.origin - aabb_light.extents);
	Vector3 mid(aabb_light.origin);

	// top, bottom, tleft, tright, bright, bleft
	points[0] = Vector3(mid[0], mid[1], max[2]);
	points[1] = Vector3(mid[0], mid[1], min[2]);
	points[2] = Vector3(min[0], max[1], mid[2]);
	points[3] = Vector3(max[0], max[1], mid[2]);
	points[4] = Vector3(max[0], min[1], mid[2]);
	points[5] = Vector3(min[0], min[1], mid[2]);
}

static void light_draw (const AABB& aabb_light, RenderStateFlags state)
{
	Vector3 points[6];
	light_vertices(aabb_light, points);

	if (state & RENDER_LIGHTING) {
		const float f = 0.70710678f;
		// North, East, South, West
		const Vector3 normals[8] = { Vector3(0, f, f), Vector3(f, 0, f), Vector3(0, -f, f), Vector3(-f, 0, f), Vector3(
				0, f, -f), Vector3(f, 0, -f), Vector3(0, -f, -f), Vector3(-f, 0, -f), };

		glBegin(GL_TRIANGLES);

		glVertex3fv(points[0]);
		glVertex3fv(points[2]);
		glNormal3fv(normals[0]);
		glVertex3fv(points[3]);

		glVertex3fv(points[0]);
		glVertex3fv(points[3]);
		glNormal3fv(normals[1]);
		glVertex3fv(points[4]);

		glVertex3fv(points[0]);
		glVertex3fv(points[4]);
		glNormal3fv(normals[2]);
		glVertex3fv(points[5]);
		glVertex3fv(points[0]);
		glVertex3fv(points[5]);

		glNormal3fv(normals[3]);
		glVertex3fv(points[2]);
		glEnd();
		glBegin(GL_TRIANGLE_FAN);

		glVertex3fv(points[1]);
		glVertex3fv(points[2]);
		glNormal3fv(normals[7]);
		glVertex3fv(points[5]);

		glVertex3fv(points[1]);
		glVertex3fv(points[5]);
		glNormal3fv(normals[6]);
		glVertex3fv(points[4]);

		glVertex3fv(points[1]);
		glVertex3fv(points[4]);
		glNormal3fv(normals[5]);
		glVertex3fv(points[3]);

		glVertex3fv(points[1]);
		glVertex3fv(points[3]);
		glNormal3fv(normals[4]);
		glVertex3fv(points[2]);

		glEnd();
	} else {
		const unsigned int indices[] = { 0, 2, 3, 0, 3, 4, 0, 4, 5, 0, 5, 2, 1, 2, 5, 1, 5, 4, 1, 4, 3, 1, 3, 2 };
		glVertexPointer(3, GL_FLOAT, 0, points);
		glDrawElements(GL_TRIANGLES, sizeof(indices) / sizeof(indices[0]), RenderIndexTypeID, indices);
	}
}

class LightRadii
{
	public:
		float m_radii[2];

	private:
		int m_intensity;
		int m_flags;

		void calculateRadii ()
		{
			m_radii[0] = m_intensity * 1.0f;
			m_radii[1] = m_intensity * 2.0f;
		}

	public:
		LightRadii () :
			m_intensity(0), m_flags(0)
		{
		}

		void primaryIntensityChanged (const std::string& value)
		{
			m_intensity = string::toInt(value);
			calculateRadii();
		}
		typedef MemberCaller1<LightRadii, const std::string&, &LightRadii::primaryIntensityChanged>
				IntensityChangedCaller;
		void flagsChanged (const std::string& value)
		{
			m_flags = string::toInt(value);
			calculateRadii();
		}
		typedef MemberCaller1<LightRadii, const std::string&, &LightRadii::flagsChanged> FlagsChangedCaller;
};

class RenderLightRadiiWire: public OpenGLRenderable
{
		LightRadii& m_radii;
		const Vector3& m_origin;
	public:
		RenderLightRadiiWire (LightRadii& radii, const Vector3& origin) :
			m_radii(radii), m_origin(origin)
		{
		}
		void render (RenderStateFlags state) const
		{
			light_draw_radius_wire(m_origin, m_radii.m_radii);
		}
};

class RenderLightRadiiFill: public OpenGLRenderable
{
		LightRadii& m_radii;
		const Vector3& m_origin;
	public:
		static Shader* m_state;

		RenderLightRadiiFill (LightRadii& radii, const Vector3& origin) :
			m_radii(radii), m_origin(origin)
		{
		}
		void render (RenderStateFlags state) const
		{
			light_draw_radius_fill(m_origin, m_radii.m_radii);
		}
};

class RenderLightRadiiBox: public OpenGLRenderable
{
		const Vector3& m_origin;
	public:
		mutable Vector3 m_points[8];
		static Shader* m_state;

		RenderLightRadiiBox (const Vector3& origin) :
			m_origin(origin)
		{
		}
		void render (RenderStateFlags state) const
		{
			//draw the bounding box of light based on light_radius key
			if ((state & RENDER_FILL) != 0) {
				aabb_draw_flatshade(m_points);
			} else {
				aabb_draw_wire(m_points);
			}

#if 1
			//disable if you dont want lines going from the center of the light bbox to the corners
			light_draw_box_lines(m_origin, m_points);
#endif
		}
};

Shader* RenderLightRadiiFill::m_state = 0;

inline void default_extents (Vector3& extents)
{
	extents = Vector3(8, 8, 8);
}

class ShaderRef
{
		std::string m_name;
		Shader* m_shader;
		void capture ()
		{
			m_shader = GlobalShaderCache().capture(m_name);
		}
		void release ()
		{
			GlobalShaderCache().release(m_name);
		}
	public:
		ShaderRef ()
		{
			capture();
		}
		~ShaderRef ()
		{
			release();
		}
		void setName (const std::string& name)
		{
			release();
			m_name = name;
			capture();
		}
		Shader* get () const
		{
			return m_shader;
		}
};

class LightShader
{
		ShaderRef m_shader;
		void setDefault ()
		{
			m_shader.setName("");
		}
	public:

		LightShader ()
		{
			setDefault();
		}
		void valueChanged (const std::string& value)
		{
			if (value.empty()) {
				setDefault();
			} else {
				m_shader.setName(value);
			}
			SceneChangeNotify();
		}
		typedef MemberCaller1<LightShader, const std::string&, &LightShader::valueChanged> ValueChangedCaller;

		Shader* get () const
		{
			return m_shader.get();
		}
};

inline const BasicVector4<double>& plane3_to_vector4 (const Plane3& self)
{
	return reinterpret_cast<const BasicVector4<double>&> (self);
}

inline BasicVector4<double>& plane3_to_vector4 (Plane3& self)
{
	return reinterpret_cast<BasicVector4<double>&> (self);
}

inline Matrix4 matrix4_from_planes (const Plane3& left, const Plane3& right, const Plane3& bottom, const Plane3& top,
		const Plane3& front, const Plane3& back)
{
	return Matrix4((right.a - left.a) / 2, (top.a - bottom.a) / 2, (back.a - front.a) / 2, right.a - (right.a - left.a)
			/ 2, (right.b - left.b) / 2, (top.b - bottom.b) / 2, (back.b - front.b) / 2, right.b - (right.b - left.b)
			/ 2, (right.c - left.c) / 2, (top.c - bottom.c) / 2, (back.c - front.c) / 2, right.c - (right.c - left.c)
			/ 2, (right.d - left.d) / 2, (top.d - bottom.d) / 2, (back.d - front.d) / 2, right.d - (right.d - left.d)
			/ 2);
}

class Light: public OpenGLRenderable, public Cullable, public Bounded, public Editable, public Snappable
{
		EntityKeyValues m_entity;
		KeyObserverMap m_keyObservers;
		TraversableNodeSet m_traverse;
		IdentityTransform m_transform;

		OriginKey m_originKey;
		Colour m_colour;

		ClassnameFilter m_filter;
		NamedEntity m_named;
		NameKeys m_nameKeys;
		TraversableObserverPairRelay m_traverseObservers;

		LightRadii m_radii;

		RenderLightRadiiWire m_radii_wire;
		RenderLightRadiiFill m_radii_fill;
		RenderLightRadiiBox m_radii_box;
		RenderableNamedEntity m_renderName;

		Vector3 m_lightOrigin;

		Vector3 m_lightTarget;
		bool m_useLightTarget;

		LightShader m_shader;

		AABB m_aabb_light;

		Callback m_transformChanged;
		Callback m_boundsChanged;
		Callback m_evaluateTransform;

		void construct ()
		{
			m_aabb_light.origin = Vector3(0, 0, 0);
			default_extents(m_aabb_light.extents);

			m_keyObservers.insert("classname", ClassnameFilter::ClassnameChangedCaller(m_filter));
			m_keyObservers.insert("targetname", NamedEntity::IdentifierChangedCaller(m_named));
			m_keyObservers.insert("_color", Colour::ColourChangedCaller(m_colour));
			m_keyObservers.insert("origin", OriginKey::OriginChangedCaller(m_originKey));
			m_keyObservers.insert("light", LightRadii::IntensityChangedCaller(m_radii));
			m_keyObservers.insert("spawnflags", LightRadii::FlagsChangedCaller(m_radii));
		}
		void destroy ()
		{
		}

		void updateOrigin ()
		{
			m_boundsChanged();

			GlobalSelectionSystem().pivotChanged();
		}

		void originChanged ()
		{
			m_aabb_light.origin = m_originKey.m_origin;
			updateOrigin();
		}
		typedef MemberCaller<Light, &Light::originChanged> OriginChangedCaller;

		void lightOriginChanged (const char* value)
		{
			originChanged();
		}
		typedef MemberCaller1<Light, const char*, &Light::lightOriginChanged> LightOriginChangedCaller;

		void lightTargetChanged (const char* value)
		{
			m_useLightTarget = !string_empty(value);
			if (m_useLightTarget) {
				read_origin(m_lightTarget, value);
			}
			projectionChanged();
		}
		typedef MemberCaller1<Light, const char*, &Light::lightTargetChanged> LightTargetChangedCaller;

	public:

		Light (EntityClass* eclass, scene::Node& node, const Callback& transformChanged, const Callback& boundsChanged,
				const Callback& evaluateTransform) :
			m_entity(eclass), m_originKey(OriginChangedCaller(*this)), m_colour(Callback()), m_filter(m_entity, node),
					m_named(m_entity), m_nameKeys(m_entity), m_radii_wire(m_radii, m_aabb_light.origin), m_radii_fill(
							m_radii, m_aabb_light.origin), m_radii_box(m_aabb_light.origin), m_renderName(m_named,
							m_aabb_light.origin), m_transformChanged(transformChanged), m_boundsChanged(boundsChanged),
					m_evaluateTransform(evaluateTransform)
		{
			construct();
		}
		Light (const Light& other, scene::Node& node, const Callback& transformChanged, const Callback& boundsChanged,
				const Callback& evaluateTransform) :
			m_entity(other.m_entity), m_originKey(OriginChangedCaller(*this)), m_colour(Callback()), m_filter(m_entity,
					node), m_named(m_entity), m_nameKeys(m_entity), m_radii_wire(m_radii, m_aabb_light.origin),
					m_radii_fill(m_radii, m_aabb_light.origin), m_radii_box(m_aabb_light.origin), m_renderName(m_named,
							m_aabb_light.origin), m_transformChanged(transformChanged), m_boundsChanged(boundsChanged),
					m_evaluateTransform(evaluateTransform)
		{
			construct();
		}
		~Light ()
		{
			destroy();
		}

		InstanceCounter m_instanceCounter;
		void instanceAttach (const scene::Path& path)
		{
			if (++m_instanceCounter.m_count == 1) {
				m_filter.instanceAttach();
				m_entity.instanceAttach(path_find_mapfile(path.begin(), path.end()));
				m_entity.attach(m_keyObservers);
			}
		}
		void instanceDetach (const scene::Path& path)
		{
			if (--m_instanceCounter.m_count == 0) {
				m_entity.detach(m_keyObservers);
				m_entity.instanceDetach(path_find_mapfile(path.begin(), path.end()));
				m_filter.instanceDetach();
			}
		}

		EntityKeyValues& getEntity ()
		{
			return m_entity;
		}
		const EntityKeyValues& getEntity () const
		{
			return m_entity;
		}

		scene::Traversable& getTraversable ()
		{
			return m_traverse;
		}
		Namespaced& getNamespaced ()
		{
			return m_nameKeys;
		}
		const NamedEntity& getNameable () const
		{
			return m_named;
		}
		NamedEntity& getNameable ()
		{
			return m_named;
		}
		TransformNode& getTransformNode ()
		{
			return m_transform;
		}

		void attach (scene::Traversable::Observer* observer)
		{
			m_traverseObservers.attach(*observer);
		}
		void detach (scene::Traversable::Observer* observer)
		{
			m_traverseObservers.detach(*observer);
		}

		void render (RenderStateFlags state) const
		{
			//aabb_draw(m_aabb_light, state);
			light_draw(m_aabb_light, state);
		}

		VolumeIntersectionValue intersectVolume (const VolumeTest& volume, const Matrix4& localToWorld) const
		{
			return volume.TestAABB(m_aabb_light, localToWorld);
		}

		// cache
		const AABB& localAABB () const
		{
			return m_aabb_light;
		}

		mutable Matrix4 m_projectionOrientation;

		void renderSolid (Renderer& renderer, const VolumeTest& volume, const Matrix4& localToWorld, bool selected) const
		{
			renderer.SetState(m_entity.getEntityClass().m_state_wire, Renderer::eWireframeOnly);
			renderer.SetState(m_colour.state(), Renderer::eFullMaterials);
			renderer.addRenderable(*this, localToWorld);

			if ((g_forceLightRadii || (selected && g_lightRadii)) && string_empty(m_entity.getKeyValue("target"))) {
				if (renderer.getStyle() == Renderer::eFullMaterials) {
					renderer.SetState(RenderLightRadiiFill::m_state, Renderer::eFullMaterials);
					renderer.Highlight(Renderer::ePrimitive, false);
					renderer.addRenderable(m_radii_fill, localToWorld);
				} else {
					renderer.addRenderable(m_radii_wire, localToWorld);
				}
			}

			renderer.SetState(m_entity.getEntityClass().m_state_wire, Renderer::eFullMaterials);
		}
		void renderWireframe (Renderer& renderer, const VolumeTest& volume, const Matrix4& localToWorld, bool selected) const
		{
			renderSolid(renderer, volume, localToWorld, selected);
			if (g_showNames) {
				renderer.addRenderable(m_renderName, localToWorld);
			}
		}

		void testSelect (Selector& selector, SelectionTest& test, const Matrix4& localToWorld)
		{
			test.BeginMesh(localToWorld);

			SelectionIntersection best;
			aabb_testselect(m_aabb_light, test, best);
			if (best.valid()) {
				selector.addIntersection(best);
			}
		}

		void translate (const Vector3& translation)
		{
			m_aabb_light.origin = origin_translated(m_aabb_light.origin, translation);
		}
		void rotate (const Quaternion& rotation)
		{
		}
		void snapto (float snap)
		{
			m_originKey.m_origin = origin_snapped(m_originKey.m_origin, snap);
			m_originKey.write(&m_entity);
		}
		void setLightRadius (const AABB& aabb)
		{
			m_aabb_light.origin = aabb.origin;
		}
		void transformLightRadius (const Matrix4& transform)
		{
			matrix4_transform_point(transform, m_aabb_light.origin);
		}
		void revertTransform ()
		{
			m_aabb_light.origin = m_originKey.m_origin;
		}
		void freezeTransform ()
		{
			m_originKey.m_origin = m_aabb_light.origin;
			m_originKey.write(&m_entity);
		}
		void transformChanged ()
		{
			revertTransform();
			m_evaluateTransform();
			updateOrigin();
		}
		typedef MemberCaller<Light, &Light::transformChanged> TransformChangedCaller;

		mutable Matrix4 m_localPivot;
		const Matrix4& getLocalPivot () const
		{
			m_localPivot = g_matrix4_identity;
			m_localPivot.t().getVector3() = m_aabb_light.origin;
			return m_localPivot;
		}

		const Vector3& colour () const
		{
			return m_colour.m_colour;
		}

		void projectionChanged ()
		{
			SceneChangeNotify();
		}
		const AABB& aabb () const
		{
			return m_aabb_light;
		}

		Shader* getShader () const
		{
			return m_shader.get();
		}
};

class LightInstance: public TargetableInstance,
		public TransformModifier,
		public Renderable,
		public SelectionTestable,
		public RendererLight
{
		class TypeCasts
		{
				InstanceTypeCastTable m_casts;
			public:
				TypeCasts ()
				{
					m_casts = TargetableInstance::StaticTypeCasts::instance().get();
					InstanceContainedCast<LightInstance, Bounded>::install(m_casts);
					InstanceStaticCast<LightInstance, Renderable>::install(m_casts);
					InstanceStaticCast<LightInstance, SelectionTestable>::install(m_casts);
					InstanceStaticCast<LightInstance, Transformable>::install(m_casts);
					InstanceIdentityCast<LightInstance>::install(m_casts);
				}
				InstanceTypeCastTable& get ()
				{
					return m_casts;
				}
		};

		Light& m_contained;
	public:
		typedef LazyStatic<TypeCasts> StaticTypeCasts;

		Bounded& get (NullType<Bounded> )
		{
			return m_contained;
		}

		STRING_CONSTANT(Name, "LightInstance");

		LightInstance (const scene::Path& path, scene::Instance* parent, Light& contained) :
			TargetableInstance(path, parent, this, StaticTypeCasts::instance().get(), contained.getEntity(), *this),
					TransformModifier(Light::TransformChangedCaller(contained), ApplyTransformCaller(*this)),
					m_contained(contained)
		{
			m_contained.instanceAttach(Instance::path());

			StaticRenderableConnectionLines::instance().attach(*this);
		}
		~LightInstance ()
		{
			StaticRenderableConnectionLines::instance().detach(*this);

			m_contained.instanceDetach(Instance::path());
		}
		void renderSolid (Renderer& renderer, const VolumeTest& volume) const
		{
			m_contained.renderSolid(renderer, volume, Instance::localToWorld(), getSelectable().isSelected());
		}
		void renderWireframe (Renderer& renderer, const VolumeTest& volume) const
		{
			m_contained.renderWireframe(renderer, volume, Instance::localToWorld(), getSelectable().isSelected());
		}
		void testSelect (Selector& selector, SelectionTest& test)
		{
			m_contained.testSelect(selector, test, Instance::localToWorld());
		}

		void testSelect (Selector& selector, SelectionTest& test, const Matrix4& localToWorld)
		{
			m_contained.testSelect(selector, test, localToWorld);
		}

		void evaluateTransform ()
		{
			if (getType() == TRANSFORM_PRIMITIVE) {
				m_contained.translate(getTranslation());
				m_contained.rotate(getRotation());
			}
		}
		void applyTransform ()
		{
			m_contained.revertTransform();
			evaluateTransform();
			m_contained.freezeTransform();
		}
		typedef MemberCaller<LightInstance, &LightInstance::applyTransform> ApplyTransformCaller;

		void lightChanged ()
		{
			GlobalShaderCache().changed(*this);
		}
		typedef MemberCaller<LightInstance, &LightInstance::lightChanged> LightChangedCaller;

		Shader* getShader () const
		{
			return m_contained.getShader();
		}
		const Vector3& colour () const
		{
			return m_contained.colour();
		}
};

class LightNode: public scene::Node,
		public scene::Instantiable,
		public scene::Cloneable,
		public scene::Traversable::Observer,
		public Nameable
{
		class TypeCasts
		{
				NodeTypeCastTable m_casts;
			public:
				TypeCasts ()
				{
					NodeContainedCast<LightNode, Editable>::install(m_casts);
					NodeContainedCast<LightNode, Snappable>::install(m_casts);
					NodeContainedCast<LightNode, TransformNode>::install(m_casts);
					NodeContainedCast<LightNode, Entity>::install(m_casts);
					NodeContainedCast<LightNode, Namespaced>::install(m_casts);
				}
				NodeTypeCastTable& get ()
				{
					return m_casts;
				}
		};

		InstanceSet m_instances;
		Light m_contained;

		void construct ()
		{
		}
		void destroy ()
		{
		}
	public:
		typedef LazyStatic<TypeCasts> StaticTypeCasts;

		scene::Traversable& get (NullType<scene::Traversable> )
		{
			return m_contained.getTraversable();
		}
		Editable& get (NullType<Editable> )
		{
			return m_contained;
		}
		Snappable& get (NullType<Snappable> )
		{
			return m_contained;
		}
		TransformNode& get (NullType<TransformNode> )
		{
			return m_contained.getTransformNode();
		}
		Entity& get (NullType<Entity> )
		{
			return m_contained.getEntity();
		}
		Namespaced& get (NullType<Namespaced> )
		{
			return m_contained.getNamespaced();
		}

		LightNode (EntityClass* eclass) :
			scene::Node(this, StaticTypeCasts::instance().get()), m_contained(eclass, *this,
					InstanceSet::TransformChangedCaller(m_instances), InstanceSet::BoundsChangedCaller(m_instances),
					InstanceSetEvaluateTransform<LightInstance>::Caller(m_instances))
		{
			construct();
		}
		LightNode (const LightNode& other) :
			scene::Node(this, StaticTypeCasts::instance().get()), scene::Instantiable(other), scene::Cloneable(other),
					scene::Traversable::Observer(other), Nameable(other), m_contained(other.m_contained, *this,
							InstanceSet::TransformChangedCaller(m_instances), InstanceSet::BoundsChangedCaller(
									m_instances), InstanceSetEvaluateTransform<LightInstance>::Caller(m_instances))
		{
			construct();
		}
		~LightNode ()
		{
			destroy();
		}

		scene::Node& node ()
		{
			return *this;
		}

		scene::Node& clone () const
		{
			return (new LightNode(*this))->node();
		}

		void insert (scene::Node& child)
		{
			m_instances.insert(child);
		}
		void erase (scene::Node& child)
		{
			m_instances.erase(child);
		}

		scene::Instance* create (const scene::Path& path, scene::Instance* parent)
		{
			return new LightInstance(path, parent, m_contained);
		}
		void forEachInstance (const scene::Instantiable::Visitor& visitor)
		{
			m_instances.forEachInstance(visitor);
		}
		void insert (scene::Instantiable::Observer* observer, const scene::Path& path, scene::Instance* instance)
		{
			m_instances.insert(observer, path, instance);
		}
		scene::Instance* erase (scene::Instantiable::Observer* observer, const scene::Path& path)
		{
			return m_instances.erase(observer, path);
		}
		// Nameable implementation
		std::string name () const
		{
			return m_contained.getNameable().name();
		}

		void attach (const NameCallback& callback)
		{
			m_contained.getNameable().attach(callback);
		}

		void detach (const NameCallback& callback)
		{
			m_contained.getNameable().detach(callback);
		}
};

void Light_Construct ()
{
	RenderLightRadiiFill::m_state = GlobalShaderCache().capture("$Q3MAP2_LIGHT_SPHERE");
}
void Light_Destroy ()
{
	GlobalShaderCache().release("$Q3MAP2_LIGHT_SPHERE");
}

scene::Node& New_Light (EntityClass* eclass)
{
	return (new LightNode(eclass))->node();
}
