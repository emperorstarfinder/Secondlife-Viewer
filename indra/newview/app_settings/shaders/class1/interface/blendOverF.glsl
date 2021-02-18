/** 
 * @file class1/interface/blendOverF.glsl
 *
 * $LicenseInfo:firstyear=2020&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2020, Linden Research, Inc.
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 * 
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */


#ifdef DEFINE_GL_FRAGCOLOR
out vec4 frag_color;
#else
#define frag_color gl_FragColor
#endif

uniform sampler2D diffuseMap;       // Base map
//uniform sampler2D altDiffuseMap;    // Overlay map, alpha-blended over base map

VARYING vec2 vary_texcoord0;

void main() 
{
    vec4 base_color = texture2D(diffuseMap, vary_texcoord0.xy);
    frag_color = base_color.grba;
    //vec4 overlay_color = texture2D(altDiffuseMap, vary_texcoord0.xy);

 //   frag_color.rgb = mix(base_color.rgb, overlay_color.rgb, overlay_color.a);
 //   frag_color.a = max(base_color.a, overlay_color.a);
}
