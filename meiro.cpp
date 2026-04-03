// =============================================================================
//  MEIRO  —  A Procedural Maze Game
//  Renderer : Windows GDI  (link: -lgdi32 -lwinmm)
//  Compiler : MinGW / Code::Blocks   -std=c++17
// =============================================================================

// =============================================================================
//  SECTION 1: INCLUDES & CONSTANTS
// =============================================================================
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>       // WinMM audio
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cmath>
#include <cstdio>
#include <vector>
#include <stack>
#include <string>
#include <algorithm>
#include <fstream>
#include <limits>

// Window — resolved at runtime to actual screen size
static int SCREEN_W = 800;   // overwritten in initGame()
static int SCREEN_H = 600;   // overwritten in initGame()
static const char* CLASS_NAME   = "MeiroClass";
static const char* WINDOW_TITLE = "MEIRO";

// ---- Timing -----------------------------------------------------------------
static const int TARGET_FPS = 60;
static const int FRAME_MS   = 1000 / TARGET_FPS;

// ---- Maze sizing per floor band ---------------------------------------------
// getMazeSize() returns {cols, rows} — always odd numbers
static std::pair<int,int> getMazeSize(int floor)
{
    if (floor <= 3) return {21, 19};
    if (floor <= 6) return {27, 23};
    if (floor <= 9) return {33, 29};
    return {39, 35};
}

// ---- Minotaur speed (cells/sec) per floor -----------------------------------
static float getMinotaurSpeed(int floor)
{
    // Player speed is 10 cells/sec
    // Minotaur runs at 80-90% of that, scaling up each floor band
    if (floor <= 3) return 8.0f;    // 80% — learnable, survivable
    if (floor <= 6) return 8.5f;    // 85%
    if (floor <= 9) return 9.0f;    // 90%
    return 9.5f;                    // 95% — nearly equal, very tense
}

// ---- Colours (GDI COLORREF = 0x00BBGGRR) ------------------------------------
static const COLORREF C_BG          = RGB(12,  10,  8);    // near-black dungeon
static const COLORREF C_WALL        = RGB(70,  60,  50);   // stone grey-brown
static const COLORREF C_WALL_DARK   = RGB(40,  34,  28);   // darker stone
static const COLORREF C_FLOOR       = RGB(22,  18,  14);   // dark floor
static const COLORREF C_PLAYER      = RGB(120, 200, 255);  // icy blue
static const COLORREF C_EXIT        = RGB(255, 180,  40);  // torch gold
static const COLORREF C_MINOTAUR    = RGB(220,  50,  50);  // blood red
static const COLORREF C_TEXT        = RGB(210, 190, 160);  // parchment
static const COLORREF C_TEXT_DIM    = RGB(120, 105,  85);  // dim parchment
static const COLORREF C_ACCENT      = RGB(255, 160,  30);  // torch orange
static const COLORREF C_ACCENT2     = RGB(100, 180, 255);  // blue accent
static const COLORREF C_BTN_EXPLORE = RGB(40,  80,  120);  // blue button bg
static const COLORREF C_BTN_MINO    = RGB(100,  28,  28);  // red button bg
static const COLORREF C_OVERLAY     = RGB(8,    6,   4);

// ---- Save file --------------------------------------------------------------
static const char* SAVE_FILE = "meiro_scores.dat";

// =============================================================================
//  SECTION 2: ENUMS & STRUCTS
// =============================================================================

enum class GameState
{
    SPLASH,
    MENU,
    MENU_ANIM,
    PLAYING,
    TRANSITION,
    GAMEOVER,
    PAUSED,         // ESC during gameplay — Resume or Main Menu
    CONFIRM_QUIT,   // Exit confirmation (from menu or pause)
    WIN
};

enum class GameMode { NONE, EXPLORE, MINOTAUR };

// ---- Cell -------------------------------------------------------------------
struct Cell
{
    bool wallN = true, wallS = true, wallE = true, wallW = true;
    bool visited = false;
};

// ---- Player -----------------------------------------------------------------
struct Player
{
    int   col = 1, row = 1;
    int   targetCol = 1, targetRow = 1;
    float drawX = 0.0f, drawY = 0.0f;
    bool  moving = false;
    bool  alive  = true;
    int   stepsFromStart = 0;  // counts moves made this floor
};

// ---- Minotaur ---------------------------------------------------------------
struct Minotaur
{
    int   col = 1, row = 1;
    int   targetCol = 1, targetRow = 1;
    float drawX = 0.0f, drawY = 0.0f;
    bool  moving = false;

    float moveTimer   = 0.0f;
    float movePeriod  = 0.4f;

    bool  spawned     = false;  // false = waiting for player to move 5 steps
};

// ---- Animated background maze -----------------------------------------------
struct BgMaze
{
    std::vector<Cell> cells;
    int cols = 21, rows = 19;
    // DFS stack for animated carving
    std::stack<std::pair<int,int>> stk;
    bool done = false;
    float stepTimer = 0.0f;
    float stepPeriod = 0.01f;   // seconds between carve steps
};

// ---- High scores ------------------------------------------------------------
struct Scores
{
    float exploreBest  = 0.0f;   // best total time (0 = not set)
    int   exploreFloor = 0;      // best floor reached
    float minoTime     = 0.0f;
    int   minoFloor    = 0;
};

// ---- Top-level game context -------------------------------------------------
struct Game
{
    HWND    hwnd   = nullptr;
    HDC     hdc    = nullptr;
    HDC     memDC  = nullptr;
    HBITMAP memBMP = nullptr;

    GameState state     = GameState::SPLASH;
    GameState prevState = GameState::MENU_ANIM;  // state to restore on quit cancel
    GameMode  mode      = GameMode::NONE;
    bool      running   = true;
    float     deltaTime = 0.0f;
    DWORD     lastTick  = 0;

    // Mouse
    int  mouseX     = 0;
    int  mouseY     = 0;
    bool mouseClick = false;  // true for exactly one frame on left button up

    // Splash screen
    float splashTimer    = 0.0f;
    static const float SPLASH_DURATION;

    // Maze
    std::vector<Cell> maze;
    int mazeCols = 21, mazeRows = 19;
    int exitCol  = 0,  exitRow  = 0;

    // Computed draw offset (centres maze on screen)
    int offsetX = 0, offsetY = 0;
    int cellSize = 20;

    // Entities
    Player   player;
    Minotaur minotaur;

    // Progression
    int   floor      = 1;
    float floorTimer = 0.0f;
    float totalTime  = 0.0f;
    int   bestDepth  = 0;    // deepest floor reached this run (for depth record SFX)

    // Transition
    float transTimer = 0.0f;
    static const float TRANS_DURATION;

    // Menu selection (0 = EXPLORE, 1 = MINOTAUR)
    int menuSel = 0;

    // Background animated maze
    BgMaze bgMaze;

    // Scores
    Scores scores;

    // Audio / UI events
    float highScoreFlashTimer = 0.0f;   // > 0 = show flashing banner
    float heartbeatTimer      = 0.0f;   // countdown between heartbeat pulses
    bool  dangerMusicActive   = false;  // tracks whether danger track is on
};
const float Game::TRANS_DURATION  = 2.2f;
const float Game::SPLASH_DURATION = 3.5f;

// Global instance (needed for WndProc)
static Game g_game;

// =============================================================================
//  SECTION 3: PROCEDURAL AUDIO ENGINE
//  - Separate audio thread owns all WinMM calls (no main-thread blocking)
//  - Main thread posts commands via atomic flags only
//  - Music streams continuously in 0.5s chunks, SFX overlaid on a 2nd device
//  Link: -lwinmm
// =============================================================================

static const int AUDIO_SR       = 44100;          // sample rate
static const int CHUNK_SAMPLES  = AUDIO_SR / 2;   // 0.5 sec per buffer chunk
static const int NUM_BUFS       = 4;              // circular music buffers
static const int SFX_SAMPLES    = AUDIO_SR * 2;   // 2 sec max SFX

enum class MusicTrack { NONE, MENU, EXPLORE, DANGER };

// ---- Oscillator helpers -----------------------------------------------------
static const float PI2 = 6.28318530f;
static float osc_sin (float p)             { return sinf(p*PI2); }
static float osc_tri (float p)             { float q=fmodf(p,1.f); return q<.5f?(4*q-1):(3-4*q); }
static float osc_saw (float p)             { return 2.f*fmodf(p,1.f)-1.f; }
static float osc_sqr (float p,float d=.5f){ return fmodf(p,1.f)<d?1.f:-1.f; }
static float lp      (float in,float& s,float c){ s=s*c+in*(1-c); return s; }
static float freq    (int semi)            { return 440.f*powf(2.f,semi/12.f); }
static float env_exp (float t,float rate) { return expf(-t*rate); }

// ---- Music synthesisers (fill buffer from time t) ---------------------------

static void synth_menu(int16_t* b, int n, double t0)
{
    static float lp1=0,lp2=0;
    float drone[3]={freq(-24),freq(-17),freq(-12)};
    float mel[]={freq(3),freq(5),freq(0),freq(-2),freq(3),freq(7)};
    float bps=40.f/60.f;  // beats per second
    for(int i=0;i<n;i++){
        double t=t0+(double)i/AUDIO_SR;
        float s=0;
        for(int d=0;d<3;d++) s+=osc_sin((float)(t*drone[d]*(1+d*.002f)))*0.14f;
        s+=osc_sin((float)(t*.28f))*0.05f;  // rumble
        int mi=(int)(t*bps)%6;
        float na=(float)fmod(t*bps,1.0);
        s+=osc_tri((float)(t*mel[mi]))*env_exp(na,3.f)*0.10f;
        s=lp(lp(s,lp1,.86f),lp2,.72f);
        b[i]=(int16_t)(s*26000);
    }
}

static void synth_explore(int16_t* b, int n, double t0)
{
    static float lp1=0,lp2=0;
    float penta[]={freq(-9),freq(-7),freq(-5),freq(-2),freq(0),freq(3),freq(5),freq(7)};
    float bps=72.f/60.f;
    float arp[]={freq(-9)*.5f,freq(-5)*.5f,freq(-2)*.5f,freq(0)*.5f};
    for(int i=0;i<n;i++){
        double t=t0+(double)i/AUDIO_SR;
        float s=0;
        // pad
        s+=osc_sin((float)(t*freq(-9)))*0.09f;
        s+=osc_sin((float)(t*freq(-9)*1.003f))*0.07f;
        // melody
        int mi=(int)(t*bps*.75f)%8;
        float na=(float)fmod(t*bps*.75f,1.0);
        float me=env_exp(na,2.5f)*0.5f+0.08f;
        if(na<.02f) me*=na/.02f;
        s+=osc_tri((float)(t*penta[mi]))*me*0.15f;
        // arp bass
        int ai=(int)(t*bps*2)%4;
        float aa=(float)fmod(t*bps*2,1.0);
        s+=osc_sqr((float)(t*arp[ai]),.4f)*env_exp(aa,4.f)*0.07f;
        // drip
        float dr=2.3f; float da=fmodf((float)t,dr);
        if(da<.15f) s+=osc_sin((float)(t*freq(12)))*env_exp(da,20.f)*0.05f;
        s=lp(lp(s,lp1,.80f),lp2,.65f);
        b[i]=(int16_t)(s*24000);
    }
}

