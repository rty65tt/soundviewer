#include <windows.h>
#include <gdiplus.h>
#include <stdio.h>
#include <process.h>
#include "bass.h"


using namespace Gdiplus;

#define WIDTH   1200  // display width
#define HEIGHT  321  // height (odd number for centre line)

static int h2       = (HEIGHT/2);
static int hScope   = (HEIGHT/4);

HWND win=NULL;
DWORD scanthread=0;
BOOL killscan=FALSE;

DWORD chan;
DWORD bpp;          // bytes per pixel
HSYNC lsync;        // looping sync

Bitmap* wavebmp;

static BOOL isfile = 1;
static CHAR fn[MAX_PATH];

//static WCHAR status_line[256];

static BOOL setloop = 1;
static QWORD seektime = 250000;
static QWORD endpos;

static Color bgcolor = Color(50, 95, 105);
//static Color bgcolor = Color(95, 95, 97);
//static Color bgcolor = Color(20, 55, 75);
//static Color bgcolor = Color(0, 35, 55);
static Color wf1colr = Color(0, 0, 0);
static Color wf2colr = Color(0, 0, 0);

struct LINEPREF
{
  Color bg;
  Color text;
  Color line;
  DWORD ypos;
  QWORD xpos;
  BOOL flag;
};

static LINEPREF pbcolor = {
  Color(255, 221, 221, 221), Color(255, 33, 33, 33), Color(255, 150, 150, 150), (DWORD)35, 0, 0
};
static LINEPREF slcolor = {
  Color(255, 33, 33, 33), Color(255, 136, 204, 51), Color(255, 170, 255, 0), (DWORD)(h2-25), 0, 0
};
static LINEPREF elcolor = {
  Color(255, 33, 33, 33), Color(255, 255, 119, 51), Color(255, 255, 68, 0), (DWORD)(h2+5), 0, 1
};

static LINEPREF* lnp[3] = {&pbcolor, &slcolor, &elcolor};

/*  Declare Windows procedure  */
void DrawTimeLine(HDC*, QWORD, LINEPREF*);

// display error messages
void Error(const char *es)
{
    char mes[200];
    sprintf(mes,"%s\n(error code: %d)",es,BASS_ErrorGetCode());
    MessageBox(win,mes,0,0);
}

void CALLBACK LoopSyncProc(HSYNC handle, DWORD channel, DWORD data, void *user)
{
    if (!BASS_ChannelSetPosition(channel,lnp[1]->xpos,BASS_POS_BYTE)) // try seeking to loop start
        BASS_ChannelSetPosition(channel,0,BASS_POS_BYTE); // failed, go to start of file instead
}

void SetLoopStart(QWORD pos)
{
    lnp[1]->xpos=pos;
    int cp = BASS_ChannelGetPosition(chan, BASS_POS_BYTE);
    if (cp && pos > cp) BASS_ChannelSetPosition(chan,pos,BASS_POS_BYTE); // set current pos
}

void SetLoopEnd(QWORD pos)
{
    lnp[2]->xpos=pos;
    BASS_ChannelRemoveSync(chan,lsync); // remove old sync
    lsync=BASS_ChannelSetSync(chan,BASS_SYNC_POS|BASS_SYNC_MIXTIME,lnp[2]->xpos,LoopSyncProc,0); // set new sync
    int cp = BASS_ChannelGetPosition(chan, BASS_POS_BYTE);
    if (cp && pos < cp) BASS_ChannelSetPosition(chan,pos,BASS_POS_BYTE); // set current pos
}

// scan the peaks
void __cdecl ScanPeaks(void *p)
{
            Graphics g(wavebmp);
            Pen wf1pen(wf1colr);
            Pen wf2pen(wf2colr);

    DWORD decoder=(DWORD)p;
    DWORD pos=0;
    float spp=BASS_ChannelBytes2Seconds(decoder,bpp); // seconds per pixel
    while (!killscan) {
        float peak[2];
        if (spp>1) { // more than 1 second per pixel, break it down...
            float todo=spp;
            peak[1]=peak[0]=0;
            do {
                float level[2],step=(todo<1?todo:1);
                BASS_ChannelGetLevelEx(decoder,level,step,BASS_LEVEL_STEREO); // scan peaks
                if (peak[0]<level[0]) peak[0]=level[0];
                if (peak[1]<level[1]) peak[1]=level[1];
                todo-=step;
            } while (todo>0);
        } else
            BASS_ChannelGetLevelEx(decoder,peak,spp,BASS_LEVEL_STEREO); // scan peaks
        {
            if (peak[0] > 0.001) {
                g.DrawLine(&wf1pen, (int)pos, h2, (int)pos, (int)(h2-(peak[0]*hScope)));
            }
            if (peak[0] > 0.001) {
                g.DrawLine(&wf2pen, (int)pos, h2, (int)pos, (int)(h2+(peak[1]*hScope)));
            }
        }
        pos++;
        if (pos>=WIDTH) break; // reached end of display
        if (!BASS_ChannelIsActive(decoder)) break; // reached end of channel
    }
    if (!killscan) {
        DWORD size;
        BASS_ChannelSetPosition(decoder,(QWORD)-1,BASS_POS_BYTE|BASS_POS_SCAN); // build seek table (scan to end)
        size=BASS_ChannelGetAttributeEx(decoder,BASS_ATTRIB_SCANINFO,0,0); // get seek table size
        if (size) { // got it
            void *info=malloc(size); // allocate a buffer
            BASS_ChannelGetAttributeEx(decoder,BASS_ATTRIB_SCANINFO,info,size); // get the seek table
            BASS_ChannelSetAttributeEx(chan,BASS_ATTRIB_SCANINFO,info,size); // apply it to the playback channel
            free(info);
        }
    }
    BASS_StreamFree(decoder); // free the decoder
    scanthread=0;
}

