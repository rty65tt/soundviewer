#include <windows.h>
#include <gdiplus.h>
#include <stdio.h>
#include <process.h>
#include "bass.h"


using namespace Gdiplus;

#define WIDTH 1200  // display width
#define HEIGHT 321  // height (odd number for centre line)

HWND win=NULL;
DWORD scanthread=0;
BOOL killscan=FALSE;

DWORD chan;
DWORD bpp;          // bytes per pixel
QWORD loop[2]={0};  // loop start & end
HSYNC lsync;        // looping sync

HDC wavedc=0;
HBITMAP wavebmp=0;
BYTE *wavebuf;

CHAR fn[MAX_PATH];

static BOOL setloop = 1;
static BOOL playstatus = 0;
static QWORD seektime = 250000;
static QWORD endpos;

static Color bgcolor = Color(207, 211, 205);
static Color wf1colr = Color(126, 129, 130);
static Color wf2colr = Color(156, 156, 156);

struct LINEPREF
{
  Color bg;
  Color text;
  Color line;
  DWORD ypos;
};

static LINEPREF pbcolor = {
  Color(255, 221, 221, 221), Color(255, 33, 33, 33), Color(255, 150, 150, 150), 10
};
static LINEPREF slcolor = {
  Color(255, 33, 33, 33), Color(255, 136, 204, 51), Color(255, 170, 255, 0), (HEIGHT/2 - 25)
};
static LINEPREF elcolor = {
  Color(255, 33, 33, 33), Color(255, 255, 119, 51), Color(255, 255, 68, 0), (HEIGHT/2 + 5)
};

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
    if (!BASS_ChannelSetPosition(channel,loop[0],BASS_POS_BYTE)) // try seeking to loop start
        BASS_ChannelSetPosition(channel,0,BASS_POS_BYTE); // failed, go to start of file instead
}

void SetLoopStart(QWORD pos)
{
    loop[0]=pos;
}

void SetLoopEnd(QWORD pos)
{
    loop[1]=pos;
    BASS_ChannelRemoveSync(chan,lsync); // remove old sync
    lsync=BASS_ChannelSetSync(chan,BASS_SYNC_POS|BASS_SYNC_MIXTIME,loop[1],LoopSyncProc,0); // set new sync
}

// scan the peaks
void __cdecl ScanPeaks(void *p)
{
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
            DWORD a;
            for (a=0;a<peak[0]*(HEIGHT/4);a++)
                wavebuf[(HEIGHT/2-a)*WIDTH+pos]=1+a; // draw left peak
            for (a=0;a<peak[1]*(HEIGHT/4);a++)
                wavebuf[(HEIGHT/2+a)*WIDTH+pos]=1+a; // draw right peak
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
    char *file = fn;

    if (!(chan=BASS_StreamCreateFile(FALSE,file,0,0,0))
        && !(chan=BASS_MusicLoad(FALSE,file,0,0,BASS_MUSIC_RAMPS|BASS_MUSIC_POSRESET|BASS_MUSIC_PRESCAN,1))) {
        Error("Can't play file");
        return FALSE; // Can't load the file
    }
    {
        BYTE data[2000]={0};
        BITMAPINFOHEADER *bh=(BITMAPINFOHEADER*)data;
        RGBQUAD *pal=(RGBQUAD*)(data+sizeof(*bh));
        int a;
        bh->biSize=sizeof(*bh);
        bh->biWidth=WIDTH;
        bh->biHeight=-HEIGHT;
        bh->biPlanes=1;
        bh->biBitCount=8;
        bh->biClrUsed=bh->biClrImportant=HEIGHT/2+1;
        // setup palette
        for (a=1;a<=HEIGHT/2;a++) {
            pal[a].rgbRed   = 82;
            pal[a].rgbGreen = 80;
            pal[a].rgbBlue  = 80;
        }
        // create the bitmap
        wavebmp=CreateDIBSection(0,(BITMAPINFO*)bh,DIB_RGB_COLORS,(void**)&wavebuf,NULL,0);
        wavedc=CreateCompatibleDC(0);
        SelectObject(wavedc,wavebmp);
        Graphics g(wavedc);
        g.Clear(bgcolor);
    }
    bpp=BASS_ChannelGetLength(chan,BASS_POS_BYTE)/WIDTH; // bytes per pixel
    {
        DWORD bpp1=BASS_ChannelSeconds2Bytes(chan,0.001); // minimum 1ms per pixel
        if (bpp<bpp1) bpp=bpp1;
    }
    BASS_ChannelSetSync(chan,BASS_SYNC_END|BASS_SYNC_MIXTIME,0,LoopSyncProc,0); // set sync to loop at end
    BASS_ChannelPlay(chan,FALSE); // start playing
    { // create another channel to scan
        DWORD chan2=BASS_StreamCreateFile(FALSE,file,0,0,BASS_STREAM_DECODE);
        if (!chan2) chan2=BASS_MusicLoad(FALSE,file,0,0,BASS_MUSIC_DECODE,1);
        scanthread=_beginthread(ScanPeaks,0,(void*)chan2); // start scanning in a new thread
    }
    return TRUE;
}