static void synth_danger(int16_t* b, int n, double t0)
{
    static float lp1=0,lp2=0;
    float bps=140.f/60.f;
    float bass[]={freq(-12),freq(-11),freq(-6),freq(-12),freq(-11),freq(-5),freq(-12),freq(-9)};
    for(int i=0;i<n;i++){
        double t=t0+(double)i/AUDIO_SR;
        float s=0;
        float eighth=1.f/(bps*2);
        float bAge=fmodf((float)t,eighth);
        int bi=(int)((float)t/eighth)%8;
        s+=osc_sqr((float)(t*bass[bi]),.45f)*env_exp(bAge,12.f)*0.20f;
        // kick
        float beat=1.f/bps;
        float kAge=fmodf((float)t,beat);
        if((int)((float)t/beat)%2==0)
            s+=osc_sin((float)(t*freq(-36)*(1+env_exp(kAge,30.f)*3)))*env_exp(kAge,18.f)*0.28f;
        // snare
        float sAge=fmodf((float)t-beat,beat*2);
        if(sAge>=0&&sAge<beat)
            s+=(osc_saw((float)(t*freq(-20)))+osc_saw((float)(t*freq(-20)*1.01f)))
               *env_exp(sAge,22.f)*0.10f;
        // stab every 4 beats
        float stAge=fmodf((float)t,beat*4);
        if(stAge<.3f){
            float se=env_exp(stAge,8.f);
            s+=osc_tri((float)(t*freq(-3)))*se*0.13f;
            s+=osc_tri((float)(t*freq(-2)))*se*0.09f;
        }
        s=lp(lp(s,lp1,.75f),lp2,.60f);
        b[i]=(int16_t)(s*24000);
    }
}

// ---- SFX synthesisers -------------------------------------------------------

static void sfx_click(int16_t* b,int n){
    for(int i=0;i<n;i++){float t=(float)i/AUDIO_SR; float e=env_exp(t,40.f);
    b[i]=(int16_t)((osc_sqr(t*freq(12),.3f)*.5f+osc_sin(t*freq(19))*.3f)*e*20000);}}

static void sfx_clear(int16_t* b,int n){
    float notes[]={freq(0),freq(4),freq(7),freq(12)};
    float nd=(float)n/AUDIO_SR/4;
    for(int i=0;i<n;i++){float t=(float)i/AUDIO_SR;
    int ni=std::min((int)(t/nd),3); float nt=fmodf(t,nd);
    b[i]=(int16_t)((osc_tri(t*notes[ni])*env_exp(nt,6.f)*.5f
                   +osc_sin(t*notes[ni]*2)*env_exp(nt,6.f)*.2f)*22000);}}

// Minotaur roar — loud jumpscare
// Layer 1: low guttural growl (descending saw sweep)
// Layer 2: mid screech (detuned square cluster)
// Layer 3: high shriek (fast vibrato sine)
// Layer 4: impact thud at the very start
static void sfx_caught(int16_t* b, int n)
{
    static float lp1=0, lp2=0; lp1=0; lp2=0;
    for(int i=0;i<n;i++)
    {
        float t = (float)i / AUDIO_SR;
        float s = 0.0f;

        // Impact thud — very short low sine boom at t=0
        float thud = osc_sin(t * freq(-24) * (1.0f + expf(-t*30.f)*4.f))
                     * expf(-t * 20.f) * 0.5f;
        s += thud;

        // Guttural growl — descending saw, pitch drops over time
        float growlPitch = freq(-12) * (1.0f + expf(-t*1.5f)*1.2f);
        float growl = osc_saw(t * growlPitch) * expf(-t*1.8f) * 0.35f;
        // add vibrato to growl
        growl *= 1.0f + sinf(t * 18.f) * 0.15f;
        s += growl;

        // Mid screech — two detuned squares, rasping
        float screechPitch = freq(-5) * (1.0f + expf(-t*2.f)*0.8f);
        float screech = (osc_sqr(t * screechPitch,      0.6f)
                       + osc_sqr(t * screechPitch*1.03f, 0.4f))
                       * expf(-t*2.5f) * 0.22f;
        s += screech;

        // High shriek — vibrato sine
        float shriekPitch = freq(7) * (1.0f - t * 0.4f);
        float shriek = osc_sin(t * shriekPitch + sinf(t*25.f)*0.3f)
                       * expf(-t*3.f) * 0.20f;
        s += shriek;

        // Slight low-pass to remove harsh digital aliasing
        s = lp(s, lp1, 0.7f);

        // Hard clip for intentional distortion — makes it rawer
        if (s >  0.9f) s =  0.9f;
        if (s < -0.9f) s = -0.9f;

        b[i] = (int16_t)(s * 30000);
    }
}

static void sfx_highscore(int16_t* b,int n){
    float notes[]={freq(0),freq(4),freq(7),freq(12),freq(16)};
    float ad=.07f; float ta=ad*5;
    for(int i=0;i<n;i++){float t=(float)i/AUDIO_SR; float s=0;
    if(t<ta){int ni=std::min((int)(t/ad),4); float na=fmodf(t,ad);
             s=osc_tri(t*notes[ni])*env_exp(na,5.f)*.5f;}
    else{float e=env_exp(t-ta,2.f); for(int j=0;j<5;j++) s+=osc_tri(t*notes[j])*e*.14f;}
    b[i]=(int16_t)(s*24000);}}

static void sfx_heartbeat(int16_t* b,int n){
    float f=freq(-30);
    for(int i=0;i<n;i++){float t=(float)i/AUDIO_SR; float s=0;
    auto th=[&](float o)->float{float a=t-o; if(a<0||a>.12f)return 0.f;
        return osc_sin(a*f)*env_exp(a,25.f)*.6f;};
    s=th(0)+th(.12f); b[i]=(int16_t)(s*28000);}}

static void sfx_depth(int16_t* b,int n){
    for(int i=0;i<n;i++){float t=(float)i/AUDIO_SR; float e=env_exp(t,5.f);
    b[i]=(int16_t)((osc_sin(t*freq(19))*e*.4f+osc_sin(t*freq(24))*e*.3f
                   +osc_tri(t*freq(16))*e*.2f)*20000);}}

// ---- Thread-safe audio engine -----------------------------------------------

struct AudioCmd { enum Type { NONE, PLAY_MUSIC, STOP_MUSIC, PLAY_SFX, QUIT } type=NONE;
                  MusicTrack track=MusicTrack::NONE; int sfxId=0; };

static HWAVEOUT        g_hMusic   = nullptr;
static HWAVEOUT        g_hSfx     = nullptr;
static WAVEHDR         g_mHdr[NUM_BUFS] = {};
static int16_t*        g_mBuf[NUM_BUFS] = {};
static WAVEHDR         g_sHdr     = {};
static int16_t*        g_sBuf     = nullptr;
static HANDLE          g_audioThread = nullptr;
static HANDLE          g_cmdEvent    = nullptr;

// Command QUEUE — holds up to 8 commands, never loses one
static const int       CMD_QUEUE_SIZE = 8;
static AudioCmd        g_cmdQueue[CMD_QUEUE_SIZE];
static int             g_cmdHead  = 0;
static int             g_cmdTail  = 0;
static CRITICAL_SECTION g_cmdCS;

static volatile MusicTrack g_curTrack    = MusicTrack::NONE;
static volatile double     g_musicTime   = 0.0;
static volatile bool       g_audioRunning= false;

static WAVEFORMATEX makeWfx()
{
    WAVEFORMATEX w={};
    w.wFormatTag=WAVE_FORMAT_PCM; w.nChannels=1;
    w.nSamplesPerSec=AUDIO_SR; w.wBitsPerSample=16;
    w.nBlockAlign=2; w.nAvgBytesPerSec=AUDIO_SR*2;
    return w;
}

// Fill one music chunk based on current track
static void fillChunk(int16_t* buf, double t)
{
    switch(g_curTrack){
        case MusicTrack::MENU:    synth_menu   (buf,CHUNK_SAMPLES,t); break;
        case MusicTrack::EXPLORE: synth_explore(buf,CHUNK_SAMPLES,t); break;
        case MusicTrack::DANGER:  synth_danger (buf,CHUNK_SAMPLES,t); break;
        default: memset(buf,0,CHUNK_SAMPLES*2); break;
    }
}

