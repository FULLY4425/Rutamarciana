// ===================== MICOR'S FOREST (tab "forest") =====================
// Inspired by "Jiji and the Mysterious Forest": walk, talk, random battles, berries & lunch.
// Map legend: # tree  . grass  , path  ~ water  R rock  f flowers  b berry bush
//             T roof  H wall  D door (Granny)  E forest exit

const char FMAP[4][11][21] PROGMEM = {
{ // 0 Whisper Clearing
  "####################",
  "#..f.....f...b....##",
  "#.....##.........f.#",
  "#..f..##...,,,.....#",
  "#.......,,,,.....,,,",
  "#.......,..f....,,,,",
  "#..##...,.......,,,,",
  "#..##...,..f.....#.#",
  "#.......,.........b#",
  "#...f...,,.....f...#",
  "########,,,,########" },
{ // 1 Lake Woods
  "####################",
  "#...f........###..f#",
  "#..~~~~.......##...#",
  "#.~~~~~~......#..b.#",
  ",.~~~~~~~..f.....R.#",
  ",.~~~~~~~~......f..#",
  ",.~~~~~~~..........#",
  "#.~~~~~~..R........#",
  "#..~~~~.....f......#",
  "#...~~........f....#",
  "##############,,,,##" },
{ // 2 Moonlit Mountain
  "########,,,,########",
  "#...R.......R.....R#",
  "#..RRR....f.....RRR#",
  "#....,,,,,,,,......#",
  "#...R,,,,,,,,,.f..,,",
  "#.f..,,,,,,,,,.....,",
  "#....,,,,,,,,,,,,,,,",
  "#.RR.....,,,,.....R#",
  "#.........f........#",
  "#..R....b......R...#",
  "####################" },
{ // 3 Granny's Meadow
  "##############,,,,##",
  "#......f.....,,,,..#",
  "#...TTTTT....,,,,.f#",
  "#...HHHHH....,,,,..#",
  ",...HHDHH....,,,,..#",
  ",.....,,,,,,,,,,,..E",
  ",.....,,,,,,,,,,,..E",
  "#.......,,,,,,,,...#",
  "#..f......f.......b#",
  "#.....b...f........#",
  "####################" }
};

const uint16_t Z_GRASS  = RGB565( 72, 152,  74);
const uint16_t Z_GRASS2 = RGB565( 56, 128,  60);
const uint16_t Z_PATH   = RGB565(208, 178, 120);
const uint16_t Z_PATH2  = RGB565(180, 150,  98);
const uint16_t Z_WATER  = RGB565( 44, 100, 190);
const uint16_t Z_WATER2 = RGB565(110, 170, 235);
const uint16_t Z_LEAF   = RGB565( 30,  96,  52);
const uint16_t Z_LEAF2  = RGB565( 52, 130,  70);
const uint16_t Z_TRUNK  = RGB565(110,  72,  40);
const uint16_t Z_ROCK   = RGB565(128, 130, 140);
const uint16_t Z_ROCK2  = RGB565(168, 170, 180);
const uint16_t Z_ROOF   = RGB565(176,  60,  56);
const uint16_t Z_WALL   = RGB565(236, 214, 170);
const uint16_t Z_DOOR   = RGB565(110,  70,  40);
const uint16_t Z_WINDOW = RGB565(120, 190, 230);
const uint16_t Z_WHITE  = RGB565(246, 248, 252);
const uint16_t Z_GRAY   = RGB565(200, 206, 214);
const uint16_t Z_BLUE   = RGB565( 60, 120, 230);
const uint16_t Z_HUD    = RGB565( 30,  26,  44);
const uint16_t Z_BUBBLE = RGB565(250, 244, 226);
const uint16_t Z_TEXT   = RGB565( 36,  32,  48);
const uint16_t Z_PBODY  = RGB565( 44,  46,  60);
const uint16_t Z_PBODY2 = RGB565( 70,  72,  90);
const uint16_t Z_BTLBG  = RGB565( 20,  36,  44);
const uint16_t Z_BTLTREE= RGB565( 18,  60,  50);
const uint16_t Z_BTLGND = RGB565( 44,  84,  56);

enum { FM_WALK, FM_TALK, FM_POCKET, FM_BATTLE, FM_END };
enum { B_MENU, B_ENEMY, B_WIN, B_LOSE, B_END, B_LVL, B_RESPAWN };

int foMode = FM_WALK, foScr = 0, foX = 3, foY = 8, foDir = 0, foAxisV = 0;
int foHp = 20, foMax = 20, foLv = 1, foXp = 0, foAtk = 3, foBerry = 2, foLunch = 1, foWins = 0, foLeft = 20;
bool qRabbit = false, qOld = false, qFruit = false, qGranny = false, qCat = false, foFruitGone = false;
uint8_t foBush = 0;
unsigned long foPressT = 0, foFaceUntil = 0;
int foFaceForce = 0, foFaceShown = -1, foPSel = 0;
const char* foPage[3]; const char* foName = ""; int foCnt = 0, foIdx = 0, foAfter = 0;
int bT = 0, bHp = 0, bMax = 0, bAtk = 0, bSel = 0, bPhase = 0, bNext = 0;
char bName[12]; char bMsg[72];

