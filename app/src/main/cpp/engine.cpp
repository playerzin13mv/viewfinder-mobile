// Viewfinder Mobile - V0.2 ALPHA - motor nativo (C++ / OpenGL ES 3.0)
#include <jni.h>
#include <GLES3/gl3.h>
#include <android/log.h>
#include <cmath>
#include <vector>
#include <mutex>
#include <atomic>
#include <algorithm>

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "VF", __VA_ARGS__)

namespace {

// ---------------------------------------------------------------- matematica
struct V3 { float x, y, z; };
V3 rotYv(V3 v, float a) { float c = cosf(a), s = sinf(a); return { c * v.x + s * v.z, v.y, -s * v.x + c * v.z }; }
V3 rotXv(V3 v, float a) { float c = cosf(a), s = sinf(a); return { v.x, c * v.y - s * v.z, s * v.y + c * v.z }; }
V3 toLocal(V3 d, float yaw, float pitch) { return rotXv(rotYv(d, -yaw), -pitch); }
V3 toWorld(V3 r, float yaw, float pitch) { return rotYv(rotXv(r, pitch), yaw); }

struct M4 { float m[16]; };
M4 ident() { M4 r{}; r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.f; return r; }
M4 mul(const M4& a, const M4& b) {
    M4 r{};
    for (int c = 0; c < 4; c++)
        for (int rr = 0; rr < 4; rr++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a.m[k * 4 + rr] * b.m[c * 4 + k];
            r.m[c * 4 + rr] = s;
        }
    return r;
}
M4 rotXm(float a) { M4 r = ident(); float c = cosf(a), s = sinf(a); r.m[5] = c; r.m[6] = s; r.m[9] = -s; r.m[10] = c; return r; }
M4 rotYm(float a) { M4 r = ident(); float c = cosf(a), s = sinf(a); r.m[0] = c; r.m[2] = -s; r.m[8] = s; r.m[10] = c; return r; }
M4 persp(float fovy, float asp, float n, float f) {
    M4 r{}; float t = 1.f / tanf(fovy * .5f);
    r.m[0] = t / asp; r.m[5] = t; r.m[10] = (f + n) / (n - f); r.m[11] = -1.f; r.m[14] = 2.f * f * n / (n - f);
    return r;
}

// ---------------------------------------------------------------- dados
const float RAD = .38f, PH = 1.75f, EYE = 1.55f, FOVY = 1.15f;
const int MAXP = 4;
const float GX = 10.f, GZ = -14.f, GY = 6.1f;
const float SUNX = .4529f, SUNY = .7850f, SUNZ = .4227f;

struct Box { float x, y, z, hx, hy, hz, yaw, r, g, b; bool base; int group; };
struct Item { V3 rel; float hx, hy, hz, ya, r, g, b; };
struct Photo { std::vector<Item> items; GLuint tex = 0; int w = 0, h = 0; V3 pos{0, 0, 0}; float yaw = 0, pitch = 0; };
struct Player { float x = 0, y = 0, z = 8, vy = 0, yaw = 0, pitch = 0; bool g = false; };

std::vector<Box> solids;
std::vector<Photo> photos;
std::vector<int> groups;
int nextGroup = 1, held = -1, SW = 1, SH = 1;
bool won = false, levelBuilt = false;
float gTime = 0;
Player P;
float slots[16] = {0};

std::mutex mu;
float inMx = 0, inMy = 0, lookDx = 0, lookDy = 0;
bool inJump = false;
std::vector<int> actions, events;
std::atomic<int> gState{0};

GLuint sceneP = 0, skyP = 0, p2P = 0, vao = 0, vbo = 0;
struct SU { GLint vp, center, scale, yaw, color, cam, sun, fog, alpha, base, ghost, emis; } su;
struct SK { GLint f, r, u, sun, t; } sk;
struct U2 { GLint rect, color, tex, use; } u2;

void pushEv(int ev, int arg) { std::lock_guard<std::mutex> l(mu); events.push_back(ev | (arg << 8)); }

// ---------------------------------------------------------------- shaders
const char* SCENE_VS = R"(#version 300 es
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNrm;
uniform mat4 uVP;
uniform vec3 uCenter;
uniform vec3 uScale;
uniform float uYaw;
out vec3 vW;
out vec3 vN;
out vec3 vL;
vec3 ry(vec3 v, float a){ float c=cos(a), s=sin(a); return vec3(c*v.x+s*v.z, v.y, -s*v.x+c*v.z); }
void main(){
  vec3 w = ry(aPos*uScale, uYaw) + uCenter;
  vW = w; vN = ry(aNrm, uYaw); vL = aPos;
  gl_Position = uVP*vec4(w,1.0);
}
)";