static void submitMusicBuf(int idx)
{
    if(!g_hMusic) return;
    WAVEHDR& h=g_mHdr[idx];
    if(h.dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(g_hMusic,&h,sizeof(WAVEHDR));
    fillChunk(g_mBuf[idx], g_musicTime);
    g_musicTime += (double)CHUNK_SAMPLES/AUDIO_SR;
    h={};
    h.lpData=(LPSTR)g_mBuf[idx];
    h.dwBufferLength=CHUNK_SAMPLES*2;
    waveOutPrepareHeader(g_hMusic,&h,sizeof(WAVEHDR));
    waveOutWrite(g_hMusic,&h,sizeof(WAVEHDR));
}

static void playSfxOn(int sfxId)
{
    if(!g_hSfx) return;
    waveOutReset(g_hSfx);
    if(g_sHdr.dwFlags & WHDR_PREPARED)
        waveOutUnprepareHeader(g_hSfx,&g_sHdr,sizeof(WAVEHDR));

    int sfxN = AUDIO_SR/2;
    switch(sfxId){
        case 1: sfx_click    (g_sBuf, AUDIO_SR/10); sfxN=AUDIO_SR/10; break;
        case 2: sfx_clear    (g_sBuf, AUDIO_SR/2);  sfxN=AUDIO_SR/2;  break;
        case 3: sfx_caught   (g_sBuf, (int)(AUDIO_SR*1.2f)); sfxN=(int)(AUDIO_SR*1.2f); break;
        case 4: sfx_highscore(g_sBuf, AUDIO_SR);    sfxN=AUDIO_SR;    break;
        case 5: sfx_heartbeat(g_sBuf, AUDIO_SR/4);  sfxN=AUDIO_SR/4;  break;
        case 6: sfx_depth    (g_sBuf, AUDIO_SR/4);  sfxN=AUDIO_SR/4;  break;
    }
    g_sHdr={};
    g_sHdr.lpData=(LPSTR)g_sBuf;
    g_sHdr.dwBufferLength=sfxN*2;
    waveOutPrepareHeader(g_hSfx,&g_sHdr,sizeof(WAVEHDR));
    waveOutWrite(g_hSfx,&g_sHdr,sizeof(WAVEHDR));
}

// Audio thread — owns all WinMM calls, never blocks main thread
static DWORD WINAPI audioThreadProc(LPVOID)
{
    WAVEFORMATEX wfx = makeWfx();

    // Open music device
    if(waveOutOpen(&g_hMusic,WAVE_MAPPER,&wfx,0,0,CALLBACK_NULL)==MMSYSERR_NOERROR)
    {
        for(int i=0;i<NUM_BUFS;i++) g_mBuf[i]=new int16_t[CHUNK_SAMPLES];
        for(int i=0;i<NUM_BUFS;i++) submitMusicBuf(i);
    }

    // Open SFX device — buffer large enough for 2 seconds (roar is 1.2s)
    if(waveOutOpen(&g_hSfx,WAVE_MAPPER,&wfx,0,0,CALLBACK_NULL)==MMSYSERR_NOERROR)
        g_sBuf = new int16_t[AUDIO_SR * 2];

    while(g_audioRunning)
    {
        WaitForSingleObject(g_cmdEvent, 50);
        ResetEvent(g_cmdEvent);

        // Drain ALL queued commands in order — none get dropped
        while(true)
        {
            EnterCriticalSection(&g_cmdCS);
            bool empty = (g_cmdHead == g_cmdTail);
            AudioCmd cmd = {};
            if(!empty)
            {
                cmd = g_cmdQueue[g_cmdHead];
                g_cmdHead = (g_cmdHead + 1) % CMD_QUEUE_SIZE;
            }
            LeaveCriticalSection(&g_cmdCS);
            if(empty) break;

            if(cmd.type == AudioCmd::QUIT) goto done;

            if(cmd.type == AudioCmd::STOP_MUSIC)
            {
                if(g_hMusic){
                    waveOutReset(g_hMusic);
                    for(int i=0;i<NUM_BUFS;i++)
                        if(g_mHdr[i].dwFlags & WHDR_PREPARED)
                            waveOutUnprepareHeader(g_hMusic,&g_mHdr[i],sizeof(WAVEHDR));
                }
                g_curTrack  = MusicTrack::NONE;
                g_musicTime = 0.0;
            }

            if(cmd.type == AudioCmd::PLAY_MUSIC && cmd.track != g_curTrack)
            {
                if(g_hMusic){
                    waveOutReset(g_hMusic);
                    for(int i=0;i<NUM_BUFS;i++)
                        if(g_mHdr[i].dwFlags & WHDR_PREPARED)
                            waveOutUnprepareHeader(g_hMusic,&g_mHdr[i],sizeof(WAVEHDR));
                }
                g_curTrack  = cmd.track;
                g_musicTime = 0.0;
                if(cmd.track != MusicTrack::NONE && g_hMusic)
                    for(int i=0;i<NUM_BUFS;i++) submitMusicBuf(i);
            }

            if(cmd.type == AudioCmd::PLAY_SFX)
                playSfxOn(cmd.sfxId);
        }

        // Refill any music buffers that finished playing
        if(g_hMusic && g_curTrack != MusicTrack::NONE)
        {
            for(int i=0;i<NUM_BUFS;i++){
                WAVEHDR& h=g_mHdr[i];
                if((h.dwFlags & WHDR_DONE) && (h.dwFlags & WHDR_PREPARED)){
                    waveOutUnprepareHeader(g_hMusic,&h,sizeof(WAVEHDR));
                    fillChunk(g_mBuf[i], g_musicTime);
                    g_musicTime+=(double)CHUNK_SAMPLES/AUDIO_SR;
                    h={}; h.lpData=(LPSTR)g_mBuf[i]; h.dwBufferLength=CHUNK_SAMPLES*2;
                    waveOutPrepareHeader(g_hMusic,&h,sizeof(WAVEHDR));
                    waveOutWrite(g_hMusic,&h,sizeof(WAVEHDR));
                }
            }
        }
    }

    done:
    // Cleanup
    if(g_hMusic){
        waveOutReset(g_hMusic);
        for(int i=0;i<NUM_BUFS;i++){
            if(g_mHdr[i].dwFlags & WHDR_PREPARED)
                waveOutUnprepareHeader(g_hMusic,&g_mHdr[i],sizeof(WAVEHDR));
            delete[] g_mBuf[i];
        }
        waveOutClose(g_hMusic); g_hMusic=nullptr;
    }
    if(g_hSfx){
        waveOutReset(g_hSfx);
        if(g_sHdr.dwFlags & WHDR_PREPARED)
            waveOutUnprepareHeader(g_hSfx,&g_sHdr,sizeof(WAVEHDR));
        delete[] g_sBuf;
        waveOutClose(g_hSfx); g_hSfx=nullptr;
    }
    return 0;
}

// ---- Public API (called from main thread — never blocks) --------------------

static void postCmd(AudioCmd cmd)
{
    EnterCriticalSection(&g_cmdCS);
    int next = (g_cmdTail + 1) % CMD_QUEUE_SIZE;
    if (next != g_cmdHead)   // drop if full (shouldn't happen)
    {
        g_cmdQueue[g_cmdTail] = cmd;
        g_cmdTail = next;
    }
    LeaveCriticalSection(&g_cmdCS);
    SetEvent(g_cmdEvent);
}

static void audioInit()
{
    InitializeCriticalSection(&g_cmdCS);
    g_cmdEvent     = CreateEvent(nullptr,FALSE,FALSE,nullptr);
    g_cmdHead      = 0;
    g_cmdTail      = 0;
    g_audioRunning = true;
    g_audioThread  = CreateThread(nullptr,0,audioThreadProc,nullptr,0,nullptr);
}

static void audioShutdown()
{
    if(!g_audioThread) return;
    AudioCmd q; q.type=AudioCmd::QUIT;
    postCmd(q);
    WaitForSingleObject(g_audioThread,3000);
    CloseHandle(g_audioThread); g_audioThread=nullptr;
    CloseHandle(g_cmdEvent);    g_cmdEvent=nullptr;
    DeleteCriticalSection(&g_cmdCS);
}

static void audioStopMusic()
{
    AudioCmd c; c.type=AudioCmd::STOP_MUSIC;
    postCmd(c);
}

static void audioPlayMusic(MusicTrack t)
{
    AudioCmd c; c.type=AudioCmd::PLAY_MUSIC; c.track=t;
    postCmd(c);
}

static void audioPlaySfx(int id)
{
    AudioCmd c; c.type=AudioCmd::PLAY_SFX; c.sfxId=id;
    postCmd(c);
}

// Convenience SFX wrappers
static void sfxClick()      { audioPlaySfx(1); }
static void sfxFloorClear() { audioPlaySfx(2); }
static void sfxCaught()     { audioStopMusic(); audioPlaySfx(3); }  // stop music, then roar
static void sfxHighScore()  { audioPlaySfx(4); }
static void sfxHeartbeat()  { audioPlaySfx(5); }
static void sfxDepthRecord(){ audioPlaySfx(6); }

// =============================================================================
//  SECTION 4: SCORE PERSISTENCE
// =============================================================================

// =============================================================================
//  SECTION 4: SCORE PERSISTENCE
// =============================================================================

static void loadScores(Scores& s)
{
    std::ifstream f(SAVE_FILE, std::ios::binary);
    if (f) f.read((char*)&s, sizeof(Scores));
}

static void saveScores(const Scores& s)
{
    std::ofstream f(SAVE_FILE, std::ios::binary);
    if (f) f.write((const char*)&s, sizeof(Scores));
}

static void updateScores(Game& game)
{
    bool changed = false;
    if (game.mode == GameMode::EXPLORE)
    {
        if (game.floor - 1 > game.scores.exploreFloor ||
           (game.floor - 1 == game.scores.exploreFloor &&
           (game.scores.exploreBest == 0.0f || game.totalTime < game.scores.exploreBest)))
        {
            game.scores.exploreFloor = game.floor - 1;
            game.scores.exploreBest  = game.totalTime;
            changed = true;
        }
    }
    else if (game.mode == GameMode::MINOTAUR)
    {
        if (game.floor - 1 > game.scores.minoFloor ||
           (game.floor - 1 == game.scores.minoFloor &&
           (game.scores.minoTime == 0.0f || game.totalTime < game.scores.minoTime)))
        {
            game.scores.minoFloor = game.floor - 1;
            game.scores.minoTime  = game.totalTime;
            changed = true;
        }
    }
    if (changed)
    {
        saveScores(game.scores);
        sfxHighScore();
        game.highScoreFlashTimer = 4.0f;  // flash for 4 seconds
    }
}

// =============================================================================
//  SECTION 4: MAZE GENERATION  (Recursive Backtracker / DFS)
// =============================================================================

static inline int cellIdx(int col, int row, int cols)
{
    return row * cols + col;
}

static std::vector<std::pair<int,int>> getUnvisitedNeighbours(
    const std::vector<Cell>& maze, int col, int row, int cols, int rows)
{
    std::vector<std::pair<int,int>> result;
    if (row-2 >= 0    && !maze[cellIdx(col,   row-2, cols)].visited) result.push_back({col,   row-2});
    if (row+2 < rows  && !maze[cellIdx(col,   row+2, cols)].visited) result.push_back({col,   row+2});
    if (col-2 >= 0    && !maze[cellIdx(col-2, row,   cols)].visited) result.push_back({col-2, row  });
    if (col+2 < cols  && !maze[cellIdx(col+2, row,   cols)].visited) result.push_back({col+2, row  });
    return result;
}

static void removeWall(std::vector<Cell>& maze,
                       int c1, int r1, int c2, int r2, int cols)
{
    int dc = c2-c1, dr = r2-r1;
    if (dr == -2) {
        maze[cellIdx(c1,r1,  cols)].wallN = false;
        maze[cellIdx(c1,r1-1,cols)].wallN = false;
        maze[cellIdx(c1,r1-1,cols)].wallS = false;
        maze[cellIdx(c2,r2,  cols)].wallS = false;
    } else if (dr == 2) {
        maze[cellIdx(c1,r1,  cols)].wallS = false;
        maze[cellIdx(c1,r1+1,cols)].wallS = false;
        maze[cellIdx(c1,r1+1,cols)].wallN = false;
        maze[cellIdx(c2,r2,  cols)].wallN = false;
    } else if (dc == -2) {
        maze[cellIdx(c1,r1,  cols)].wallW = false;
        maze[cellIdx(c1-1,r1,cols)].wallW = false;
        maze[cellIdx(c1-1,r1,cols)].wallE = false;
        maze[cellIdx(c2,r2,  cols)].wallE = false;
    } else if (dc == 2) {
        maze[cellIdx(c1,r1,  cols)].wallE = false;
        maze[cellIdx(c1+1,r1,cols)].wallE = false;
        maze[cellIdx(c1+1,r1,cols)].wallW = false;
        maze[cellIdx(c2,r2,  cols)].wallW = false;
    }
}

// BFS — returns cell furthest from start
static std::pair<int,int> findFurthest(
    const std::vector<Cell>& maze, int sc, int sr, int cols, int rows)
{
    std::vector<int> dist(cols * rows, -1);
    std::vector<std::pair<int,int>> q;
    q.reserve(cols * rows);
    q.push_back({sc, sr});
    dist[cellIdx(sc, sr, cols)] = 0;
    int best = 0;
    std::pair<int,int> furthest = {sc, sr};

    for (int head = 0; head < (int)q.size(); head++)
    {
        auto [c, r] = q[head];
        int d = dist[cellIdx(c, r, cols)];
        const Cell& cell = maze[cellIdx(c, r, cols)];
        auto push = [&](int nc, int nr){
            if (dist[cellIdx(nc,nr,cols)] == -1){
                dist[cellIdx(nc,nr,cols)] = d+1;
                q.push_back({nc,nr});
                if (d+1 > best){ best=d+1; furthest={nc,nr}; }
            }
        };
        if (!cell.wallN && r > 0)      push(c, r-1);
        if (!cell.wallS && r < rows-1) push(c, r+1);
        if (!cell.wallW && c > 0)      push(c-1, r);
        if (!cell.wallE && c < cols-1) push(c+1, r);
    }
    return furthest;
}

// Full instant generation
static void generateMaze(std::vector<Cell>& maze, int cols, int rows)
{
    maze.assign(cols * rows, Cell{});
    std::stack<std::pair<int,int>> stk;
    maze[cellIdx(1,1,cols)].visited = true;
    stk.push({1,1});
    while (!stk.empty())
    {
        auto [c, r] = stk.top();
        auto nb = getUnvisitedNeighbours(maze, c, r, cols, rows);
        if (!nb.empty()){
            auto [nc,nr] = nb[rand() % (int)nb.size()];
            removeWall(maze, c, r, nc, nr, cols);
            maze[cellIdx(nc,nr,cols)].visited = true;
            stk.push({nc,nr});
        } else stk.pop();
    }
}

// Compute draw metrics so maze is always centred
static void computeLayout(Game& game)
{
    // Pick cell size so maze fits with some margin
    int maxCellW = (SCREEN_W - 20) / game.mazeCols;
    int maxCellH = (SCREEN_H - 80) / game.mazeRows;  // 80px headroom for HUD
    game.cellSize = std::min(maxCellW, maxCellH);
    game.cellSize = std::max(game.cellSize, 8);   // minimum 8px

    game.offsetX = (SCREEN_W - game.mazeCols * game.cellSize) / 2;
    game.offsetY = 40 + (SCREEN_H - 40 - game.mazeRows * game.cellSize) / 2;
}

// Start a new floor
static void startFloor(Game& game)
{
    auto [cols, rows] = getMazeSize(game.floor);
    game.mazeCols = cols;
    game.mazeRows = rows;

    generateMaze(game.maze, cols, rows);
    computeLayout(game);

    // Player start
    game.player.col = game.player.targetCol = 1;
    game.player.row = game.player.targetRow = 1;
    game.player.drawX = (float)(game.offsetX + 1 * game.cellSize);
    game.player.drawY = (float)(game.offsetY + 1 * game.cellSize);
    game.player.moving = false;
    game.player.alive  = true;
    game.player.stepsFromStart = 0;

    // Exit at furthest cell
    auto [ec, er] = findFurthest(game.maze, 1, 1, cols, rows);
    game.exitCol = ec;
    game.exitRow = er;

    // Minotaur — hidden at spawn point, waits until player moves 5 steps
    if (game.mode == GameMode::MINOTAUR)
    {
        game.minotaur.col = game.minotaur.targetCol = 1;
        game.minotaur.row = game.minotaur.targetRow = 1;
        game.minotaur.drawX = (float)(game.offsetX + 1 * game.cellSize);
        game.minotaur.drawY = (float)(game.offsetY + 1 * game.cellSize);
        game.minotaur.moving    = false;
        game.minotaur.moveTimer = 0.0f;
        game.minotaur.spawned   = false;

        float speed = getMinotaurSpeed(game.floor);
        game.minotaur.movePeriod = 1.0f / speed;
    }

    game.floorTimer = 0.0f;
}

// =============================================================================
//  SECTION 5: BACKGROUND ANIMATED MAZE
// =============================================================================

static void initBgMaze(BgMaze& bg)
{
    bg.cols = 41; bg.rows = 31;
    bg.cells.assign(bg.cols * bg.rows, Cell{});
    while (!bg.stk.empty()) bg.stk.pop();
    bg.cells[bg.rows/2 * bg.cols + bg.cols/2].visited = true;
    bg.stk.push({bg.cols/2, bg.rows/2});
    bg.done = false;
    bg.stepTimer = 0.0f;
}

static void stepBgMaze(BgMaze& bg)
{
    if (bg.done || bg.stk.empty()) { bg.done = true; return; }
    auto [c, r] = bg.stk.top();
    auto nb = getUnvisitedNeighbours(bg.cells, c, r, bg.cols, bg.rows);
    if (!nb.empty()){
        auto [nc,nr] = nb[rand() % (int)nb.size()];
        removeWall(bg.cells, c, r, nc, nr, bg.cols);
        bg.cells[cellIdx(nc,nr,bg.cols)].visited = true;
        bg.stk.push({nc,nr});
    } else bg.stk.pop();
    if (bg.stk.empty()) bg.done = true;
}

// =============================================================================
//  SECTION 6: INPUT
// =============================================================================

static bool g_keys[256]       = {};
static bool g_keyPressed[256] = {};

static void handleKeyDown(WPARAM w){ if(w<256){if(!g_keys[w])g_keyPressed[w]=true; g_keys[w]=true;} }
static void handleKeyUp(WPARAM w)  { if(w<256) g_keys[w]=false; }
static void clearFrameKeys()       { memset(g_keyPressed,0,sizeof(g_keyPressed)); }

static bool tryMove(Game& game, int dc, int dr)
{
    if (game.player.moving || !game.player.alive) return false;
    int c = game.player.col, r = game.player.row;
    const Cell& cell = game.maze[cellIdx(c, r, game.mazeCols)];
    if (dr==-1 && cell.wallN) return false;
    if (dr== 1 && cell.wallS) return false;
    if (dc==-1 && cell.wallW) return false;
    if (dc== 1 && cell.wallE) return false;
    game.player.targetCol = c + dc;
    game.player.targetRow = r + dr;
    game.player.moving = true;
    game.player.stepsFromStart++;

    // Trigger minotaur spawn after 10 steps
    if (game.mode == GameMode::MINOTAUR &&
        !game.minotaur.spawned &&
        game.player.stepsFromStart >= 10)
    {
        game.minotaur.spawned = true;
        // Hard cut to danger music
        audioPlayMusic(MusicTrack::DANGER);
        game.dangerMusicActive = true;
    }

    return true;
}

// Hit-test helper — returns true if point (px,py) is inside rect
static bool hitTest(int px, int py, int rx, int ry, int rw, int rh)
{
    return px >= rx && px < rx+rw && py >= ry && py < ry+rh;
}

// Start a game mode — shared between keyboard and mouse confirm
static void startGame(Game& game, GameMode m)
{
    sfxClick();
    game.mode      = m;
    game.floor     = 1;
    game.totalTime = 0.0f;
    game.bestDepth = 0;
    game.dangerMusicActive   = false;
    game.highScoreFlashTimer = 0.0f;
    startFloor(game);
    audioPlayMusic(MusicTrack::EXPLORE);
    game.state = GameState::PLAYING;
}

static void processInput(Game& game)
{
    // =========================================================================
    // CONFIRM QUIT — Y/N keys or clickable buttons
    // =========================================================================
    if (game.state == GameState::CONFIRM_QUIT)
    {
        // MUST match renderConfirmQuit exactly: ph=200, yesY=py+116, yesH=40
        int pw=360, ph=200;
        int px=SCREEN_W/2-pw/2, py=SCREEN_H/2-ph/2;
        int yesX=px+30,     yesY=py+116, yesW=130, yesH=40;
        int noX =px+pw-160, noY =py+116, noW =130, noH =40;

        bool confirmYes = g_keyPressed['Y'] || g_keyPressed[VK_RETURN]
                       || (game.mouseClick && hitTest(game.mouseX,game.mouseY,yesX,yesY,yesW,yesH));
        bool confirmNo  = g_keyPressed['N'] || g_keyPressed[VK_BACK] || g_keyPressed[VK_ESCAPE]
                       || (game.mouseClick && hitTest(game.mouseX,game.mouseY,noX,noY,noW,noH));

        if (confirmYes) { game.running = false; }
        if (confirmNo)  { sfxClick(); game.state = game.prevState; }
        game.mouseClick = false;
        return;
    }

    // =========================================================================
    // PAUSED — Resume or Main Menu (keyboard or mouse)
    // =========================================================================
    if (game.state == GameState::PAUSED)
    {
        // MUST match renderPaused exactly: ph=220, resumeY=py+88, menuY=py+140, h=40
        int pw=360, ph=220;
        int px=SCREEN_W/2-pw/2, py=SCREEN_H/2-ph/2;
        int resumeX=px+30, resumeY=py+88,  resumeW=pw-60, resumeH=40;
        int menuX  =px+30, menuY  =py+140, menuW  =pw-60, menuH  =40;

        bool doResume = g_keyPressed[VK_ESCAPE] || g_keyPressed[VK_RETURN]
                     || (game.mouseClick && hitTest(game.mouseX,game.mouseY,resumeX,resumeY,resumeW,resumeH));
        bool doMenu   = g_keyPressed['M']
                     || (game.mouseClick && hitTest(game.mouseX,game.mouseY,menuX,menuY,menuW,menuH));

        if (doResume)
        {
            sfxClick();
            game.state = GameState::PLAYING;
        }
        if (doMenu)
        {
            sfxClick();
            updateScores(game);
            audioPlayMusic(MusicTrack::MENU);
            game.dangerMusicActive = false;
            initBgMaze(game.bgMaze);
            game.state = GameState::MENU_ANIM;
        }
        game.mouseClick = false;
        return;
    }

    // =========================================================================
    // MENU
    // =========================================================================
    if (game.state == GameState::MENU || game.state == GameState::MENU_ANIM)
    {
        // Button layout mirrors renderMenu
        int btnW = 280, btnH = 180, btnGap = 24;
        int titleH=110, subtitleH=32, scoreH=74, promptH=58, hintsH=38;
        int topUsed    = titleH+subtitleH+scoreH;
        int bottomUsed = promptH+hintsH;
        int remaining  = SCREEN_H - topUsed - bottomUsed;
        int btnY  = topUsed + (remaining - btnH) / 2;
        int btn1X = SCREEN_W/2 - btnW - btnGap/2;
        int btn2X = SCREEN_W/2 + btnGap/2;
        // Exit button — MUST match renderMenu exactly:
        // promptY = btnY + btnH + 10, exitY = promptY + 58
        int exitW=160, exitH=36;
        int exitX = SCREEN_W/2 - exitW/2;
        int promptY = btnY + btnH + 10;
        int exitY = promptY + 58;

        // ESC → confirm quit
        if (g_keyPressed[VK_ESCAPE])
        {
            sfxClick();
            game.prevState = game.state;
            game.state     = GameState::CONFIRM_QUIT;
            game.mouseClick = false;
            return;
        }

        // Keyboard selection
        if (g_keyPressed[VK_LEFT]  || g_keyPressed['A']) { sfxClick(); game.menuSel = 0; }
        if (g_keyPressed[VK_RIGHT] || g_keyPressed['D']) { sfxClick(); game.menuSel = 1; }
        if (g_keyPressed[VK_UP]    || g_keyPressed['W']) { sfxClick(); game.menuSel = 0; }
        if (g_keyPressed[VK_DOWN]  || g_keyPressed['S']) { sfxClick(); game.menuSel = 1; }

        // Keyboard confirm
        if (g_keyPressed[VK_RETURN] || g_keyPressed[VK_SPACE])
            startGame(game, game.menuSel == 0 ? GameMode::EXPLORE : GameMode::MINOTAUR);

        // Mouse hover — update selection
        if (hitTest(game.mouseX, game.mouseY, btn1X, btnY, btnW, btnH)) game.menuSel = 0;
        if (hitTest(game.mouseX, game.mouseY, btn2X, btnY, btnW, btnH)) game.menuSel = 1;

        // Mouse click on buttons
        if (game.mouseClick)
        {
            if (hitTest(game.mouseX, game.mouseY, btn1X, btnY, btnW, btnH))
                startGame(game, GameMode::EXPLORE);
            else if (hitTest(game.mouseX, game.mouseY, btn2X, btnY, btnW, btnH))
                startGame(game, GameMode::MINOTAUR);
            else if (hitTest(game.mouseX, game.mouseY, exitX, exitY, exitW, exitH))
            {
                sfxClick();
                game.prevState = game.state;
                game.state     = GameState::CONFIRM_QUIT;
            }
        }
        game.mouseClick = false;
        return;
    }

    // =========================================================================
    // GAME OVER
    // =========================================================================
    if (game.state == GameState::GAMEOVER)
    {
        // MUST match renderGameOver exactly: menuBX=SCREEN_W/2-120, menuBY=py+185, w=240, h=40
        int pw=400, ph=260;
        int px=SCREEN_W/2-pw/2, py=SCREEN_H/2-ph/2;
        int menuBX=SCREEN_W/2-120, menuBY=py+185, menuBW=240, menuBH=40;

        if (g_keyPressed[VK_ESCAPE])
        {
            sfxClick();
            game.prevState = GameState::GAMEOVER;
            game.state     = GameState::CONFIRM_QUIT;
            game.mouseClick = false;
            return;
        }
        if (g_keyPressed[VK_RETURN] || g_keyPressed[VK_SPACE]
            || (game.mouseClick && hitTest(game.mouseX,game.mouseY,menuBX,menuBY,menuBW,menuBH)))
        {
            sfxClick();
            audioPlayMusic(MusicTrack::MENU);
            initBgMaze(game.bgMaze);
            game.state = GameState::MENU_ANIM;
        }
        game.mouseClick = false;
        return;
    }

    // =========================================================================
    // PLAYING
    // =========================================================================
    if (game.state == GameState::PLAYING)
    {
        if (g_keyPressed[VK_ESCAPE])
        {
            sfxClick();
            game.state = GameState::PAUSED;
            game.mouseClick = false;
            return;
        }
        if (g_keys[VK_UP]    || g_keys['W']) tryMove(game,  0, -1);
        if (g_keys[VK_DOWN]  || g_keys['S']) tryMove(game,  0,  1);
        if (g_keys[VK_LEFT]  || g_keys['A']) tryMove(game, -1,  0);
        if (g_keys[VK_RIGHT] || g_keys['D']) tryMove(game,  1,  0);
    }

    game.mouseClick = false;
}

// =============================================================================
//  SECTION 7: MINOTAUR AI  (BFS pathfinding)
// =============================================================================

// Returns next step toward target using BFS
static std::pair<int,int> bfsNextStep(
    const std::vector<Cell>& maze, int cols, int rows,
    int fromC, int fromR, int toC, int toR)
{
    if (fromC == toC && fromR == toR) return {fromC, fromR};

    std::vector<int>              dist(cols * rows, -1);
    std::vector<std::pair<int,int>> prev(cols * rows, {-1,-1});
    std::vector<std::pair<int,int>> q;
    q.reserve(cols * rows);

    dist[cellIdx(fromC, fromR, cols)] = 0;
    q.push_back({fromC, fromR});

    bool found = false;
    for (int head = 0; head < (int)q.size() && !found; head++)
    {
        auto [c, r] = q[head];
        int d = dist[cellIdx(c,r,cols)];
        const Cell& cell = maze[cellIdx(c,r,cols)];

        auto push = [&](int nc, int nr){
            if (dist[cellIdx(nc,nr,cols)] == -1){
                dist[cellIdx(nc,nr,cols)] = d+1;
                prev[cellIdx(nc,nr,cols)] = {c,r};
                q.push_back({nc,nr});
                if (nc==toC && nr==toR) found=true;
            }
        };
        if (!cell.wallN && r > 0)      push(c, r-1);
        if (!cell.wallS && r < rows-1) push(c, r+1);
        if (!cell.wallW && c > 0)      push(c-1, r);
        if (!cell.wallE && c < cols-1) push(c+1, r);
    }

    if (!found) return {fromC, fromR};

    // Trace back to find first step
    std::pair<int,int> cur = {toC, toR};
    while (true)
    {
        auto p = prev[cellIdx(cur.first, cur.second, cols)];
        if (p.first == fromC && p.second == fromR) return cur;
        cur = p;
    }
}

// =============================================================================
//  SECTION 8: UPDATE
// =============================================================================

static const float PLAYER_SPEED    = 10.0f;   // cells/sec
static const float MINOTAUR_LERP   = 8.0f;    // visual lerp speed

static void updateEntity(float& drawX, float& drawY,
                         int targetCol, int targetRow,
                         int& col, int& row,
                         bool& moving,
                         float speed, float dt,
                         int offsetX, int offsetY, int cellSize)
{
    float targetX = (float)(offsetX + targetCol * cellSize);
    float targetY = (float)(offsetY + targetRow * cellSize);

    if (moving)
    {
        float spd = speed * cellSize * dt;
        float dx  = targetX - drawX;
        float dy  = targetY - drawY;
        float dist = sqrtf(dx*dx + dy*dy);
        if (dist <= spd){
            drawX = targetX; drawY = targetY;
            col = targetCol; row = targetRow;
            moving = false;
        } else {
            drawX += (dx/dist)*spd;
            drawY += (dy/dist)*spd;
        }
    }
}

static void update(Game& game)
{
    float dt = game.deltaTime;

    // ---- Splash screen ------------------------------------------------------
    if (game.state == GameState::SPLASH)
    {
        game.splashTimer += dt;
        // Also animate the bg maze during splash so it's ready when menu appears
        if (!game.bgMaze.done)
        {
            game.bgMaze.stepTimer += dt;
            while (game.bgMaze.stepTimer >= game.bgMaze.stepPeriod && !game.bgMaze.done)
            {
                stepBgMaze(game.bgMaze);
                game.bgMaze.stepTimer -= game.bgMaze.stepPeriod;
            }
        }
        if (game.splashTimer >= Game::SPLASH_DURATION)
        {
            audioPlayMusic(MusicTrack::MENU);
            game.state = GameState::MENU_ANIM;
        }
        return;
    }

    // ---- Background maze animation ------------------------------------------
    if (game.state == GameState::MENU_ANIM || game.state == GameState::MENU)
    {
        if (!game.bgMaze.done)
        {
            game.bgMaze.stepTimer += dt;
            while (game.bgMaze.stepTimer >= game.bgMaze.stepPeriod && !game.bgMaze.done)
            {
                stepBgMaze(game.bgMaze);
                game.bgMaze.stepTimer -= game.bgMaze.stepPeriod;
            }
        }
    }

    if (game.state != GameState::PLAYING && game.state != GameState::TRANSITION)
        return;

    // ---- Timers -------------------------------------------------------------
    if (game.state == GameState::PLAYING)
    {
        game.floorTimer += dt;
        game.totalTime  += dt;
        if (game.highScoreFlashTimer > 0.0f)
            game.highScoreFlashTimer -= dt;
    }

    // ---- Player movement ----------------------------------------------------
    updateEntity(game.player.drawX, game.player.drawY,
                 game.player.targetCol, game.player.targetRow,
                 game.player.col, game.player.row,
                 game.player.moving,
                 PLAYER_SPEED, dt,
                 game.offsetX, game.offsetY, game.cellSize);

    // ---- Minotaur -----------------------------------------------------------
    if (game.mode == GameMode::MINOTAUR &&
        game.minotaur.spawned &&
        game.state == GameState::PLAYING)
    {
        // Visual lerp
        float targetX = (float)(game.offsetX + game.minotaur.targetCol * game.cellSize);
        float targetY = (float)(game.offsetY + game.minotaur.targetRow * game.cellSize);
        game.minotaur.drawX += (targetX - game.minotaur.drawX) * MINOTAUR_LERP * dt;
        game.minotaur.drawY += (targetY - game.minotaur.drawY) * MINOTAUR_LERP * dt;

        // Step timer
        game.minotaur.moveTimer += dt;
        if (!game.minotaur.moving && game.minotaur.moveTimer >= game.minotaur.movePeriod)
        {
            game.minotaur.moveTimer = 0.0f;
            auto [nc, nr] = bfsNextStep(
                game.maze, game.mazeCols, game.mazeRows,
                game.minotaur.col, game.minotaur.row,
                game.player.col,   game.player.row);
            game.minotaur.targetCol = nc;
            game.minotaur.targetRow = nr;
            game.minotaur.col       = nc;
            game.minotaur.row       = nr;
        }

        // Heartbeat when within 5 cells (BFS distance)
        {
            // Quick Manhattan estimate for performance
            int mdist = abs(game.minotaur.col - game.player.col)
                      + abs(game.minotaur.row - game.player.row);
            if (mdist <= 5)
            {
                game.heartbeatTimer -= dt;
                if (game.heartbeatTimer <= 0.0f)
                {
                    sfxHeartbeat();
                    // Pulse faster the closer the minotaur is
                    game.heartbeatTimer = 0.2f + (mdist / 5.0f) * 0.5f;
                }
            }
            else
            {
                game.heartbeatTimer = 0.0f;
            }
        }

        // Collision with player
        float mdx = game.minotaur.drawX - game.player.drawX;
        float mdy = game.minotaur.drawY - game.player.drawY;
        float threshold = (float)game.cellSize * 0.75f;
        if (sqrtf(mdx*mdx + mdy*mdy) < threshold)
        {
            game.player.alive = false;
            sfxCaught();
            updateScores(game);
            // Wipe all key state so no stale keypresses fire on game over screen
            memset(g_keys,       0, sizeof(g_keys));
            memset(g_keyPressed, 0, sizeof(g_keyPressed));
            game.state = GameState::GAMEOVER;
            return;
        }
    }

    // ---- Reached exit? ------------------------------------------------------
    if (game.state == GameState::PLAYING &&
        !game.player.moving &&
        game.player.col == game.exitCol &&
        game.player.row == game.exitRow)
    {
        sfxFloorClear();

        // Depth record check
        if (game.floor > game.bestDepth)
        {
            game.bestDepth = game.floor;
            sfxDepthRecord();
        }

        game.floor++;
        game.transTimer = 0.0f;
        game.state = GameState::TRANSITION;
    }

    // ---- Transition timer ---------------------------------------------------
    if (game.state == GameState::TRANSITION)
    {
        game.transTimer += dt;
        if (game.transTimer >= Game::TRANS_DURATION)
        {
            startFloor(game);
            game.state = GameState::PLAYING;
        }
    }
}

// =============================================================================
//  SECTION 9: RENDERING HELPERS
// =============================================================================

static void fillRect(HDC dc, int x, int y, int w, int h, COLORREF col)
{
    HBRUSH br = CreateSolidBrush(col);
    RECT r    = {x, y, x+w, y+h};
    FillRect(dc, &r, br);
    DeleteObject(br);
}

static void drawRect(HDC dc, int x, int y, int w, int h, COLORREF col, int thickness=1)
{
    HPEN pen = CreatePen(PS_SOLID, thickness, col);
    HPEN old = (HPEN)SelectObject(dc, pen);
    SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, x, y, x+w, y+h);
    SelectObject(dc, old);
    DeleteObject(pen);
}