const char* AREA_L[4] = {"Whisper Clearing", "Lake Woods", "Moonlit Mountain", "Granny's Meadow"};
const char* AREA_S[4] = {"Clearing", "Lake Woods", "Mountain", "Meadow"};
const uint8_t NPC[3][4]  = {{1,0,5,5}, {2,1,11,5}, {3,2,3,6}};                    // type, screen, x, y
const uint8_t BUSH[6][3] = {{0,13,1}, {0,18,8}, {1,17,3}, {2,8,9}, {3,18,8}, {3,6,9}};
const char* EN_NAME[3] = {"Bit", "Volt", "Prime"};
const uint8_t EN_HP[3] = {8, 14, 22}, EN_ATK[3] = {2, 3, 5}, EN_XP[3] = {4, 7, 12};
const uint16_t EN_COL[3] = {RGB565(255,50,70), RGB565(255,150,30), RGB565(230,60,230)};

// ---------- map helpers ----------
char foMap(int s, int x, int y) { return (char)pgm_read_byte(&FMAP[s][y][x]); }
int foBushId(int s, int x, int y) { for (int i = 0; i < 6; i++) if (BUSH[i][0] == s && BUSH[i][1] == x && BUSH[i][2] == y) return i; return -1; }
bool foBushGone(int s, int x, int y) { int i = foBushId(s, x, y); return i >= 0 && (foBush & (1 << i)); }
int foEntity(int s, int x, int y) {
  for (int i = 0; i < 3; i++) if (NPC[i][1] == s && NPC[i][2] == x && NPC[i][3] == y) return NPC[i][0];
  if (!foFruitGone && s == 1 && x == 18 && y == 4) return 9;
  return 0;
}
bool foBlocked(char c) { return c == '#' || c == '~' || c == 'R' || c == 'H' || c == 'T' || c == 'D' || c == 'b'; }

