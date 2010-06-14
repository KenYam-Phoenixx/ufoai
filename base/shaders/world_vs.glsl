/* world vertex shader */

uniform float OFFSET;
uniform int BUMPMAP;
uniform int ANIMATE;

/* from includes:
varying vec3 point;
varying vec3 normal;

attribute vec4 TANGENT;
uniform int DYNAMICLIGHTS;

attribute vec4 NEXT_FRAME_VERTS;
attribute vec4 NEXT_FRAME_NORMALS;
attribute vec4 NEXT_FRAME_TANGENTS;
uniform float TIME;
varying vec4 Tangent;

varying vec3 eyedir;

#if r_dynamic_lights
varying vec3 lightDirs[];
#endif
varying vec3 staticLightDir;

varying float fog; 
*/


#include "lerp_vs.glsl"
#include "light_vs.glsl"
#include "bump_vs.glsl"
#include "fog_vs.glsl"

/**
 * main
 */
void main (void)
{
	if (ANIMATE > 0) {
		lerpVertex();
	} else {
		Vertex = gl_Vertex;
		Normal = gl_Normal;
		Tangent = TANGENTS;
	}

	/* mvp transform into clip space */
	gl_Position = gl_ProjectionMatrix * gl_ModelViewMatrix * vec4(Vertex);

	/* pass texcoords through */
	gl_TexCoord[0] = gl_MultiTexCoord0 + OFFSET;
	gl_TexCoord[1] = gl_MultiTexCoord1 + OFFSET;

	LightVertex();

#if r_bumpmap
	if (BUMPMAP > 0 || DYNAMICLIGHTS > 0)
		BumpVertex();
#endif

#if r_fog
	FogVertex();
#endif
}