static void drawTextAt(HDC dc, const std::string& text,
                        int x, int y, int fontSize, COLORREF col,
                        bool bold=true, const char* face="Courier New")
{
    HFONT font = CreateFontA(fontSize,0,0,0,bold?FW_BOLD:FW_NORMAL,
        FALSE,FALSE,FALSE,ANSI_CHARSET,OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,
        DEFAULT_PITCH|FF_DONTCARE, face);
    HFONT old = (HFONT)SelectObject(dc, font);
    SetTextColor(dc, col);
    SetBkMode(dc, TRANSPARENT);
    TextOutA(dc, x, y, text.c_str(), (int)text.size());
    SelectObject(dc, old);
    DeleteObject(font);
}

static void drawTextCentred(HDC dc, const std::string& text,
                             int cx, int cy, int fontSize, COLORREF col,
                             bool bold=true, const char* face="Courier New")
{
    HFONT font = CreateFontA(fontSize,0,0,0,bold?FW_BOLD:FW_NORMAL,
        FALSE,FALSE,FALSE,ANSI_CHARSET,OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,
        DEFAULT_PITCH|FF_DONTCARE, face);
    HFONT old = (HFONT)SelectObject(dc, font);
    SetTextColor(dc, col);
    SetBkMode(dc, TRANSPARENT);
    SIZE sz;
    GetTextExtentPoint32A(dc, text.c_str(), (int)text.size(), &sz);
    TextOutA(dc, cx-sz.cx/2, cy-sz.cy/2, text.c_str(), (int)text.size());
    SelectObject(dc, old);
    DeleteObject(font);
}