const char* SCENE_FS = R"(#version 300 es
precision highp float;
in vec3 vW;
in vec3 vN;
in vec3 vL;
uniform vec3 uColor, uCam, uSun, uFog, uScale;
uniform float uAlpha, uBase, uGhost, uEmis;
out vec4 o;
void main(){
  vec3 n = normalize(vN);
  float wrap = dot(n,uSun)*0.5+0.5;
  vec3 amb = mix(vec3(0.60,0.58,0.64), vec3(0.80,0.90,1.00), n.y*0.5+0.5);
  vec3 lit = amb*0.60 + vec3(1.0,0.94,0.82)*pow(wrap,1.7)*0.70;
  float h = (vL.y+0.5)*uScale.y;
  float ao = mix(0.72, 1.0, smoothstep(0.0, 1.5, h));
  vec3 col = uColor*lit*ao;
  vec3 q = (0.5-abs(vL))*uScale;
  float e = max(min(q.x,q.y), min(max(q.x,q.y), q.z));
  col *= 1.0 - 0.20*(1.0 - smoothstep(0.0, 0.045, e));
  vec3 V = normalize(uCam - vW);
  col += pow(1.0 - max(dot(n,V),0.0), 3.0)*0.07;
  float dist = length(uCam - vW);
  if(uBase > 0.5){
    vec2 g = abs(fract(vW.xz*0.5+0.5)-0.5);
    float ln = 1.0 - smoothstep(0.008, 0.03, min(g.x,g.y));
    col *= 1.0 - 0.14*ln*(1.0 - smoothstep(25.0, 70.0, dist));
  }
  col = mix(col, uColor, uEmis);
  if(uGhost > 0.5) col = mix(col, vec3(0.40,0.95,0.88), 0.5) + 0.06;
  col = mix(col, uFog, smoothstep(45.0, 140.0, dist)*(1.0-uGhost));
  o = vec4(col, uAlpha);
}
)";

const char* SKY_VS = R"(#version 300 es
out vec2 vP;
void main(){
  vec2 p = vec2(float((gl_VertexID<<1)&2), float(gl_VertexID&2));
  vP = p*2.0-1.0;
  gl_Position = vec4(vP,0.0,1.0);
}
)";

const char* SKY_FS = R"(#version 300 es
precision highp float;
in vec2 vP;
uniform vec3 uF, uR, uU, uSun;
uniform vec2 uT;
out vec4 o;
void main(){
  vec3 d = normalize(uF + vP.x*uT.x*uR + vP.y*uT.y*uU);
  vec3 hor = vec3(0.95,0.91,0.88);
  vec3 zen = vec3(0.38,0.64,0.95);
  float t = clamp(d.y, 0.0, 1.0);
  vec3 c = mix(hor, zen, pow(t, 0.5));
  float s = max(dot(d,uSun), 0.0);
  c += vec3(1.0,0.85,0.6)*(pow(s,6.0)*0.25 + pow(s,400.0)*1.5);
  c = mix(c, hor, 1.0 - smoothstep(-0.25, 0.0, d.y));
  o = vec4(c,1.0);
}
)";

const char* P2_VS = R"(#version 300 es
uniform vec4 uRect;
out vec2 vUV;
void main(){
  vec2 p = vec2(float(gl_VertexID&1), float((gl_VertexID>>1)&1));
  vUV = p;
  gl_Position = vec4(mix(uRect.xy, uRect.zw, p), 0.0, 1.0);
}
)";

const char* P2_FS = R"(#version 300 es
precision mediump float;
in vec2 vUV;
uniform sampler2D uTex;
uniform vec4 uColor;
uniform float uUse;
out vec4 o;
void main(){
  vec4 c = uColor;
  if(uUse > 0.5) c = texture(uTex, vUV)*uColor;
  o = c;
}
)";