// ---------- drawing: tiles, characters ----------
void foTile(int tx, int ty, char c) {
  int x = tx * 16, y = ty * 16;
  if (c == '~') {
    tft.fillRect(x, y, 16, 16, Z_WATER);
    tft.drawFastHLine(x + 2 + (tx & 1) * 4, y + 5, 6, Z_WATER2);
    tft.drawFastHLine(x + 7 - (ty & 1) * 4, y + 11, 6, Z_WATER2);
    return;
  }
  if (c == ',' || c == 'E') {
    tft.fillRect(x, y, 16, 16, Z_PATH);
    tft.fillRect(x + 3, y + 4, 2, 2, Z_PATH2); tft.fillRect(x + 10, y + 10, 2, 2, Z_PATH2);
    if (c == 'E') tft.fillTriangle(x + 5, y + 3, x + 5, y + 13, x + 12, y + 8, K_YELLOW);
    return;
  }
  if (c == 'T') {
    tft.fillRect(x, y, 16, 16, Z_ROOF);
    tft.drawFastHLine(x, y + 5, 16, RGB565(140, 44, 44)); tft.drawFastHLine(x, y + 11, 16, RGB565(140, 44, 44));
    return;
  }
  if (c == 'H' || c == 'D') {
    tft.fillRect(x, y, 16, 16, Z_WALL);
    if (c == 'D') { tft.fillRect(x + 3, y + 1, 10, 15, Z_DOOR); tft.fillRect(x + 10, y + 8, 2, 2, K_YELLOW); }
    else if (tx == 6) { tft.fillRect(x + 3, y + 3, 10, 8, Z_WINDOW); tft.drawFastVLine(x + 8, y + 3, 8, Z_WALL); }
    return;
  }
  tft.fillRect(x, y, 16, 16, Z_GRASS);
  if (((tx * 5 + ty * 3) & 3) == 0) tft.fillRect(x + 4, y + 9, 2, 2, Z_GRASS2);
  if (c == '#') { tft.fillRect(x + 6, y + 10, 4, 6, Z_TRUNK); tft.fillRoundRect(x + 1, y, 14, 12, 5, Z_LEAF); tft.fillRect(x + 4, y + 3, 4, 2, Z_LEAF2); }
  else if (c == 'R') { tft.fillRoundRect(x + 2, y + 4, 12, 10, 4, Z_ROCK); tft.fillRect(x + 5, y + 6, 4, 2, Z_ROCK2); }
  else if (c == 'f') { tft.fillRect(x + 4, y + 5, 2, 2, K_PINK); tft.fillRect(x + 10, y + 10, 2, 2, K_YELLOW); }
  else if (c == 'b') {
    tft.fillRoundRect(x + 1, y + 4, 14, 11, 5, Z_LEAF);
    if (!foBushGone(foScr, tx, ty)) { tft.fillRect(x + 4, y + 7, 2, 2, K_RED); tft.fillRect(x + 9, y + 6, 2, 2, K_RED); tft.fillRect(x + 7, y + 10, 2, 2, K_RED); }
  }
}
void foEnt(int t, int x, int y) {
  if (t == 1) {            // rabbit
    tft.fillRect(x + 5, y, 2, 7, Z_WHITE); tft.fillRect(x + 9, y, 2, 7, Z_WHITE);
    tft.fillRect(x + 5, y + 1, 1, 4, K_PINK); tft.fillRect(x + 10, y + 1, 1, 4, K_PINK);
    tft.fillRoundRect(x + 3, y + 6, 10, 9, 4, Z_WHITE);
    tft.fillRect(x + 5, y + 9, 2, 2, 0); tft.fillRect(x + 9, y + 9, 2, 2, 0);
    tft.fillRect(x + 7, y + 11, 2, 1, K_PINK);
  } else if (t == 2) {     // old man
    tft.fillRoundRect(x + 3, y + 7, 10, 9, 3, RGB565(120, 80, 150));
    tft.fillCircle(x + 8, y + 5, 4, RGB565(240, 200, 170));
    tft.fillTriangle(x + 4, y + 7, x + 12, y + 7, x + 8, y + 14, Z_WHITE);
    tft.fillRect(x + 6, y + 4, 1, 2, 0); tft.fillRect(x + 10, y + 4, 1, 2, 0);
    tft.fillRect(x + 3, y + 1, 10, 2, RGB565(90, 60, 130));
  } else if (t == 3) {     // mountain cat
    tft.fillTriangle(x + 3, y + 5, x + 4, y, x + 7, y + 4, K_ORANGE);
    tft.fillTriangle(x + 13, y + 5, x + 12, y, x + 9, y + 4, K_ORANGE);
    tft.fillRoundRect(x + 3, y + 3, 10, 12, 4, K_ORANGE);
    tft.fillRect(x + 5, y + 7, 2, 2, 0); tft.fillRect(x + 9, y + 7, 2, 2, 0);
    tft.fillRect(x + 7, y + 10, 2, 1, K_PINK);
    tft.drawFastHLine(x + 3, y + 12, 3, RGB565(170, 80, 20)); tft.drawFastHLine(x + 10, y + 12, 3, RGB565(170, 80, 20));
  } else {                 // red fruit
    tft.fillCircle(x + 8, y + 9, 5, K_RED); tft.fillRect(x + 7, y + 2, 2, 3, Z_LEAF2); tft.fillRect(x + 6, y + 7, 2, 2, K_PINK);
  }
}
void foMicor(int x, int y, int dir, int s) {       // dir: 0 down, 1 up, 2 left, 3 right
  int tx = (dir == 3) ? 1 : 12;
  tft.fillRoundRect(x + tx * s, y + 9 * s, 3 * s, 3 * s, s, Z_GRAY);
  tft.fillRoundRect(x + 4 * s, y + 8 * s, 8 * s, 7 * s, 2 * s, Z_WHITE);
  tft.fillRect(x + 5 * s, y + 14 * s, 2 * s, 2 * s, Z_GRAY); tft.fillRect(x + 9 * s, y + 14 * s, 2 * s, 2 * s, Z_GRAY);
  tft.fillTriangle(x + 3 * s, y + 4 * s, x + 4 * s, y, x + 7 * s, y + 3 * s, Z_WHITE);
  tft.fillTriangle(x + 13 * s, y + 4 * s, x + 12 * s, y, x + 9 * s, y + 3 * s, Z_WHITE);
  tft.fillRoundRect(x + 3 * s, y + 2 * s, 10 * s, 8 * s, 3 * s, Z_WHITE);
  if (dir == 1) return;
  int a = 5, b2 = 9, nx = 7;
  if (dir == 2) { a = 4; b2 = 8; nx = 3; } else if (dir == 3) { a = 8; b2 = 11; nx = 12; }
  tft.fillRect(x + a * s, y + 5 * s, 2 * s, 2 * s, Z_BLUE); tft.fillRect(x + b2 * s, y + 5 * s, 2 * s, 2 * s, Z_BLUE);
  tft.fillRect(x + nx * s, y + 8 * s, 2 * s, s, 0);
}
void foProto(int x, int y, int s, uint16_t vc) {   // evil protogen
  tft.fillRoundRect(x + 12 * s, y + 10 * s, 4 * s, 2 * s, s, Z_PBODY2);
  tft.fillRect(x + 15 * s, y + 10 * s, s, 2 * s, vc);
  tft.fillRoundRect(x + 3 * s, y + 9 * s, 10 * s, 7 * s, 2 * s, Z_PBODY);
  tft.fillRect(x + 7 * s, y + 11 * s, 2 * s, 2 * s, vc);
  tft.fillTriangle(x + 3 * s, y + 4 * s, x + 4 * s, y, x + 7 * s, y + 3 * s, Z_PBODY);
  tft.fillTriangle(x + 13 * s, y + 4 * s, x + 12 * s, y, x + 9 * s, y + 3 * s, Z_PBODY);
  tft.fillTriangle(x + 4 * s, y + 3 * s, x + 4 * s + s / 2, y + s, x + 6 * s, y + 3 * s, vc);
  tft.fillTriangle(x + 12 * s, y + 3 * s, x + 12 * s - s / 2, y + s, x + 10 * s, y + 3 * s, vc);
  tft.fillRoundRect(x + 2 * s, y + 2 * s, 12 * s, 9 * s, 3 * s, Z_PBODY);
  tft.fillRoundRect(x + 3 * s, y + 3 * s, 10 * s, 7 * s, 2 * s, RGB565(14, 14, 22));
  tft.fillTriangle(x + 4 * s, y + 4 * s, x + 7 * s, y + 6 * s, x + 4 * s, y + 6 * s, vc);
  tft.fillTriangle(x + 12 * s, y + 4 * s, x + 9 * s, y + 6 * s, x + 12 * s, y + 6 * s, vc);
  tft.fillRect(x + 6 * s, y + 8 * s, 4 * s, s, vc);
}
void foCell(int x, int y) {
  foTile(x, y, foMap(foScr, x, y));
  int e = foEntity(foScr, x, y);
  if (e) foEnt(e, x * 16, y * 16);
  if (x == foX && y == foY) foMicor(x * 16, y * 16, foDir, 1);
}
void foAxis() {
  tft.fillRoundRect(288, 2, 30, 12, 3, Z_TEXT);
  putText(293, 4, 1, K_YELLOW, Z_TEXT, foAxisV ? "U/D" : "L/R");
}
void foScreen() {
  for (int y = 0; y < 11; y++) for (int x = 0; x < 20; x++) {
    foTile(x, y, foMap(foScr, x, y));
    int e = foEntity(foScr, x, y);
    if (e) foEnt(e, x * 16, y * 16);
  }
  foMicor(foX * 16, foY * 16, foDir, 1);
  foAxis();
}

