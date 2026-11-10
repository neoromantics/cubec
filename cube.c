/*
 * CUBE - raylib 6.2 / C11
 * A standalone, native recreation of the dot-pattern cube animation.
 *
 * macOS/Homebrew:
 *   cc -std=c11 -O2 main.c -o cube $(pkg-config --cflags --libs raylib) -lm
 *   ./cube
 *
 * No HTML, JavaScript, external textures, or shaders required.
 */
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "raylib.h"

#define WINDOW_W 1000
#define WINDOW_H 900
#define SIZE 3
#define COUNT (SIZE * SIZE * SIZE)
#define FACES 6
#define DOTS 7
#define MAX_RING_TILES 27
#define PI_F 3.14159265358979323846f

#define TURN_SECS 0.90f
#define TURN_STAGGER 0.14f
#define TURN_PAUSE 0.35f
#define RING_SECS 1.50f
#define RING_STAGGER 0.08f
#define RING_ANGLE_DEGREES 120.0f

typedef struct {
  int x, y, z;
} IVec3;
typedef struct {
  IVec3 pos;           // Exact integer position (-1, 0, +1)
  IVec3 basis[3];      // Exact orientation; columns are local X,Y,Z
  Color paint[FACES];  // Color of each LOCAL face's dot pattern
  unsigned char
      style[FACES];     // 0..5: filled/outlined squares, triangles, circles
  bool sticker[FACES];  // Legal stickers rotate with the cubie
} Piece;

typedef struct {
  int axis, count;
  int layer[2], direction[2];
  float delay[2], elapsed, duration;
  bool active;
} Turn;

typedef struct {
  int piece, face;
  Color sourceColor;
  int sourceStyle;
  float angle;
  Vector3 worldCenter;
  Vector3 worldU, worldV;
  Vector2 screen;
} RingTile;

typedef struct {
  RingTile tile[MAX_RING_TILES];
  int count, direction, shift;
  float delay;
} Ring;

typedef struct {
  Ring groups[SIZE];
  int groupOf[COUNT][FACES];
  int indexOf[COUNT][FACES];
  float elapsed, duration;
  Vector2 pivot;
  bool active;
} RingEffect;

static Piece pieces[COUNT];
static Turn turn;
static RingEffect effect;
static Camera3D camera;
static int previousAxis = -1;
static int previousLayers[2] = {-1, -1};
static int previousCount = 0;
static int turnsSinceEffect = 0;
static int pendingSteps = 0;
static float pauseRemaining = TURN_PAUSE;
static float speed = 1.0f;
static bool playing = true;
static bool autoplay = true;
static bool showHelp = true;

static const Color BG = {245, 242, 235, 255};
static const Color INTERIOR = {133, 139, 146, 255};
static const Color COLORS[FACES] = {
    {230, 75, 53, 255},   // +Z front (red)
    {237, 140, 50, 255},  // -Z back (orange)
    {56, 121, 199, 255},  // +X right (blue)
    {75, 155, 101, 255},  // -X left (green)
    {129, 82, 184, 255},  // +Y top (purple)
    {233, 199, 71, 255}   // -Y bottom (yellow)
};
static const IVec3 NORMALS[FACES] = {{0, 0, 1},  {0, 0, -1}, {1, 0, 0},
                                     {-1, 0, 0}, {0, 1, 0},  {0, -1, 0}};
static const IVec3 U_AXES[FACES] = {{1, 0, 0}, {-1, 0, 0}, {0, 0, -1},
                                    {0, 0, 1}, {1, 0, 0},  {1, 0, 0}};
static const IVec3 V_AXES[FACES] = {{0, 1, 0}, {0, 1, 0},  {0, 1, 0},
                                    {0, 1, 0}, {0, 0, -1}, {0, 0, 1}};