// select a file to play, and start scanning it
BOOL PlayFile()
{
/*    if (!isfile) {
        char fn[MAX_PATH]="";
        OPENFILENAME ofn={0};
        ofn.lStructSize=sizeof(ofn);
        ofn.hwndOwner=win;
        ofn.nMaxFile=MAX_PATH;
        ofn.lpstrFile=fn;
        ofn.Flags=OFN_FILEMUSTEXIST|OFN_HIDEREADONLY|OFN_EXPLORER;
        ofn.lpstrTitle="Select a file to play";
        ofn.lpstrFilter="Playable files\0*.mp3;*.mp2;*.mp1;*.ogg;*.wav;*.aif;*.mo3;*.it;*.xm;*.s3m;*.mtm;*.mod;*.umx\0All files\0*.*\0\0";
        if (!GetOpenFileName(&ofn)) return FALSE;
    }*/

    char *file = fn;

    BASS_PluginLoad("bassalac.dll", 0);
    BASS_PluginLoad("basswebm.dll", 0);
    BASS_PluginLoad("bassflac.dll", 0);
    BASS_PluginLoad("bassopus.dll", 0);
    if (!(chan=BASS_StreamCreateFile(FALSE,file,0,0,0))
        && !(chan=BASS_MusicLoad(FALSE,file,0,0,BASS_MUSIC_RAMPS|BASS_MUSIC_POSRESET|BASS_MUSIC_PRESCAN,1))) {
        Error("Can't play file");
        return FALSE; // Can't load the file
    }
        BYTE data[2000]={0};
    {
        wavebmp = new Bitmap(WIDTH, HEIGHT, PixelFormat32bppRGB);
        Graphics g(wavebmp);
        g.Clear(bgcolor);
    }

    bpp=BASS_ChannelGetLength(chan,BASS_POS_BYTE)/WIDTH; // bytes per pixel
    {
        DWORD bpp1=BASS_ChannelSeconds2Bytes(chan,0.001); // minimum 1ms per pixel
        if (bpp<bpp1) bpp=bpp1;
    }
    lsync=BASS_ChannelSetSync(chan,BASS_SYNC_END|BASS_SYNC_MIXTIME,0,LoopSyncProc,0); // set sync to loop at end
    BASS_ChannelPlay(chan, FALSE);
     // start playing
    { // create another channel to scan
        DWORD chan2=BASS_StreamCreateFile(FALSE,file,0,0,BASS_STREAM_DECODE);
        if (!chan2) chan2=BASS_MusicLoad(FALSE,file,0,0,BASS_MUSIC_DECODE,1);
        scanthread=_beginthread(ScanPeaks,0,(void*)chan2); // start scanning in a new thread
    }
    return TRUE;
}

void DrawTimeLine(HDC *hdc)
{
    Graphics graphics(*hdc);

    Bitmap* backBuffer = wavebmp->Clone(Rect(0, 0, WIDTH, HEIGHT), PixelFormat32bppRGB);
    Graphics g(backBuffer);

    g.SetSmoothingMode(SmoothingModeHighQuality);
    g.SetTextRenderingHint(TextRenderingHintAntiAlias);

    WCHAR text2[16];

    FontFamily fontFamily(L"Arial");
    Font font(&fontFamily, 14, FontStyleRegular, UnitPixel);

    StringFormat format;
    format.SetAlignment(StringAlignmentCenter);
    format.SetLineAlignment(StringAlignmentCenter);
for (int a=0; a<3; a++)
{
    if (lnp[a]->xpos){
        int wpos=lnp[a]->xpos/bpp;
        Pen  lpen(lnp[a]->line);
        Pen rpen(lnp[a]->bg);

        g.DrawLine(&lpen, wpos, 0, wpos, HEIGHT);
        SolidBrush fgBrush(lnp[a]->text);
        SolidBrush bgBrush(lnp[a]->bg);

        DWORD time=BASS_ChannelBytes2Seconds(chan,lnp[a]->xpos)*1000; // position in milliseconds
        int cx = swprintf(text2, L" %u:%02u.%03u ",time/60000,(time/1000)%60,time%1000);
        DWORD fsize =cx*7;
        int fpos = wpos<=((lnp[a]->flag?WIDTH-fsize:fsize))?wpos:wpos-fsize;
        RectF rectF(fpos, lnp[a]->ypos, fsize, 20.0f);
        PointF drawPoint(wpos, lnp[a]->ypos);

        g.DrawRectangle(&rpen, rectF);
        g.FillRectangle(&bgBrush, rectF);
        g.DrawString(text2,cx,&font, rectF, &format,&fgBrush);
    }
}
Pen rpen(Color(255, 100, 100, 100));
SolidBrush fgBrush(Color(255, 0, 0, 0));
SolidBrush bgBrush(Color(255, 120, 120, 120));

wchar_t text3[256];
int cx = swprintf(text3, L"Loop: %s ", (setloop) ? L"ON" : L"OFF");

RectF rectF(0.0f, 0.0f, WIDTH, 20.0f);

g.DrawRectangle(&rpen, rectF);
g.FillRectangle(&bgBrush, rectF);
g.DrawString(text3,cx,&font, rectF, &format,&fgBrush);

    graphics.DrawImage(backBuffer, 0, 0, 0, 0, WIDTH, HEIGHT, UnitPixel);
    delete backBuffer;
}