// ---------- HUD: face cam + text bubble ----------
void foFace(int e) {
  foFaceShown = e;
  int bx = 6, by = 182;
  uint16_t bg = (e == 0) ? RGB565(120, 205, 130) : (e == 1) ? RGB565(235, 210, 100) : (e == 2) ? RGB565(240, 150, 70) : (e == 3) ? RGB565(235, 80, 80) : RGB565(110, 110, 125);
  tft.fillRoundRect(bx, by, 52, 52, 8, bg);
  tft.drawRoundRect(bx, by, 52, 52, 8, Z_TEXT);
  tft.fillTriangle(bx + 8, by + 22, bx + 12, by + 4, bx + 22, by + 16, Z_WHITE);
  tft.fillTriangle(bx + 44, by + 22, bx + 40, by + 4, bx + 30, by + 16, Z_WHITE);
  tft.fillTriangle(bx + 12, by + 17, bx + 13, by + 9, bx + 18, by + 14, K_PINK);
  tft.fillTriangle(bx + 40, by + 17, bx + 39, by + 9, bx + 34, by + 14, K_PINK);
  tft.fillRoundRect(bx + 7, by + 14, 38, 30, 12, Z_WHITE);
  tft.fillTriangle(bx + 7, by + 30, bx + 3, by + 38, bx + 12, by + 40, Z_WHITE);
  tft.fillTriangle(bx + 45, by + 30, bx + 49, by + 38, bx + 40, by + 40, Z_WHITE);
  tft.fillRoundRect(bx + 17, by + 31, 18, 12, 5, Z_GRAY);
  tft.fillRoundRect(bx + 23, by + 30, 6, 4, 2, 0);
  int lx = bx + 17, rx = bx + 35, ey = by + 26;
  for (int i = 0; i < 2; i++) {
    int ex = i ? rx : lx;
    if (e == 0) {                                       // happy ^ ^
      tft.drawLine(ex - 4, ey + 3, ex, ey - 1, 0); tft.drawLine(ex, ey - 1, ex + 4, ey + 3, 0);
      tft.drawLine(ex - 4, ey + 4, ex, ey, 0);     tft.drawLine(ex, ey, ex + 4, ey + 4, 0);
    } else if (e == 4) {                                // dizzy x x
      tft.drawLine(ex - 3, ey - 3, ex + 3, ey + 3, 0); tft.drawLine(ex - 3, ey + 3, ex + 3, ey - 3, 0);
      tft.drawLine(ex - 2, ey - 3, ex + 4, ey + 3, 0); tft.drawLine(ex - 2, ey + 3, ex + 4, ey - 3, 0);
    } else {                                            // open eyes
      tft.fillRoundRect(ex - 3, ey - 5, 7, 9, 2, Z_BLUE);
      tft.fillRect(ex - 1, ey - 3, 2, 2, Z_WHITE);
      if (e == 3) { if (i == 0) tft.drawLine(ex - 5, ey - 8, ex + 3, ey - 5, 0); else tft.drawLine(ex + 5, ey - 8, ex - 3, ey - 5, 0); }
    }
  }
  if (e == 0) { tft.drawLine(bx + 22, by + 38, bx + 26, by + 41, 0); tft.drawLine(bx + 26, by + 41, bx + 30, by + 38, 0); }
  else if (e == 1) tft.drawFastHLine(bx + 22, by + 39, 8, 0);
  else if (e == 2) { tft.fillCircle(bx + 45, by + 15, 3, K_CYAN); tft.drawLine(bx + 22, by + 40, bx + 26, by + 38, 0); tft.drawLine(bx + 26, by + 38, bx + 30, by + 40, 0); }
  else if (e == 3) {
    tft.fillRect(bx + 25, by + 9, 13, 5, RGB565(240, 210, 160)); tft.drawFastVLine(bx + 31, by + 9, 5, RGB565(200, 160, 110));
    tft.drawLine(bx + 22, by + 41, bx + 26, by + 38, 0); tft.drawLine(bx + 26, by + 38, bx + 30, by + 41, 0);
  } else tft.drawFastHLine(bx + 22, by + 40, 8, 0);
}
int foExpr(uint32_t now) {
  if (foHp <= 0) return 4;
  if (now < foFaceUntil) return foFaceForce;
  int r = foHp * 100 / foMax;
  return r > 66 ? 0 : (r > 40 ? 1 : (r > 20 ? 2 : 3));
}
void foFaceTick(uint32_t now) { int e = foExpr(now); if (e != foFaceShown) foFace(e); }
void foFaceFor(int e, int ms) { foFaceForce = e; foFaceUntil = millis() + ms; }