void DrawTimeLine(HDC *hdc, QWORD pos, LINEPREF *line, RECT rc)
{
    Graphics graphics(*hdc);

    //Bitmap backBuffer(rc.right, rc.bottom, &graphics);
    Bitmap backBuffer(wavebmp, 0);
    //BitBlt(&backBuffer,0,0,WIDTH,HEIGHT,wavedc,0,0,SRCCOPY); // draw peak waveform
    Graphics g(&backBuffer);
    //g.Clear(bgcolor);

    g.SetSmoothingMode(SmoothingModeHighQuality);
    g.SetTextRenderingHint(TextRenderingHintAntiAlias);

    Pen      lpen(line->line);

    int wpos=pos/bpp;

    g.DrawLine(&lpen, wpos, 0, wpos, HEIGHT);

    FontFamily fontFamily(L"Arial");
    Font font(&fontFamily, 12, FontStyleRegular, UnitPixel);

    StringFormat format;
    format.SetAlignment(StringAlignmentCenter);
    format.SetLineAlignment(StringAlignmentCenter);

    SolidBrush fgBrush(line->text);
    SolidBrush bgBrush(line->bg);

    WCHAR text2[16];
    DWORD time=BASS_ChannelBytes2Seconds(chan,pos)*1000; // position in milliseconds
    swprintf(text2, L" %u:%02u.%03u ",time/60000,(time/1000)%60,time%1000);

    
    RectF rectF(wpos<=WIDTH/2?wpos:wpos-70, line->ypos, 70.0f, 20.0f);

    PointF drawPoint(wpos, line->ypos);

    Pen rpen(line->bg);
    g.DrawRectangle(&rpen, rectF);
    g.FillRectangle(&bgBrush, rectF);
    g.DrawString(text2,10,&font, rectF, &format,&fgBrush);

    //BitBlt(*hdc,0,0,WIDTH,HEIGHT,wavedc,0,0,SRCCOPY); // draw peak waveform

    graphics.DrawImage(&backBuffer, 0, 0, 0, 0, rc.right, rc.bottom, UnitPixel);

/*

    HPEN pen=CreatePen(PS_SOLID,2,col),oldpen;
    DWORD wpos=pos/bpp;
    DWORD time=BASS_ChannelBytes2Seconds(chan,pos)*1000; // position in milliseconds
    char text[16];
    sprintf(text," %u:%02u.%03u \n",time/60000,(time/1000)%60,time%1000);
    oldpen=(HPEN)SelectObject(dc,pen);
    
    MoveToEx(dc,wpos,0,NULL);
    LineTo(dc,wpos,HEIGHT);
    SetTextColor(dc,txtcol);
    SetBkColor(dc, bgcol);
    SetBkMode(dc, OPAQUE);
    SetTextAlign(dc,wpos>=WIDTH/2?TA_RIGHT:TA_LEFT);
    TextOut(dc,wpos,y,text,strlen(text));
    SelectObject(dc,oldpen);
    DeleteObject(pen);*/
}