// Draw a stone-textured panel
static void drawStonePanel(HDC dc, int x, int y, int w, int h,
                            COLORREF bgCol, COLORREF borderCol)
{
    fillRect(dc, x,   y,   w,   h,   RGB(18,14,10));       // deep shadow
    fillRect(dc, x+2, y+2, w-4, h-4, bgCol);               // main fill
    // highlight top-left
    fillRect(dc, x+2, y+2, w-4, 2,   RGB(80,65,50));
    fillRect(dc, x+2, y+2, 2,   h-4, RGB(80,65,50));
    // border
    drawRect(dc, x, y, w, h, borderCol, 2);
}

// =============================================================================
//  SECTION 10: RENDER — BACKGROUND MAZE
// =============================================================================

static void renderBgMaze(HDC dc, const BgMaze& bg)
{
    // Scale bg maze to fill screen
    int cs   = std::min(SCREEN_W / bg.cols, SCREEN_H / bg.rows);
    int offX = (SCREEN_W - bg.cols * cs) / 2;
    int offY = (SCREEN_H - bg.rows * cs) / 2;
    const int W = 1;

    for (int r = 0; r < bg.rows; r++)
    for (int c = 0; c < bg.cols; c++)
    {
        int px = offX + c * cs;
        int py = offY + r * cs;
        const Cell& cell = bg.cells[r * bg.cols + c];

        fillRect(dc, px, py, cs, cs, bg.cells[r*bg.cols+c].visited ?
                 RGB(18,14,11) : RGB(12,10,8));

        if (cell.wallN && r > 0)
            fillRect(dc, px, py, cs, W, C_WALL_DARK);
        if (cell.wallS && r < bg.rows-1)
            fillRect(dc, px, py+cs-W, cs, W, C_WALL_DARK);
        if (cell.wallW && c > 0)
            fillRect(dc, px, py, W, cs, C_WALL_DARK);
        if (cell.wallE && c < bg.cols-1)
            fillRect(dc, px+cs-W, py, W, cs, C_WALL_DARK);
    }
}