static float rnd(void) { return (float)GetRandomValue(0, 100000) / 100000.0f; }
static float clamp01(float x) { return x < 0 ? 0 : x > 1 ? 1 : x; }
static float cubic(float x) {
  x = clamp01(x);
  return x < .5f ? 4 * x * x * x : 1 - powf(-2 * x + 2, 3) / 2;
}
static float smooth(float x) {
  x = clamp01(x);
  return x * x * (3 - 2 * x);
}
static float lerpf(float x, float y, float t) { return x + (y - x) * t; }
static Vector3 v3(float x, float y, float z) { return (Vector3){x, y, z}; }
static Vector3 vi(IVec3 a) { return v3((float)a.x, (float)a.y, (float)a.z); }
static Vector3 add(Vector3 a, Vector3 b) {
  return v3(a.x + b.x, a.y + b.y, a.z + b.z);
}
static Vector3 sub(Vector3 a, Vector3 b) {
  return v3(a.x - b.x, a.y - b.y, a.z - b.z);
}
static Vector3 mul(Vector3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static float dot(Vector3 a, Vector3 b) {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}
static Vector3 norm(Vector3 a) {
  float d = sqrtf(dot(a, a));
  return d > 1e-8f ? mul(a, 1 / d) : v3(0, 0, 0);
}
static Vector3 plane(Vector3 c, Vector3 u, Vector3 v, float x, float y) {
  return add(c, add(mul(u, x), mul(v, y)));
}
static Color blend(Color a, Color b, float f) {
  f = clamp01(f);
  return (Color){(unsigned char)(lerpf(a.r, b.r, f) + .5f),
                 (unsigned char)(lerpf(a.g, b.g, f) + .5f),
                 (unsigned char)(lerpf(a.b, b.b, f) + .5f), 255};
}
static Color shade(Color c, float amount) {
  if (amount >= 0) return blend(c, WHITE, clamp01(amount));
  return blend(c, BLACK, clamp01(-amount));
}
static IVec3 integerTurn(IVec3 v, int axis, int direction) {
  if (axis == 0) return (IVec3){v.x, -direction * v.z, direction * v.y};
  if (axis == 1) return (IVec3){direction * v.z, v.y, -direction * v.x};
  return (IVec3){-direction * v.y, direction * v.x, v.z};
}
static Vector3 axisRotate(Vector3 v, int axis, float angle) {
  const float c = cosf(angle), s = sinf(angle);
  if (axis == 0) return v3(v.x, c * v.y - s * v.z, s * v.y + c * v.z);
  if (axis == 1) return v3(c * v.x + s * v.z, v.y, -s * v.x + c * v.z);
  return v3(c * v.x - s * v.y, s * v.x + c * v.y, v.z);
}
static int coord(IVec3 v, int axis) {
  return axis == 0 ? v.x : axis == 1 ? v.y : v.z;
}

static int randomStyle(void) {
  int shape = GetRandomValue(0, 2);
  bool hollow = rnd() >= .75f;
  return shape * 2 + (hollow ? 1 : 0);
}
static void initCube(bool scramble);
static Turn randomTurn(void);
static void applyTurn(Turn m);
static void prepareRing(void);
static void finishRing(void);

static void initCube(bool scramble) {
  int n = 0;
  for (int x = -1; x <= 1; x++)
    for (int y = -1; y <= 1; y++)
      for (int z = -1; z <= 1; z++) {
        Piece *p = &pieces[n++];
        p->pos = (IVec3){x, y, z};
        p->basis[0] = (IVec3){1, 0, 0};
        p->basis[1] = (IVec3){0, 1, 0};
        p->basis[2] = (IVec3){0, 0, 1};
        for (int f = 0; f < FACES; f++) {
          IVec3 normal = NORMALS[f];
          p->sticker[f] = dot(vi(p->pos), vi(normal)) == 1.0f;
          p->style[f] = (unsigned char)randomStyle();
          // Stable variation within each solved face; it travels with the tile.
          int u = coord(p->pos, (f < 2 ? 0 : f < 4 ? 1 : 0));
          int v = coord(p->pos, (f < 2 ? 1 : f < 4 ? 2 : 2));
          int linear = (u + 1) * 3 + (v + 1);
          float tone = 2.0f * linear / 8.0f - 1.0f;
          p->paint[f] =
              p->sticker[f] ? shade(COLORS[f], tone * .24f) : INTERIOR;
        }
      }
  turn = (Turn){0};
  effect = (RingEffect){0};
  previousAxis = -1;
  previousCount = 0;
  turnsSinceEffect = 0;
  pendingSteps = 0;
  pauseRemaining = TURN_PAUSE;
  if (scramble)
    for (int i = 0; i < 30; i++) {
      Turn m = randomTurn();
      applyTurn(m);
    }
}

static bool sameAsPrevious(int axis, int count, int layers[2]) {
  if (axis != previousAxis || count != previousCount) return false;
  for (int i = 0; i < count; i++)
    if (previousLayers[i] != layers[i]) return false;
  return true;
}
static Turn randomTurn(void) {
  Turn m = {0};
  // A band of 1 or 2 neighboring layers. Never rotate the entire cube.
  for (int tries = 0; tries < 30; tries++) {
    m.axis = GetRandomValue(0, 2);
    m.count = (rnd() < .5f) ? 2 : 1;
    int start = GetRandomValue(0, 3 - m.count);
    for (int i = 0; i < m.count; i++) m.layer[i] = -1 + start + i;
    if (!sameAsPrevious(m.axis, m.count, m.layer)) break;
  }
  int d = (rnd() < .5f) ? -1 : 1;
  for (int i = 0; i < m.count; i++) m.direction[i] = d;
  if (m.count == 2 && rnd() < .5f) m.direction[1] = -d;
  return m;
}
static void applyTurn(Turn m) {
  for (int i = 0; i < COUNT; i++) {
    Piece *p = &pieces[i];
    for (int j = 0; j < m.count; j++)
      if (coord(p->pos, m.axis) == m.layer[j]) {
        p->pos = integerTurn(p->pos, m.axis, m.direction[j]);
        for (int b = 0; b < 3; b++)
          p->basis[b] = integerTurn(p->basis[b], m.axis, m.direction[j]);
        break;
      }
  }
  previousAxis = m.axis;
  previousCount = m.count;
  for (int i = 0; i < m.count; i++) previousLayers[i] = m.layer[i];
}
static void startTurn(Turn m) {
  m.active = true;
  m.elapsed = 0;
  for (int i = 0; i < m.count; i++) m.delay[i] = 0;
  if (m.count == 2) {
    if (rnd() < .5f)
      m.delay[0] = TURN_STAGGER * (.5f + .5f * rnd());
    else
      m.delay[1] = TURN_STAGGER * (.5f + .5f * rnd());
  }
  m.duration = TURN_SECS + (m.delay[0] > m.delay[1] ? m.delay[0] : m.delay[1]);
  turn = m;
}
static void currentTransform(int index, Vector3 *center, Vector3 b[3]) {
  Piece *p = &pieces[index];
  *center = vi(p->pos);
  for (int j = 0; j < 3; j++) b[j] = vi(p->basis[j]);
  if (!turn.active) return;
  for (int j = 0; j < turn.count; j++)
    if (coord(p->pos, turn.axis) == turn.layer[j]) {
      float alpha = cubic((turn.elapsed - turn.delay[j]) / TURN_SECS);
      float angle = (PI_F * .5f) * turn.direction[j] * alpha;
      *center = axisRotate(*center, turn.axis, angle);
      for (int k = 0; k < 3; k++) b[k] = axisRotate(b[k], turn.axis, angle);
      break;
    }
}
static Vector3 localToWorld(IVec3 local, Vector3 b[3]) {
  return add(add(mul(b[0], (float)local.x), mul(b[1], (float)local.y)),
             mul(b[2], (float)local.z));
}

// All visible geometry is native triangles. No texture or custom shader
// required.
static void triangle(Vector3 a, Vector3 b, Vector3 c, Color color) {
  DrawTriangle3D(a, b, c, color);
}
static void quadCorners(Vector3 a, Vector3 b, Vector3 c, Vector3 d,
                        Color color) {
  triangle(a, b, c, color);
  triangle(a, c, d, color);
}
static void quad(Vector3 c, Vector3 u, Vector3 v, float rx, float ry,
                 Color color) {
  quadCorners(plane(c, u, v, -rx, -ry), plane(c, u, v, rx, -ry),
              plane(c, u, v, rx, ry), plane(c, u, v, -rx, ry), color);
}
static void ringCircle(Vector3 c, Vector3 u, Vector3 v, float radius,
                       float inner, Color color) {
  const int N = 16;
  for (int i = 0; i < N; i++) {
    float a = i * (2 * PI_F / N), b = (i + 1) * (2 * PI_F / N);
    Vector3 a0 = plane(c, u, v, cosf(a) * radius, sinf(a) * radius);
    Vector3 b0 = plane(c, u, v, cosf(b) * radius, sinf(b) * radius);
    if (inner <= 0)
      triangle(c, a0, b0, color);
    else {
      Vector3 a1 = plane(c, u, v, cosf(a) * inner, sinf(a) * inner);
      Vector3 b1 = plane(c, u, v, cosf(b) * inner, sinf(b) * inner);
      quadCorners(a0, b0, b1, a1, color);
    }
  }
}
static void drawShape(Vector3 c, Vector3 u, Vector3 v, float half, int style,
                      Color color) {
  const bool hollow = (style % 2) == 1;
  const int type = style / 2;
  if (type == 0) {  // Square
    if (!hollow)
      quad(c, u, v, half, half, color);
    else {
      float thick = half * .28f;
      quad(plane(c, u, v, 0, half - thick / 2), u, v, half, thick / 2, color);
      quad(plane(c, u, v, 0, -half + thick / 2), u, v, half, thick / 2, color);
      quad(plane(c, u, v, -half + thick / 2, 0), u, v, thick / 2, half - thick,
           color);
      quad(plane(c, u, v, half - thick / 2, 0), u, v, thick / 2, half - thick,
           color);
    }
  } else if (type == 1) {  // Triangle
    Vector3 o[3] = {plane(c, u, v, -half, -half), plane(c, u, v, half, -half),
                    plane(c, u, v, 0, half)};
    if (!hollow)
      triangle(o[0], o[1], o[2], color);
    else {
      Vector3 centroid = mul(add(add(o[0], o[1]), o[2]), 1.0f / 3);
      Vector3 in[3];
      for (int i = 0; i < 3; i++)
        in[i] = add(centroid, mul(sub(o[i], centroid), .65f));
      for (int i = 0; i < 3; i++) {
        int j = (i + 1) % 3;
        quadCorners(o[i], o[j], in[j], in[i], color);
      }
    }
  } else {  // Circle
    ringCircle(c, u, v, half, hollow ? half * .68f : 0, color);
  }
}
static float lighting(Vector3 point, Vector3 normal) {
  const Vector3 lightPosition = {-4.5f, 7.0f, 9.0f};
  Vector3 L = norm(sub(lightPosition, point));
  Vector3 viewer = norm(sub(camera.position, point));
  Vector3 halfVector = norm(add(L, viewer));
  float diffuse = fmaxf(0, dot(normal, L));
  float spec = powf(fmaxf(0, dot(normal, halfVector)), 24.0f);
  return .75f + .37f * diffuse + .12f * spec;
}
static Color litDot(Color base, Vector3 position, Vector3 normal, int row,
                    int column) {
  const float a = PI_F / 4;
  float u = 2.0f * column / (DOTS - 1) - 1.0f;
  float v = 2.0f * row / (DOTS - 1) - 1.0f;
  float grad = -(u * cosf(a) + v * sinf(a)) / (fabsf(cosf(a)) + fabsf(sinf(a)));
  Color color = shade(base, grad * .28f);
  float bright = lighting(position, normal);
  return shade(color, bright - 1.0f);
}

// Ring interpolation is independent of physical cube rotation.
static int wrapped(int i, int n) { return (i % n + n) % n; }
static Color ringColorAt(const Ring *g, float x, int *styleOut) {
  // sample a circular sequence at a continuous index (like CSS color-mix)
  int ix = (int)floorf(x);
  float f = x - floorf(x);
  int a = wrapped(ix, g->count), b = wrapped(ix + 1, g->count);
  *styleOut = (f < .5f) ? g->tile[a].sourceStyle : g->tile[b].sourceStyle;
  return blend(g->tile[a].sourceColor, g->tile[b].sourceColor, smooth(f));
}
static void effectDot(int piece, int face, float u, float v, Color *color,
                      int *style) {
  int gid = effect.groupOf[piece][face];
  if (gid < 0) return;
  const Ring *g = &effect.groups[gid];
  int id = effect.indexOf[piece][face];
  float p = smooth((effect.elapsed - g->delay) / RING_SECS);
  float amount = (float)g->shift * p;
  const RingTile *tile = &g->tile[id];
  Vector2 center2 = tile->screen;
  // A screen-space tangent makes the color front cross each diamond.
  Vector2 corner = effect.pivot;
  float dx = center2.x - corner.x, dy = center2.y - corner.y;
  float length = sqrtf(dx * dx + dy * dy) + 1e-6f;
  float tx = -dy / length, ty = dx / length;
  Vector2 u2 =
      GetWorldToScreen(add(tile->worldCenter, mul(tile->worldU, .25f)), camera);
  Vector2 v2 =
      GetWorldToScreen(add(tile->worldCenter, mul(tile->worldV, .25f)), camera);
  float tu = (u2.x - center2.x) * tx + (u2.y - center2.y) * ty;
  float tv = (v2.x - center2.x) * tx + (v2.y - center2.y) * ty;
  float phase = (u * tu + v * tv) / (fabsf(tu) + fabsf(tv) + 1e-6f);
  float shift = amount - .19f * phase * sinf(PI_F * p);
  *color = ringColorAt(g, id - shift, style);
  // Three fading afterimages follow the moving pattern.
  float envelope = sinf(PI_F * p);
  envelope *= envelope;
  for (int k = 1; k <= 3; k++) {
    int previousStyle;
    float earlier = fmaxf(0, amount - g->direction * (.42f * k / 3));
    // Handle reverse rotations without changing which direction the trail
    // follows.
    if (g->direction < 0)
      earlier = fminf(0, amount - g->direction * (.42f * k / 3));
    Color past = ringColorAt(g, id - earlier - .19f * phase * sinf(PI_F * p),
                             &previousStyle);
    *color = blend(*color, past, .32f * envelope / 3);
  }
}
static void renderCube(void) {
  Vector3 towards = norm(camera.position);
  for (int i = 0; i < COUNT; i++) {
    Piece *p = &pieces[i];
    Vector3 pos, basis[3];
    currentTransform(i, &pos, basis);
    for (int f = 0; f < FACES; f++) {
      Vector3 normal = localToWorld(NORMALS[f], basis);
      // Facing-away surfaces need not be rasterized. Depth buffer handles cuts.
      if (dot(normal, towards) <= .025f) continue;
      // Only the exterior sticker surfaces are visible between turns.
      if (!turn.active && !p->sticker[f]) continue;
      Vector3 u = localToWorld(U_AXES[f], basis);
      Vector3 v = localToWorld(V_AXES[f], basis);
      Vector3 surface = add(pos, mul(normal, .5f));
      quad(surface, u, v, .5f, .5f, BG);
      Vector3 top = add(surface, mul(normal, .002f));
      float cell = 1.0f / DOTS;
      float radius = cell * .76f * .5f;
      for (int row = 0; row < DOTS; row++)
        for (int col = 0; col < DOTS; col++) {
          float a = ((float)col + .5f) / DOTS - .5f;
          float b = .5f - ((float)row + .5f) / DOTS;
          Vector3 where = plane(top, u, v, a, b);
          int style = p->style[f];
          Color color = p->paint[f];
          if (effect.active && p->sticker[f]) {
            float fu = 2.0f * col / (DOTS - 1) - 1.0f;
            float fv = 2.0f * row / (DOTS - 1) - 1.0f;
            effectDot(i, f, fu, fv, &color, &style);
          }
          color = litDot(color, where, normal, row, col);
          drawShape(where, u, v, radius, style, color);
        }
    }
  }
}
static void prepareRing(void) {
  memset(&effect, 0, sizeof(effect));
  for (int i = 0; i < COUNT; i++)
    for (int f = 0; f < FACES; f++) effect.groupOf[i][f] = -1;
  Vector2 pivot = GetWorldToScreen(v3(1.5f, 1.5f, 1.5f), camera);
  effect.pivot = pivot;
  Vector3 towards = norm(camera.position);
  for (int i = 0; i < COUNT; i++) {
    Piece *p = &pieces[i];
    Vector3 basis[3];
    for (int b = 0; b < 3; b++) basis[b] = vi(p->basis[b]);
    for (int f = 0; f < FACES; f++) {
      if (!p->sticker[f]) continue;
      Vector3 normal = localToWorld(NORMALS[f], basis);
      if (dot(normal, towards) < .5f) continue;
      int worldAxis = fabsf(normal.x) > .5f ? 0 : fabsf(normal.y) > .5f ? 1 : 2;
      int group = 0;
      for (int axis = 0; axis < 3; axis++)
        if (axis != worldAxis) {
          int distance = 1 - coord(p->pos, axis);
          if (distance > group) group = distance;
        }
      if (group < 0 || group >= SIZE) continue;
      Ring *g = &effect.groups[group];
      if (g->count >= MAX_RING_TILES) continue;
      RingTile *t = &g->tile[g->count++];
      Vector3 center = add(vi(p->pos), mul(normal, .5f));
      Vector2 screen = GetWorldToScreen(center, camera);
      *t = (RingTile){.piece = i,
                      .face = f,
                      .sourceColor = p->paint[f],
                      .sourceStyle = p->style[f],
                      .angle = atan2f(screen.y - pivot.y, screen.x - pivot.x),
                      .worldCenter = center,
                      .worldU = localToWorld(U_AXES[f], basis),
                      .worldV = localToWorld(V_AXES[f], basis),
                      .screen = screen};
    }
  }
  int first = (rnd() < .5f) ? -1 : 1;
  bool varied = false;
  for (int r = 0; r < SIZE; r++) {
    Ring *g = &effect.groups[r];
    // Insertion sort (at most 15 tiles), angle measured clockwise on screen.
    for (int i = 1; i < g->count; i++) {
      RingTile t = g->tile[i];
      int j = i - 1;
      while (j >= 0 && g->tile[j].angle > t.angle) {
        g->tile[j + 1] = g->tile[j];
        j--;
      }
      g->tile[j + 1] = t;
    }
    g->direction = (r == 0) ? first : ((rnd() < .5f) ? -1 : 1);
    if (g->direction != first) varied = true;
    g->delay = r * RING_STAGGER;
    g->shift =
        (int)lroundf(g->count * (RING_ANGLE_DEGREES / 360.0f)) * g->direction;
    for (int j = 0; j < g->count; j++) {
      RingTile *t = &g->tile[j];
      effect.groupOf[t->piece][t->face] = r;
      effect.indexOf[t->piece][t->face] = j;
    }
  }
  if (!varied && SIZE > 1) {
    Ring *g = &effect.groups[1];
    g->direction = -first;
    g->shift = -g->shift;
  }
  effect.duration = RING_SECS + (SIZE - 1) * RING_STAGGER;
  effect.elapsed = 0;
  effect.active = true;
}
static void finishRing(void) {
  for (int r = 0; r < SIZE; r++) {
    Ring *g = &effect.groups[r];
    if (g->count == 0) continue;
    // Read source buffers before committing; destination tiles are distinct.
    for (int i = 0; i < g->count; i++) {
      int src = wrapped(i - g->shift, g->count);
      RingTile *dst = &g->tile[i], *origin = &g->tile[src];
      pieces[dst->piece].paint[dst->face] = origin->sourceColor;
      pieces[dst->piece].style[dst->face] = (unsigned char)origin->sourceStyle;
    }
  }
  effect.active = false;
  turnsSinceEffect = 0;
  pauseRemaining = TURN_PAUSE;
}
static void stepSimulation(float dt) {
  if (!playing) return;
  dt *= speed;
  if (effect.active) {
    effect.elapsed += dt;
    if (effect.elapsed >= effect.duration) finishRing();
    return;
  }
  if (turn.active) {
    turn.elapsed += dt;
    if (turn.elapsed >= turn.duration) {
      applyTurn(turn);
      turn.active = false;
      turnsSinceEffect++;
      pauseRemaining = TURN_PAUSE;
    }
    return;
  }
  pauseRemaining = fmaxf(0, pauseRemaining - dt);
  if (pauseRemaining > 0) return;
  if (pendingSteps > 0) {
    pendingSteps--;
    startTurn(randomTurn());
  } else if (autoplay) {
    if (turnsSinceEffect >= 4 && rnd() < .18f)
      prepareRing();
    else
      startTurn(randomTurn());
  }
}

static void drawOverlay(void) {
  DrawText("CUBE", 32, 28, 25, (Color){69, 65, 59, 255});
  DrawText("raylib  /  C  /  3D", 32, 63, 16, (Color){118, 111, 100, 255});
  DrawFPS(GetScreenWidth() - 108, 34);
  if (showHelp) {
    DrawText("SPACE  pause / play       R  scramble       S  solved", 32,
             GetScreenHeight() - 104, 17, (Color){90, 84, 77, 255});
    DrawText("A  autoplay       N  next turn       F  ring sweep", 32,
             GetScreenHeight() - 77, 17, (Color){90, 84, 77, 255});
    DrawText("+ / -  change speed       H  hide controls", 32,
             GetScreenHeight() - 50, 17, (Color){90, 84, 77, 255});
  }
  if (!playing)
    DrawText("PAUSED", WINDOW_W / 2 - 57, 32, 20, (Color){125, 88, 140, 255});
  if (!autoplay)
    DrawText("MANUAL", WINDOW_W / 2 - 60, 62, 14, (Color){125, 88, 140, 255});
  if (effect.active)
    DrawText("RING FLOW", WINDOW_W / 2 - 58, GetScreenHeight() - 130, 15,
             (Color){125, 88, 140, 255});
}

int main(void) {
  SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
  InitWindow(WINDOW_W, WINDOW_H, "Cube - raylib");
  SetTargetFPS(60);
  camera = (Camera3D){0};
  camera.position = v3(6.0f, 6.0f, 6.0f);
  camera.target = v3(0, 0, 0);
  camera.up = v3(0, 1, 0);
  camera.fovy = 6.8f;
  camera.projection = CAMERA_ORTHOGRAPHIC;
  initCube(true);
  while (!WindowShouldClose()) {
    if (IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_P)) playing = !playing;
    if (IsKeyPressed(KEY_R)) initCube(true);
    if (IsKeyPressed(KEY_S)) initCube(false);
    if (IsKeyPressed(KEY_H)) showHelp = !showHelp;
    if (IsKeyPressed(KEY_A)) autoplay = !autoplay;
    if (IsKeyPressed(KEY_N)) {
      pendingSteps++;
      pauseRemaining = 0;
      playing = true;
    }
    if (IsKeyPressed(KEY_F)) {
      if (!turn.active && !effect.active) prepareRing();
    }
    if (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD))
      speed = fminf(3, speed + .25f);
    if (IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT))
      speed = fmaxf(.25f, speed - .25f);
    if (IsWindowResized()) {
      // Preserve a constant-size stage relative to the smaller window
      // dimension.
      float fraction = fminf((float)GetScreenWidth() / WINDOW_W,
                             (float)GetScreenHeight() / WINDOW_H);
      camera.fovy = 6.8f / fmaxf(.5f, fraction);
    }
    float dt = GetFrameTime();
    if (dt > .05f) dt = .05f;
    stepSimulation(dt);
    BeginDrawing();
    ClearBackground(BG);
    BeginMode3D(camera);
    renderCube();
    EndMode3D();
    drawOverlay();
    EndDrawing();
  }
  CloseWindow();
  return 0;
}
