uniform sampler2D SAMPLER0;
uniform sampler2D SAMPLER1;

uniform float GLOWSCALE;

void main (void)
{
	vec4 color = texture2D(SAMPLER0, gl_TexCoord[0].st);
	gl_FragData[0].rgb = color.rgb * color.a;
	gl_FragData[0].a = 1.0;

	color = texture2D(SAMPLER1, gl_TexCoord[0].st);
	gl_FragData[1].rgb = color.rgb * color.a * GLOWSCALE;
	gl_FragData[1].a = 1.0;
}
