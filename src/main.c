
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspgu.h>
#include <pspaudio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
 
PSP_MODULE_INFO("RetroViz", PSP_MODULE_USER, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER);
PSP_HEAP_SIZE_KB(16384);
 
#define SCREEN_W 480
#define SCREEN_H 272
#define NUM_MODES 8
#define NUM_BARS  28
#define BUF_WIDTH 512
 
static unsigned short __attribute__((aligned(64))) fb[BUF_WIDTH * SCREEN_H];
 
static int done = 0;
 
int exit_callback(int arg1, int arg2, void *common) { done = 1; return 0; }
int cbthread(SceSize args, void *argp) {
    int id = sceKernelCreateCallback("Exit", exit_callback, NULL);
    sceKernelRegisterExitCallback(id);
    sceKernelSleepThreadCB();
    return 0;
}
void setup_cbs(void) {
    int th = sceKernelCreateThread("cb", cbthread, 0x11, 0xFA0, 0, 0);
    if (th >= 0) sceKernelStartThread(th, 0, 0);
}
 
static inline unsigned short rgb(int r, int g, int b) {
    return ((r>>3)<<11)|((g>>2)<<5)|(b>>3);
}
static void pset(int x, int y, unsigned short c) {
    if (x>=0&&x<SCREEN_W&&y>=0&&y<SCREEN_H) fb[y*BUF_WIDTH+x]=c;
}
static void frect(int x,int y,int w,int h,unsigned short c){
    for(int dy=0;dy<h;dy++) for(int dx=0;dx<w;dx++) pset(x+dx,y+dy,c);
}
static void line(int x0,int y0,int x1,int y1,unsigned short c){
    int dx=abs(x1-x0),sx=x0<x1?1:-1,dy=-abs(y1-y0),sy=y0<y1?1:-1,e=dx+dy;
    while(1){pset(x0,y0,c);if(x0==x1&&y0==y1)break;int e2=2*e;if(e2>=dy){e+=dy;x0+=sx;}if(e2<=dx){e+=dx;y0+=sy;}}
}
 