GLuint compile(GLenum t, const char* src) {
    GLuint s = glCreateShader(t);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char b[1024]; glGetShaderInfoLog(s, 1024, nullptr, b); LOGE("shader: %s", b); }
    return s;
}
GLuint link(const char* vs, const char* fs) {
    GLuint p = glCreateProgram();
    glAttachShader(p, compile(GL_VERTEX_SHADER, vs));
    glAttachShader(p, compile(GL_FRAGMENT_SHADER, fs));
    glLinkProgram(p);
    return p;
}

void buildCube(std::vector<float>& v) {
    const float N[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    const int cs[6][2] = {{-1,-1},{1,-1},{1,1},{-1,-1},{1,1},{-1,1}};
    for (int f = 0; f < 6; f++) {
        float nx = N[f][0], ny = N[f][1], nz = N[f][2];
        float ux, uy, uz, vx, vy, vz;
        if (nx != 0) { ux = 0; uy = 1; uz = 0; vx = 0; vy = 0; vz = 1; }
        else if (ny != 0) { ux = 1; uy = 0; uz = 0; vx = 0; vy = 0; vz = 1; }
        else { ux = 1; uy = 0; uz = 0; vx = 0; vy = 1; vz = 0; }
        for (int i = 0; i < 6; i++) {
            float a = cs[i][0] * .5f, b = cs[i][1] * .5f;
            v.push_back(nx * .5f + ux * a + vx * b);
            v.push_back(ny * .5f + uy * a + vy * b);
            v.push_back(nz * .5f + uz * a + vz * b);
            v.push_back(nx); v.push_back(ny); v.push_back(nz);
        }
    }
}

// ---------------------------------------------------------------- nivel
void addBox(float x, float y0, float z, float w, float h, float d, float r, float g, float b) {
    solids.push_back({x, y0 + h * .5f, z, w * .5f, h * .5f, d * .5f, 0.f, r, g, b, false, 0});
}
void buildLevel() {
    solids.clear();
    solids.push_back({0.f, -.5f, 0.f, 40.f, .5f, 40.f, 0.f, .90f, .88f, .84f, true, 0});
    for (int i = 0; i < 4; i++) addBox(-10.f + i * 2.f, 0, -2, 2, (float)(i + 1), 4, .37f, .89f, .82f);   // escada
    addBox(10, 0, -14, 12, 5, 6, .95f, .65f, .35f);                      // parede alta (objetivo)
    addBox(-14, 3, -16, 6, .6f, 6, .64f, .60f, .90f);                    // plataforma flutuante 1
    addBox(-2, 7, -26, 6, .6f, 6, .96f, .62f, .74f);                     // plataforma flutuante 2
    addBox(0, 0, -6, 2, 3, 2, .90f, .42f, .44f);                         // pilar
    addBox(-3, 0, -14, 3, 1, 3, .55f, .60f, .70f);
    addBox(3, 0, 3, 3, 1, 3, 1.f, .82f, .40f);
    addBox(7, 0, 0, 3, 2, 3, .95f, .65f, .35f);
    addBox(14, .5f, 4, 3, .5f, 3, .37f, .89f, .82f);                     // caminho de plataformas
    addBox(18.5f, 1.2f, 4, 3, .5f, 3, .64f, .60f, .90f);
    addBox(23, 2.0f, 0, 3, .5f, 3, .96f, .62f, .74f);
    addBox(27, 3.0f, -5, 4, .5f, 4, 1.f, .82f, .40f);
}

// ---------------------------------------------------------------- fisica
bool circ(const Box& b, float r, float& px, float& pz) {
    float dx = P.x - b.x, dz = P.z - b.z, c = cosf(b.yaw), s = sinf(b.yaw);
    float lx = dx * c - dz * s, lz = dx * s + dz * c;
    float ox = lx - std::max(-b.hx, std::min(b.hx, lx));
    float oz = lz - std::max(-b.hz, std::min(b.hz, lz));
    float d2 = ox * ox + oz * oz;
    if (d2 >= r * r) return false;
    float qx, qz;
    if (d2 > 1e-8f) { float d = sqrtf(d2), k = (r - d) / d; qx = ox * k; qz = oz * k; }
    else {
        float ex = b.hx - fabsf(lx), ez = b.hz - fabsf(lz);
        if (ex < ez) { qx = (lx >= 0 ? 1.f : -1.f) * (ex + r); qz = 0; }
        else { qz = (lz >= 0 ? 1.f : -1.f) * (ez + r); qx = 0; }
    }
    px = qx * c + qz * s; pz = -qx * s + qz * c;
    return true;
}
void respawn() { P = Player(); }

void stepPhysics(float dt, float mx, float my, bool jump) {
    float sy = sinf(P.yaw), cy = cosf(P.yaw), sp = 5.4f;
    P.x += (cy * mx + sy * my) * sp * dt;
    P.z += (-sy * mx + cy * my) * sp * dt;
    for (int k = 0; k < 2; k++)
        for (auto& b : solids)
            if (P.y < b.y + b.hy - .05f && P.y + PH > b.y - b.hy + .02f) {
                float ox, oz;
                if (circ(b, RAD, ox, oz)) { P.x += ox; P.z += oz; }
            }
    if (jump && P.g) P.vy = 9.f;
    P.vy -= 28.f * dt;
    float y0 = P.y;
    P.y += P.vy * dt;
    P.g = false;
    for (auto& b : solids) {
        float ox, oz;
        if (!circ(b, RAD - .03f, ox, oz)) continue;
        float top = b.y + b.hy, bot = b.y - b.hy;
        if (P.vy <= 0 && y0 >= top - .1f && P.y < top) { P.y = top; P.vy = 0; P.g = true; }
        else if (P.vy > 0 && y0 + PH <= bot + .1f && P.y + PH > bot) { P.y = bot - PH; P.vy = 0; }
    }
    if (P.y < -25.f) respawn();
}

// ---------------------------------------------------------------- render
void drawBox(const Box& b, float alpha, bool ghost, float emis) {
    glUniform3f(su.center, b.x, b.y, b.z);
    glUniform3f(su.scale, b.hx * 2, b.hy * 2, b.hz * 2);
    glUniform1f(su.yaw, b.yaw);
    glUniform3f(su.color, b.r, b.g, b.b);
    glUniform1f(su.alpha, alpha);
    glUniform1f(su.base, b.base ? 1.f : 0.f);
    glUniform1f(su.ghost, ghost ? 1.f : 0.f);
    glUniform1f(su.emis, emis);
    glDrawArrays(GL_TRIANGLES, 0, 36);
}

void renderScene(float aspect) {
    glClearColor(.95f, .91f, .88f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    float sy = sinf(P.yaw), cy = cosf(P.yaw), sp = sinf(P.pitch), cp = cosf(P.pitch);
    float tanV = tanf(FOVY * .5f), tanH = tanV * aspect;
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glUseProgram(skyP);
    glUniform3f(sk.f, -sy * cp, sp, -cy * cp);
    glUniform3f(sk.r, cy, 0.f, -sy);
    glUniform3f(sk.u, sy * sp, cp, cy * sp);
    glUniform3f(sk.sun, SUNX, SUNY, SUNZ);
    glUniform2f(sk.t, tanH, tanV);
    glBindVertexArray(0);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glEnable(GL_DEPTH_TEST);

    M4 tr = ident(); tr.m[12] = -P.x; tr.m[13] = -(P.y + EYE); tr.m[14] = -P.z;
    M4 vp = mul(persp(FOVY, aspect, .1f, 220.f), mul(rotXm(-P.pitch), mul(rotYm(-P.yaw), tr)));
    glUseProgram(sceneP);
    glUniformMatrix4fv(su.vp, 1, GL_FALSE, vp.m);
    glUniform3f(su.cam, P.x, P.y + EYE, P.z);
    glUniform3f(su.sun, SUNX, SUNY, SUNZ);
    glUniform3f(su.fog, .95f, .91f, .88f);
    glBindVertexArray(vao);
    for (auto& b : solids) drawBox(b, 1.f, false, 0.f);
    Box g{GX, GY + sinf(gTime * 2.5f) * .15f, GZ, .32f, .32f, .32f, gTime * 1.6f, 1.f, .93f, .45f, false, 0};
    drawBox(g, 1.f, false, 1.f);
}

void rect2d(float x, float y, float w, float h, float r, float g, float b, float a, GLuint tex) {
    float x0 = 2 * x / SW - 1, x1 = 2 * (x + w) / SW - 1, yt = 1 - 2 * y / SH, yb = 1 - 2 * (y + h) / SH;
    glUniform4f(u2.rect, x0, yb, x1, yt);
    glUniform4f(u2.color, r, g, b, a);
    glUniform1f(u2.use, tex ? 1.f : 0.f);
    if (tex) { glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, tex); glUniform1i(u2.tex, 0); }
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void drawHeld() {
    if (held < 0 || held >= (int)photos.size()) return;
    // fantasma 3D do que vai virar realidade
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glUseProgram(sceneP);
    glBindVertexArray(vao);
    V3 eye{P.x, P.y + EYE, P.z};
    for (auto& it : photos[held].items) {
        V3 w = toWorld(it.rel, P.yaw, P.pitch);
        Box g{eye.x + w.x, eye.y + w.y, eye.z + w.z, it.hx, it.hy, it.hz, it.ya + P.yaw, it.r, it.g, it.b, false, 0};
        drawBox(g, .5f, true, 0.f);
    }
    glDepthMask(GL_TRUE);
    // a foto sobreposta ao mundo para alinhar
    glDisable(GL_DEPTH_TEST);
    glUseProgram(p2P);
    glBindVertexArray(0);
    rect2d(0, 0, (float)SW, (float)SH, 1, 1, 1, .34f, photos[held].tex);
    float t = 8.f;
    rect2d(0, 0, (float)SW, t, 1, 1, 1, .9f, 0);
    rect2d(0, SH - t, (float)SW, t, 1, 1, 1, .9f, 0);
    rect2d(0, 0, t, (float)SH, 1, 1, 1, .9f, 0);
    rect2d(SW - t, 0, t, (float)SH, 1, 1, 1, .9f, 0);
}

void drawSlots() {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glUseProgram(p2P);
    glBindVertexArray(0);
    for (int i = 0; i < (int)photos.size() && i < MAXP; i++) {
        float x = slots[i * 4], y = slots[i * 4 + 1], w = slots[i * 4 + 2], h = slots[i * 4 + 3];
        if (w <= 0) continue;
        if (i == held) rect2d(x - 8, y - 8, w + 16, h + 22, .37f, .89f, .82f, 1, 0);
        rect2d(x - 4, y - 4, w + 8, h + 14, 1, 1, 1, .96f, 0);
        rect2d(x, y, w, h, 1, 1, 1, 1, photos[i].tex);
    }
    glDisable(GL_BLEND);
}

// ---------------------------------------------------------------- fotos
void doCapture() {
    if (held >= 0) held = -1;
    Photo ph;
    float aspect = (float)SW / (float)SH;
    ph.h = 384;
    ph.w = std::max(128, std::min(1024, (int)(384 * aspect)));
    V3 eye{P.x, P.y + EYE, P.z};
    float tanV = tanf(FOVY * .5f), tanH = tanV * aspect;
    for (auto& b : solids) {
        if (b.base || ph.items.size() >= 80) continue;
        V3 l = toLocal({b.x - eye.x, b.y - eye.y, b.z - eye.z}, P.yaw, P.pitch);
        float dep = -l.z, r = sqrtf(b.hx * b.hx + b.hy * b.hy + b.hz * b.hz);
        if (dep < -r || dep > 34.f) continue;
        if ((dep * tanH - fabsf(l.x)) / sqrtf(1 + tanH * tanH) < -r) continue;
        if ((dep * tanV - fabsf(l.y)) / sqrtf(1 + tanV * tanV) < -r) continue;
        ph.items.push_back({l, b.hx, b.hy, b.hz, b.yaw - P.yaw, b.r, b.g, b.b});
    }
    GLuint fbo = 0, rbo = 0;
    glGenTextures(1, &ph.tex);
    glBindTexture(GL_TEXTURE_2D, ph.tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, ph.w, ph.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glGenRenderbuffers(1, &rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, ph.w, ph.h);
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, ph.tex, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, rbo);
    glViewport(0, 0, ph.w, ph.h);
    renderScene((float)ph.w / (float)ph.h);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, SW, SH);
    glDeleteFramebuffers(1, &fbo);
    glDeleteRenderbuffers(1, &rbo);
    ph.pos = {P.x, P.y, P.z}; ph.yaw = P.yaw; ph.pitch = P.pitch;
    int n = (int)ph.items.size();
    if ((int)photos.size() >= MAXP) { glDeleteTextures(1, &photos[0].tex); photos.erase(photos.begin()); }
    photos.push_back(ph);
    pushEv(1, n);
}

void doPlace() {
    if (held < 0 || held >= (int)photos.size()) return;
    V3 eye{P.x, P.y + EYE, P.z};
    int n = 0, grp = nextGroup;
    for (auto& it : photos[held].items) {
        V3 w = toWorld(it.rel, P.yaw, P.pitch);
        Box nb{eye.x + w.x, eye.y + w.y, eye.z + w.z, it.hx, it.hy, it.hz, it.ya + P.yaw, it.r, it.g, it.b, false, grp};
        bool dup = false;
        for (auto& e : solids)
            if (!e.base && fabsf(e.x - nb.x) < .06f && fabsf(e.y - nb.y) < .06f && fabsf(e.z - nb.z) < .06f &&
                fabsf(e.hx - nb.hx) < .01f && fabsf(e.hy - nb.hy) < .01f && fabsf(e.hz - nb.hz) < .01f &&
                fabsf(sinf(e.yaw - nb.yaw)) < .02f) { dup = true; break; }
        if (dup) continue;
        float ox, oz;
        if (circ(nb, RAD + .25f, ox, oz) && P.y < nb.y + nb.hy + .1f && P.y + PH > nb.y - nb.hy - .1f) continue;
        if (solids.size() >= 500) break;
        solids.push_back(nb);
        n++;
    }
    if (n > 0) { groups.push_back(grp); nextGroup++; }
    held = -1;
    pushEv(n > 0 ? 2 : 3, n);
}

void doTeleport() {
    if (held < 0 || held >= (int)photos.size()) return;
    const Photo& ph = photos[held];
    P.x = ph.pos.x; P.y = ph.pos.y + .05f; P.z = ph.pos.z; P.yaw = ph.yaw; P.pitch = ph.pitch; P.vy = 0;
    for (int n = 0; n < 30; n++) {
        bool bad = false;
        for (auto& b : solids) {
            float ox, oz;
            if (circ(b, RAD - .03f, ox, oz) && P.y < b.y + b.hy && P.y + PH > b.y - b.hy) { bad = true; break; }
        }
        if (!bad) break;
        P.y += .5f;
    }
    held = -1;
    pushEv(4, 0);
}

void doUndo() {
    if (groups.empty()) return;
    int g = groups.back();
    groups.pop_back();
    solids.erase(std::remove_if(solids.begin(), solids.end(), [g](const Box& b) { return b.group == g; }), solids.end());
    pushEv(5, 0);
}

}  // namespace