// =============================================================================
//  SECTION 11: RENDER — MENU
// =============================================================================

// Helper: draw a clickable button with hover highlight
static bool drawButton(HDC dc, int x, int y, int w, int h,
                       const std::string& label, int fontSize,
                       COLORREF normalBg, COLORREF hoverBg,
                       COLORREF borderCol, COLORREF textCol,
                       int mouseX, int mouseY)
{
    bool hover = hitTest(mouseX, mouseY, x, y, w, h);
    fillRect(dc, x, y, w, h, hover ? hoverBg : normalBg);
    drawRect(dc, x, y, w, h, borderCol, hover ? 2 : 1);
    drawTextCentred(dc, label, x + w/2, y + h/2, fontSize, textCol);
    return hover;
}

static void renderMenu(HDC dc, const Game& game)
{
    // Background animated maze (dim it with ONE dark rect, not three)
    renderBgMaze(dc, game.bgMaze);
    fillRect(dc, 0, 0, SCREEN_W, SCREEN_H, RGB(6, 4, 2));

    // =========================================================================
    // LAYOUT — compute all positions relative to screen centre
    // =========================================================================
    // Button dimensions (larger than before)
    int btnW   = 280, btnH = 180;
    int btnGap = 24;
    int totalBtnW = btnW * 2 + btnGap;

    // Vertical regions (from top):
    //   titleH   = 110px  (title bar)
    //   subtitleH = 32px
    //   scoreH   = 70px
    //   gapAboveBtn = flexible (centres buttons)
    //   btnH     = 180px
    //   promptH  = 55px
    //   (bottom hints bar = 38px fixed at bottom)
    int titleH    = 110;
    int subtitleH = 32;
    int scoreH    = 74;
    int promptH   = 58;
    int hintsH    = 38;
    int topUsed   = titleH + subtitleH + scoreH;
    int bottomUsed = promptH + hintsH;
    int remaining  = SCREEN_H - topUsed - bottomUsed;
    // Centre buttons in remaining space
    int btnY  = topUsed + (remaining - btnH) / 2;
    int btn1X = SCREEN_W/2 - btnW - btnGap/2;
    int btn2X = SCREEN_W/2 + btnGap/2;

    // =========================================================================
    // TITLE BAR
    // =========================================================================
    fillRect(dc, 0, 0, SCREEN_W, titleH, RGB(18, 12, 6));
    fillRect(dc, 0, titleH - 2, SCREEN_W, 2, RGB(180, 110, 20));
    drawTextCentred(dc, "MEIRO", SCREEN_W/2, titleH/2, 80, RGB(255, 200, 60));

    // =========================================================================
    // SUBTITLE
    // =========================================================================
    int subY = titleH;
    fillRect(dc, 0, subY, SCREEN_W, subtitleH, RGB(12, 8, 4));
    fillRect(dc, 0, subY + subtitleH - 1, SCREEN_W, 1, RGB(80, 55, 15));
    drawTextCentred(dc, "~ A Procedural Dungeon Maze ~",
                    SCREEN_W/2, subY + subtitleH/2, 17, RGB(210, 175, 110));

    // =========================================================================
    // BEST SCORES PANEL
    // =========================================================================
    {
        char buf[80];
        int by  = titleH + subtitleH;
        int bpad = 16;
        // Draw two score lines centred
        if (game.scores.exploreFloor > 0) {
            sprintf(buf, "EXPLORE  >  Floor %d     Time  %02d:%02d",
                game.scores.exploreFloor,
                (int)game.scores.exploreBest / 60,
                (int)game.scores.exploreBest % 60);
            drawTextCentred(dc, buf, SCREEN_W/2, by + bpad + 8,  16, RGB(100, 180, 255));
        } else {
            drawTextCentred(dc, "EXPLORE  >  No record yet",
                            SCREEN_W/2, by + bpad + 8,  16, RGB(80, 120, 160));
        }
        if (game.scores.minoFloor > 0) {
            sprintf(buf, "MINOTAUR >  Floor %d     Time  %02d:%02d",
                game.scores.minoFloor,
                (int)game.scores.minoTime / 60,
                (int)game.scores.minoTime % 60);
            drawTextCentred(dc, buf, SCREEN_W/2, by + bpad + 36, 16, RGB(255, 110, 110));
        } else {
            drawTextCentred(dc, "MINOTAUR >  No record yet",
                            SCREEN_W/2, by + bpad + 36, 16, RGB(150, 80, 80));
        }
        // bottom rule
        fillRect(dc, SCREEN_W/2 - 260, by + scoreH - 2, 520, 1, RGB(100, 70, 20));
    }

    // =========================================================================
    // MODE BUTTONS  (vertically centred)
    // =========================================================================

    // --- EXPLORE ---
    {
        bool sel  = (game.menuSel == 0);
        fillRect(dc, btn1X, btnY, btnW, btnH,
                 sel ? RGB(30, 60, 100) : RGB(15, 28, 45));
        drawRect(dc, btn1X, btnY, btnW, btnH,
                 sel ? RGB(80, 160, 255) : RGB(50, 80, 120), sel ? 3 : 1);
        if (sel)
            drawRect(dc, btn1X+5, btnY+5, btnW-10, btnH-10, RGB(50, 110, 190), 1);

        drawTextCentred(dc, "EXPLORE",
                        btn1X + btnW/2, btnY + 44, 30, RGB(120, 200, 255));
        fillRect(dc, btn1X + 24, btnY + 68, btnW - 48, 1, RGB(50, 90, 140));
        drawTextCentred(dc, "Navigate the maze",
                        btn1X + btnW/2, btnY + 98,  17, RGB(190, 215, 235), false);
        drawTextCentred(dc, "Find the exit",
                        btn1X + btnW/2, btnY + 122, 17, RGB(190, 215, 235), false);
        drawTextCentred(dc, "Beat your best time",
                        btn1X + btnW/2, btnY + 148, 17, RGB(190, 215, 235), false);
    }

    // --- MINOTAUR ---
    {
        bool sel  = (game.menuSel == 1);
        fillRect(dc, btn2X, btnY, btnW, btnH,
                 sel ? RGB(100, 20, 20) : RGB(45, 10, 10));
        drawRect(dc, btn2X, btnY, btnW, btnH,
                 sel ? RGB(255, 80, 80) : RGB(120, 40, 40), sel ? 3 : 1);
        if (sel)
            drawRect(dc, btn2X+5, btnY+5, btnW-10, btnH-10, RGB(190, 50, 50), 1);

        drawTextCentred(dc, "MINOTAUR",
                        btn2X + btnW/2, btnY + 44, 30, RGB(255, 110, 110));
        fillRect(dc, btn2X + 24, btnY + 68, btnW - 48, 1, RGB(130, 40, 40));
        drawTextCentred(dc, "A beast hunts you",
                        btn2X + btnW/2, btnY + 98,  17, RGB(235, 205, 205), false);
        drawTextCentred(dc, "Escape through the dark",
                        btn2X + btnW/2, btnY + 122, 17, RGB(235, 205, 205), false);
        drawTextCentred(dc, "Or be caught",
                        btn2X + btnW/2, btnY + 148, 17, RGB(235, 205, 205), false);
    }

    // =========================================================================
    // SELECTION PROMPT  (centred below buttons)
    // =========================================================================
    int promptY = btnY + btnH + 10;
    drawTextCentred(dc, "[ LEFT / RIGHT  to select ]",
                    SCREEN_W/2, promptY + 16, 16, RGB(170, 148, 100));
    drawTextCentred(dc, "[ ENTER or SPACE to begin ]",
                    SCREEN_W/2, promptY + 38, 16, RGB(230, 185, 55));

    // =========================================================================
    // EXIT BUTTON  (clickable, centred below prompt)
    // =========================================================================
    int exitW=160, exitH=36;
    int exitX = SCREEN_W/2 - exitW/2;
    int exitY = promptY + 58;
    drawButton(dc, exitX, exitY, exitW, exitH, "EXIT GAME", 15,
               RGB(40,10,10), RGB(70,18,18), RGB(180,50,50),
               RGB(220,80,80), game.mouseX, game.mouseY);

    // =========================================================================
    // CONTROL HINTS BAR  (fixed at very bottom, fully centred)
    // =========================================================================
    int barY = SCREEN_H - hintsH;
    fillRect(dc, 0, barY, SCREEN_W, hintsH, RGB(8, 6, 4));
    fillRect(dc, 0, barY, SCREEN_W, 1, RGB(100, 70, 20));

    // Build hint string and draw it fully centred as one line
    drawTextCentred(dc,
        "LEFT / RIGHT  Select       ENTER  Confirm       ESC  Quit",
        SCREEN_W/2, barY + hintsH/2, 15, RGB(170, 148, 100));
}