void foBubbleFrame(const char* name) {
  tft.fillRoundRect(64, 182, 252, 56, 8, Z_BUBBLE);
  tft.drawRoundRect(64, 182, 252, 56, 8, Z_TEXT);
  tft.fillTriangle(61, 208, 68, 202, 68, 214, Z_BUBBLE);
  tft.drawLine(61, 208, 68, 202, Z_TEXT); tft.drawLine(61, 208, 68, 214, Z_TEXT);
  tft.fillRoundRect(74, 177, strlen(name) * 6 + 8, 11, 4, Z_TEXT);
  putText(78, 179, 1, Z_BUBBLE, Z_TEXT, name);
}
void foBubble(const char* name, const char* text) {
  foBubbleFrame(name);
  char line[44];
  int y = 192;
  const char* p = text;
  while (*p == ' ') p++;
  for (int ln = 0; ln < 5 && *p; ln++) {
    int n = 0, last = -1;
    while (p[n] && n < 40) { if (p[n] == ' ') last = n; n++; }
    int len = (p[n] && last > 0) ? last : n;
    memcpy(line, p, len); line[len] = 0;
    putText(72, y, 1, Z_TEXT, Z_BUBBLE, line);
    y += 9; p += len;
    while (*p == ' ') p++;
  }
}
const char* foGoal() {
  if (!qRabbit) return "Talk to the rabbit";
  if (!qOld) return "Find the Old Man";
  if (!qFruit && !qGranny) return "Get the red fruit";
  if (qFruit && !qGranny) return "Bring fruit to Granny";
  if (!qCat) return "Ask the Mountain Cat";
  return "Go back to Granny";
}
void foIdle() {
  char b[170];
  snprintf(b, sizeof b, "Goal: %s   Turn = walk %s. Press = switch to %s. Press twice = pocket.",
           foGoal(), foAxisV ? "UP/DOWN" : "LEFT/RIGHT", foAxisV ? "LEFT/RIGHT" : "UP/DOWN");
  foBubble(AREA_L[foScr], b);
}

// ---------- dialogue ----------
void foSay(const char* name, const char* a, const char* b, const char* c, int after) {
  foName = name; foPage[0] = a; foPage[1] = b; foPage[2] = c;
  foCnt = c ? 3 : (b ? 2 : 1); foIdx = 0; foAfter = after;
  foMode = FM_TALK;
  foBubble(foName, foPage[0]);
}
void foEnd(bool good) {
  foMode = FM_END; stopSound();
  if (good) SFX(T_WIN); else SFX(T_LOSE);
  uint16_t bg = good ? RGB565(24, 60, 50) : RGB565(40, 24, 40);
  char b[48];
  tft.fillScreen(bg);
  centerText("THE END", 22, 5, good ? K_YELLOW : K_PINK, bg);
  foMicor(136, 80, 0, 3);
  centerText(good ? "Micor found the way home! x3" : "Micor ran out of the forest alone... ;-;", 140, 1, K_CREAM, bg);
  snprintf(b, sizeof b, "Lv %d   protogens beaten: %d", foLv, foWins);
  centerText(b, 160, 1, K_CYAN, bg);
  centerText(good ? "GOOD ENDING" : "BAD ENDING (try helping Granny!)", 180, 1, good ? K_YELLOW : K_PINK, bg);
  centerText("press: play again    hold: menu", 218, 1, K_CYAN, bg);
  mood = good ? "x3" : ";-;"; oledDirty = true;
}
void foRunAfter(int a) {
  if (a == 1) qRabbit = true;
  else if (a == 2) qOld = true;
  else if (a == 3) { qGranny = true; qFruit = false; foLunch++; foHp = foMax; }
  else if (a == 4) qCat = true;
  else if (a == 5) { foEnd(true); return; }
  oledDirty = true;
}
void foDialogNext() {
  foIdx++;
  if (foIdx < foCnt) { foBubble(foName, foPage[foIdx]); return; }
  foMode = FM_WALK;
  foRunAfter(foAfter);
  if (foMode == FM_WALK) foIdle();
}
void foTalk(int e) {
  if (e == 1) {
    if (!qRabbit) foSay("Rabbit", "Oh! A little white wolf, all alone in the Mysterious Forest?", "The Old Man by the lake knows every path. Follow the sandy trail east!", nullptr, 1);
    else foSay("Rabbit", "Careful out there! Evil protogens roam the lake woods and the mountain.", "Eat berries or lunch to get your strength back!", nullptr, 0);
  } else if (e == 2) {
    if (!qRabbit) foSay("Old Man", "Hm? A wolf pup... Have you met the rabbit yet? Hehe.", nullptr, nullptr, 0);
    else if (!qOld) foSay("Old Man", "Granny in the south meadow is sick. A red fruit grows by the big rock, east of the lake.", "Bring it to her, little wolf. And beware the evil protogens!", nullptr, 2);
    else if (!qFruit && !qGranny) foSay("Old Man", "The red fruit is by the big rock. Then go to Granny's house in the south meadow.", nullptr, nullptr, 0);
    else foSay("Old Man", "You have the fruit? Hurry to Granny's house, little wolf!", nullptr, nullptr, 0);
  } else if (e == 3) {
    if (!qGranny) foSay("Mountain Cat", "Mrrow? I only help wolves who have helped Granny.", nullptr, nullptr, 0);
    else if (!qCat) foSay("Mountain Cat", "So Granny sent you. The way out of the forest is through her house!", "Tell her the cat says 'purr'. Mrrow!", nullptr, 4);
    else foSay("Mountain Cat", "Go on, Micor. Granny is waiting for you!", nullptr, nullptr, 0);
  }
}
void foGranny() {
  bool healed = foHp < foMax;
  foHp = foMax; foFaceFor(0, 900); oledDirty = true;
  if (qCat) foSay("Granny", "The cat said purr? Then you have earned the secret path home, Micor!", "Thank you for everything, little wolf. Off you go!", nullptr, 5);
  else if (qGranny) foSay("Granny", healed ? "Rest a while, dear (HP restored). The Mountain Cat lives west of my meadow." : "The Mountain Cat lives west of my meadow. Go and see her!", nullptr, nullptr, 0);
  else if (qFruit) foSay("Granny", "A red fruit! Bless you, little wolf. I feel better already!", "Take this lunch box. The Mountain Cat knows the way out. Go west, then come back to me.", nullptr, 3);
  else if (qOld) foSay("Granny", healed ? "Rest a while, dear (HP restored). I need a red fruit to get well..." : "I need a red fruit to get well... it grows by the big rock, east of the lake.", nullptr, nullptr, 0);
  else foSay("Granny", "Oh? A little wolf! Go and talk to the Old Man by the lake, dear.", nullptr, nullptr, 0);
}
void foHeal(int amt) { foHp += amt; if (foHp > foMax) foHp = foMax; foFaceFor(0, 900); oledDirty = true; }