// ---------------------------------------------------------------- JNI
extern "C" {

JNIEXPORT void JNICALL Java_com_viewfinder_mobile_Native_nativeCreate(JNIEnv*, jobject) {
    sceneP = link(SCENE_VS, SCENE_FS);
    skyP = link(SKY_VS, SKY_FS);
    p2P = link(P2_VS, P2_FS);
    std::vector<float> v;
    buildCube(v);
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(v.size() * sizeof(float)), v.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 24, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 24, (void*)12);
    glBindVertexArray(0);
    su.vp = glGetUniformLocation(sceneP, "uVP");
    su.center = glGetUniformLocation(sceneP, "uCenter");
    su.scale = glGetUniformLocation(sceneP, "uScale");
    su.yaw = glGetUniformLocation(sceneP, "uYaw");
    su.color = glGetUniformLocation(sceneP, "uColor");
    su.cam = glGetUniformLocation(sceneP, "uCam");
    su.sun = glGetUniformLocation(sceneP, "uSun");
    su.fog = glGetUniformLocation(sceneP, "uFog");
    su.alpha = glGetUniformLocation(sceneP, "uAlpha");
    su.base = glGetUniformLocation(sceneP, "uBase");
    su.ghost = glGetUniformLocation(sceneP, "uGhost");
    su.emis = glGetUniformLocation(sceneP, "uEmis");
    sk.f = glGetUniformLocation(skyP, "uF");
    sk.r = glGetUniformLocation(skyP, "uR");
    sk.u = glGetUniformLocation(skyP, "uU");
    sk.sun = glGetUniformLocation(skyP, "uSun");
    sk.t = glGetUniformLocation(skyP, "uT");
    u2.rect = glGetUniformLocation(p2P, "uRect");
    u2.color = glGetUniformLocation(p2P, "uColor");
    u2.tex = glGetUniformLocation(p2P, "uTex");
    u2.use = glGetUniformLocation(p2P, "uUse");
    glDisable(GL_CULL_FACE);
    photos.clear();  // texturas antigas morreram junto com o contexto GL
    held = -1;
    if (!levelBuilt) { buildLevel(); levelBuilt = true; }
}