// minimal 5x7 font: digits + A-Z + colon + slash + space
static const unsigned char font[][5]={
{0,0,0,0,0},{0,0,0x5F,0,0},{0,7,0,7,0},{0x14,0x7F,0x14,0x7F,0x14},
{0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,8,0x64,0x62},{0x36,0x49,0x55,0x22,0x50},
{0,5,3,0,0},{0,0x1C,0x22,0x41,0},{0,0x41,0x22,0x1C,0},
{0x14,8,0x3E,8,0x14},{8,8,0x3E,8,8},{0,0x50,0x30,0,0},
{8,8,8,8,8},{0,0x60,0x60,0,0},{0x20,0x10,8,4,2},
{0x3E,0x51,0x49,0x45,0x3E},{0,0x42,0x7F,0x40,0},
{0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
{0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},
{0x3C,0x4A,0x49,0x49,0x30},{1,0x71,9,5,3},
{0x36,0x49,0x49,0x49,0x36},{6,0x49,0x49,0x29,0x1E},
{0,0x36,0x36,0,0},{0,0x56,0x36,0,0},{8,0x14,0x22,0x41,0},
{0x14,0x14,0x14,0x14,0x14},{0,0x41,0x22,0x14,8},
{2,1,0x51,9,6},{0x32,0x49,0x79,0x41,0x3E},
{0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},
{0x3E,0x41,0x41,0x41,0x22},{0x7F,0x41,0x41,0x22,0x1C},
{0x7F,0x49,0x49,0x49,0x41},{0x7F,9,9,9,1},
{0x3E,0x41,0x49,0x49,0x7A},{0x7F,8,8,8,0x7F},
{0,0x41,0x7F,0x41,0},{0x20,0x40,0x41,0x3F,1},
{0x7F,8,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},
{0x7F,2,0x0C,2,0x7F},{0x7F,4,8,0x10,0x7F},
{0x3E,0x41,0x41,0x41,0x3E},{0x7F,9,9,9,6},
{0x3E,0x41,0x51,0x21,0x5E},{0x7F,9,0x19,0x29,0x46},
{0x46,0x49,0x49,0x49,0x31},{1,1,0x7F,1,1},
{0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},
{0x3F,0x40,0x38,0x40,0x3F},{0x63,0x14,8,0x14,0x63},
{7,8,0x70,8,7},{0x61,0x51,0x49,0x45,0x43},
};
static void dchar(int x,int y,char c,unsigned short col,int s){
    if(c>='a'&&c<='z')c-=32;
    int i=(c>=32&&c<=90)?c-32:0;
    for(int col2=0;col2<5;col2++){
        unsigned char b=font[i][col2];
        for(int row=0;row<7;row++) if(b&(1<<row)) frect(x+col2*s,y+row*s,s,s,col);
    }
}
static void dtext(int x,int y,const char*s,unsigned short col,int sc){
    while(*s){dchar(x,y,*s++,col,sc);x+=(5+1)*sc;}
}
 
// bars
static float bars[NUM_BARS],btgt[NUM_BARS],bpeak[NUM_BARS];
static unsigned int tick=0;
static int cur_mode=0;
 
static void upd_bars(void){
    if((tick%8)==0) for(int i=0;i<NUM_BARS;i++){
        float c=NUM_BARS/2.0f,d=fabsf(i-c)/c;
        btgt[i]=(0.2f+((float)rand()/RAND_MAX)*0.75f-d*0.2f)*110.f;
    }
    for(int i=0;i<NUM_BARS;i++){
        bars[i]=bars[i]<btgt[i]?fminf(bars[i]+6,btgt[i]):fmaxf(bars[i]-3,0);
        if(bars[i]>bpeak[i])bpeak[i]=bars[i]; else bpeak[i]=fmaxf(0,bpeak[i]-0.4f);
    }
}
 
static char trk[64]="NO NAME";
static char art[32]="UNKNOWN";
static int  trk_n=1, trk_s=0;
 
static void draw_info(void){
    dtext(10,SCREEN_H-22,trk,rgb(255,204,0),1);
    dtext(10,SCREEN_H-12,art,rgb(0,170,204),1);
    frect(SCREEN_W-92,SCREEN_H-18,82,3,rgb(30,30,30));
    frect(SCREEN_W-92,SCREEN_H-18,50,3,rgb(0,119,187));
    char b[16]; snprintf(b,16,"TR %02d",trk_n);
    dtext(SCREEN_W-92,SCREEN_H-9,b,rgb(60,60,60),1);
}
static void scanl(void){
    for(int y=0;y<SCREEN_H;y+=4) for(int x=0;x<SCREEN_W;x++) pset(x,y,(fb[y*BUF_WIDTH+x]>>1)&0x7BEF);
}
 
// ---- MODE 0: EQ bars ----
static void m0(void){
    frect(0,0,SCREEN_W,SCREEN_H,rgb(0,13,26));
    int bw=12,gap=4,tot=NUM_BARS*(bw+gap)-gap,sx=(SCREEN_W-tot)/2,by=SCREEN_H-42;
    for(int i=0;i<NUM_BARS;i++){
        int x=sx+i*(bw+gap),bh=(int)bars[i],sg=bh/6;
        for(int j=0;j<sg;j++){
            unsigned short c=j>sg*85/100?rgb(255,34,0):j>sg*65/100?rgb(255,170,0):rgb(0,136,238);
            frect(x,by-j*7,bw,5,c);
        }
        if(bpeak[i]>4) frect(x,by-(int)bpeak[i],bw,2,rgb(255,255,255));
    }
    draw_info();
}
 
// ---- MODE 1: Oscilloscope ----
static void m1(void){
    frect(0,0,SCREEN_W,SCREEN_H,rgb(0,13,26));
    for(int x=0;x<SCREEN_W;x+=40) line(x,0,x,SCREEN_H-45,rgb(0,25,15));
    for(int y=0;y<SCREEN_H-45;y+=20) line(0,y,SCREEN_W,y,rgb(0,25,15));
    int my=(SCREEN_H-45)/2; float avg=0;
    for(int i=0;i<NUM_BARS;i++) avg+=bars[i]; avg=avg/NUM_BARS*0.35f+12.f;
    int py=my;
    for(int x=0;x<SCREEN_W;x++){
        float p=x*0.04f+tick*0.05f;
        int y=my+(int)(sinf(p)*avg+sinf(p*1.7f)*avg*0.4f+sinf(p*3.1f)*6.f);
        if(x>0) line(x-1,py,x,y,rgb(0,255,136));
        py=y;
    }
    draw_info();
}
 
// ---- MODE 2: Circular spectrum ----
static void m2(void){
    frect(0,0,SCREEN_W,SCREEN_H,rgb(0,13,26));
    int cx=SCREEN_W/2,cy=(SCREEN_H-45)/2;
    int rr[]={40,60,80,100};
    for(int r=0;r<4;r++) for(int a=0;a<360;a++){
        float ang=a*3.14159f/180.f;
        pset(cx+(int)(cosf(ang)*rr[r]),cy+(int)(sinf(ang)*rr[r]),rgb(0,17,51));
    }
    for(int i=0;i<48;i++){
        float ang=i/48.f*6.2832f-1.5708f;
        float bl=bars[i*NUM_BARS/48]*0.55f+16.f,rat=bars[i*NUM_BARS/48]/110.f;
        unsigned short c=rat>0.75f?rgb(255,51,0):rat>0.5f?rgb(255,170,0):rgb(0,153,255);
        line(cx+(int)(cosf(ang)*38),cy+(int)(sinf(ang)*38),
             cx+(int)(cosf(ang)*(38+bl*0.65f)),cy+(int)(sinf(ang)*(38+bl*0.65f)),c);
    }
    dtext(cx-15,cy-4,"SPEC",rgb(0,170,255),1);
    draw_info();
}
 
// ---- MODE 3: VU meters ----
static float vl=0,vr=0,vlp=0,vrp=0,vtl=0.6f,vtr=0.5f;
static void m3(void){
    frect(0,0,SCREEN_W,SCREEN_H,rgb(0,13,26));
    if((tick%12)==0){vtl=0.3f+((float)rand()/RAND_MAX)*0.65f;vtr=0.3f+((float)rand()/RAND_MAX)*0.65f;}
    vl=vl<vtl?fminf(vl+0.04f,vtl):fmaxf(vl-0.02f,0);
    vr=vr<vtr?fminf(vr+0.04f,vtr):fmaxf(vr-0.02f,0);
    if(vl>vlp)vlp=vl; else vlp=fmaxf(0,vlp-0.005f);
    if(vr>vrp)vrp=vr; else vrp=fmaxf(0,vrp-0.005f);
    int vh=SCREEN_H-70,vw=60,vt=20;
    float vs[2]={vl,vr}; float vps[2]={vlp,vrp};
    const char*lb[2]={"L","R"}; int xs[2]={SCREEN_W/2-90,SCREEN_W/2+30};
    for(int s=0;s<2;s++){
        frect(xs[s],vt,vw,vh,rgb(0,17,34));
        for(int i=0;i<20;i++){
            float rat=(float)i/20,f=rat<vs[s];
            unsigned short c=rat>0.85f?(f?rgb(255,34,0):rgb(34,5,0)):rat>0.65f?(f?rgb(255,170,0):rgb(34,21,0)):(f?rgb(0,204,85):rgb(0,34,16));
            frect(xs[s]+4,vt+vh-(i+1)*(vh/20)+1,vw-8,vh/20-2,c);
        }
        frect(xs[s]+4,vt+vh-(int)(vps[s]*vh),vw-8,2,rgb(255,255,255));
        dtext(xs[s]+vw/2-3,vt+vh+6,lb[s],rgb(0,170,255),1);
        char db[8]; snprintf(db,8,"%dDB",(int)(vs[s]*40)-40);
        dtext(xs[s]+vw/2-10,vt+vh+16,db,rgb(255,204,0),1);
    }
    draw_info();
}
 
// ---- MODE 4: Dot matrix ----
static void m4(void){
    frect(0,0,SCREEN_W,SCREEN_H,rgb(0,13,26));
    int cols=40,rows=16,cw=SCREEN_W/cols,ch=(SCREEN_H-50)/rows;
    for(int r=0;r<rows;r++) for(int c=0;c<cols;c++){
        float thr=(float)(rows-r)/rows*110.f,rat=bars[c*NUM_BARS/cols]/110.f;
        unsigned short col=bars[c*NUM_BARS/cols]>thr?(rat>0.85f?rgb(255,51,0):rat>0.6f?rgb(255,187,0):rgb(0,153,255)):rgb(0,24,51);
        frect(c*cw+1,r*ch+1,cw-2,ch-2,col);
    }
    draw_info();
}
 
// ---- MODE 5: Starfield ----
typedef struct{float x,y,z,sp;}Star;
static Star st[120]; static int st_ok=0;
static void m5(void){
    if(!st_ok){for(int i=0;i<120;i++){st[i].x=((float)rand()/RAND_MAX-.5f)*2;st[i].y=((float)rand()/RAND_MAX-.5f)*2;st[i].z=(float)rand()/RAND_MAX;st[i].sp=0.003f+((float)rand()/RAND_MAX)*0.007f;}st_ok=1;}
    frect(0,0,SCREEN_W,SCREEN_H,rgb(0,8,20));
    float avg=0; for(int i=0;i<NUM_BARS;i++) avg+=bars[i]; avg/=(NUM_BARS*110.f);
    int cx=SCREEN_W/2,cy=(SCREEN_H-50)/2;
    for(int i=0;i<120;i++){
        st[i].z+=st[i].sp*(1+avg*3);
        if(st[i].z>1){st[i].z=0.01f;st[i].x=((float)rand()/RAND_MAX-.5f)*2;st[i].y=((float)rand()/RAND_MAX-.5f)*2;}
        int sx=cx+(int)(st[i].x/st[i].z*cx*.9f),sy=cy+(int)(st[i].y/st[i].z*cy*.9f);
        if(sx<0||sx>=SCREEN_W||sy<0||sy>=SCREEN_H-50) continue;
        int b=(int)(st[i].z*255),sz=(int)(st[i].z*3)+1;
        frect(sx,sy,sz,sz,rgb(b<205?b+50:255,b,b<155?b+100:255));
    }
    draw_info();
}
 
// ---- MODE 6: Sony MDX ----
static int scx=SCREEN_W;
static void m6(void){
    frect(0,0,SCREEN_W,SCREEN_H,rgb(0,5,16));
    frect(8,8,SCREEN_W-16,SCREEN_H-55,rgb(0,8,32));
    for(int gx=12;gx<SCREEN_W-16;gx+=9) for(int gy=12;gy<SCREEN_H-57;gy+=9) frect(gx,gy,8,8,rgb(0,13,40));
    dtext(scx,SCREEN_H/2-16,trk,rgb(232,244,255),2);
    dtext(scx+60,SCREEN_H/2+4,art,rgb(255,153,0),1);
    scx-=2; if(scx<-(int)(strlen(trk)*12+20)) scx=SCREEN_W;
    frect(14,14,38,18,rgb(255,102,0)); dtext(16,18,"AUX",rgb(0,0,0),1);
    frect(58,14,28,18,rgb(0,136,255)); dtext(60,18,"MP3",rgb(255,255,255),1);
    int nm=36,bw2=10,g2=2,tot2=nm*(bw2+g2)-g2,ex=(SCREEN_W-tot2)/2,ey=SCREEN_H-44;
    for(int i=0;i<nm;i++){
        int bh=(int)(bars[i*NUM_BARS/nm]/110.f*28.f);
        for(int s=0;s<bh;s++){
            unsigned short c=s>22?rgb(255,51,0):s>14?rgb(255,204,0):rgb(0,170,255);
            frect(ex+i*(bw2+g2),ey+(28-s*3)-3,bw2,2,c);
        }
    }
    dtext(SCREEN_W-40,SCREEN_H-6,"SONY",rgb(30,40,60),1);
}
 
// ---- MODE 7: Kenwood VFD ----
static void m7(void){
    unsigned short bg=rgb(8,4,0),C=rgb(255,170,0),CL=rgb(255,204,68),CD=rgb(80,50,0),CR=rgb(255,68,0);
    frect(0,0,SCREEN_W,SCREEN_H,bg);
    line(4,4,SCREEN_W-4,4,rgb(58,40,0)); line(4,SCREEN_H-4,SCREEN_W-4,SCREEN_H-4,rgb(58,40,0));
    line(4,4,4,SCREEN_H-4,rgb(58,40,0)); line(SCREEN_W-4,4,SCREEN_W-4,SCREEN_H-4,rgb(58,40,0));
    const char*st2[]={"CD","TAPE","TUNER"};
    for(int i=0;i<3;i++){unsigned short c=i==0?CL:CD;dtext(14+i*52+4,13,st2[i],c,1);}
    dtext(SCREEN_W/2-16,13,"SPEC",CL,1); dtext(SCREEN_W/2+32,13,"EQ",CL,1);
    dtext(SCREEN_W-56,13,"KENWOOD",C,1);
    dtext(14,34,"TR",CD,1);
    char tb[4]; snprintf(tb,4,"%02d",trk_n); dtext(32,30,tb,CL,2);
    int mn=trk_s/60,sc=trk_s%60; char tm[8]; snprintf(tm,8,"%02d:%02d",mn,sc);
    dtext(SCREEN_W-74,28,tm,CL,2); dtext(SCREEN_W-12,30,"M",CD,1); dtext(SCREEN_W-12,38,"S",CD,1);
    dtext(SCREEN_W-130,54,"KENWOOD DPX-440",C,1);
    int et=62,eb=168,eh=eb-et,bw=14,bg2=2,tw=NUM_BARS*(bw+bg2)-bg2,esx=(SCREEN_W-tw)/2;
    for(int i=0;i<NUM_BARS;i++){
        int x=esx+i*(bw+bg2),fh=(int)(bars[i]/110.f*eh),by2=eb-fh;
        for(int sy=by2;sy<eb;sy+=3){float r=(float)(eb-sy)/eh;unsigned short c=r>0.85f?CR:r>0.65f?C:CL;frect(x,sy,bw,2,c);}
        if(bpeak[i]>4) frect(x,eb-(int)(bpeak[i]/110.f*eh)-2,bw,2,CL);
    }
    line(10,eb+2,SCREEN_W-10,eb+2,C);
    const char*fr[]={"63","160","400","1K","2.5K","6.3K","16K"};
    int pw=(SCREEN_W-20)/7,elt=eb+8,elh=36;
    for(int i=0;i<7;i++){
        int px=20+i*pw; float en=bars[i*NUM_BARS/7]/110.f;
        int ll=(int)((pw-16)*fmaxf(0.1f,en)); frect(px+6,elt+elh/2-1,ll,3,C);
        dtext(px+pw/2-((int)strlen(fr[i])*3),elt+elh+4,fr[i],CD,1);
    }
    const char*st3[]={"LOUD","DSP","RPT","RDM","ST"};
    for(int i=0;i<5;i++) dtext(14+i*42,SCREEN_H-20,st3[i],i<2?CL:C,1);
    dtext(SCREEN_W-28,SCREEN_H-22,"VOL",CD,1); dtext(SCREEN_W-20,SCREEN_H-12,"22",CL,1);
    for(int y=0;y<SCREEN_H;y+=2) for(int x=0;x<SCREEN_W;x++) pset(x,y,(fb[y*BUF_WIDTH+x]>>1)&0x7BEF);
}
 
static const char*mnames[NUM_MODES]={"EQ BARS","OSCILLO","SPECTRUM","VU METER","DOT MATR","STARFLD","SONY MDX","KW VFD"};
typedef void(*Fn)(void);
static Fn fns[NUM_MODES]={m0,m1,m2,m3,m4,m5,m6,m7};
static int lbl_t=0;
 
int main(void){
    setup_cbs();
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    sceDisplaySetMode(0,SCREEN_W,SCREEN_H);
    sceDisplaySetFrameBuf(fb,BUF_WIDTH,PSP_DISPLAY_PIXEL_FORMAT_565,PSP_DISPLAY_SETBUF_NEXTFRAME);
    srand(sceKernelGetSystemTimeLow());
    lbl_t=90;
    int pm=-1;
    while(!done){
        SceCtrlData pad; sceCtrlReadBufferPositive(&pad,1);
        static unsigned int prev=0;
        unsigned int pr=pad.Buttons&~prev;
        if(pr&PSP_CTRL_LTRIGGER){cur_mode=(cur_mode-1+NUM_MODES)%NUM_MODES;lbl_t=90;}
        if(pr&PSP_CTRL_RTRIGGER){cur_mode=(cur_mode+1)%NUM_MODES;lbl_t=90;}
        if(pr&PSP_CTRL_TRIANGLE){trk_n++;trk_s=0;}
        if(pr&PSP_CTRL_CROSS){if(trk_n>1){trk_n--;trk_s=0;}}
        prev=pad.Buttons;
        upd_bars();
        fns[cur_mode]();
        if(lbl_t>0){lbl_t--;dtext((SCREEN_W-(int)strlen(mnames[cur_mode])*6)/2,4,mnames[cur_mode],rgb(0,204,255),1);}
        if((tick%60)==0) trk_s++;
        tick++;
        sceDisplayWaitVblankStart();
    }
    sceKernelExitGame();
    return 0;
}