// ---------- pocket (eat outside battle) ----------
void foPocketDraw() {
  char b[80];
  snprintf(b, sizeof b, "%s Lunch x%d    %s Berry x%d    %s Back", foPSel == 0 ? ">" : " ", foLunch, foPSel == 1 ? ">" : " ", foBerry, foPSel == 2 ? ">" : " ");
  foBubble("Pocket", b);
}
void foPocketGo() {
  foMode = FM_WALK;
  if (foPSel == 2) { foIdle(); return; }
  bool has = (foPSel == 0) ? foLunch > 0 : foBerry > 0;
  if (!has) { foSay("Micor", "None of that left in my bag...", nullptr, nullptr, 0); return; }
  if (foHp >= foMax) { foSay("Micor", "I'm not hungry right now!", nullptr, nullptr, 0); return; }
  if (foPSel == 0) { foLunch--; foHeal(14); foSay("Micor", "Yum, a lunch box! +14 HP", nullptr, nullptr, 0); }
  else { foBerry--; foHeal(6); foSay("Micor", "Sweet berry! +6 HP", nullptr, nullptr, 0); }
  SFX(T_YOU);
}

// ---------- battle ----------
void foBarDraw(int x, int y, int w, int cur, int mx, uint16_t col) {
  int f = (w - 2) * cur / (mx > 0 ? mx : 1);
  tft.drawRect(x, y, w, 8, Z_WHITE);
  tft.fillRect(x + 1, y + 1, f, 6, col);
  tft.fillRect(x + 1 + f, y + 1, w - 2 - f, 6, RGB565(40, 40, 50));
}
void foBattleBars() {
  char b[28];
  tft.fillRect(10, 6, 134, 30, Z_BTLBG);
  snprintf(b, sizeof b, "MICOR Lv%d  %d/%d", foLv, foHp, foMax);
  putText(14, 8, 1, Z_WHITE, Z_BTLBG, b);
  foBarDraw(14, 20, 110, foHp, foMax, (foHp * 3 > foMax) ? RGB565(80, 220, 110) : K_RED);
  tft.fillRect(180, 6, 134, 30, Z_BTLBG);
  snprintf(b, sizeof b, "%s  %d/%d", bName, bHp, bMax);
  putText(184, 8, 1, Z_WHITE, Z_BTLBG, b);
  foBarDraw(184, 20, 110, bHp, bMax, EN_COL[bT]);
}
void foBattleScene() {
  tft.fillRect(0, 0, W, 176, Z_BTLBG);
  for (int i = 0; i < 9; i++) tft.fillRoundRect(i * 38 - 8, 40 + (i % 3) * 10, 34, 80, 12, Z_BTLTREE);
  tft.fillRect(0, 112, W, 64, Z_BTLGND);
  tft.fillRect(0, 112, W, 2, RGB565(70, 120, 80));
  foMicor(36, 92, 3, 3);
  foProto(214, 46, 4, EN_COL[bT]);
  foBattleBars();
}
void foBSay(const char* t, int next) {
  strncpy(bMsg, t, sizeof(bMsg) - 1); bMsg[sizeof(bMsg) - 1] = 0;
  bNext = next; bPhase = 0;
  foBubble("Battle", bMsg);
}
void foBMenu() {
  const char* o[4] = {"SCRATCH", "BITE", "LUNCH", "FLEE"};
  const int ox[4] = {88, 196, 88, 196}, oy[4] = {195, 195, 215, 215};
  bPhase = 1;
  foBubbleFrame("Micor's turn");
  for (int i = 0; i < 4; i++) {
    if (i == bSel) putText(ox[i] - 14, oy[i], 2, Z_ROOF, Z_BUBBLE, ">");
    putText(ox[i], oy[i], 2, i == bSel ? Z_ROOF : Z_TEXT, Z_BUBBLE, o[i]);
  }
}
void foStartBattle() {
  bT = (foScr == 1) ? (random(100) < 70 ? 0 : 1) : (random(100) < 55 ? 1 : 2);
  float difficulty = gameDifficultyFactor(APP_FOREST);
  bHp = bMax = (int)(EN_HP[bT] * difficulty + 0.5f);
  bAtk = max(1, (int)(EN_ATK[bT] * difficulty + 0.5f));
  strcpy(bName, EN_NAME[bT]);
  foMode = FM_BATTLE; bSel = 0; mood = ">w<";
  SFX(T_BOOM);
  foBattleScene();
  char b[64]; snprintf(b, sizeof b, "An evil protogen, %s, blocks the way!", bName);
  foBSay(b, B_MENU);
  oledDirty = true;
}
void foEndBattle() {
  foMode = FM_WALK; mood = ":3";
  float speed = gameSpeedFactor(APP_FOREST);
  foLeft = random(max(8, (int)(14 / speed)), max(10, (int)(30 / speed)));
  foScreen(); foFace(foExpr(millis())); foIdle();
  oledDirty = true;
}
void foEnemyTurn() {
  char b[64];
  if (random(100) < 15) { foBSay("The protogen's zap fizzled out!", B_MENU); return; }
  int d = bAtk + random(0, 3);
  foHp -= d; if (foHp < 0) foHp = 0;
  foFaceFor(3, 700); foBattleBars(); SFX(T_CPU); oledDirty = true;
  snprintf(b, sizeof b, "%s zaps Micor! -%d HP", bName, d);
  foBSay(b, foHp <= 0 ? B_LOSE : B_MENU);
}
void foAfterHit(const char* msg) {
  if (bHp <= 0) { bHp = 0; foBattleBars(); foBSay(msg, B_WIN); }
  else { foBattleBars(); foBSay(msg, B_ENEMY); }
  oledDirty = true;
}
void foAct(int a) {
  char b[64];
  if (a == 0) {
    int d = foAtk + random(0, 3); bHp -= d; SFX(T_HIT);
    snprintf(b, sizeof b, "Micor scratches %s! -%d HP", bName, d); foAfterHit(b);
  } else if (a == 1) {
    if (random(100) < 75) {
      int d = foAtk * 2 + random(0, 3); bHp -= d; SFX(T_HIT);
      snprintf(b, sizeof b, "Micor BITES %s! -%d HP", bName, d); foAfterHit(b);
    } else foBSay("Micor's bite missed!", B_ENEMY);
  } else if (a == 2) {
    if (foHp >= foMax) { foBSay("Micor isn't hungry!", B_MENU); return; }
    if (foLunch > 0) { foLunch--; foHeal(14); snprintf(b, sizeof b, "Micor munches a lunch box! +14 HP"); }
    else if (foBerry > 0) { foBerry--; foHeal(6); snprintf(b, sizeof b, "Micor eats a berry! +6 HP"); }
    else { foBSay("No food left in the bag!", B_MENU); return; }
    SFX(T_YOU); foBattleBars(); foBSay(b, B_ENEMY);
  } else {
    if (random(100) < 55 + foLv * 3) foBSay("Micor ran away!", B_END);
    else foBSay("Couldn't get away!", B_ENEMY);
  }
}
void foWin() {
  char b[72];
  int gain = EN_XP[bT];
  foWins++; foXp += gain;
  bool berry = random(100) < 55, lunch = random(100) < 12;
  if (berry) foBerry++;
  if (lunch) foLunch++;
  snprintf(b, sizeof b, "%s crashed! +%d XP%s%s", bName, gain, berry ? ", +1 berry" : "", lunch ? ", +1 lunch" : "");
  SFX(T_YOU); foFaceFor(0, 1500); mood = "^w^";
  foBSay(b, (foXp >= foLv * 10) ? B_LVL : B_END);
  oledDirty = true;
}
void foLevelUp() {
  char b[48];
  foXp -= foLv * 10; foLv++; foMax += 5; foHp = foMax; foAtk++;
  SFX(T_WAVE); foBattleBars();
  snprintf(b, sizeof b, "LEVEL UP! Lv%d  HP %d  ATK %d", foLv, foMax, foAtk);
  foBSay(b, B_END); oledDirty = true;
}
void foBPress() {
  switch (bNext) {
    case B_MENU:    foBMenu(); break;
    case B_ENEMY:   foEnemyTurn(); break;
    case B_WIN:     foWin(); break;
    case B_LVL:     foLevelUp(); break;
    case B_LOSE:    foBSay("Micor fainted... but wakes up at Granny's meadow.", B_RESPAWN); break;
    case B_RESPAWN: foScr = 3; foX = 8; foY = 6; foHp = foMax; foBerry /= 2; foEndBattle(); break;
    default:        foEndBattle(); break;
  }
}
void foBattleTick(bool press, int clicks) {
  if (bPhase == 0) { if (press) foBPress(); return; }
  if (clicks) { bSel = ((bSel + (clicks > 0 ? 1 : -1)) % 4 + 4) % 4; foBMenu(); }
  if (press) foAct(bSel);
}