JNIEXPORT void JNICALL Java_com_viewfinder_mobile_Native_nativeResize(JNIEnv*, jobject, jint w, jint h) {
    SW = std::max(1, (int)w); SH = std::max(1, (int)h);
    glViewport(0, 0, SW, SH);
}

JNIEXPORT void JNICALL Java_com_viewfinder_mobile_Native_nativeInput(JNIEnv*, jobject, jfloat mx, jfloat my, jboolean j) {
    std::lock_guard<std::mutex> l(mu);
    inMx = mx; inMy = my; inJump = j != 0;
}

JNIEXPORT void JNICALL Java_com_viewfinder_mobile_Native_nativeLook(JNIEnv*, jobject, jfloat dx, jfloat dy) {
    std::lock_guard<std::mutex> l(mu);
    lookDx += dx; lookDy += dy;
}

JNIEXPORT void JNICALL Java_com_viewfinder_mobile_Native_nativeAction(JNIEnv*, jobject, jint a) {
    std::lock_guard<std::mutex> l(mu);
    actions.push_back((int)a);
}

JNIEXPORT void JNICALL Java_com_viewfinder_mobile_Native_nativeSlots(JNIEnv* env, jobject, jfloatArray arr) {
    jfloat tmp[16];
    env->GetFloatArrayRegion(arr, 0, 16, tmp);
    std::lock_guard<std::mutex> l(mu);
    for (int i = 0; i < 16; i++) slots[i] = tmp[i];
}