// =============================================================================
//  SECTION 12: RENDER — GAME MAZE
// =============================================================================

static void renderMaze(HDC dc, const Game& game)
{
    const int W  = 2;
    const int cs = game.cellSize;

    for (int r = 0; r < game.mazeRows; r++)
    for (int c = 0; c < game.mazeCols; c++)
    {
        int px = game.offsetX + c * cs;
        int py = game.offsetY + r * cs;
        const Cell& cell = game.maze[cellIdx(c, r, game.mazeCols)];

        fillRect(dc, px, py, cs, cs, C_FLOOR);

        if (cell.wallN) fillRect(dc, px,      py,      cs, W,  C_WALL);
        if (cell.wallS) fillRect(dc, px,      py+cs-W, cs, W,  C_WALL);
        if (cell.wallW) fillRect(dc, px,      py,      W,  cs, C_WALL);
        if (cell.wallE) fillRect(dc, px+cs-W, py,      W,  cs, C_WALL);
    }
}

static void renderExit(HDC dc, const Game& game)
{
    int px  = game.offsetX + game.exitCol * game.cellSize;
    int py  = game.offsetY + game.exitRow * game.cellSize;
    int cs  = game.cellSize;
    int pad = cs / 5;

    // Glowing gold exit
    fillRect(dc, px+pad,   py+pad,   cs-pad*2, cs-pad*2, RGB(100,60,0));
    fillRect(dc, px+pad+2, py+pad+2, cs-pad*2-4, cs-pad*2-4, C_EXIT);
}

static void renderPlayer(HDC dc, const Game& game)
{
    int px  = (int)game.player.drawX;
    int py  = (int)game.player.drawY;
    int cs  = game.cellSize;
    int pad = cs / 5;

    HBRUSH br  = CreateSolidBrush(C_PLAYER);
    HBRUSH old = (HBRUSH)SelectObject(dc, br);
    SelectObject(dc, GetStockObject(NULL_PEN));
    Ellipse(dc, px+pad, py+pad, px+cs-pad, py+cs-pad);
    SelectObject(dc, old);
    DeleteObject(br);
}

static void renderMinotaur(HDC dc, const Game& game)
{
    int px  = (int)game.minotaur.drawX;
    int py  = (int)game.minotaur.drawY;
    int cs  = game.cellSize;
    int pad = cs / 6;

    // Body
    HBRUSH br  = CreateSolidBrush(C_MINOTAUR);
    HBRUSH old = (HBRUSH)SelectObject(dc, br);
    SelectObject(dc, GetStockObject(NULL_PEN));
    Ellipse(dc, px+pad, py+pad, px+cs-pad, py+cs-pad);
    SelectObject(dc, old);
    DeleteObject(br);

    // Horns (two small rectangles)
    int hw = cs/6, hh = cs/5;
    fillRect(dc, px + cs/4 - hw/2,       py,            hw, hh, RGB(255,80,80));
    fillRect(dc, px + 3*cs/4 - hw/2,     py,            hw, hh, RGB(255,80,80));
}

static void renderHUD(HDC dc, const Game& game)
{
    // Top bar background
    fillRect(dc, 0, 0, SCREEN_W, 34, RGB(10,8,6));
    fillRect(dc, 0, 33, SCREEN_W, 2, C_WALL_DARK);

    char buf[64];

    // Mode label
    COLORREF mc = (game.mode == GameMode::MINOTAUR) ? C_MINOTAUR : C_ACCENT2;
    drawTextAt(dc, (game.mode==GameMode::MINOTAUR)?"MINOTAUR":"EXPLORE",
               10, 8, 15, mc);

    // Floor
    sprintf(buf, "FLOOR  %d", game.floor);
    drawTextCentred(dc, buf, SCREEN_W/2, 17, 16, C_TEXT);

    // Floor timer (right side)
    int s = (int)game.floorTimer;
    sprintf(buf, "%02d:%02d", s/60, s%60);
    drawTextAt(dc, buf, SCREEN_W - 70, 8, 15, C_ACCENT);

    // Total time (below floor timer)
    int ts = (int)game.totalTime;
    sprintf(buf, "TOT %02d:%02d", ts/60, ts%60);
    drawTextAt(dc, buf, SCREEN_W - 80, 22, 11, C_TEXT_DIM, false);

    // ---- High score flash banner --------------------------------------------
    if (game.highScoreFlashTimer > 0.0f)
    {
        // Flash by alternating colour every 0.25 seconds
        int flashTick = (int)(game.highScoreFlashTimer * 4.0f);
        COLORREF flashCol = (flashTick % 2 == 0)
                            ? RGB(255, 230, 0)    // bright yellow
                            : RGB(80,  255, 120); // bright green

        // Banner strip just below HUD bar
        fillRect(dc, 0, 36, SCREEN_W, 26, RGB(10, 8, 4));
        fillRect(dc, 0, 36, SCREEN_W, 1,  flashCol);
        fillRect(dc, 0, 61, SCREEN_W, 1,  flashCol);

        drawTextCentred(dc, "*** NEW HIGH SCORE! ***",
                        SCREEN_W/2, 49, 17, flashCol);
    }
}

static void renderTransition(HDC dc, const Game& game)
{
    // Dark overlay
    fillRect(dc, 0, 0, SCREEN_W, SCREEN_H, C_OVERLAY);

    // Stone panel
    int pw = 340, ph = 160;
    int px = SCREEN_W/2 - pw/2, py = SCREEN_H/2 - ph/2;
    drawStonePanel(dc, px, py, pw, ph, RGB(22,18,14), C_ACCENT);

    char buf[64];
    sprintf(buf, "FLOOR  %d", game.floor);
    drawTextCentred(dc, buf,          SCREEN_W/2, py+50,  36, C_ACCENT);
    drawTextCentred(dc, "Descending deeper...", SCREEN_W/2, py+95,  16, C_TEXT_DIM);

    // Progress bar
    float prog = game.transTimer / Game::TRANS_DURATION;
    int barW = pw - 40;
    fillRect(dc, px+20, py+125, barW,      10, RGB(30,24,18));
    fillRect(dc, px+20, py+125, (int)(barW*prog), 10, C_ACCENT);
}

static void renderGameOver(HDC dc, const Game& game)
{
    renderBgMaze(dc, game.bgMaze);
    for (int i = 0; i < 3; i++)
        fillRect(dc, 0, 0, SCREEN_W, SCREEN_H, RGB(8, 4, 4));

    int pw = 400, ph = 260;
    int px = SCREEN_W/2 - pw/2, py = SCREEN_H/2 - ph/2;
    COLORREF bord = (game.mode==GameMode::MINOTAUR) ? C_MINOTAUR : C_ACCENT;
    drawStonePanel(dc, px, py, pw, ph, RGB(20,10,10), bord);

    if (game.mode == GameMode::MINOTAUR)
        drawTextCentred(dc, "CAUGHT  BY  THE  MINOTAUR",
                        SCREEN_W/2, py+40, 18, C_MINOTAUR);
    else
        drawTextCentred(dc, "GAME  OVER", SCREEN_W/2, py+40, 28, C_MINOTAUR);

    char buf[64];
    sprintf(buf, "Floors cleared:  %d", game.floor - 1);
    drawTextCentred(dc, buf, SCREEN_W/2, py+90,  18, C_TEXT);

    int ts = (int)game.totalTime;
    sprintf(buf, "Total time:   %02d:%02d", ts/60, ts%60);
    drawTextCentred(dc, buf, SCREEN_W/2, py+118, 16, C_TEXT);

    // Best score for this mode
    bool hasBest = (game.mode==GameMode::EXPLORE && game.scores.exploreFloor > 0) ||
                   (game.mode==GameMode::MINOTAUR && game.scores.minoFloor > 0);
    if (hasBest)
    {
        int bf = (game.mode==GameMode::EXPLORE) ? game.scores.exploreFloor : game.scores.minoFloor;
        float bt = (game.mode==GameMode::EXPLORE) ? game.scores.exploreBest  : game.scores.minoTime;
        sprintf(buf, "Best:  Floor %d  (%02d:%02d)", bf, (int)bt/60, (int)bt%60);
        drawTextCentred(dc, buf, SCREEN_W/2, py+146, 14, C_ACCENT);
    }

    // Clickable Return to Menu button
    int menuBX=SCREEN_W/2-120, menuBY=py+185, menuBW=240, menuBH=40;
    drawButton(dc, menuBX, menuBY, menuBW, menuBH, "RETURN TO MENU", 16,
               RGB(30,20,8), RGB(55,38,12), C_ACCENT,
               RGB(255,200,80), game.mouseX, game.mouseY);
}

// =============================================================================
//  SECTION 13: RENDER — SPLASH SCREEN
// =============================================================================

