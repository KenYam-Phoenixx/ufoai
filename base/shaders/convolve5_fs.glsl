/**
 * @file convolve5_fs.glsl
 * @brief convolve5 fragment shader.
 */

#ifndef glsl110
	/** After glsl1110 this need to be explicitly declared; used by fixed functionality at the end of the OpenGL pipeline.*/
	out vec4 gl_FragColor;
#endif

uniform sampler2D SAMPLER0;
uniform float COEFFICIENTS[5];
uniform vec2 OFFSETS[5];

/**
 * @brief Fragment shader that convolves a 5 element filter with the specified texture.
 *
 * Orientation of the filter is controlled by "OFFSETS".
 * The filter itself is specified by "COEFFICIENTS".
 */
void main(void) {
	vec2 inColor = gl_TexCoord[0].st;
	vec4 outColor = vec4(0, 0, 0, 1);

	outColor += COEFFICIENTS[0] * texture2D(SAMPLER0, inColor + OFFSETS[0]);
	outColor += COEFFICIENTS[1] * texture2D(SAMPLER0, inColor + OFFSETS[1]);
	outColor += COEFFICIENTS[2] * texture2D(SAMPLER0, inColor + OFFSETS[2]);
	outColor += COEFFICIENTS[3] * texture2D(SAMPLER0, inColor + OFFSETS[3]);
	outColor += COEFFICIENTS[4] * texture2D(SAMPLER0, inColor + OFFSETS[4]);

	gl_FragColor = outColor;
}
