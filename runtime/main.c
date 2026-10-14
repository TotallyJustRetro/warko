/* main.c - frontend. SDL2 window/audio/input by default; -DHEADLESS builds a test harness. */
#include "gb.h"
#include <math.h>
#ifndef HEADLESS
#include <SDL2/SDL.h>
#endif

static const char *trace_path;
int gb_pick_rom(char *buf, int n);

static void usage(const char *p) {
    fprintf(stderr,
        "usage: %s <rom> [options]\n"
        "  --interp         ignore compiled code, interpret only (reference / tracing)\n"
        "  --trace FILE     record executed instructions into FILE (merged with existing); implies --interp\n"
        "  --headless N     run N frames without a window (also: --mash, --shot FILE.ppm)\n"
        "  --mash           pseudo-random button input (for tracing/testing)\n"
        "  --shot FILE      write final frame as PPM\n"
        "  --volume N       output volume in percent (default 50)\n"
        "  --companion      open the live Wario Land Treasure Expedition deck\n", p);
}

static uint32_t rng = 0x1234567;
static uint32_t xr(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static uint8_t mash_input(int frame) {
    static uint8_t cur;
    if (frame % 6 == 0) {
        cur = 0; uint32_t r = xr();
        if (r & 1) cur |= 16;                 /* A */
        if ((r >> 1) & 1) cur |= 32;          /* B */
        if (((r >> 2) & 15) == 0) cur |= 128; /* Start */
        if (((r >> 6) & 3) == 0) cur |= 64;   /* Select */
        cur |= (r >> 8) & 1 ? 1 : 0; cur |= ((r >> 9) & 1) ? 2 : 0; cur |= ((r >> 10) & 3) == 0 ? 4 : 0; cur |= ((r >> 12) & 3) == 0 ? 8 : 0;
        if ((cur & 3) == 3) cur &= ~2;
    }
    return cur;
}
static void write_ppm(const char *p) {
    FILE *f = fopen(p, "wb"); if (!f) return;
    fprintf(f, "P6\n160 144\n255\n");
    for (int i = 0; i < 160 * 144; i++) { uint32_t c = g_fb[i]; fputc(c >> 16, f); fputc(c >> 8, f); fputc(c, f); }
    fclose(f);
}

#ifndef HEADLESS
static SDL_GameController *pad;
static void open_pad(void) {
    if (pad) return;
    for (int i = 0; i < SDL_NumJoysticks(); i++) if (SDL_IsGameController(i)) { pad = SDL_GameControllerOpen(i); if (pad) break; }
}
static uint8_t read_input(void) {
    const Uint8 *k = SDL_GetKeyboardState(NULL); uint8_t j = 0;
    if (k[SDL_SCANCODE_RIGHT] || k[SDL_SCANCODE_D]) j |= 1;
    if (k[SDL_SCANCODE_LEFT]  || k[SDL_SCANCODE_A]) j |= 2;
    if (k[SDL_SCANCODE_UP]    || k[SDL_SCANCODE_W]) j |= 4;
    if (k[SDL_SCANCODE_DOWN]  || k[SDL_SCANCODE_S]) j |= 8;
    if (k[SDL_SCANCODE_Z] || k[SDL_SCANCODE_J]) j |= 16;                       /* A */
    if (k[SDL_SCANCODE_X] || k[SDL_SCANCODE_K]) j |= 32;                       /* B */
    if (k[SDL_SCANCODE_BACKSPACE] || k[SDL_SCANCODE_RSHIFT] || k[SDL_SCANCODE_TAB]) j |= 64;   /* Select */
    if (k[SDL_SCANCODE_RETURN] || k[SDL_SCANCODE_SPACE]) j |= 128;              /* Start */
    if (pad) {
#define PB(b) SDL_GameControllerGetButton(pad, b)
        if (PB(SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) j |= 1;
        if (PB(SDL_CONTROLLER_BUTTON_DPAD_LEFT)) j |= 2;
        if (PB(SDL_CONTROLLER_BUTTON_DPAD_UP)) j |= 4;
        if (PB(SDL_CONTROLLER_BUTTON_DPAD_DOWN)) j |= 8;
        if (PB(SDL_CONTROLLER_BUTTON_A) || PB(SDL_CONTROLLER_BUTTON_Y)) j |= 16;
        if (PB(SDL_CONTROLLER_BUTTON_B) || PB(SDL_CONTROLLER_BUTTON_X)) j |= 32;
        if (PB(SDL_CONTROLLER_BUTTON_BACK)) j |= 64;
        if (PB(SDL_CONTROLLER_BUTTON_START)) j |= 128;
        int ax = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX), ay = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
        if (ax > 12000) j |= 1; if (ax < -12000) j |= 2; if (ay < -12000) j |= 4; if (ay > 12000) j |= 8;
    }
    return j;
}
#endif


#ifndef HEADLESS
/* Wario Land's Companion Deck is rendered by the generic player itself.
 * PipeClean launches this process separately, so a host-owned deck cannot
 * observe its live game loop. All art below is original geometric UI art. */
static SDL_Window *deck_window;
static SDL_Renderer *deck_renderer;
static int deck_enabled;
static int deck_baseline;
static int deck_treasure_anim, deck_treasure_anim_id;
static uint32_t deck_treasure_anim_started;

typedef struct {
    int valid, course, lives, hearts, power, coins, time_left, treasure_mask;
} WarioDeckState;

static WarioDeckState deck_state, deck_previous;
static char deck_log[5][64];
static int deck_log_count, deck_coin_milestone = -1;

static const uint8_t deck_font[36][7] = {
    {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30},
    {14,17,16,16,16,17,14}, {30,17,17,17,17,17,30},
    {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16},
    {14,17,16,23,17,17,15}, {17,17,17,31,17,17,17},
    {14,4,4,4,4,4,14}, {7,2,2,2,18,18,12},
    {17,18,20,24,20,18,17}, {16,16,16,16,16,16,31},
    {17,27,21,21,17,17,17}, {17,25,21,19,17,17,17},
    {14,17,17,17,17,17,14}, {30,17,17,30,16,16,16},
    {14,17,17,17,21,18,13}, {30,17,17,30,20,18,17},
    {15,16,16,14,1,1,30}, {31,4,4,4,4,4,4},
    {17,17,17,17,17,17,14}, {17,17,17,17,17,10,4},
    {17,17,17,21,21,21,10}, {17,17,10,4,10,17,17},
    {17,17,10,4,4,4,4}, {31,1,2,4,8,16,31},
    {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14},
    {14,17,1,2,4,8,31}, {30,1,1,14,1,1,30},
    {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
    {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8},
    {14,17,17,14,17,17,14}, {14,17,17,15,1,1,14}
};

static void deck_color(Uint8 r, Uint8 g, Uint8 b, Uint8 a) {
    if (deck_renderer) SDL_SetRenderDrawColor(deck_renderer, r, g, b, a);
}
static void deck_fill(int x, int y, int w, int h, Uint8 r, Uint8 g, Uint8 b, Uint8 a) {
    SDL_Rect rc = {x,y,w,h}; deck_color(r,g,b,a); SDL_RenderFillRect(deck_renderer,&rc);
}
static void deck_outline(int x, int y, int w, int h, Uint8 r, Uint8 g, Uint8 b, Uint8 a) {
    SDL_Rect rc = {x,y,w,h}; deck_color(r,g,b,a); SDL_RenderDrawRect(deck_renderer,&rc);
}
static void deck_circle(int cx, int cy, int radius, Uint8 r, Uint8 g, Uint8 b, Uint8 a) {
    deck_color(r,g,b,a);
    for (int dy=-radius; dy<=radius; dy++) {
        int dx=(int)sqrt((double)radius*radius-(double)dy*dy);
        SDL_RenderDrawLine(deck_renderer,cx-dx,cy+dy,cx+dx,cy+dy);
    }
}
static void deck_ring(int cx, int cy, int radius, Uint8 r, Uint8 g, Uint8 b, Uint8 a) {
    deck_color(r,g,b,a);
    for (int dy=-radius; dy<=radius; dy++) {
        int dx=(int)sqrt((double)radius*radius-(double)dy*dy);
        SDL_RenderDrawPoint(deck_renderer,cx-dx,cy+dy);
        SDL_RenderDrawPoint(deck_renderer,cx+dx,cy+dy);
    }
}
static int deck_glyph(char ch) {
    if (ch>='a'&&ch<='z') ch=(char)(ch-'a'+'A');
    if (ch>='A'&&ch<='Z') return ch-'A';
    if (ch>='0'&&ch<='9') return 26+ch-'0';
    return -1;
}
static int deck_text_width(const char *str, int px) {
    int n=0; for (const char *p=str; p&&*p; p++) n += (*p==' ')?4*px:6*px;
    return n;
}
static void deck_text(int x, int y, int px, Uint8 r, Uint8 g, Uint8 b, const char *str) {
    if (px<1) px=1;
    for (const unsigned char *p=(const unsigned char *)(str?str:""); *p; ++p) {
        if (*p==' ') { x+=4*px; continue; }
        if (*p=='-') { deck_fill(x+px,y+3*px,3*px,px,r,g,b,255); x+=5*px; continue; }
        if (*p==':') { deck_fill(x+px,y+2*px,px,px,r,g,b,255); deck_fill(x+px,y+5*px,px,px,r,g,b,255); x+=4*px; continue; }
        if (*p=='/') { for(int row=0;row<7;row++) deck_fill(x+(4-row%5)*px,y+row*px,px,px,r,g,b,255); x+=5*px; continue; }
        if (*p=='+') { deck_fill(x+2*px,y+px,px,5*px,r,g,b,255); deck_fill(x,y+3*px,5*px,px,r,g,b,255); x+=6*px; continue; }
        int id=deck_glyph((char)*p);
        if(id>=0) for(int row=0;row<7;row++) for(int col=0;col<5;col++)
            if(deck_font[id][row]&(1u<<(4-col))) deck_fill(x+col*px,y+row*px,px,px,r,g,b,255);
        x+=6*px;
    }
}
static void deck_text_fit(int x, int y, int maxw, int px, Uint8 r, Uint8 g, Uint8 b, const char *str) {
    while(px>1 && deck_text_width(str,px)>maxw) px--;
    deck_text(x,y,px,r,g,b,str);
}
static void deck_text_right(int right, int y, int px, Uint8 r, Uint8 g, Uint8 b, const char *str) {
    deck_text(right-deck_text_width(str,px),y,px,r,g,b,str);
}
static void deck_card(int x,int y,int w,int h,const char *label) {
    deck_fill(x+2,y+4,w,h,0,0,0,100);
    deck_fill(x,y,w,h,54,47,37,248);
    deck_outline(x,y,w,h,189,133,55,255);
    deck_fill(x+12,y+9,w-24,3,255,200,87,255);
    deck_text(x+12,y+21,1,184,170,149,label);
}
static void deck_gem(int cx,int cy,int size,int found) {
    Uint8 r=found?255:101, g=found?200:88, b=found?87:70;
    deck_color(r,g,b,255);
    SDL_Point p[7]={{cx,cy-size},{cx+size*7/10,cy-size/5},
      {cx+size/2,cy+size*7/10},{cx,cy+size},
      {cx-size/2,cy+size*7/10},{cx-size*7/10,cy-size/5},{cx,cy-size}};
    SDL_RenderDrawLines(deck_renderer,p,7);
    if(found) {
        deck_fill(cx-1,cy-1,3,3,255,244,193,255);
        deck_color(255,235,164,255);
        SDL_RenderDrawLine(deck_renderer,cx-size/3,cy-size/4,cx+size/4,cy+size/3);
    } else deck_fill(cx-1,cy-1,2,2,73,63,50,255);
}
static int deck_bcd2(uint8_t value) {
    int tens=(value>>4)&15, ones=value&15;
    return tens<=9&&ones<=9 ? tens*10+ones : -1;
}
static int deck_bcd_coins(void) {
    int high=gb_cart_ram_peek(0x805), mid=gb_cart_ram_peek(0x806), low=gb_cart_ram_peek(0x807);
    int d0=high&15, d1=(mid>>4)&15, d2=mid&15, d3=(low>>4)&15, d4=low&15;
    if(d0>9||d1>9||d2>9||d3>9||d4>9) return -1;
    return d0*10000+d1*1000+d2*100+d3*10+d4;
}
static int deck_bcd_time(void) {
    int high=gb_cart_ram_peek(0x964), low=gb_cart_ram_peek(0x965);
    int hundreds=high&15, tens=(low>>4)&15, ones=low&15;
    if(high==0xFF||low==0xFF||hundreds>9||tens>9||ones>9) return -1;
    return hundreds*100+tens*10+ones;
}
static const char *deck_power_name(int power) {
    switch(power) { case 0:return "SMALL"; case 1:return "GARLIC"; case 2:return "BULL";
        case 3:return "JET"; case 4:return "DRAGON"; default:return "UNKNOWN"; }
}
static void deck_log_push(const char *entry) {
    for(int i=4;i>0;i--) memcpy(deck_log[i],deck_log[i-1],sizeof deck_log[i]);
    snprintf(deck_log[0],sizeof deck_log[0],"%s",entry?entry:"");
    if(deck_log_count<5) deck_log_count++;
}
static WarioDeckState deck_read_state(void) {
    WarioDeckState d;
    memset(&d,0,sizeof d);
    int course=gb_cart_ram_peek(0x804), life=gb_cart_ram_peek(0x809);
    int hearts=gb_cart_ram_peek(0x808), power=gb_cart_ram_peek(0x80A);
    int lo=gb_cart_ram_peek(0x80E), hi=gb_cart_ram_peek(0x80F);
    d.course=course==0xFF?-1:course;
    d.lives=deck_bcd2((uint8_t)life);
    d.hearts=deck_bcd2((uint8_t)hearts);
    d.power=power<=4?power:-1;
    d.coins=deck_bcd_coins();
    d.time_left=deck_bcd_time();
    d.treasure_mask=(lo!=0xFF&&hi!=0xFF)?(lo|(hi<<8)):-1;
    d.valid=d.course>=0 && d.lives>=0 && d.hearts>=0 && d.power>=0 && d.treasure_mask>=0;
    return d;
}
static int deck_treasure_count(void) {
    if(deck_state.treasure_mask<0) return -1;
    int n=0; for(int i=0;i<15;i++) if(deck_state.treasure_mask&(1<<i)) n++;
    return n;
}
static void deck_update(void) {
    if(!deck_enabled) return;
    WarioDeckState next=deck_read_state();
    deck_state=next;
    if(!next.valid) { deck_baseline=0; deck_treasure_anim=0; return; }
    if(!deck_baseline) {
        memset(deck_log,0,sizeof deck_log); deck_log_count=0;
        deck_log_push("EXPEDITION LINKED");
        char entry[64]; snprintf(entry,sizeof entry,"COURSE %02X ENTERED",next.course&255);
        deck_log_push(entry);
        deck_previous=next; deck_baseline=1; deck_coin_milestone=next.coins>=0?next.coins/2500:-1;
        return;
    }
    if(next.course!=deck_previous.course) {
        char entry[64]; snprintf(entry,sizeof entry,"COURSE %02X ENTERED",next.course&255); deck_log_push(entry);
    }
    int oldmask=deck_previous.treasure_mask&0x7FFF, newmask=next.treasure_mask&0x7FFF;
    if((newmask&oldmask)!=oldmask) {
        deck_log_push("TIMELINE RESTORED");
        deck_treasure_anim=0;
    } else {
        int gained=newmask&~oldmask;
        if(gained) {
            for(int bit=0;bit<15;bit++) if(gained&(1<<bit)) {
                char entry[64]; snprintf(entry,sizeof entry,"TREASURE %02d FOUND",bit+1); deck_log_push(entry);
            }
            int bit=14; while(bit>=0&&!(gained&(1<<bit))) bit--;
            deck_treasure_anim_id=bit+1; deck_treasure_anim_started=SDL_GetTicks(); deck_treasure_anim=1;
        }
    }
    if(next.power!=deck_previous.power) {
        char entry[64]; snprintf(entry,sizeof entry,"POWER: %s",deck_power_name(next.power)); deck_log_push(entry);
    }
    if(next.lives>deck_previous.lives) {
        char entry[64]; snprintf(entry,sizeof entry,"EXTRA LIFE + %d",next.lives-deck_previous.lives); deck_log_push(entry);
    }
    if(next.hearts>deck_previous.hearts) deck_log_push("HEARTS RESTORED");
    if(next.coins>=0&&deck_previous.coins>=0&&next.coins>deck_previous.coins) {
        int milestone=next.coins/2500;
        if(milestone>deck_coin_milestone) {
            char entry[64]; snprintf(entry,sizeof entry,"COINS REACHED %05d",milestone*2500); deck_log_push(entry);
            deck_coin_milestone=milestone;
        }
    }
    deck_previous=next;
}
static void deck_open(void) {
    if(!deck_enabled||deck_window) return;
    int displays=SDL_GetNumVideoDisplays();
    int x=24,y=40;
    if(displays>1) { x=SDL_WINDOWPOS_CENTERED_DISPLAY(1); y=SDL_WINDOWPOS_CENTERED_DISPLAY(1); }
    deck_window=SDL_CreateWindow("PipeClean Companion Deck - Treasure Expedition",
        x,y,800,450,SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI);
    if(!deck_window) { fprintf(stderr,"Companion Deck window: %s\n",SDL_GetError()); deck_enabled=0; return; }
    SDL_SetWindowMinimumSize(deck_window,480,320);
    deck_renderer=SDL_CreateRenderer(deck_window,-1,SDL_RENDERER_ACCELERATED);
    if(!deck_renderer) deck_renderer=SDL_CreateRenderer(deck_window,-1,0);
    if(!deck_renderer) { fprintf(stderr,"Companion Deck renderer: %s\n",SDL_GetError()); SDL_DestroyWindow(deck_window); deck_window=NULL; deck_enabled=0; return; }
    SDL_SetRenderDrawBlendMode(deck_renderer,SDL_BLENDMODE_BLEND);
    SDL_RenderSetLogicalSize(deck_renderer,800,450);
}
static void deck_close(void) {
    if(deck_renderer) SDL_DestroyRenderer(deck_renderer);
    if(deck_window) SDL_DestroyWindow(deck_window);
    deck_renderer=NULL; deck_window=NULL; deck_enabled=0;
}
static void deck_draw_chest(int cx,int cy) {
    uint32_t now=SDL_GetTicks();
    deck_circle(cx,cy+3,66,67,42,13,150);
    deck_circle(cx,cy,57,48,38,27,255);
    deck_ring(cx,cy,53,255,214,107,255);
    deck_ring(cx,cy,44,189,133,55,255);
    deck_fill(cx-31,cy-1,62,31,142,79,34,255);
    deck_fill(cx-31,cy-18,62,21,199,129,52,255);
    deck_fill(cx-4,cy-17,8,46,255,214,107,255);
    deck_fill(cx-23,cy+11,46,4,113,62,27,255);
    deck_circle(cx,cy+5,5,255,241,181,255);
    for(int i=0;i<6;i++) {
        double a=(double)now/1500.0+6.2831853*i/6.0;
        int orbit=70+(int)(sin((double)now/500.0+i)*2.0);
        int sx=cx+(int)(cos(a)*orbit), sy=cy+(int)(sin(a)*orbit);
        deck_fill(sx-2,sy-2,4,4,255,(i%2)?200:240,107,255);
    }
}
static void deck_draw_animation(void) {
    if(!deck_treasure_anim) return;
    uint32_t elapsed=SDL_GetTicks()-deck_treasure_anim_started;
    if(elapsed>=1850) { deck_treasure_anim=0; return; }
    float phase=elapsed/1850.f;
    int alpha=(int)((1.f-phase)*170.f);
    for(int i=0;i<3;i++) {
        int radius=(int)(22+phase*(95+i*20));
        deck_ring(400,142,radius,255,(Uint8)(214-i*15),107,(Uint8)alpha);
    }
    deck_fill(246,206,308,44,43,31,17,238);
    deck_outline(246,206,308,44,255,214,107,255);
    deck_text(327,214,1,255,235,165,"TREASURE FOUND!");
    char text[48]; snprintf(text,sizeof text,"RELIC %02d ADDED TO YOUR VAULT",deck_treasure_anim_id);
    deck_text_fit(265,232,270,1,247,233,208,text);
    for(int i=0;i<12;i++) {
        double a=6.2831853*i/12.0+phase*1.3;
        int distance=(int)(32+phase*70+(i%3)*7), size=(int)(4.5f*(1.f-phase));
        if(size<1) size=1;
        deck_fill(400+(int)(cos(a)*distance),142+(int)(sin(a)*distance),size,size,255,(i%3)?200:243,107,255);
    }
}
static void deck_render(void) {
    if(!deck_enabled||!deck_renderer||!deck_window) return;
    deck_update();
    deck_color(29,24,18,255); SDL_RenderClear(deck_renderer);
    for(int y=0;y<450;y+=6) deck_fill(0,y,800,1,0,0,0,18);
    deck_fill(18,7,764,49,54,47,37,250); deck_outline(18,7,764,49,189,133,55,255);
    deck_fill(28,14,744,3,255,200,87,255);
    deck_text(28,22,1,255,200,87,"PIPE CLEAN / TREASURE EXPEDITION");
    deck_text_fit(28,34,730,2,255,241,212,"WARIO LAND: SUPER MARIO LAND 3");
    deck_card(20,70,230,133,"TREASURE VAULT");
    char value[64]; int treasures=deck_treasure_count();
    if(treasures<0) snprintf(value,sizeof value,"-- / 15 TREASURES");
    else snprintf(value,sizeof value,"%02d / 15 TREASURES",treasures);
    deck_text_fit(34,109,200,1,255,241,212,value);
    deck_fill(34,120,202,7,69,54,34,255);
    if(treasures>0) deck_fill(34,120,202*treasures/15,7,255,200,87,255);
    for(int i=0;i<15;i++) deck_gem(35+i*13,174,5,deck_state.treasure_mask>=0&&(deck_state.treasure_mask&(1<<i)));
    deck_text(34,188,1,184,170,149,"RELICS FOUND");
    deck_draw_chest(400,142);
    deck_card(556,70,220,133,"EXPEDITION STATUS");
    deck_text(570,108,1,184,170,149,"LIVES");
    snprintf(value,sizeof value,"%02d",deck_state.lives);
    if(deck_state.lives<0) snprintf(value,sizeof value,"--");
    deck_text_right(762,106,2,255,241,212,value);
    deck_text(570,127,1,184,170,149,"HEARTS");
    snprintf(value,sizeof value,"%02d",deck_state.hearts);
    if(deck_state.hearts<0) snprintf(value,sizeof value,"--");
    deck_text_right(762,125,2,255,241,212,value);
    deck_text(570,146,1,184,170,149,"POWER");
    deck_text_right(762,146,1,255,200,87,deck_power_name(deck_state.power));
    deck_text(570,165,1,184,170,149,"TIME LEFT");
    snprintf(value,sizeof value,"%03d",deck_state.time_left);
    if(deck_state.time_left<0) snprintf(value,sizeof value,"---");
    deck_text_right(762,163,2,255,241,212,value);
    deck_card(20,213,232,218,"TREASURE COLLECTION");
    for(int i=0;i<15;i++) {
        int found=deck_state.treasure_mask>=0&&(deck_state.treasure_mask&(1<<i));
        int x=52+(i%5)*40, y=279+(i/5)*37;
        deck_gem(x,y,10,found);
        snprintf(value,sizeof value,"%02d",i+1);
        deck_text_fit(x-9,y+14,22,1,found?255:133,found?200:119,found?87:89,value);
    }
    deck_text_fit(34,410,204,1,184,170,149,"15 RELICS HIDDEN IN COURSES");
    deck_card(267,213,266,94,"CURRENT COURSE");
    if(deck_state.course<0) snprintf(value,sizeof value,"COURSE --");
    else snprintf(value,sizeof value,"COURSE %02X",deck_state.course&255);
    deck_text_fit(283,250,234,2,255,200,87,value);
    deck_text(283,278,1,255,241,212,deck_state.valid?"EXPEDITION IN PROGRESS":"WAITING FOR GAME STATE");
    deck_card(267,320,266,111,"COIN BANK");
    snprintf(value,sizeof value,"%05d",deck_state.coins);
    if(deck_state.coins<0) snprintf(value,sizeof value,"-----");
    deck_text_fit(290,353,220,3,255,214,107,value);
    deck_text(283,393,1,184,170,149,"TOTAL COINS COLLECTED");
    deck_card(556,213,220,218,"EXPEDITION LOG");
    if(!deck_log_count) deck_text(570,263,1,184,170,149,"NO EVENTS RECORDED YET");
    for(int i=0;i<deck_log_count&&i<5;i++) {
        int y=254+i*32;
        deck_fill(570,y-1,3,14,i==0?255:189,i==0?200:133,i==0?87:55,255);
        deck_text_fit(580,y,1,184,170,149,deck_log[i]);
    }
    if(deck_treasure_anim) deck_draw_animation();
    SDL_RenderPresent(deck_renderer);
}
#endif

int main(int argc, char **argv) {
    const char *rom = NULL, *shot = NULL; int headless = 0, nframes = 0, mash = 0, companion = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--interp")) g_interp_only = 1;
        else if (!strcmp(argv[i], "--trace") && i + 1 < argc) { trace_path = argv[++i]; g_interp_only = 1; }
        else if (!strcmp(argv[i], "--headless") && i + 1 < argc) { headless = 1; nframes = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--volume") && i + 1 < argc) g_volume = atoi(argv[++i]) / 100.0f;
        else if (!strcmp(argv[i], "--mash")) mash = 1;
        else if (!strcmp(argv[i], "--companion")) companion = 1;
        else if (!strcmp(argv[i], "--shot") && i + 1 < argc) shot = argv[++i];
        else if (argv[i][0] != '-' && !rom) rom = argv[i];
        else { usage(argv[0]); return 1; }
    }
    static char pick[1024];
#ifndef HEADLESS
    if (!rom && !headless && gb_pick_rom(pick, sizeof pick)) rom = pick;
#endif
    if (!rom) { usage(argv[0]); return 1; }
    int e = gb_load(rom);
    if (e) {
        fprintf(stderr, "cannot load %s (%d)\n", rom, e);
#ifndef HEADLESS
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "gbrecomp", "Could not load that ROM file.", NULL);
#endif
        return 1;
    }
    if (!gb_blk0p) g_interp_only = 1;
    if (trace_path) gb_trace_load(trace_path);
    gb_reset();
    fprintf(stderr, "%s: %u banks, %s mode%s\n", rom, g_nbanks, g_cgb ? "CGB" : "DMG", g_interp_only ? ", interpreter only" : ", recompiled");

#ifdef HEADLESS
    headless = 1;
#endif
    if (headless) {
        for (int f = 0; f < nframes; f++) { if (mash) gb_set_joy(mash_input(f)); gb_run_frame(); }
        if (shot) write_ppm(shot);
        if (trace_path) gb_trace_save(trace_path);
        gb_save(); return 0;
    }
#ifndef HEADLESS
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER)) { fprintf(stderr, "SDL: %s\n", SDL_GetError()); return 1; }
    SDL_Window *w = SDL_CreateWindow("gbrecomp  |  arrows/WASD  Z/J=A  X/K=B  Enter=Start  Bksp=Select  Esc=quit",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 640, 576, SDL_WINDOW_RESIZABLE);
    /* No vsync: pacing comes from the audio clock below (vsync would force 60.00 Hz vs the Game Boy's 59.73 Hz). */
    SDL_Renderer *r = SDL_CreateRenderer(w, -1, SDL_RENDERER_ACCELERATED);
    if (!r) r = SDL_CreateRenderer(w, -1, 0);
    SDL_RenderSetLogicalSize(r, 160, 144);
    deck_enabled = companion;
    if (deck_enabled) deck_open();
    SDL_Texture *t = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 160, 144);
    SDL_AudioSpec want = {0}, have; want.freq = 44100; want.format = AUDIO_S16SYS; want.channels = 2; want.samples = 1024; want.callback = NULL;
    SDL_AudioDeviceID ad = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);   /* queue mode: we push samples */
    const int frame_samples = 738, target_bytes = frame_samples * 3 * 4;      /* keep ~3 frames (~50 ms) queued */
    if (ad) { static int16_t silence[738 * 3 * 2]; SDL_QueueAudio(ad, silence, sizeof silence); SDL_PauseAudioDevice(ad, 0); }
    else fprintf(stderr, "audio unavailable: %s\n", SDL_GetError());
    open_pad();
    int run = 1; uint64_t freq = SDL_GetPerformanceFrequency(), next = SDL_GetPerformanceCounter(), frame = 0;
    const double frame_s = 70224.0 / 4194304.0;
    static int16_t abuf[8192 * 2];
    while (run) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_QUIT) run = 0;
            if (ev.type == SDL_WINDOWEVENT && ev.window.event == SDL_WINDOWEVENT_CLOSE &&
                deck_window && ev.window.windowID == SDL_GetWindowID(deck_window)) { deck_close(); continue; }
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_ESCAPE) run = 0;
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_MINUS && g_volume > 0.05f) g_volume -= 0.05f;
            if (ev.type == SDL_KEYDOWN && ev.key.keysym.sym == SDLK_EQUALS && g_volume < 1.0f) g_volume += 0.05f;
            if (ev.type == SDL_CONTROLLERDEVICEADDED) open_pad();
            if (ev.type == SDL_CONTROLLERDEVICEREMOVED && pad) { SDL_GameControllerClose(pad); pad = NULL; open_pad(); }
        }
        gb_set_joy(read_input());
        gb_run_frame();
        int n = gb_audio_pull(abuf, 8192);
        if (ad) SDL_QueueAudio(ad, abuf, n * 4);
        SDL_UpdateTexture(t, NULL, g_fb, 160 * 4); SDL_RenderClear(r); SDL_RenderCopy(r, t, NULL, NULL); SDL_RenderPresent(r);
        deck_render();
        if ((++frame & 255) == 0) gb_save();
        if (ad) {   /* audio-clock pacing: wait until the device has drained below the target */
            while (run && SDL_GetQueuedAudioSize(ad) > (Uint32)target_bytes) SDL_Delay(1);
        } else {
            next += (uint64_t)(frame_s * freq); uint64_t now = SDL_GetPerformanceCounter();
            if (next > now) SDL_Delay((Uint32)((next - now) * 1000 / freq)); else if (now - next > freq / 4) next = now;
        }
    }
    if (trace_path) gb_trace_save(trace_path);
    gb_save(); if (ad) SDL_CloseAudioDevice(ad); if (pad) SDL_GameControllerClose(pad); deck_close(); SDL_Quit();
#endif
    return 0;
}