static void renderSplash(HDC dc, const Game& game)
{
    // Full black background
    fillRect(dc, 0, 0, SCREEN_W, SCREEN_H, RGB(0, 0, 0));

    float progress = game.splashTimer / Game::SPLASH_DURATION;
    if (progress > 1.0f) progress = 1.0f;

    // ---- Fade in effect (simulate by brightening text over time) ------------
    int alpha = (int)(progress * 2.0f * 255);
    if (alpha > 255) alpha = 255;
    COLORREF titleCol  = RGB(
        (int)(255 * alpha / 255),
        (int)(200 * alpha / 255),
        (int)(60  * alpha / 255));
    COLORREF subCol = RGB(
        (int)(210 * alpha / 255),
        (int)(175 * alpha / 255),
        (int)(110 * alpha / 255));

    // ---- MEIRO title centred vertically slightly above middle ---------------
    int cy = SCREEN_H / 2 - 40;
    drawTextCentred(dc, "MEIRO", SCREEN_W/2, cy, 96, titleCol);

    // ---- Subtitle -----------------------------------------------------------
    drawTextCentred(dc, "~ A Procedural Dungeon Maze ~",
                    SCREEN_W/2, cy + 70, 18, subCol);

    // ---- Loading bar --------------------------------------------------------
    int barW    = 320;
    int barH    = 6;
    int barX    = SCREEN_W/2 - barW/2;
    int barY    = SCREEN_H/2 + 80;

    // Track
    fillRect(dc, barX, barY, barW, barH, RGB(30, 22, 12));
    // Filled portion
    fillRect(dc, barX, barY, (int)(barW * progress), barH, RGB(220, 160, 40));
    // End cap glow
    int filled = (int)(barW * progress);
    if (filled > 4)
        fillRect(dc, barX + filled - 4, barY, 4, barH, RGB(255, 220, 120));

    // ---- "Loading..." text --------------------------------------------------
    drawTextCentred(dc, "Loading...",
                    SCREEN_W/2, barY + 24, 15, RGB(140, 115, 70), false);

    // ---- Small version tag --------------------------------------------------
    drawTextCentred(dc, "v1.0",
                    SCREEN_W/2, SCREEN_H - 30, 13, RGB(60, 50, 35), false);
}

// =============================================================================
//  SECTION 14: RENDER — FRAME
// =============================================================================

static void renderConfirmQuit(HDC dc, const Game& game)
{
    // Background layer
    if (game.prevState == GameState::PLAYING ||
        game.prevState == GameState::TRANSITION)
    {
        renderMaze(dc, game);
        renderExit(dc, game);
        if (game.mode == GameMode::MINOTAUR && game.minotaur.spawned)
            renderMinotaur(dc, game);
        renderPlayer(dc, game);
        renderHUD(dc, game);
    }
    else if (game.prevState == GameState::GAMEOVER)
        renderGameOver(dc, game);
    else
        renderMenu(dc, game);

    // Dim overlay
    fillRect(dc, 0, 0, SCREEN_W, SCREEN_H, RGB(0, 0, 0));

    // Panel
    int pw=360, ph=200;
    int px=SCREEN_W/2-pw/2, py=SCREEN_H/2-ph/2;
    fillRect(dc, px, py, pw, ph, RGB(18,12,8));
    fillRect(dc, px,      py,      pw, 2,  RGB(200,140,30));
    fillRect(dc, px,      py+ph-2, pw, 2,  RGB(200,140,30));
    fillRect(dc, px,      py,      2, ph,  RGB(200,140,30));
    fillRect(dc, px+pw-2, py,      2, ph,  RGB(200,140,30));

    drawTextCentred(dc, "QUIT MEIRO?", SCREEN_W/2, py+42, 26, RGB(255,200,60));
    fillRect(dc, px+30, py+66, pw-60, 1, RGB(100,70,20));
    drawTextCentred(dc, "Your progress will be lost.",
                    SCREEN_W/2, py+88, 15, RGB(190,170,130), false);

    // Clickable YES / NO buttons — same coords as hitTest in processInput
    int yesX=px+30,      yesY=py+116, yesW=130, yesH=40;
    int noX =px+pw-160,  noY =py+116, noW =130, noH =40;

    drawButton(dc, yesX, yesY, yesW, yesH, "YES - Quit",   15,
               RGB(80,16,16), RGB(140,30,30), RGB(220,60,60),
               RGB(255,120,120), game.mouseX, game.mouseY);
    drawButton(dc, noX,  noY,  noW,  noH,  "NO - Go Back", 15,
               RGB(14,40,14), RGB(24,70,24), RGB(60,180,60),
               RGB(100,220,100), game.mouseX, game.mouseY);

    drawTextCentred(dc, "[ Y ] Confirm     [ N ] Cancel",
                    SCREEN_W/2, py+172, 13, RGB(120,105,80), false);
}

static void renderPaused(HDC dc, const Game& game)
{
    // Draw game underneath
    renderMaze(dc, game);
    renderExit(dc, game);
    if (game.mode == GameMode::MINOTAUR && game.minotaur.spawned)
        renderMinotaur(dc, game);
    renderPlayer(dc, game);
    renderHUD(dc, game);

    // Semi-dark overlay (just darken, not full black)
    fillRect(dc, 0, 0, SCREEN_W, SCREEN_H, RGB(0,0,0));

    // Panel
    int pw=360, ph=220;
    int px=SCREEN_W/2-pw/2, py=SCREEN_H/2-ph/2;
    fillRect(dc, px, py, pw, ph, RGB(14,10,6));
    fillRect(dc, px,      py,      pw, 2,  C_ACCENT);
    fillRect(dc, px,      py+ph-2, pw, 2,  C_ACCENT);
    fillRect(dc, px,      py,      2, ph,  C_ACCENT);
    fillRect(dc, px+pw-2, py,      2, ph,  C_ACCENT);

    drawTextCentred(dc, "PAUSED", SCREEN_W/2, py+44, 32, RGB(255,200,60));
    fillRect(dc, px+30, py+68, pw-60, 1, RGB(100,70,20));

    // Resume button
    int resumeX=px+30, resumeY=py+88, resumeW=pw-60, resumeH=40;
    drawButton(dc, resumeX, resumeY, resumeW, resumeH, "RESUME GAME", 17,
               RGB(20,50,20), RGB(30,80,30), RGB(60,180,60),
               RGB(120,230,120), game.mouseX, game.mouseY);

    // Main Menu button
    int menuX=px+30, menuY=py+140, menuW=pw-60, menuH=40;
    drawButton(dc, menuX, menuY, menuW, menuH, "BACK TO MAIN MENU", 17,
               RGB(50,30,10), RGB(80,50,15), C_ACCENT,
               RGB(255,200,80), game.mouseX, game.mouseY);

    drawTextCentred(dc, "ESC / ENTER  Resume     M  Main Menu",
                    SCREEN_W/2, py+196, 13, RGB(120,105,80), false);
}

static void renderFrame(Game& game)
{
    HDC dc = game.memDC;
    fillRect(dc, 0, 0, SCREEN_W, SCREEN_H, C_BG);

    switch (game.state)
    {
        case GameState::SPLASH:
            renderSplash(dc, game);
            break;

        case GameState::MENU:
        case GameState::MENU_ANIM:
            renderMenu(dc, game);
            break;

        case GameState::CONFIRM_QUIT:
            renderConfirmQuit(dc, game);
            break;

        case GameState::PAUSED:
            renderPaused(dc, game);
            break;

        case GameState::PLAYING:
            renderMaze(dc, game);
            renderExit(dc, game);
            if (game.mode == GameMode::MINOTAUR && game.minotaur.spawned) renderMinotaur(dc, game);
            renderPlayer(dc, game);
            renderHUD(dc, game);
            break;

        case GameState::TRANSITION:
            renderMaze(dc, game);
            renderExit(dc, game);
            if (game.mode == GameMode::MINOTAUR && game.minotaur.spawned) renderMinotaur(dc, game);
            renderPlayer(dc, game);
            renderHUD(dc, game);
            renderTransition(dc, game);
            break;

        case GameState::GAMEOVER:
            renderGameOver(dc, game);
            break;

        default: break;
    }

    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    BitBlt(game.hdc, 0, 0, sw, sh, game.memDC, 0, 0, SRCCOPY);
}

// =============================================================================
//  SECTION 14: WINDOWS MESSAGE HANDLER
// =============================================================================

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
        case WM_KEYDOWN: handleKeyDown(wParam); return 0;
        case WM_KEYUP:   handleKeyUp(wParam);   return 0;

        case WM_MOUSEMOVE:
            g_game.mouseX = LOWORD(lParam);
            g_game.mouseY = HIWORD(lParam);
            return 0;

        case WM_LBUTTONUP:
            g_game.mouseX     = LOWORD(lParam);
            g_game.mouseY     = HIWORD(lParam);
            g_game.mouseClick = true;
            return 0;
        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            g_game.hdc = BeginPaint(hwnd, &ps);
            renderFrame(g_game);
            EndPaint(hwnd, &ps);
            g_game.hdc = nullptr;
            return 0;
        }
        case WM_DESTROY:
            g_game.running = false;
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcA(hwnd, msg, wParam, lParam);
}

// =============================================================================
//  SECTION 15: INIT & CLEANUP
// =============================================================================

static bool initGame(Game& game)
{
    // Resolve actual screen size first — everything else scales from this
    SCREEN_W = GetSystemMetrics(SM_CXSCREEN);
    SCREEN_H = GetSystemMetrics(SM_CYSCREEN);

    srand((unsigned int)time(nullptr));
    audioInit();
    loadScores(game.scores);
    initBgMaze(game.bgMaze);

    WNDCLASSA wc     = {};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = GetModuleHandleA(nullptr);
    wc.lpszClassName = CLASS_NAME;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    if (!RegisterClassA(&wc)) return false;

    // Get actual screen dimensions
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);

    game.hwnd = CreateWindowExA(
        0, CLASS_NAME, WINDOW_TITLE,
        WS_POPUP,       // borderless, no title bar
        0, 0,
        screenW, screenH,
        nullptr, nullptr, GetModuleHandleA(nullptr), nullptr);
    if (!game.hwnd) return false;

    // Off-screen buffer scaled to actual screen size
    HDC screenDC = GetDC(game.hwnd);
    game.memDC   = CreateCompatibleDC(screenDC);
    game.memBMP  = CreateCompatibleBitmap(screenDC, screenW, screenH);
    SelectObject(game.memDC, game.memBMP);
    ReleaseDC(game.hwnd, screenDC);

    ShowWindow(game.hwnd, SW_SHOW);
    UpdateWindow(game.hwnd);
    SetForegroundWindow(game.hwnd);
    SetFocus(game.hwnd);

    game.lastTick = GetTickCount();
    return true;
}

static void shutdownGame(Game& game)
{
    audioShutdown();
    if (game.memBMP) DeleteObject(game.memBMP);
    if (game.memDC)  DeleteDC(game.memDC);
}

// =============================================================================
//  SECTION 16: MAIN
// =============================================================================

int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
    if (!initGame(g_game)) return 1;

    while (g_game.running)
    {
        MSG msg;
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT) g_game.running = false;
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }

        DWORD now     = GetTickCount();
        DWORD elapsed = now - g_game.lastTick;
        g_game.lastTick  = now;
        g_game.deltaTime = (elapsed < 100u) ? elapsed / 1000.0f : 0.1f;

        processInput(g_game);
        clearFrameKeys();
        update(g_game);

        g_game.hdc = GetDC(g_game.hwnd);
        renderFrame(g_game);
        ReleaseDC(g_game.hwnd, g_game.hdc);

        DWORD cost = GetTickCount() - now;
        if (cost < (DWORD)FRAME_MS) Sleep(FRAME_MS - cost);
    }

    shutdownGame(g_game);
    return 0;
}
