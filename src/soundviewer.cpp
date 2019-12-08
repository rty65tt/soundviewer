#if defined(UNICODE) && !defined(_UNICODE)
    #define _UNICODE
#elif defined(_UNICODE) && !defined(UNICODE)
    #define UNICODE
#endif

#include <tchar.h>
#include <windows.h>
#include <gdiplus.h>
#include <stdio.h>
#include "soundviewer.h"
#include "resource.h"
#include "bass.h"

using namespace Gdiplus;

/*  Declare Windows procedure  */
LRESULT CALLBACK WaveFormWindowProc (HWND, UINT, WPARAM, LPARAM);

int WINAPI WinMain (HINSTANCE hInstance, HINSTANCE hPrevInstance,LPSTR lpCmdLine, int nCmdShow)
{
	GdiplusStartupInput gdiplusStartupInput;
    ULONG_PTR gdiplusToken;
    GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL);

    HWND hWnd;               /* This is the handle for our window */
    MSG messages;            /* Here messages to the application are saved */
    WNDCLASSEX wincl;        /* Data structure for the windowclass */

	char path_buffer[_MAX_PATH];
	char drive[_MAX_DRIVE];
	char dir[_MAX_DIR];
	char fname[_MAX_FNAME];
	char ext[_MAX_EXT];
	_splitpath(lpCmdLine, drive, dir, fname, ext);

	strcat(fn, fname);
	strcat(fn, ext);
	fn[strlen(fn) - 1] = ' ';

	// check the correct BASS was loaded
	if (HIWORD(BASS_GetVersion())!=BASSVERSION) {
		MessageBox(0,"An incorrect version of BASS.DLL was loaded",0,MB_ICONERROR);
		return 0;
	}

    /* The Window structure */
    wincl.lpfnWndProc = WaveFormWindowProc;
	wincl.hInstance = hInstance;
	wincl.lpszClassName = _T("Sound Viewer");
	wincl.style = CS_DBLCLKS; 
	wincl.cbSize = sizeof (WNDCLASSEX);

 /* Use default icon and mouse-pointer */
    wincl.hIcon = (HICON)LoadImage(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_APPICON), IMAGE_ICON, 16, 16, 0);
    wincl.hIconSm = NULL;
    wincl.hCursor = LoadCursor (NULL, IDC_ARROW);
    wincl.lpszMenuName = NULL;                 /* No menu */
    wincl.cbClsExtra = 0;                      /* No extra bytes after the window class */
    wincl.cbWndExtra = 0;                      /* structure or the window instance */
    /* Use Windows's default colour as the background of the window */
    wincl.hbrBackground = (HBRUSH) (COLOR_BACKGROUND+1);

    /* Register the window class, and if it fails quit the program */
    if (!RegisterClassEx (&wincl))
        return 0;

    hWnd = CreateWindowEx (
		0,
		"Sound Viewer",
		_T("Sound Viewer"),
		WS_POPUPWINDOW|WS_CAPTION|WS_VISIBLE, 
		(GetSystemMetrics(SM_CXSCREEN) - WIDTH)/2,
		(GetSystemMetrics(SM_CYSCREEN) - HEIGHT)/6,
		WIDTH,
		HEIGHT,
		HWND_DESKTOP,
		NULL,
		hInstance, 
		NULL
		);
    /* Make the window visible on the screen */
	ShowWindow(hWnd, SW_SHOWNORMAL);

/* Run the message loop. It will run until GetMessage() returns 0 */
    while (GetMessage (&messages, NULL, 0, 0))
    {
        /* Translate virtual-key messages into character messages */
        TranslateMessage(&messages);
        /* Send message to WindowProcedure */
        DispatchMessage(&messages);
    }

    /* The program return-value is 0 - The value that PostQuitMessage() gave */
    return messages.wParam;
}
// window procedure
LRESULT CALLBACK WaveFormWindowProc(HWND h, UINT m, WPARAM w, LPARAM l)
{

	static HDC          hdc;      // device-context handle  
    static RECT         rc;       // RECT structure  
    static PAINTSTRUCT  ps;

    GetClientRect(h, &rc);

	switch (m) {

		case WM_CREATE:
			win=h;
			// initialize output
			if (!BASS_Init(-1,44100,0,win,NULL)) {
				Error("Can't initialize device");
				return -1;
			}
			if (!PlayFile()) { // start a file playing
				BASS_Free();
				return -1;
			}
			SetTimer(h,0,100,0); // set update timer (10hz)
			break;

		case WM_TIMER:
			InvalidateRect(h,0,0); // refresh window
			return 0;

		case WM_PAINT:
			if (GetUpdateRect(h,0,0)) {
				if (!(hdc=BeginPaint(h,&ps))) return 0;
                lnp[0]->xpos = BASS_ChannelGetPosition(chan,BASS_POS_BYTE);
				DrawTimeLine(&hdc); // current pos
				EndPaint(h,&ps);
			}
			return 0;

		case WM_LBUTTONDOWN:
		case WM_RBUTTONDOWN:
		case WM_MOUSEMOVE:
			if (w&MK_LBUTTON) SetLoopStart(LOWORD(l)*bpp); // set loop start
			if (w&MK_RBUTTON) SetLoopEnd(LOWORD(l)*bpp); // set loop end
			return 0;

		case WM_MBUTTONDOWN:
			BASS_ChannelSetPosition(chan,LOWORD(l)*bpp,BASS_POS_BYTE); // set current pos
			return 0;

		case WM_KEYDOWN:
			if ((DWORD)w == 0x1B || (DWORD)w == 0x51)
			{
				SendMessage(h, WM_DESTROY, 0, 0);
			}
			if ((DWORD)w == 0x25)
			{
				QWORD pos = BASS_ChannelGetPosition(chan, BASS_POS_BYTE);
				if (pos < seektime)
				{
					BASS_ChannelSetPosition(chan, 0, BASS_POS_BYTE);
				}
				else
				{
					BASS_ChannelSetPosition(chan, pos - 250000, BASS_POS_BYTE);
				}
			}
			if ((DWORD)w == 0x27)
			{
				QWORD pos = BASS_ChannelGetPosition(chan, BASS_POS_BYTE) + seektime;
				endpos = BASS_ChannelGetLength(chan, BASS_POS_BYTE);
				if (pos < endpos)
				{
					BASS_ChannelSetPosition(chan, pos, BASS_POS_BYTE);
				}
				else {
					BASS_ChannelSetPosition(chan, endpos-bpp, BASS_POS_BYTE);
				}
				
			}
            if ((DWORD)w == 0x4C) // If "L"
            {
                setloop = (setloop) ? 0 : 1;
                BASS_ChannelRemoveSync(chan,lsync);
                if (setloop)
                {
                    lsync=BASS_ChannelSetSync(chan,BASS_SYNC_END|BASS_SYNC_MIXTIME,0,LoopSyncProc,0);
                }
            }
			if ((DWORD)w == 0x20)
			{

                if (BASS_ChannelIsActive(chan) == BASS_ACTIVE_PLAYING) {
                    BASS_ChannelPause(chan);
                }
                else {
					BASS_ChannelPlay(chan, 0);
                }
			}
            if ((DWORD)w == 0x08)
            {
                SetLoopStart(0);
                SetLoopEnd(0);
            }
		break;
		case WM_DESTROY:
			KillTimer(h,0);
			if (scanthread) { // still scanning
				killscan=TRUE;
				WaitForSingleObject((HANDLE)scanthread,1000); // wait for the thread
			}
			BASS_Free();

			PostQuitMessage(0);
			break;
	}
	return DefWindowProc(h, m, w, l);
}
