#pragma once
// GLSL for the backlit ink background, carried over unchanged from the HTML prototype (minus the ES precision line).
namespace hlshaders {
static const char* vertex = R"GLSL(
attribute vec2 a;
void main(){ gl_Position = vec4(a, 0., 1.); }
)GLSL";
static const char* substance = R"GLSL(

  uniform vec2 res; uniform float t, tm; uniform vec2 drift; uniform float rot, warp, solid, smoke, wet, grit, lightY;
  float hash(vec2 p){ p = fract(p*vec2(123.34, 456.21)); p += dot(p, p + 45.32); return fract(p.x*p.y); }
  float noise(vec2 p){ vec2 i = floor(p), f = fract(p), u = f*f*(3.-2.*f);
    return mix(mix(hash(i), hash(i+vec2(1.,0.)), u.x), mix(hash(i+vec2(0.,1.)), hash(i+vec2(1.,1.)), u.x), u.y); }
  float fbm(vec2 p){ float v = 0., a = .5; mat2 m = mat2(.8,.6,-.6,.8);
    for(int i=0;i<5;i++){ v += a*noise(p); p = m*p*2.03 + vec2(1.7, 9.2); a *= .5; } return v; }
  float field(vec2 p, out vec2 qo){
    vec2 q = vec2(fbm(p + vec2(0., t*.008)), fbm(p + vec2(5.2, 1.3) - vec2(t*.006, 0.)));
    vec2 r = vec2(fbm(p + 3.0*warp*q + vec2(1.7, 9.2) + t*.007), fbm(p + 3.0*warp*q + vec2(8.3, 2.8) - t*.005));
    qo = q; return fbm(p + 3.2*warp*r);
  }
  void main(){
    vec2 fc = gl_FragCoord.xy, uv = fc/res.y;
    vec2 p = uv*1.45 + drift;
    p.y += t*.006;                                        // the mass sags
    vec2 q, q2; float f = field(p, q);
    float e = .006;
    float fx = field(p + vec2(e, 0.), q2), fy = field(p + vec2(0., e), q2);
    // solidity: a slow field that decides, region by region, what the material is. Where it is high the mass is a thick
    // liquid / crusted earth with hard edges and relief; where it is low it thins into soft, translucent, smoke-like drifts.
    float S = smoothstep(.32, .68, fbm(uv*1.25 + vec2(-tm*.004, tm*.003) + 21.) + solid);
    float th = .53;
    float body = smoothstep(th - mix(.13, .01, S), th + mix(.06, .01, S), f);
    body *= mix(clamp(fbm(p*5. + q*4. - vec2(0., t*.01))*1.5 - .15, 0., 1.)*(.35 + .9*smoke), 1., S);   // smoke regions break into wisps
    vec3 n = normalize(vec3(-(fx - f)/e*.07*mix(.3, 1., S), -(fy - f)/e*.07*mix(.3, 1., S), 1.));
    vec3 L = normalize(vec3(-.45, lightY, .65));
    float diff = clamp(dot(n, L), 0., 1.);
    float spec = pow(clamp(dot(reflect(-L, n), vec3(0., 0., 1.)), 0., 1.), 14.)*S*wet;
    float depth = smoothstep(th, .8, f);
    float rim = 1. - smoothstep(0., .03, abs(f - th));
    float lum = body*(.09 + .38*diff - .07*depth + .4*spec) + rim*.24*S*step(.5, body);

    // rust blooms: a slower field of its own, bent by the body so it follows the folds
    float rr = fbm(p*2.1 + q*1.6 + vec2(3.1, -t*.002));
    float rust = smoothstep(.5 - rot*.26, .74 - rot*.26, rr)*body*(.35 + .65*S);
    float flake = fbm(p*16. + q*3.);                       // flaking oxide
        // stains: runs of rust that drip down from the blooms
    float run = smoothstep(.5, .78, fbm(vec2(uv.x*22., uv.y*1.1 + t*.002) + q*2.))*smoothstep(.3, .6, fbm(p*2.1 + q*1.6 + vec2(3.1, .25)))*body;

    // erosion: the deepest rust eats through the mass
    float grt = ((noise(fc*.5) - .5)*.07 + (noise(fc*1.4) - .5)*.035)*(.4 + grit);
    float eat = rr*.75 + fbm(p*5. + 11.)*.35 + grt;
    float tEat = .82 - rot*.3;
    float gw = mix(.07, .012, S);                          // hard torn edges in the thick parts, dissolving ones in the smoke
    float gone = smoothstep(tEat - gw, tEat + gw, eat)*body;
    float crust = smoothstep(tEat - .06, tEat - .005, eat)*(1. - gone)*body;
    // pitting gathers where the rot is about to break through, so the holes are announced before they open
    float pz = smoothstep(tEat - .2, tEat - .03, eat);
    float pit = smoothstep(.86 - .14*pz, .9 - .12*pz, noise(fc*.16 + q*30.)*.7 + noise(fc*.45)*.3)*pz*body;

    vec3 ash = vec3(.35, .335, .32)*lum*mix(1.5, 1., S);
    vec3 ox = mix(vec3(.14, .065, .045), vec3(.44, .23, .15), flake)*(.3 + .7*diff) + spec*vec3(.12, .09, .07);
    vec3 col = mix(ash, ox*body, rust*.9);
    col *= 1. - pit*min(1., .5 + .5*grit);
    col = mix(col, col*vec3(.62, .48, .42), run*.6);
    col = mix(col, vec3(.13, .07, .05)*(.5 + flake), crust*.7);
    col *= 1. - gone;
    // the edges sink into black
    vec2 c = uv - vec2(.5*res.x/res.y, .45);
    col *= .5 + .5*smoothstep(1.2, .2, length(c*vec2(.8, 1.)));
    // washed out: bleach the colour, lift the blacks, flatten the contrast, add fine grain
    float g = dot(col, vec3(.333));
    col = mix(vec3(g), col, .7);
    col = vec3(.028, .026, .025) + col*.74;
    col += (hash(fc + fract(t*.37)*91.) - .5)*.016;
    // ink density: continuous, so the light shows every gradient of the substance; rust and pits are more opaque
    float dens = clamp((smoothstep(th - .16, th + .22, f)*mix(.55, 1., S) + rust*.25 + pit*.4)*(1. - gone) + crust*.35, 0., 1.);
    // texture inside the ink: pigment granulation (clumps at two scales), fine fibres, and hairline cracks.
    // It only shows where light passes through, which is exactly where you look.
    float gran = fbm(p*26. + q*6.);
    float fib = noise(vec2(dot(fc, vec2(.8, .6))*.04, dot(fc, vec2(-.6, .8))*.9));
    float crack = 1. - smoothstep(0., .028, abs(fbm(p*5. + q*2. + 31.) - .5));
    float band = smoothstep(.02, .25, dens)*(1. - smoothstep(.55, .95, dens));     // the thin, half-lit part of the ink
    dens = clamp(dens*(.55 + .9*gran) + (gran - .5)*.8*band + (fib - .5)*.2*dens, 0., 1.);
    dens *= 1. - crack*.75*smoothstep(.25, .6, dens);   // dried ink cracks, and light leaks through the cracks
    gl_FragColor = vec4(col, dens);
  }
)GLSL";
static const char* light = R"GLSL(

  uniform sampler2D tex; uniform vec2 res, lp; uniform float t, power, K;
  float hash(vec2 p){ p = fract(p*vec2(123.34, 456.21)); p += dot(p, p + 45.32); return fract(p.x*p.y); }
  float noise(vec2 p){ vec2 i = floor(p), f = fract(p), u = f*f*(3.-2.*f);
    return mix(mix(hash(i), hash(i+vec2(1.,0.)), u.x), mix(hash(i+vec2(0.,1.)), hash(i+vec2(1.,1.)), u.x), u.y); }
  float src(vec2 s, float asp){ vec2 d = (s - lp)*vec2(mix(1., asp, .35), 1.); float r2 = dot(d, d); return exp(-r2*2.4)*.8 + exp(-r2*11.)*.25 + .1; }   // a large, soft source stretched to the window's shape, so it reaches every edge
  void main(){
    vec2 uv = gl_FragCoord.xy/res; float asp = res.x/res.y;
    vec4 A = texture2D(tex, uv);
    // fine pigment grain at native resolution, strongest in the thin, half-lit ink
    vec2 gc = gl_FragCoord.xy;
    float band = smoothstep(.02, .25, A.a)*(1. - smoothstep(.55, .95, A.a));
    float fg = hash(gc)*.5 + noise(gc*.5)*.3 + noise(gc*.17)*.2;
    A.a = clamp(A.a + (fg - .5)*.55*band, 0., 1.);
    // the substance as ink on a lightbox: light is absorbed exponentially with thickness (Beer–Lambert), and the ink
    // absorbs blue first, so thin films pass a dim rust-brown and thick parts go black
    vec3 absorb = vec3(2.6, 3.4, 4.3);
    vec3 T = exp(-absorb*A.a*2.1);
    float L = src(uv, asp);
    // a little scattering: light that gets through nearby areas bleeds a short way toward the light
    const int STEPS = 28;
    vec2 st = (lp - uv)/float(STEPS); vec2 s = uv + st*hash(gl_FragCoord.xy + t);
    vec3 acc = vec3(0.); float w = 1., ws = 0.;
    for(int i=0;i<STEPS;i++){ s += st; float a = texture2D(tex, s).a; acc += exp(-absorb*a*2.1)*src(s, asp)*w; ws += w; w *= .965; }
    acc /= ws;
    vec3 lc = vec3(.70, .68, .64)*power*.78;
    // rays: streaks fanning out from the light, slowly turning and breathing. They are only visible where the light
    // actually gets through (acc), so they look like shafts escaping through gaps in the ink
    vec2 d = (uv - lp)*vec2(asp, 1.); float ang = atan(d.y, d.x), rr = length(d);
    float rays = noise(vec2(ang*9. + t*.012, t*.05))*.6 + noise(vec2(ang*23. - t*.02, t*.08 + 7.))*.4;
    rays = pow(smoothstep(.35, .95, rays), 1.6)*smoothstep(.05, .35, rr)*exp(-rr*1.1);
    // the light itself passes through a fibrous, uneven diffuser, so even the open areas are not clean
    vec2 pc = gc/K; float paper = .78 + .14*noise(pc*.018 + 5.) + .1*noise(pc*.07) + .06*(hash(gc) - .5);
    vec3 col = lc*L*T*.95*paper + lc*acc*(.18 + 1.2*rays) + A.rgb*.08;
    float vig = smoothstep(.95, .15, length(uv - vec2(.5)));   // follows the window's own shape: the edges only dim, they never go dead
    col *= .55 + .45*vig;
    // crushed, suffocating darks: a steep curve that pushes the shadows into black and, because it acts per channel,
    // leaves what remains of the dark tones a deeper, more saturated brown-red instead of grey
    col = max(col - .012, 0.);
    col = pow(col/.5, vec3(1.9, 2.05, 2.2))*.5;
    col = min(col, vec3(.52));
    col *= .9 + .2*hash(gc + fract(t*.7)*57.);   // grain that multiplies, so the blacks stay black
    gl_FragColor = vec4(col, 1.);
  }
)GLSL";
}