// ---------- walking ----------
void foStep() {
  if (foScr == 1 || foScr == 2) { foLeft--; if (foLeft <= 0) foStartBattle(); }
}
void foTryMove(int dx, int dy) {
  foDir = dx > 0 ? 3 : (dx < 0 ? 2 : (dy > 0 ? 0 : 1));
  int nx = foX + dx, ny = foY + dy;
  if (nx < 0 || nx > 19 || ny < 0 || ny > 10) {
    int sx = foScr & 1, sy = foScr >> 1, ns;
    if (nx < 0)       { if (sx == 0) return; ns = foScr - 1; nx = 19; }
    else if (nx > 19) { if (sx == 1) return; ns = foScr + 1; nx = 0; }
    else if (ny < 0)  { if (sy == 0) return; ns = foScr - 2; ny = 10; }
    else              { if (sy == 1) return; ns = foScr + 2; ny = 0; }
    if (foBlocked(foMap(ns, nx, ny))) return;
    foScr = ns; foX = nx; foY = ny;
    foScreen(); foIdle(); oledDirty = true;
    foStep();
    return;
  }
  char t = foMap(foScr, nx, ny);
  int e = foEntity(foScr, nx, ny);
  if (e >= 1 && e <= 3) { foCell(foX, foY); foTalk(e); return; }
  if (t == 'D') { foCell(foX, foY); foGranny(); return; }
  if (t == 'b') {
    foCell(foX, foY);
    int id = foBushId(foScr, nx, ny);
    if (id >= 0 && (foBush & (1 << id))) foSay("Micor", "This bush is empty now.", nullptr, nullptr, 0);
    else {
      int n = random(1, 3);
      if (id >= 0) foBush |= (1 << id);
      foBerry += n; foTile(nx, ny, 'b'); SFX(T_HOME); oledDirty = true;
      static char bb[32]; snprintf(bb, sizeof bb, "Found %d berr%s!", n, n == 1 ? "y" : "ies");
      foSay("Micor", bb, nullptr, nullptr, 0);
    }
    return;
  }
  if (t == 'E') { foEnd(false); return; }
  if (foBlocked(t)) { foCell(foX, foY); SFX(T_WALL); return; }
  int ox = foX, oy = foY;
  foX = nx; foY = ny;
  foCell(ox, oy); foCell(foX, foY);
  if (e == 9) {
    foFruitGone = true; qFruit = true; SFX(T_HOME); oledDirty = true;
    foSay("Micor", "A shiny RED FRUIT! This must be what Granny needs!", nullptr, nullptr, 0);
    return;
  }
  foStep();
}