JNIEXPORT jint JNICALL Java_com_viewfinder_mobile_Native_nativeState(JNIEnv*, jobject) { return gState.load(); }

JNIEXPORT jint JNICALL Java_com_viewfinder_mobile_Native_nativePoll(JNIEnv*, jobject) {
    std::lock_guard<std::mutex> l(mu);
    if (events.empty()) return 0;
    int e = events.front();
    events.erase(events.begin());
    return e;
}

JNIEXPORT void JNICALL Java_com_viewfinder_mobile_Native_nativeDraw(JNIEnv*, jobject, jfloat dt) {
    std::vector<int> acts;
    float mx, my, ldx, ldy;
    bool jm;
    {
        std::lock_guard<std::mutex> l(mu);
        acts.swap(actions);
        mx = inMx; my = inMy; jm = inJump; ldx = lookDx; ldy = lookDy;
        lookDx = lookDy = 0;
    }
    gTime += dt;
    P.yaw -= ldx * .0055f;
    P.pitch = std::max(-1.45f, std::min(1.45f, P.pitch - ldy * .0055f));
    for (int a : acts) {
        if (a == 1) doCapture();
        else if (a == 2) doPlace();
        else if (a == 3) doTeleport();
        else if (a == 4) doUndo();
        else if (a >= 10 && a < 10 + MAXP) {
            int i = a - 10;
            if (i < (int)photos.size()) held = (held == i) ? -1 : i;
        }
    }
    stepPhysics(dt, mx, my, jm);
    if (!won && hypotf(P.x - GX, P.z - GZ) < 1.8f && fabsf(P.y - 5.f) < 1.5f) { won = true; pushEv(6, 0); }
    glViewport(0, 0, SW, SH);
    renderScene((float)SW / (float)SH);
    drawHeld();
    drawSlots();
    gState.store((held >= 0 ? 1 : 0) | (groups.empty() ? 0 : 2) | ((int)photos.size() << 4) | ((held >= 0 ? held : 0) << 8));
}

}  // extern "C"