// ---------- new game / enter / tick ----------
void foNewGame() {
  foHp = foMax = gameDifficulty[APP_FOREST] == 0 ? 25 : (gameDifficulty[APP_FOREST] == 2 ? 16 : 20);
  foLv = 1; foXp = 0; foAtk = 3; foBerry = 2; foLunch = 1; foWins = 0;
  qRabbit = qOld = qFruit = qGranny = qCat = false; foFruitGone = false; foBush = 0;
  float speed = gameSpeedFactor(APP_FOREST);
  foScr = 0; foX = 3; foY = 8; foDir = 0; foAxisV = 0;
  foLeft = random(max(8, (int)(14 / speed)), max(10, (int)(30 / speed)));
  foMode = FM_WALK; mood = ":3"; foFaceShown = -1; foFaceUntil = 0;
  tft.fillScreen(Z_HUD);
  foScreen();
  foFace(0);
  foSay("Micor", "Where did that flying fish go? Oh no... I'm lost in the Mysterious Forest!", "Maybe someone here can help me find the way. Let me look around.", nullptr, 0);
  oledDirty = true;
}
void forestEnter() { foNewGame(); }

void forestTick(uint32_t now) {
  bool press = takePress();
  takeShort();
  int clicks = takeClicks() * ENC_DIR;
  if (foMode != FM_END) foFaceTick(now);
  switch (foMode) {
    case FM_WALK:
      if (press) {
        if (now - foPressT < 350) { foAxisV = !foAxisV; foPressT = 0; foMode = FM_POCKET; foPSel = 0; foPocketDraw(); break; }
        foAxisV = !foAxisV; foPressT = now; foAxis(); foIdle(); oledDirty = true;
      }
      if (clicks) {
        int n = abs(clicks); if (n > 3) n = 3;
        int sg = clicks > 0 ? 1 : -1;
        for (int i = 0; i < n && foMode == FM_WALK; i++) foTryMove(foAxisV ? 0 : sg, foAxisV ? sg : 0);
      }
      break;
    case FM_TALK:
      if (press) foDialogNext();
      break;
    case FM_POCKET:
      if (clicks) { foPSel = ((foPSel + (clicks > 0 ? 1 : -1)) % 3 + 3) % 3; foPocketDraw(); }
      if (press) foPocketGo();
      break;
    case FM_BATTLE:
      foBattleTick(press, clicks);
      break;
    case FM_END:
      if (press) foNewGame();
      break;
  }
}

// ---------- OLED info (called from the main tab) ----------
void foOledBar(int y, int cur, int mx) {
  oled.drawRect(16, y, 70, 7, SSD1306_WHITE);
  oled.fillRect(17, y + 1, 68 * cur / (mx > 0 ? mx : 1), 5, SSD1306_WHITE);
}
void oledForest() {
  char b[28];
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  if (foMode == FM_BATTLE) { snprintf(b, sizeof b, "BATTLE vs %s", bName); oled.print(b); }
  else oled.print(AREA_S[foScr]);
  oled.setCursor(104, 0); oled.print(mood);
  oled.setCursor(0, 10); oled.print("HP");
  foOledBar(10, foHp, foMax);
  snprintf(b, sizeof b, "%d/%d", foHp, foMax); oled.setCursor(92, 10); oled.print(b);
  if (foMode == FM_BATTLE) {
    oled.setCursor(0, 21); oled.print("EN");
    foOledBar(21, bHp, bMax);
    snprintf(b, sizeof b, "%d/%d", bHp, bMax); oled.setCursor(92, 21); oled.print(b);
    snprintf(b, sizeof b, "Lv%d  XP %d/%d", foLv, foXp, foLv * 10); oled.setCursor(0, 32); oled.print(b);
    snprintf(b, sizeof b, "Berry %d  Lunch %d", foBerry, foLunch); oled.setCursor(0, 43); oled.print(b);
    oled.setCursor(0, 54); oled.print("turn: pick  press: go");
  } else {
    snprintf(b, sizeof b, "Lv%d  XP %d/%d", foLv, foXp, foLv * 10); oled.setCursor(0, 21); oled.print(b);
    snprintf(b, sizeof b, "Berry %d  Lunch %d", foBerry, foLunch); oled.setCursor(0, 32); oled.print(b);
    oled.setCursor(0, 43); oled.print("Goal:");
    oled.setCursor(0, 54); oled.print(foGoal());
  }
}